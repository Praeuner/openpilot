#!/usr/bin/env python3
import os
import re
import datetime
import subprocess
import psutil
import shutil
import signal
import fcntl
import threading
import json
import time
from collections import defaultdict
from pathlib import Path

from openpilot.common.basedir import BASEDIR
from openpilot.common.params import Params
from openpilot.common.time_helpers import system_time_valid
from openpilot.common.markdown import parse_markdown
from openpilot.common.swaglog import cloudlog
from openpilot.selfdrive.selfdrived.alertmanager import set_offroad_alert
from openpilot.system.hardware import AGNOS, HARDWARE
from openpilot.system.version import get_build_metadata, SP_BRANCH_MIGRATIONS

LOCK_FILE = os.getenv("UPDATER_LOCK_FILE", "/tmp/safe_staging_overlay.lock")
STAGING_ROOT = os.getenv("UPDATER_STAGING_ROOT", "/data/safe_staging")

OVERLAY_UPPER = os.path.join(STAGING_ROOT, "upper")
OVERLAY_METADATA = os.path.join(STAGING_ROOT, "metadata")
OVERLAY_MERGED = os.path.join(STAGING_ROOT, "merged")
FINALIZED = os.path.join(STAGING_ROOT, "finalized")

OVERLAY_INIT = Path(os.path.join(BASEDIR, ".overlay_init"))

# do not allow to engage after this many hours onroad and this many routes
HOURS_NO_CONNECTIVITY_MAX = 27
ROUTES_NO_CONNECTIVITY_MAX = 84
# send an offroad prompt after this many hours onroad and this many routes
HOURS_NO_CONNECTIVITY_PROMPT = 23
ROUTES_NO_CONNECTIVITY_PROMPT = 80

# Command system constants
MAX_OUTPUT_LINES = 1000  # Maximum lines to keep in output buffer
OUTPUT_BUFFER_SIZE = 100 * 1024  # 100KB max output buffer


class UpdaterCommand:
  """Supported updater commands"""
  MANUAL_UPDATE = "manual_update"
  REPAIR_REPO = "repair_repo"
  RESET_CHANGES = "reset_changes"
  GET_HISTORY = "get_history"
  CHECKOUT_COMMIT = "checkout_commit"
  GET_REPO_STATUS = "get_repo_status"


class CommandStatus:
  """Command execution status"""
  IDLE = "idle"
  RUNNING = "running"
  COMPLETED = "completed"
  FAILED = "failed"
  CANCELLED = "cancelled"


class CommandExecutor:
  """Executes advanced git operations with real-time output streaming"""

  def __init__(self, params: Params):
    self.params = params
    self.current_command = None
    self.current_process = None
    self.output_lines = []
    self.start_time = None

  def read_command(self) -> dict | None:
    """Read and parse UpdaterCommand param"""
    try:
      cmd_str = self.params.get("UpdaterCommand", encoding='utf-8')
      if cmd_str:
        cmd = json.loads(cmd_str)
        # Clear the command param so we don't execute it again
        self.params.remove("UpdaterCommand")
        return cmd
    except (json.JSONDecodeError, Exception) as e:
      cloudlog.exception("Failed to parse UpdaterCommand")
      self.write_error(f"Failed to parse command: {e}")
    return None

  def check_cancel(self) -> bool:
    """Check if command should be cancelled"""
    return self.params.get_bool("UpdaterCommandCancel")

  def write_status(self, status: str, progress: int = 0):
    """Update status params"""
    self.params.put("UpdaterCommandStatus", status)
    self.params.put("UpdaterCommandProgress", str(progress))

  def stream_output(self, line: str):
    """Stream output line to UI (rolling buffer)"""
    self.output_lines.append(line)

    # Keep only last MAX_OUTPUT_LINES
    if len(self.output_lines) > MAX_OUTPUT_LINES:
      self.output_lines = self.output_lines[-MAX_OUTPUT_LINES:]

    # Join and truncate to max size
    output = '\n'.join(self.output_lines)
    if len(output) > OUTPUT_BUFFER_SIZE:
      output = output[-OUTPUT_BUFFER_SIZE:]

    self.params.put("UpdaterCommandOutput", output)

  def write_result(self, result: dict):
    """Write final command result"""
    self.params.put("UpdaterCommandResult", json.dumps(result))
    self.write_status(CommandStatus.COMPLETED, 100)

  def write_error(self, error: str):
    """Write error result"""
    result = {
      "success": False,
      "error": error,
      "exit_code": -1,
      "duration": time.time() - self.start_time if self.start_time else 0
    }
    self.params.put("UpdaterCommandResult", json.dumps(result))
    self.write_status(CommandStatus.FAILED, 0)

  def run_streaming(self, cmd: list[str], cwd: str = BASEDIR) -> tuple[int, str]:
    """
    Run command and stream output line-by-line
    Returns: (exit_code, full_output)
    """
    cloudlog.info(f"Running command: {' '.join(cmd)} in {cwd}")
    self.stream_output(f"$ {' '.join(cmd)}\n")

    try:
      self.current_process = subprocess.Popen(
        cmd,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=1,
        universal_newlines=True
      )

      output_lines = []
      for line in iter(self.current_process.stdout.readline, ''):
        if not line:
          break

        line = line.rstrip()
        output_lines.append(line)
        self.stream_output(line)

        # Check for cancellation
        if self.check_cancel():
          cloudlog.warning("Command cancelled by user")
          self.current_process.kill()
          self.current_process.wait()
          return -1, "Command cancelled by user"

      exit_code = self.current_process.wait()
      self.current_process = None

      return exit_code, '\n'.join(output_lines)

    except Exception as e:
      cloudlog.exception("Command execution failed")
      if self.current_process:
        self.current_process.kill()
        self.current_process = None
      return -1, str(e)

  def handle_manual_update(self, args: dict) -> dict:
    """Execute manual update with build"""
    cloudlog.info("Executing manual update")
    self.write_status(CommandStatus.RUNNING, 0)

    commands = [
      (["git", "reset", "--hard", "HEAD"], 10),
      (["git", "clean", "-xdff"], 20),
      (["rm", "-f", ".git/index.lock"], 25),
      (["git", "fetch"], 40),
      (["git", "pull"], 50),
      (["git", "submodule", "update", "--init", "--recursive"], 60),
      (["scons", f"-j{os.cpu_count() or 4}"], 90),
    ]

    for cmd, progress in commands:
      self.write_status(CommandStatus.RUNNING, progress)
      exit_code, output = self.run_streaming(cmd)

      if exit_code != 0:
        return {
          "success": False,
          "error": f"Command failed: {' '.join(cmd)}",
          "exit_code": exit_code,
          "output": output
        }

    return {
      "success": True,
      "exit_code": 0,
      "message": "Manual update completed successfully"
    }

  def handle_repair_repo(self, args: dict) -> dict:
    """Repair repository"""
    cloudlog.info("Executing repository repair")
    self.write_status(CommandStatus.RUNNING, 0)

    commands = [
      (["git", "reset", "--hard", "HEAD"], 25),
      (["git", "clean", "-xdff"], 50),
      (["git", "submodule", "foreach", "--recursive", "git", "reset", "--hard"], 75),
      (["git", "submodule", "foreach", "--recursive", "git", "clean", "-xdff"], 90),
    ]

    for cmd, progress in commands:
      self.write_status(CommandStatus.RUNNING, progress)
      exit_code, output = self.run_streaming(cmd)

      if exit_code != 0:
        return {
          "success": False,
          "error": f"Command failed: {' '.join(cmd)}",
          "exit_code": exit_code,
          "output": output
        }

    return {
      "success": True,
      "exit_code": 0,
      "message": "Repository repaired successfully"
    }

  def handle_reset_changes(self, args: dict) -> dict:
    """Reset uncommitted changes"""
    cloudlog.info("Resetting uncommitted changes")
    self.write_status(CommandStatus.RUNNING, 50)

    exit_code, output = self.run_streaming(["git", "reset", "--hard", "HEAD"])

    if exit_code != 0:
      return {
        "success": False,
        "error": "Git reset failed",
        "exit_code": exit_code,
        "output": output
      }

    return {
      "success": True,
      "exit_code": 0,
      "message": "Changes reset successfully"
    }

  def handle_get_history(self, args: dict) -> dict:
    """Get commit history"""
    cloudlog.info("Fetching commit history")
    limit = args.get("limit", 30)

    cmd = ["git", "log", "--all", f"-n{limit}", "--pretty=format:%H|||%h|||%s|||%an|||%ct"]
    exit_code, output = self.run_streaming(cmd)

    if exit_code != 0:
      return {
        "success": False,
        "error": "Failed to get commit history",
        "exit_code": exit_code
      }

    commits = []
    for line in output.split('\n'):
      if not line.strip():
        continue
      parts = line.split('|||')
      if len(parts) == 5:
        hash_full, hash_short, message, author, timestamp = parts
        try:
          dt = datetime.datetime.fromtimestamp(int(timestamp))
          commits.append({
            "hash": hash_full,
            "short_hash": hash_short,
            "message": message,
            "author": author,
            "date": dt.strftime("%Y-%m-%d %H:%M:%S"),
            "date_relative": self._get_relative_time(int(timestamp))
          })
        except Exception:
          continue

    return {
      "success": True,
      "commits": commits
    }

  def handle_checkout_commit(self, args: dict) -> dict:
    """Checkout specific commit"""
    commit = args.get("commit")
    if not commit:
      return {
        "success": False,
        "error": "No commit specified"
      }

    cloudlog.info(f"Checking out commit {commit}")
    self.write_status(CommandStatus.RUNNING, 0)

    commands = [
      (["git", "checkout", "-f", commit], 33),
      (["git", "reset", "--hard"], 66),
      (["git", "clean", "-xdff"], 90),
    ]

    for cmd, progress in commands:
      self.write_status(CommandStatus.RUNNING, progress)
      exit_code, output = self.run_streaming(cmd)

      if exit_code != 0:
        return {
          "success": False,
          "error": f"Command failed: {' '.join(cmd)}",
          "exit_code": exit_code,
          "output": output
        }

    return {
      "success": True,
      "exit_code": 0,
      "message": f"Successfully checked out {commit}"
    }

  def handle_get_repo_status(self, args: dict) -> dict:
    """Get current repository status"""
    cloudlog.info("Getting repository status")

    try:
      branch = subprocess.check_output(
        ["git", "rev-parse", "--abbrev-ref", "HEAD"],
        cwd=BASEDIR,
        encoding='utf-8',
        stderr=subprocess.DEVNULL
      ).strip()

      commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"],
        cwd=BASEDIR,
        encoding='utf-8',
        stderr=subprocess.DEVNULL
      ).strip()[:10]

      commit_msg = subprocess.check_output(
        ["git", "log", "-1", "--pretty=format:%s"],
        cwd=BASEDIR,
        encoding='utf-8',
        stderr=subprocess.DEVNULL
      ).strip()

      commit_date = subprocess.check_output(
        ["git", "log", "-1", "--pretty=format:%ci"],
        cwd=BASEDIR,
        encoding='utf-8',
        stderr=subprocess.DEVNULL
      ).strip()

      # Check for local changes
      status_output = subprocess.check_output(
        ["git", "status", "--porcelain"],
        cwd=BASEDIR,
        encoding='utf-8',
        stderr=subprocess.DEVNULL
      )
      has_changes = bool(status_output.strip())

      return {
        "success": True,
        "branch": branch,
        "commit": commit,
        "commit_message": commit_msg,
        "commit_date": commit_date,
        "has_local_changes": has_changes,
        "is_dirty": has_changes
      }

    except Exception as e:
      return {
        "success": False,
        "error": str(e)
      }

  def _get_relative_time(self, timestamp: int) -> str:
    """Get human-readable relative time"""
    now = time.time()
    diff = now - timestamp

    if diff < 60:
      return "just now"
    elif diff < 3600:
      mins = int(diff / 60)
      return f"{mins} minute{'s' if mins != 1 else ''} ago"
    elif diff < 86400:
      hours = int(diff / 3600)
      return f"{hours} hour{'s' if hours != 1 else ''} ago"
    elif diff < 2592000:
      days = int(diff / 86400)
      return f"{days} day{'s' if days != 1 else ''} ago"
    else:
      months = int(diff / 2592000)
      return f"{months} month{'s' if months != 1 else ''} ago"

  def execute_command(self, cmd: dict):
    """Main command dispatcher"""
    command_type = cmd.get("cmd")
    args = cmd.get("args", {})
    cmd_id = cmd.get("id", "unknown")

    cloudlog.info(f"Executing command: {command_type} (id: {cmd_id})")

    # Store command info
    self.current_command = cmd
    self.output_lines = []
    self.start_time = time.time()

    # Clear cancel flag
    self.params.remove("UpdaterCommandCancel")

    # Write initial status
    self.write_status(CommandStatus.RUNNING, 0)
    self.params.put("UpdaterCommandID", cmd_id)

    try:
      # Dispatch to appropriate handler
      if command_type == UpdaterCommand.MANUAL_UPDATE:
        result = self.handle_manual_update(args)
      elif command_type == UpdaterCommand.REPAIR_REPO:
        result = self.handle_repair_repo(args)
      elif command_type == UpdaterCommand.RESET_CHANGES:
        result = self.handle_reset_changes(args)
      elif command_type == UpdaterCommand.GET_HISTORY:
        result = self.handle_get_history(args)
      elif command_type == UpdaterCommand.CHECKOUT_COMMIT:
        result = self.handle_checkout_commit(args)
      elif command_type == UpdaterCommand.GET_REPO_STATUS:
        result = self.handle_get_repo_status(args)
      else:
        result = {
          "success": False,
          "error": f"Unknown command: {command_type}"
        }

      # Add duration to result
      result["duration"] = time.time() - self.start_time

      # Write final result
      if result.get("success"):
        self.write_result(result)
      else:
        self.write_error(result.get("error", "Command failed"))

    except Exception as e:
      cloudlog.exception("Command execution failed")
      self.write_error(str(e))

    finally:
      self.current_command = None
      self.current_process = None


class UserRequest:
  NONE = 0
  CHECK = 1
  FETCH = 2

class WaitTimeHelper:
  def __init__(self):
    self.ready_event = threading.Event()
    self.user_request = UserRequest.NONE
    signal.signal(signal.SIGHUP, self.update_now)
    signal.signal(signal.SIGUSR1, self.check_now)

  def update_now(self, signum: int, frame) -> None:
    cloudlog.info("caught SIGHUP, attempting to downloading update")
    self.user_request = UserRequest.FETCH
    self.ready_event.set()

  def check_now(self, signum: int, frame) -> None:
    cloudlog.info("caught SIGUSR1, checking for updates")
    self.user_request = UserRequest.CHECK
    self.ready_event.set()

  def sleep(self, t: float) -> None:
    self.ready_event.wait(timeout=t)

def write_time_to_param(params, param) -> None:
  t = datetime.datetime.now(datetime.UTC).replace(tzinfo=None)
  params.put(param, t)

def run(cmd: list[str], cwd: str = None) -> str:
  return subprocess.check_output(cmd, cwd=cwd, stderr=subprocess.STDOUT, encoding='utf8')


def set_consistent_flag(consistent: bool) -> None:
  os.sync()
  consistent_file = Path(os.path.join(FINALIZED, ".overlay_consistent"))
  if consistent:
    consistent_file.touch()
  elif not consistent:
    consistent_file.unlink(missing_ok=True)
  os.sync()

def parse_release_notes(basedir: str) -> bytes:
  try:
    with open(os.path.join(basedir, "CHANGELOG.md"), "rb") as f:
      r = f.read().split(b'\n\n', 1)[0]  # Slice latest release notes
    try:
      return bytes(parse_markdown(r.decode("utf-8")), encoding="utf-8")
    except Exception:
      return r + b"\n"
  except FileNotFoundError:
    pass
  except Exception:
    cloudlog.exception("failed to parse release notes")
  return b""

def setup_git_options(cwd: str) -> None:
  # We sync FS object atimes (which NEOS doesn't use) and mtimes, but ctimes
  # are outside user control. Make sure Git is set up to ignore system ctimes,
  # because they change when we make hard links during finalize. Otherwise,
  # there is a lot of unnecessary churn. This appears to be a common need on
  # OSX as well: https://www.git-tower.com/blog/make-git-rebase-safe-on-osx/

  # We are using copytree to copy the directory, which also changes
  # inode numbers. Ignore those changes too.

  # Set protocol to the new version (default after git 2.26) to reduce data
  # usage on git fetch --dry-run from about 400KB to 18KB.
  git_cfg = [
    ("core.trustctime", "false"),
    ("core.checkStat", "minimal"),
    ("protocol.version", "2"),
    ("gc.auto", "0"),
    ("gc.autoDetach", "false"),
  ]
  for option, value in git_cfg:
    run(["git", "config", option, value], cwd)


def dismount_overlay() -> None:
  if os.path.ismount(OVERLAY_MERGED):
    cloudlog.info("unmounting existing overlay")
    run(["sudo", "umount", "-l", OVERLAY_MERGED])


def init_overlay() -> None:

  # Re-create the overlay if BASEDIR/.git has changed since we created the overlay
  if OVERLAY_INIT.is_file() and os.path.ismount(OVERLAY_MERGED):
    git_dir_path = os.path.join(BASEDIR, ".git")
    new_files = run(["find", git_dir_path, "-newer", str(OVERLAY_INIT)])
    if not len(new_files.splitlines()):
      # A valid overlay already exists
      return
    else:
      cloudlog.info(".git directory changed, recreating overlay")

  cloudlog.info("preparing new safe staging area")

  params = Params()
  params.put_bool("UpdateAvailable", False)
  set_consistent_flag(False)
  dismount_overlay()
  run(["sudo", "rm", "-rf", STAGING_ROOT])
  if os.path.isdir(STAGING_ROOT):
    shutil.rmtree(STAGING_ROOT)

  for dirname in [STAGING_ROOT, OVERLAY_UPPER, OVERLAY_METADATA, OVERLAY_MERGED]:
    os.mkdir(dirname, 0o755)

  if os.lstat(BASEDIR).st_dev != os.lstat(OVERLAY_MERGED).st_dev:
    raise RuntimeError("base and overlay merge directories are on different filesystems; not valid for overlay FS!")

  # Leave a timestamped canary in BASEDIR to check at startup. The device clock
  # should be correct by the time we get here. If the init file disappears, or
  # critical mtimes in BASEDIR are newer than .overlay_init, continue.sh can
  # assume that BASEDIR has used for local development or otherwise modified,
  # and skips the update activation attempt.
  consistent_file = Path(os.path.join(BASEDIR, ".overlay_consistent"))
  if consistent_file.is_file():
    consistent_file.unlink()
  OVERLAY_INIT.touch()

  os.sync()
  overlay_opts = f"lowerdir={BASEDIR},upperdir={OVERLAY_UPPER},workdir={OVERLAY_METADATA}"

  mount_cmd = ["mount", "-t", "overlay", "-o", overlay_opts, "none", OVERLAY_MERGED]
  run(["sudo"] + mount_cmd)
  run(["sudo", "chmod", "755", os.path.join(OVERLAY_METADATA, "work")])

  git_diff = run(["git", "diff", "--submodule=diff"], OVERLAY_MERGED)
  params.put("GitDiff", git_diff)
  cloudlog.info(f"git diff output:\n{git_diff}")


def finalize_update() -> None:
  """Take the current OverlayFS merged view and finalize a copy outside of
  OverlayFS, ready to be swapped-in at BASEDIR. Copy using shutil.copytree"""

  # Remove the update ready flag and any old updates
  cloudlog.info("creating finalized version of the overlay")
  set_consistent_flag(False)

  # Copy the merged overlay view and set the update ready flag
  if os.path.exists(FINALIZED):
    shutil.rmtree(FINALIZED)
  shutil.copytree(OVERLAY_MERGED, FINALIZED, symlinks=True)

  run(["git", "reset", "--hard"], FINALIZED)
  run(["git", "submodule", "foreach", "--recursive", "git", "reset", "--hard"], FINALIZED)

  set_consistent_flag(True)
  cloudlog.info("done finalizing overlay")


def handle_agnos_update() -> None:
  from openpilot.system.hardware.tici.agnos import flash_agnos_update, get_target_slot_number

  cur_version = HARDWARE.get_os_version()
  updated_version = run(["bash", "-c", r"unset AGNOS_VERSION && source launch_env.sh && \
                          echo -n $AGNOS_VERSION"], OVERLAY_MERGED).strip()

  cloudlog.info(f"AGNOS version check: {cur_version} vs {updated_version}")
  if cur_version == updated_version:
    return

  # prevent an openpilot getting swapped in with a mismatched or partially downloaded agnos
  set_consistent_flag(False)

  cloudlog.info(f"Beginning background installation for AGNOS {updated_version}")
  set_offroad_alert("Offroad_NeosUpdate", True)

  manifest_path = os.path.join(OVERLAY_MERGED, "system/hardware/tici/agnos.json")
  target_slot_number = get_target_slot_number()
  flash_agnos_update(manifest_path, target_slot_number, cloudlog)
  set_offroad_alert("Offroad_NeosUpdate", False)



class Updater:
  def __init__(self):
    self.params = Params()
    self.branches = defaultdict(str)
    self._has_internet: bool = False

  @property
  def has_internet(self) -> bool:
    return self._has_internet

  @property
  def target_branch(self) -> str:
    b: str | None = self.params.get("UpdaterTargetBranch")
    if b is None:
      b = self.get_branch(BASEDIR)
    b = SP_BRANCH_MIGRATIONS.get((HARDWARE.get_device_type(), b), b)
    return b

  @property
  def update_ready(self) -> bool:
    consistent_file = Path(os.path.join(FINALIZED, ".overlay_consistent"))
    if consistent_file.is_file():
      hash_mismatch = self.get_commit_hash(BASEDIR) != self.branches[self.target_branch]
      branch_mismatch = self.get_branch(BASEDIR) != self.target_branch
      on_target_branch = self.get_branch(FINALIZED) == self.target_branch
      return ((hash_mismatch or branch_mismatch) and on_target_branch)
    return False

  @property
  def update_available(self) -> bool:
    if os.path.isdir(OVERLAY_MERGED) and len(self.branches) > 0:
      hash_mismatch = self.get_commit_hash(OVERLAY_MERGED) != self.branches[self.target_branch]
      branch_mismatch = self.get_branch(OVERLAY_MERGED) != self.target_branch
      return hash_mismatch or branch_mismatch
    return False

  def get_branch(self, path: str) -> str:
    return run(["git", "rev-parse", "--abbrev-ref", "HEAD"], path).rstrip()

  def get_commit_hash(self, path: str = OVERLAY_MERGED) -> str:
    return run(["git", "rev-parse", "HEAD"], path).rstrip()

  def set_params(self, update_success: bool, failed_count: int, exception: str | None) -> None:
    self.params.put("UpdateFailedCount", failed_count)
    self.params.put("UpdaterTargetBranch", self.target_branch)

    self.params.put_bool("UpdaterFetchAvailable", self.update_available)
    if len(self.branches):
      self.params.put("UpdaterAvailableBranches", ','.join(self.branches.keys()))

    last_uptime_onroad = self.params.get("UptimeOnroad", return_default=True)
    last_route_count = self.params.get("RouteCount", return_default=True)
    if update_success:
      self.params.put("LastUpdateTime", datetime.datetime.now(datetime.UTC).replace(tzinfo=None))
      self.params.put("LastUpdateUptimeOnroad", last_uptime_onroad)
      self.params.put("LastUpdateRouteCount", last_route_count)
    else:
      last_uptime_onroad = self.params.get("LastUpdateUptimeOnroad", return_default=True)
      last_route_count = self.params.get("LastUpdateRouteCount", return_default=True)

    if exception is None:
      self.params.remove("LastUpdateException")
    else:
      self.params.put("LastUpdateException", exception)

    # Write out current and new version info
    def get_description(basedir: str) -> str:
      if not os.path.exists(basedir):
        return ""

      version = ""
      branch = ""
      commit = ""
      commit_date = ""
      try:
        branch = self.get_branch(basedir)
        commit = self.get_commit_hash(basedir)[:7]
        with open(os.path.join(basedir, "sunnypilot", "common", "version.h")) as f:
          version = f.read().split('"')[1]

        commit_unix_ts = run(["git", "show", "-s", "--format=%ct", "HEAD"], basedir).rstrip()
        dt = datetime.datetime.fromtimestamp(int(commit_unix_ts))
        commit_date = dt.strftime("%b %d")
      except Exception:
        cloudlog.exception("updater.get_description")
      return f"{version} / {branch} / {commit} / {commit_date}"
    self.params.put("UpdaterCurrentDescription", get_description(BASEDIR))
    self.params.put("UpdaterCurrentReleaseNotes", parse_release_notes(BASEDIR))
    self.params.put("UpdaterNewDescription", get_description(FINALIZED))
    self.params.put("UpdaterNewReleaseNotes", parse_release_notes(FINALIZED))
    self.params.put_bool("UpdateAvailable", self.update_ready)

    # Handle user prompt
    for alert in ("Offroad_UpdateFailed", "Offroad_ConnectivityNeeded", "Offroad_ConnectivityNeededPrompt"):
      set_offroad_alert(alert, False)

    dt_uptime_onroad = (self.params.get("UptimeOnroad", return_default=True) - last_uptime_onroad) / (60*60)
    dt_route_count = self.params.get("RouteCount", return_default=True) - last_route_count
    build_metadata = get_build_metadata()
    if failed_count > 15 and exception is not None and self.has_internet:
      if build_metadata.tested_channel:
        extra_text = "Ensure the software is correctly installed. Uninstall and re-install if this error persists."
      else:
        extra_text = exception
      set_offroad_alert("Offroad_UpdateFailed", True, extra_text=extra_text)
    elif failed_count > 0:
      if dt_uptime_onroad > HOURS_NO_CONNECTIVITY_MAX and dt_route_count > ROUTES_NO_CONNECTIVITY_MAX:
        set_offroad_alert("Offroad_ConnectivityNeeded", True)
      elif dt_uptime_onroad > HOURS_NO_CONNECTIVITY_PROMPT and dt_route_count > ROUTES_NO_CONNECTIVITY_PROMPT:
        remaining = max(HOURS_NO_CONNECTIVITY_MAX - dt_uptime_onroad, 1)
        set_offroad_alert("Offroad_ConnectivityNeededPrompt", True, extra_text=f"{remaining} hour{'' if remaining == 1 else 's'}.")

  def check_for_update(self) -> None:
    cloudlog.info("checking for updates")

    excluded_branches = ('release2', 'release2-staging')

    try:
      run(["git", "ls-remote", "origin", "HEAD"], OVERLAY_MERGED)
      self._has_internet = True
    except subprocess.CalledProcessError:
      self._has_internet = False

    setup_git_options(OVERLAY_MERGED)
    output = run(["git", "ls-remote", "--heads"], OVERLAY_MERGED)

    self.branches = defaultdict(lambda: None)
    for line in output.split('\n'):
      ls_remotes_re = r'(?P<commit_sha>\b[0-9a-f]{5,40}\b)(\s+)(refs\/heads\/)(?P<branch_name>.*$)'
      x = re.fullmatch(ls_remotes_re, line.strip())
      if x is not None and x.group('branch_name') not in excluded_branches:
        self.branches[x.group('branch_name')] = x.group('commit_sha')

    cur_branch = self.get_branch(OVERLAY_MERGED)
    cur_commit = self.get_commit_hash(OVERLAY_MERGED)
    new_branch = self.target_branch
    new_commit = self.branches[new_branch]
    if (cur_branch, cur_commit) != (new_branch, new_commit):
      cloudlog.info(f"update available, {cur_branch} ({str(cur_commit)[:7]}) -> {new_branch} ({str(new_commit)[:7]})")
    else:
      cloudlog.info(f"up to date on {cur_branch} ({str(cur_commit)[:7]})")

  def fetch_update(self) -> None:
    cloudlog.info("attempting git fetch inside staging overlay")

    self.params.put("UpdaterState", "downloading...")

    # TODO: cleanly interrupt this and invalidate old update
    set_consistent_flag(False)
    self.params.put_bool("UpdateAvailable", False)

    setup_git_options(OVERLAY_MERGED)

    branch = self.target_branch
    git_fetch_output = run(["git", "fetch", "origin", branch], OVERLAY_MERGED)
    cloudlog.info("git fetch success: %s", git_fetch_output)

    cloudlog.info("git reset in progress")
    cmds = [
      ["git", "checkout", "--force", "--no-recurse-submodules", "-B", branch, "FETCH_HEAD"],
      ["git", "reset", "--hard"],
      ["git", "clean", "-xdff"],
      ["git", "submodule", "sync"],
      ["git", "submodule", "update", "--init", "--recursive"],
      ["git", "submodule", "foreach", "--recursive", "git", "reset", "--hard"],
    ]
    r = [run(cmd, OVERLAY_MERGED) for cmd in cmds]
    cloudlog.info("git reset success: %s", '\n'.join(r))

    # TODO: show agnos download progress
    if AGNOS:
      handle_agnos_update()

    # Create the finalized, ready-to-swap update
    self.params.put("UpdaterState", "finalizing update...")
    finalize_update()
    cloudlog.info("finalize success!")


def main() -> None:
  params = Params()

  if params.get_bool("DisableUpdates"):
    cloudlog.warning("updates are disabled by the DisableUpdates param")
    exit(0)

  with open(LOCK_FILE, 'w') as ov_lock_fd:
    try:
      fcntl.flock(ov_lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError as e:
      raise RuntimeError("couldn't get overlay lock; is another instance running?") from e

    # Set low io priority
    proc = psutil.Process()
    if psutil.LINUX:
      proc.ionice(psutil.IOPRIO_CLASS_BE, value=7)

    # Check if we just performed an update
    if Path(os.path.join(STAGING_ROOT, "old_openpilot")).is_dir():
      cloudlog.event("update installed")

    if not params.get("InstallDate"):
      t = datetime.datetime.now(datetime.UTC).replace(tzinfo=None)
      params.put("InstallDate", t)

    updater = Updater()
    update_failed_count = 0 # TODO: Load from param?
    wait_helper = WaitTimeHelper()
    command_executor = CommandExecutor(params)

    # invalidate old finalized update
    set_consistent_flag(False)

    # set initial state
    params.put("UpdaterState", "idle")
    params.put("UpdaterCommandStatus", CommandStatus.IDLE)

    # Run the update loop
    first_run = True
    while True:
      wait_helper.ready_event.clear()

      # Check for advanced commands
      cmd = command_executor.read_command()
      if cmd:
        cloudlog.info(f"Processing command: {cmd}")
        try:
          command_executor.execute_command(cmd)
        except Exception as e:
          cloudlog.exception("Command execution failed in main loop")
        # After command execution, continue to next loop iteration
        continue

      # Attempt an update
      exception = None
      try:
        # TODO: reuse overlay from previous updated instance if it looks clean
        init_overlay()

        # ensure we have some params written soon after startup
        updater.set_params(False, update_failed_count, exception)

        if not system_time_valid() or first_run:
          first_run = False
          wait_helper.sleep(60)
          continue

        update_failed_count += 1

        # check for update
        params.put("UpdaterState", "checking...")
        updater.check_for_update()

        # download update
        last_fetch = params.get("UpdaterLastFetchTime")
        timed_out = last_fetch is None or (datetime.datetime.now(datetime.UTC).replace(tzinfo=None) - last_fetch > datetime.timedelta(days=3))
        user_requested_fetch = wait_helper.user_request == UserRequest.FETCH
        if params.get_bool("NetworkMetered") and not timed_out and not user_requested_fetch:
          cloudlog.info("skipping fetch, connection metered")
        elif wait_helper.user_request == UserRequest.CHECK:
          cloudlog.info("skipping fetch, only checking")
        else:
          updater.fetch_update()
          write_time_to_param(params, "UpdaterLastFetchTime")
        update_failed_count = 0
      except subprocess.CalledProcessError as e:
        cloudlog.event(
          "update process failed",
          cmd=e.cmd,
          output=e.output,
          returncode=e.returncode
        )
        exception = f"command failed: {e.cmd}\n{e.output}"
        OVERLAY_INIT.unlink(missing_ok=True)
      except Exception as e:
        cloudlog.exception("uncaught updated exception, shouldn't happen")
        exception = str(e)
        OVERLAY_INIT.unlink(missing_ok=True)

      try:
        params.put("UpdaterState", "idle")
        update_successful = (update_failed_count == 0)
        updater.set_params(update_successful, update_failed_count, exception)
      except Exception:
        cloudlog.exception("uncaught updated exception while setting params, shouldn't happen")

      # infrequent attempts if we successfully updated recently
      wait_helper.user_request = UserRequest.NONE
      wait_helper.sleep(5*60 if update_failed_count > 0 else 1.5*60*60)


if __name__ == "__main__":
  main()
