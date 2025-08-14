import pyray as rl
import numpy as np
import time
import threading
from collections.abc import Callable
from enum import Enum
from cereal import messaging, log
from openpilot.common.filter_simple import FirstOrderFilter
from openpilot.common.params import Params, UnknownKeyName
from openpilot.common.swaglog import cloudlog
from openpilot.selfdrive.ui.lib.prime_state import PrimeState
from openpilot.system.ui.lib.application import DEFAULT_FPS
from openpilot.system.hardware import HARDWARE
from openpilot.system.ui.lib.application import gui_app

UI_BORDER_SIZE = 30
BACKLIGHT_OFFROAD = 50


class UIStatus(Enum):
  DISENGAGED = "disengaged"
  ENGAGED = "engaged"
  OVERRIDE = "override"


class UIState:
  _instance: 'UIState | None' = None

  def __new__(cls):
    if cls._instance is None:
      cls._instance = super().__new__(cls)
      cls._instance._initialize()
    return cls._instance

  def _initialize(self):
    self.params = Params()
    self.sm = messaging.SubMaster(
      [
        "modelV2",
        "controlsState",
        "liveCalibration",
        "radarState",
        "deviceState",
        "pandaStates",
        "carParams",
        "driverMonitoringState",
        "carState",
        "driverStateV2",
        "roadCameraState",
        "wideRoadCameraState",
        "managerState",
        "selfdriveState",
        "longitudinalPlan",
      ]
    )

    self.prime_state = PrimeState()

    # UI Status tracking
    self.status: UIStatus = UIStatus.DISENGAGED
    self.started_frame: int = 0
    self._engaged_prev: bool = False
    self._started_prev: bool = False

    # Core state variables
    self.is_metric: bool = self.params.get_bool("IsMetric")
    self.started: bool = False
    self.ignition: bool = False
    self.panda_type: log.PandaState.PandaType = log.PandaState.PandaType.unknown
    self.personality: log.LongitudinalPersonality = log.LongitudinalPersonality.standard
    self.light_sensor: float = -1.0

    self._update_params()

  @property
  def engaged(self) -> bool:
    return self.started and self.sm["selfdriveState"].enabled

  def is_onroad(self) -> bool:
    return self.started

  def is_offroad(self) -> bool:
    return not self.started

  def update(self) -> None:
    self.sm.update(0)
    self._update_state()
    self._update_status()
    device.update()

  def _update_state(self) -> None:
    # Handle panda states updates
    if self.sm.updated["pandaStates"]:
      panda_states = self.sm["pandaStates"]

      if len(panda_states) > 0:
        # Get panda type from first panda
        self.panda_type = panda_states[0].pandaType
        # Check ignition status across all pandas
        if self.panda_type != log.PandaState.PandaType.unknown:
          self.ignition = any(state.ignitionLine or state.ignitionCan for state in panda_states)
    elif self.sm.frame - self.sm.recv_frame["pandaStates"] > 5 * rl.get_fps():
      self.panda_type = log.PandaState.PandaType.unknown

    # Handle wide road camera state updates
    if self.sm.updated["wideRoadCameraState"]:
      cam_state = self.sm["wideRoadCameraState"]

      # Scale factor based on sensor type
      scale = 6.0 if cam_state.sensor == 'ar0231' else 1.0
      self.light_sensor = max(100.0 - scale * cam_state.exposureValPercent, 0.0)
    elif not self.sm.alive["wideRoadCameraState"] or not self.sm.valid["wideRoadCameraState"]:
      self.light_sensor = -1

    # Update started state
    self.started = self.sm["deviceState"].started and self.ignition

  def _update_status(self) -> None:
    if self.started and self.sm.updated["selfdriveState"]:
      ss = self.sm["selfdriveState"]
      state = ss.state

      if state in (log.SelfdriveState.OpenpilotState.preEnabled, log.SelfdriveState.OpenpilotState.overriding):
        self.status = UIStatus.OVERRIDE
      else:
        self.status = UIStatus.ENGAGED if ss.enabled else UIStatus.DISENGAGED

    # Check for engagement state changes
    if self.engaged != self._engaged_prev:
      self._engaged_prev = self.engaged

    # Handle onroad/offroad transition
    if self.started != self._started_prev or self.sm.frame == 1:
      if self.started:
        self.status = UIStatus.DISENGAGED
        self.started_frame = self.sm.frame

      self._started_prev = self.started

  def _update_params(self) -> None:
    try:
      self.is_metric = self.params.get_bool("IsMetric")
    except UnknownKeyName:
      self.is_metric = False


class Device:
  def __init__(self):
    self._ignition = False
    self._interaction_time: float = -1
    self._interactive_timeout_callbacks: list[Callable] = []
    self._prev_timed_out = False
    self._awake = False

    self._offroad_brightness: int = BACKLIGHT_OFFROAD
    self._last_brightness: int = 0
    self._brightness_filter = FirstOrderFilter(BACKLIGHT_OFFROAD, 10.00, 1 / DEFAULT_FPS)
    self._brightness_thread: threading.Thread | None = None

    # Onroad display behavior variables
    self._onroad_display_behavior = 0  # Default: Do Nothing
    self._onroad_display_timeout = 0    # Default: 30 seconds
    self._onroad_display_timer: float = 0
    self._onroad_display_active = False
    self._original_brightness = 0

    # Test mode variables
    self._test_mode_active = False
    self._test_sequence_step = 0
    self._test_step_timer = 0
    self._test_alert_simulated = False

  def reset_interactive_timeout(self, timeout: int = -1) -> None:
    if timeout == -1:
      timeout = 10 if ui_state.ignition else 30
    self._interaction_time = time.monotonic() + timeout

  def add_interactive_timeout_callback(self, callback: Callable):
    self._interactive_timeout_callbacks.append(callback)

  def update(self):
    # do initial reset
    if self._interaction_time <= 0:
      self.reset_interactive_timeout()

    self._update_brightness()
    self._update_wakefulness()
    self._update_onroad_display_behavior()
    self._update_test_mode()

  def set_offroad_brightness(self, brightness: int):
    # TODO: not yet used, should be used in prime widget for QR code, etc.
    self._offroad_brightness = min(max(brightness, 0), 100)

  def _get_onroad_display_timeout_seconds(self) -> int:
    """Convert timeout index to actual seconds"""
    timeout_map = {
      0: 30,   # 30 seconds
      1: 60,   # 1 minute
      2: 120,  # 2 minutes
      3: 180,  # 3 minutes
      4: 300,  # 5 minutes
      5: 600,  # 10 minutes
      6: 900   # 15 minutes
    }
    return timeout_map.get(self._onroad_display_timeout, 30)

  def _update_test_mode(self):
    """Update test mode when offroad"""
    if ui_state.started:
      return  # Only run test when offroad

    try:
      test_enabled = self.params.get("OffroadDisplayTest") == "1"
    except (ValueError, TypeError):
      test_enabled = False

    if test_enabled and not self._test_mode_active:
      print(f"[DEBUG] Test mode activated")
      self._test_mode_active = True
      self._test_sequence_step = 0
      self._test_step_timer = time.monotonic()
      self._test_alert_simulated = False
      # Store current brightness for test
      self._original_brightness = max(1, self._last_brightness)
      print(f"[DEBUG] Test mode: stored brightness {self._original_brightness}")

    elif not test_enabled and self._test_mode_active:
      print(f"[DEBUG] Test mode deactivated, restoring brightness")
      self._test_mode_active = False
      self._restore_original_brightness()
      return

    if not self._test_mode_active:
      return

    # Test sequence timing (3 seconds per step)
    step_duration = 3.0
    current_time = time.monotonic()

    if current_time - self._test_step_timer >= step_duration:
      self._test_sequence_step += 1
      self._test_step_timer = current_time
      print(f"[DEBUG] Test mode: step {self._test_sequence_step}")

      if self._test_sequence_step == 1:
        # Step 1: Dim to 70%
        print(f"[DEBUG] Test mode: dimming to 70%")
        self._apply_test_brightness(70)
      elif self._test_sequence_step == 2:
        # Step 2: Dim to 50%
        print(f"[DEBUG] Test mode: dimming to 50%")
        self._apply_test_brightness(50)
      elif self._test_sequence_step == 3:
        # Step 3: Dim to 30%
        print(f"[DEBUG] Test mode: dimming to 30%")
        self._apply_test_brightness(30)
      elif self._test_sequence_step == 4:
        # Step 4: Turn off (dim to 5% for safety)
        print(f"[DEBUG] Test mode: turning off (dimming to 5%)")
        self._apply_test_brightness(5)
      elif self._test_sequence_step == 5:
        # Step 5: Simulate alert - wake screen
        print(f"[DEBUG] Test mode: simulating alert - waking screen")
        self._simulate_alert_wake()
      elif self._test_sequence_step == 6:
        # Step 6: Restore original brightness
        print(f"[DEBUG] Test mode: restoring original brightness")
        self._restore_original_brightness()
        self._test_sequence_step = 0  # Reset for next cycle
        print(f"[DEBUG] Test mode: cycle complete, restarting")

  def _apply_test_brightness(self, percentage: int):
    """Apply brightness for test mode"""
    brightness = max(1, int(self._original_brightness * percentage / 100))
    print(f"[DEBUG] Test mode: setting brightness to {brightness} ({percentage}% of {self._original_brightness})")

    if self._brightness_thread is None or not self._brightness_thread.is_alive():
      self._brightness_thread = threading.Thread(target=HARDWARE.set_screen_brightness, args=(brightness,))
      self._brightness_thread.start()
      self._last_brightness = brightness

  def _simulate_alert_wake(self):
    """Simulate an alert to wake the screen"""
    print(f"[DEBUG] Test mode: simulating alert wake")
    # Restore to full brightness to simulate alert
    if self._brightness_thread is None or not self._brightness_thread.is_alive():
      self._brightness_thread = threading.Thread(target=HARDWARE.set_screen_brightness, args=(self._original_brightness,))
      self._brightness_thread.start()
      self._last_brightness = self._original_brightness

  def _update_onroad_display_behavior(self):
    """Update onroad display behavior based on parameters"""
    print(f"[DEBUG] _update_onroad_display_behavior called - ui_state.started: {ui_state.started}")

    # Skip if test mode is active
    if self._test_mode_active:
      print(f"[DEBUG] Skipping onroad behavior - test mode active")
      return

    # Read current parameters
    try:
      behavior = int(self.params.get("OnroadDisplayBehavior", "0"))
      timeout = int(self.params.get("OnroadDisplayTimeout", "0"))
      print(f"[DEBUG] Read parameters - behavior: {behavior}, timeout: {timeout}")
    except (ValueError, TypeError) as e:
      print(f"[DEBUG] Error reading parameters: {e}, using defaults")
      behavior = 0
      timeout = 0

    # Check if parameters changed
    if (behavior != self._onroad_display_behavior or
        timeout != self._onroad_display_timeout):
      print(f"[DEBUG] Parameters changed - old behavior: {self._onroad_display_behavior}, new: {behavior}")
      print(f"[DEBUG] Parameters changed - old timeout: {self._onroad_display_timeout}, new: {timeout}")
      self._onroad_display_behavior = behavior
      self._onroad_display_timeout = timeout
      self._onroad_display_timer = 0
      self._onroad_display_active = False
      print(f"[DEBUG] Reset state - timer: {self._onroad_display_timer}, active: {self._onroad_display_active}")

    # Only apply behavior if not "Do Nothing" (behavior != 0)
    if behavior == 0:
      print(f"[DEBUG] Behavior is 'Do Nothing', checking if need to restore")
      if self._onroad_display_active:
        print(f"[DEBUG] Restoring brightness from 'Do Nothing' state")
        # Restore original brightness
        self._restore_original_brightness()
        self._onroad_display_active = False
      return

    # Check if we should activate the onroad display behavior
    if ui_state.started and not self._onroad_display_active:
      print(f"[DEBUG] Onroad and not active, checking timeout")
      timeout_seconds = self._get_onroad_display_timeout_seconds()
      print(f"[DEBUG] Timeout seconds: {timeout_seconds}")
      if timeout_seconds > 0:
        if self._onroad_display_timer == 0:
          self._onroad_display_timer = time.monotonic() + timeout_seconds
          print(f"[DEBUG] Started timer at {time.monotonic()}, will expire at {self._onroad_display_timer}")
        elif time.monotonic() >= self._onroad_display_timer:
          print(f"[DEBUG] Timer expired, applying behavior {behavior}")
          self._apply_onroad_display_behavior(behavior)
          self._onroad_display_active = True
        else:
          remaining = self._onroad_display_timer - time.monotonic()
          print(f"[DEBUG] Timer running, {remaining:.1f} seconds remaining")

    # Reset timer when not onroad
    if not ui_state.started:
      if self._onroad_display_timer > 0 or self._onroad_display_active:
        print(f"[DEBUG] No longer onroad, resetting state")
      self._onroad_display_timer = 0
      if self._onroad_display_active:
        print(f"[DEBUG] Restoring brightness due to offroad transition")
        self._restore_original_brightness()
        self._onroad_display_active = False

  def _apply_onroad_display_behavior(self, behavior: int):
    """Apply the selected onroad display behavior"""
    print(f"[DEBUG] _apply_onroad_display_behavior called with behavior: {behavior}")
    if behavior == 0:  # Do Nothing
      print(f"[DEBUG] Behavior 0 - Do Nothing, returning")
      return
    elif behavior == 1:  # Dim 70%
      print(f"[DEBUG] Behavior 1 - Dimming to 70%")
      self._dim_display(70)
    elif behavior == 2:  # Dim 50%
      print(f"[DEBUG] Behavior 2 - Dimming to 50%")
      self._dim_display(50)
    elif behavior == 3:  # Dim 30%
      print(f"[DEBUG] Behavior 3 - Dimming to 30%")
      self._dim_display(30)
    elif behavior == 4:  # Turn Off
      print(f"[DEBUG] Behavior 4 - Turning off display")
      self._turn_off_display()
    else:
      print(f"[DEBUG] Unknown behavior: {behavior}")

  def _dim_display(self, percentage: int):
    """Dim the display to the specified percentage"""
    print(f"[DEBUG] _dim_display called with percentage: {percentage}")
    if not self._onroad_display_active:
      # Store current brightness before dimming
      self._original_brightness = self._last_brightness
      self._onroad_display_active = True
      print(f"[DEBUG] Stored original brightness: {self._original_brightness}")

    # Calculate dimmed brightness
    dimmed_brightness = max(1, int(self._original_brightness * percentage / 100))
    print(f"[DEBUG] Calculated dimmed brightness: {dimmed_brightness} (from {self._original_brightness} * {percentage}%)")

    if dimmed_brightness != self._last_brightness:
      print(f"[DEBUG] Applying dimmed brightness: {dimmed_brightness}")
      if self._brightness_thread is None or not self._brightness_thread.is_alive():
        print(f"[DEBUG] Starting brightness thread")
        self._brightness_thread = threading.Thread(target=HARDWARE.set_screen_brightness, args=(dimmed_brightness,))
        self._brightness_thread.start()
        self._last_brightness = dimmed_brightness
        print(f"[DEBUG] Brightness thread started, last_brightness updated to: {self._last_brightness}")
      else:
        print(f"[DEBUG] Brightness thread already running, skipping")
    else:
      print(f"[DEBUG] Brightness unchanged, no action needed")

  def _turn_off_display(self):
    """Turn off the display completely"""
    print(f"[DEBUG] _turn_off_display called")
    if not self._onroad_display_active:
      # Store current brightness before turning off
      self._original_brightness = self._last_brightness
      self._onroad_display_active = True
      print(f"[DEBUG] Stored original brightness: {self._original_brightness}")

    if self._last_brightness != 0:
      print(f"[DEBUG] Turning off display (current brightness: {self._last_brightness})")
      if self._brightness_thread is None or not self._brightness_thread.is_alive():
        print(f"[DEBUG] Starting brightness thread for turn off")
        self._brightness_thread = threading.Thread(target=HARDWARE.set_screen_brightness, args=(0,))
        self._brightness_thread.start()
        self._last_brightness = 0
        print(f"[DEBUG] Display turned off, last_brightness set to: {self._last_brightness}")
      else:
        print(f"[DEBUG] Brightness thread already running, skipping turn off")
    else:
      print(f"[DEBUG] Display already off, no action needed")

  def _restore_original_brightness(self):
    """Restore the original brightness after onroad display behavior"""
    print(f"[DEBUG] _restore_original_brightness called")
    print(f"[DEBUG] Original brightness: {self._original_brightness}, current: {self._last_brightness}")
    if self._original_brightness > 0 and self._last_brightness != self._original_brightness:
      print(f"[DEBUG] Restoring brightness from {self._last_brightness} to {self._original_brightness}")
      if self._brightness_thread is None or not self._brightness_thread.is_alive():
        print(f"[DEBUG] Starting brightness thread for restore")
        self._brightness_thread = threading.Thread(target=HARDWARE.set_screen_brightness, args=(self._original_brightness,))
        self._brightness_thread.start()
        self._last_brightness = self._original_brightness
        print(f"[DEBUG] Brightness restored, last_brightness updated to: {self._last_brightness}")
      else:
        print(f"[DEBUG] Brightness thread already running, skipping restore")
    else:
      print(f"[DEBUG] No restore needed - original: {self._original_brightness}, current: {self._last_brightness}")

  def _update_brightness(self):
    print(f"[DEBUG] _update_brightness called - onroad_display_active: {self._onroad_display_active}")
    # Skip brightness update if onroad display behavior is active
    if self._onroad_display_active:
      print(f"[DEBUG] Skipping brightness update - onroad display behavior active")
      return

    print(f"[DEBUG] Proceeding with normal brightness update")
    clipped_brightness = self._offroad_brightness

    if ui_state.started and ui_state.light_sensor >= 0:
      clipped_brightness = ui_state.light_sensor

      # CIE 1931 - https://www.photonstophotos.net/GeneralTopics/Exposure/Psychometric_Lightness_and_Gamma.htm
      if clipped_brightness <= 8:
        clipped_brightness = clipped_brightness / 903.3
      else:
        clipped_brightness = ((clipped_brightness + 16.0) / 116.0) ** 3.0

      clipped_brightness = float(np.clip(100 * clipped_brightness, 10, 100))

    brightness = round(self._brightness_filter.update(clipped_brightness))
    if not self._awake:
      brightness = 0

    if brightness != self._last_brightness:
      if self._brightness_thread is None or not self._brightness_thread.is_alive():
        print(f"[DEBUG] Setting display brightness to: {brightness}")
        self._brightness_thread = threading.Thread(target=HARDWARE.set_screen_brightness, args=(brightness,))
        self._brightness_thread.start()
        self._last_brightness = brightness
      else:
        print(f"[DEBUG] Brightness thread already running, skipping update")
    else:
      print(f"[DEBUG] Brightness unchanged: {brightness}")

  def _update_wakefulness(self):
    # Handle interactive timeout
    ignition_just_turned_off = not ui_state.ignition and self._ignition
    self._ignition = ui_state.ignition

    if ignition_just_turned_off or any(ev.left_down for ev in gui_app.mouse_events):
      self.reset_interactive_timeout()

    interaction_timeout = time.monotonic() > self._interaction_time
    if interaction_timeout and not self._prev_timed_out:
      for callback in self._interactive_timeout_callbacks:
        callback()
    self._prev_timed_out = interaction_timeout

    self._set_awake(ui_state.ignition or not interaction_timeout)

  def _set_awake(self, on: bool):
    if on != self._awake:
      self._awake = on
      cloudlog.debug(f"setting display power {int(on)}")
      HARDWARE.set_display_power(on)


# Global instance
ui_state = UIState()
device = Device()
