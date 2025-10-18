// selfdrive/ui/bluepilot/qt/offroad/panels/updater/bp_git_worker.cc

#include "bp_git_worker.h"
#include "common/params.h"
#include "selfdrive/ui/bluepilot/bp_logging.h"

#include <QProcess>
#include <QElapsedTimer>
#include <QDir>
#include <QCoreApplication>

BPGitWorker::BPGitWorker(QObject *parent)
    : QObject(parent), operationInProgress(false) {
  // Set working directory to openpilot root
  workingDir = QCoreApplication::applicationDirPath() + "/../..";
  BPLog::bpInfo() << "[bp.updater.worker] Initialized with working dir: " << workingDir.toStdString() << std::endl;
}

BPGitWorker::~BPGitWorker() {
  BPLog::bpInfo() << "[bp.updater.worker] Destroyed" << std::endl;
}

bool BPGitWorker::canPerformOperations() const {
  Params params;
  bool onroad = params.getBool("IsOnroad");
  if (onroad) {
    return false;
  }

  if (!isValidGitRepo()) {
    return false;
  }

  return true;
}

bool BPGitWorker::isValidGitRepo() const {
  QDir dir(workingDir);
  return dir.exists() && QDir(workingDir + "/.git").exists();
}

GitResult BPGitWorker::executeCommand(const QString &command, int timeoutMs) {
  GitResult result;
  result.success = false;
  result.timedOut = false;
  result.exitCode = -1;

  QProcess process;
  process.setWorkingDirectory(workingDir);
  process.start("/bin/bash", QStringList() << "-c" << command);

  if (!process.waitForStarted(5000)) {
    result.error = "Failed to start command: " + command;
    BPLog::bpError() << "[bp.updater.worker] " << result.error.toStdString() << std::endl;
    return result;
  }

  QElapsedTimer timer;
  timer.start();

  QByteArray stdoutData;
  QByteArray stderrData;

  while (process.state() != QProcess::NotRunning) {
    if (timer.elapsed() > timeoutMs) {
      process.kill();
      process.waitForFinished(1000);
      result.timedOut = true;
      result.error = QString("Command timed out after %1 seconds").arg(timeoutMs / 1000);
      BPLog::bpError() << "[bp.updater.worker] " << result.error.toStdString() << std::endl;
      return result;
    }

    if (process.waitForReadyRead(1000)) {
      stdoutData += process.readAllStandardOutput();
      stderrData += process.readAllStandardError();

      // Emit progress updates
      QString latestOutput = QString::fromUtf8(stdoutData).split('\n').last();
      if (!latestOutput.trimmed().isEmpty()) {
        emit operationProgress(latestOutput.trimmed());
      }
    }
  }

  stdoutData += process.readAllStandardOutput();
  stderrData += process.readAllStandardError();

  result.exitCode = process.exitCode();
  result.output = QString::fromUtf8(stdoutData).trimmed();
  result.error = QString::fromUtf8(stderrData).trimmed();
  result.success = (result.exitCode == 0);

  return result;
}

QString BPGitWorker::getCurrentBranch() const {
  QProcess process;
  process.setWorkingDirectory(workingDir);
  process.start("git", QStringList() << "rev-parse" << "--abbrev-ref" << "HEAD");

  if (process.waitForFinished(5000) && process.exitCode() == 0) {
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  }

  return QString();
}

bool BPGitWorker::hasUncommittedChanges() const {
  QProcess process;
  process.setWorkingDirectory(workingDir);
  process.start("git", QStringList() << "status" << "--porcelain");

  if (process.waitForFinished(5000) && process.exitCode() == 0) {
    return !QString::fromUtf8(process.readAllStandardOutput()).trimmed().isEmpty();
  }

  return false;
}

bool BPGitWorker::checkInternetConnectivity() const {
  QProcess process;
  process.start("ping", QStringList() << "-c" << "1" << "-W" << "2" << "8.8.8.8");
  return process.waitForFinished(3000) && process.exitCode() == 0;
}

bool BPGitWorker::checkSSHAccess() const {
  GitResult result = const_cast<BPGitWorker*>(this)->executeCommand(
    "ssh -T git@github.com 2>&1 | grep -q 'successfully authenticated'", 5000);
  return result.success;
}

void BPGitWorker::checkStatus() {
  QMutexLocker locker(&operationMutex);

  if (!canPerformOperations()) {
    emit canOperateChanged(false);
    return;
  }

  GitStatus status;

  // Get current branch
  status.currentBranch = getCurrentBranch();

  // Get last commit info
  GitResult commitInfo = executeCommand(
    "git log -1 --pretty=format:'%h|%s|%cr'", 5000);

  if (commitInfo.success) {
    QStringList parts = commitInfo.output.split('|');
    if (parts.size() >= 3) {
      status.lastCommitHash = parts[0];
      status.lastCommitMessage = parts[1];
      status.lastCommitTime = parts[2];
    }
  }

  // Check for local changes
  status.hasLocalChanges = hasUncommittedChanges();

  // Check commits ahead/behind
  GitResult aheadBehind = executeCommand(
    "git rev-list --left-right --count HEAD...@{u} 2>/dev/null", 5000);

  if (aheadBehind.success) {
    QStringList counts = aheadBehind.output.split('\t');
    if (counts.size() >= 2) {
      status.commitsAhead = counts[0].toInt();
      status.commitsBehind = counts[1].toInt();
      status.hasUpdatesAvailable = (status.commitsBehind > 0);
    }
  }

  emit statusReady(status);
  emit canOperateChanged(true);
}

void BPGitWorker::checkForUpdates() {
  QMutexLocker locker(&operationMutex);

  if (operationInProgress) {
    emit errorOccurred("Another operation is in progress");
    return;
  }

  operationInProgress = true;
  emit operationStarted("Checking for updates");

  // Check internet connectivity
  if (!checkInternetConnectivity()) {
    emit operationComplete(false, "No internet connection");
    operationInProgress = false;
    return;
  }

  // Fetch from remote
  GitResult fetchResult = executeCommand("git fetch --all", 60000);

  if (!fetchResult.success) {
    emit operationComplete(false, "Failed to fetch updates: " + fetchResult.error);
    operationInProgress = false;
    return;
  }

  // Check commits behind
  GitResult behindCount = executeCommand(
    "git rev-list --count HEAD..@{u} 2>/dev/null", 5000);

  int commitsBehind = 0;
  if (behindCount.success) {
    commitsBehind = behindCount.output.toInt();
  }

  emit updatesCheckComplete(commitsBehind > 0, commitsBehind);
  emit operationComplete(true, commitsBehind > 0 ?
    QString("%1 update(s) available").arg(commitsBehind) : "Up to date");

  operationInProgress = false;

  // Refresh status
  locker.unlock();
  checkStatus();
}

void BPGitWorker::fetchBranches(bool includeRemote) {
  QMutexLocker locker(&operationMutex);

  QString command = includeRemote ?
    "git branch -a | sed 's/^[* ] //' | sed 's|remotes/origin/||' | sort -u" :
    "git branch | sed 's/^[* ] //'";

  GitResult result = executeCommand(command, 10000);

  if (result.success) {
    QStringList branches = result.output.split('\n', Qt::SkipEmptyParts);
    branches.removeAll("HEAD");
    emit branchListReady(branches);
  } else {
    emit errorOccurred("Failed to fetch branches: " + result.error);
  }
}

void BPGitWorker::updateRepository() {
  QMutexLocker locker(&operationMutex);

  if (operationInProgress) {
    emit errorOccurred("Another operation is in progress");
    return;
  }

  operationInProgress = true;
  emit operationStarted("Updating repository");

  // Check for uncommitted changes
  if (hasUncommittedChanges()) {
    emit operationComplete(false, "Cannot update: uncommitted changes present");
    operationInProgress = false;
    return;
  }

  // Pull updates
  GitResult pullResult = executeCommand("git pull --rebase", 120000);

  if (!pullResult.success) {
    emit operationComplete(false, "Update failed: " + pullResult.error);
    operationInProgress = false;
    return;
  }

  // Update submodules
  emit operationProgress("Updating submodules...");
  GitResult submoduleResult = executeCommand(
    "git submodule update --init --recursive", 120000);

  if (!submoduleResult.success) {
    emit operationComplete(false, "Submodule update failed: " + submoduleResult.error);
    operationInProgress = false;
    return;
  }

  emit operationComplete(true, "Repository updated successfully");
  operationInProgress = false;

  // Refresh status
  locker.unlock();
  checkStatus();
}

void BPGitWorker::switchBranch(const QString &branch) {
  QMutexLocker locker(&operationMutex);

  if (operationInProgress) {
    emit errorOccurred("Another operation is in progress");
    return;
  }

  operationInProgress = true;
  emit operationStarted(QString("Switching to branch: %1").arg(branch));

  // Check for uncommitted changes
  if (hasUncommittedChanges()) {
    emit operationComplete(false, "Cannot switch: uncommitted changes present");
    operationInProgress = false;
    return;
  }

  // Checkout branch
  GitResult checkoutResult = executeCommand(
    QString("git checkout %1").arg(branch), 30000);

  if (!checkoutResult.success) {
    emit operationComplete(false, "Branch switch failed: " + checkoutResult.error);
    operationInProgress = false;
    return;
  }

  // Update submodules
  emit operationProgress("Updating submodules...");
  GitResult submoduleResult = executeCommand(
    "git submodule update --init --recursive", 120000);

  if (!submoduleResult.success) {
    emit operationComplete(false, "Submodule update failed: " + submoduleResult.error);
    operationInProgress = false;
    return;
  }

  emit operationComplete(true, QString("Switched to branch: %1").arg(branch));
  operationInProgress = false;

  // Refresh status
  locker.unlock();
  checkStatus();
}

void BPGitWorker::resetRepository() {
  QMutexLocker locker(&operationMutex);

  if (operationInProgress) {
    emit errorOccurred("Another operation is in progress");
    return;
  }

  operationInProgress = true;
  emit operationStarted("Resetting repository");

  // Reset to HEAD
  GitResult resetResult = executeCommand("git reset --hard HEAD", 30000);

  if (!resetResult.success) {
    emit operationComplete(false, "Reset failed: " + resetResult.error);
    operationInProgress = false;
    return;
  }

  // Clean untracked files
  emit operationProgress("Cleaning untracked files...");
  GitResult cleanResult = executeCommand("git clean -fd", 30000);

  if (!cleanResult.success) {
    emit operationComplete(false, "Clean failed: " + cleanResult.error);
    operationInProgress = false;
    return;
  }

  // Reset submodules
  emit operationProgress("Resetting submodules...");
  GitResult submoduleReset = executeCommand(
    "git submodule foreach --recursive 'git reset --hard HEAD && git clean -fd'", 60000);

  if (!submoduleReset.success) {
    emit operationComplete(false, "Submodule reset failed: " + submoduleReset.error);
    operationInProgress = false;
    return;
  }

  emit operationComplete(true, "Repository reset successfully");
  operationInProgress = false;

  // Refresh status
  locker.unlock();
  checkStatus();
}

void BPGitWorker::repairRepository() {
  QMutexLocker locker(&operationMutex);

  if (operationInProgress) {
    emit errorOccurred("Another operation is in progress");
    return;
  }

  operationInProgress = true;
  emit operationStarted("Repairing repository");

  // Run git fsck
  emit operationProgress("Checking repository integrity...");
  GitResult fsckResult = executeCommand("git fsck --full", 120000);

  // Run git gc
  emit operationProgress("Cleaning up repository...");
  GitResult gcResult = executeCommand("git gc --aggressive --prune=now", 300000);

  if (!gcResult.success) {
    emit operationComplete(false, "Repair failed: " + gcResult.error);
    operationInProgress = false;
    return;
  }

  // Repair submodules
  emit operationProgress("Repairing submodules...");
  GitResult submoduleSync = executeCommand("git submodule sync", 30000);
  GitResult submoduleUpdate = executeCommand(
    "git submodule update --init --recursive --force", 120000);

  if (!submoduleUpdate.success) {
    emit operationComplete(false, "Submodule repair failed: " + submoduleUpdate.error);
    operationInProgress = false;
    return;
  }

  emit operationComplete(true, "Repository repaired successfully");
  operationInProgress = false;

  // Refresh status
  locker.unlock();
  checkStatus();
}

void BPGitWorker::unshallowRepository() {
  QMutexLocker locker(&operationMutex);

  if (operationInProgress) {
    emit errorOccurred("Another operation is in progress");
    return;
  }

  operationInProgress = true;
  emit operationStarted("Unshallowing repository");

  // Check if repo is shallow
  GitResult shallowCheck = executeCommand(
    "test -f .git/shallow && echo 'shallow' || echo 'complete'", 5000);

  if (shallowCheck.output == "complete") {
    emit operationComplete(true, "Repository is already complete");
    operationInProgress = false;
    return;
  }

  // Unshallow
  GitResult unshallowResult = executeCommand(
    "git fetch --unshallow", 300000);

  if (!unshallowResult.success) {
    emit operationComplete(false, "Unshallow failed: " + unshallowResult.error);
    operationInProgress = false;
    return;
  }

  emit operationComplete(true, "Repository unshallowed successfully");
  operationInProgress = false;

  // Refresh status
  locker.unlock();
  checkStatus();
}

void BPGitWorker::fetchCommitHistory(int count) {
  QMutexLocker locker(&operationMutex);

  QString command = QString(
    "git log -%1 --pretty=format:'%h|%s|%cr|%an'").arg(count);

  GitResult result = executeCommand(command, 10000);

  if (result.success) {
    QStringList commits = result.output.split('\n', Qt::SkipEmptyParts);
    emit commitHistoryReady(commits);
  } else {
    emit errorOccurred("Failed to fetch commit history: " + result.error);
  }
}
