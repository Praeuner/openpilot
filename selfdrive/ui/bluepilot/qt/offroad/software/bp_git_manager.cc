// selfdrive/ui/bluepilot/qt/offroad/software/bp_git_manager.cc

#include "bp_git_manager.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include "selfdrive/ui/bluepilot/concurrent_tracker.h"

BPGitManager::BPGitManager(QObject *parent) : QObject(parent) {
  gitRoot = getGitRoot();
  BPLog::bpDebugGeneral() << "[bp.git.manager] Initialized with git root: " << gitRoot.toStdString() << std::endl;
}

QString BPGitManager::getGitRoot() {
  // Try to get git root from application dir
  QString appDir = QCoreApplication::applicationDirPath();
  QString gitDir = appDir + "/../..";

  if (QDir(gitDir + "/.git").exists()) {
    return QDir(gitDir).absolutePath();
  }

  // Fallback to /data/openpilot
  if (QDir("/data/openpilot/.git").exists()) {
    return "/data/openpilot";
  }

  BPLog::bpError() << "[bp.git.manager] getGitRoot | Git root not found" << std::endl;
  return appDir;
}

bool BPGitManager::isValidGitRepo(const QString &path) {
  QString repoPath = path.isEmpty() ? getGitRoot() : path;
  return QDir(repoPath + "/.git").exists();
}

bool BPGitManager::hasSubmodules(const QString &path) {
  QString repoPath = path.isEmpty() ? getGitRoot() : path;
  return QFileInfo(repoPath + "/.gitmodules").exists();
}

BPGitManager::CommandResult BPGitManager::executeCommand(const QString &command, const QString &workingDir, int timeoutMs) {
  CommandResult result;
  result.success = false;
  result.timedOut = false;
  result.exitCode = -1;

  QProcess process;
  if (!workingDir.isEmpty()) {
    process.setWorkingDirectory(workingDir);
  } else {
    process.setWorkingDirectory(getGitRoot());
  }

  BPLog::bpDebugGeneral() << "[bp.git.manager] executeCommand | Running: " << command.toStdString()
                          << " in " << process.workingDirectory().toStdString() << std::endl;

  process.start("/bin/bash", QStringList() << "-c" << command);

  if (!process.waitForStarted(5000)) {
    result.error = "Failed to start command: " + command;
    BPLog::bpError() << "[bp.git.manager] executeCommand | " << result.error.toStdString() << std::endl;
    return result;
  }

  // Track overall execution time
  QElapsedTimer totalTimer;
  totalTimer.start();

  // Read output asynchronously
  QByteArray stdoutData;
  QByteArray stderrData;

  while (process.state() != QProcess::NotRunning) {
    // Check if total timeout has been exceeded
    if (totalTimer.elapsed() > timeoutMs) {
      process.kill();
      process.waitForFinished(1000);
      result.timedOut = true;
      result.error = "Command timed out after " + QString::number(timeoutMs / 1000) + " seconds";
      BPLog::bpError() << "[bp.git.manager] executeCommand | " << result.error.toStdString() << std::endl;
      return result;
    }

    // Use a shorter timeout for individual reads
    if (process.waitForReadyRead(1000)) {
      stdoutData += process.readAllStandardOutput();
      stderrData += process.readAllStandardError();
    }
  }

  // Get remaining output
  stdoutData += process.readAllStandardOutput();
  stderrData += process.readAllStandardError();

  result.exitCode = process.exitCode();
  result.output = QString::fromUtf8(stdoutData).trimmed();
  result.error = QString::fromUtf8(stderrData).trimmed();
  result.success = (result.exitCode == 0);

  if (!result.success) {
    BPLog::bpError() << "[bp.git.manager] executeCommand | Command failed with exit code " << result.exitCode
                     << ": " << result.error.toStdString() << std::endl;
  }

  return result;
}

void BPGitManager::getRepoStatus(std::function<void(const RepoStatus&)> callback) {
  QtConcurrent::run([this, callback]() {
    TRACK_CONCURRENT_TASK("BPGitManager::getRepoStatus");

    RepoStatus status = parseRepoStatus(gitRoot);

    QMetaObject::invokeMethod(this, [this, status, callback]() {
      emit repoStatusReady(status);
      if (callback) {
        callback(status);
      }
    }, Qt::QueuedConnection);
  });
}

BPGitManager::RepoStatus BPGitManager::parseRepoStatus(const QString &workingDir) {
  RepoStatus status;
  status.isValid = false;

  // Check if valid git repo
  if (!isValidGitRepo(workingDir)) {
    status.error = "Not a valid git repository";
    BPLog::bpError() << "[bp.git.manager] parseRepoStatus | " << status.error.toStdString() << std::endl;
    return status;
  }

  // Get current branch
  auto branchResult = executeCommand("git rev-parse --abbrev-ref HEAD", workingDir, 5000);
  if (branchResult.success) {
    status.branch = branchResult.output;
  } else {
    status.error = "Failed to get current branch";
    return status;
  }

  // Get current commit
  auto commitResult = executeCommand("git rev-parse --short HEAD", workingDir, 5000);
  if (commitResult.success) {
    status.commit = commitResult.output;
  }

  // Get commit message and date
  auto logResult = executeCommand("git log -1 --pretty=format:'%s|||%cr'", workingDir, 5000);
  if (logResult.success) {
    QStringList parts = logResult.output.split("|||");
    if (parts.size() == 2) {
      status.commitMessage = parts[0];
      status.commitDate = parts[1];
    }
  }

  // Check for local changes
  auto statusResult = executeCommand("git status --porcelain", workingDir, 5000);
  if (statusResult.success) {
    status.hasLocalChanges = !statusResult.output.isEmpty();
  }

  // Check for updates (requires fetch)
  auto revListResult = executeCommand("git rev-list HEAD..@{u} --count 2>/dev/null || echo '0'", workingDir, 5000);
  if (revListResult.success) {
    status.hasUpdatesAvailable = revListResult.output.toInt() > 0;
  }

  status.isValid = true;
  return status;
}

void BPGitManager::getBranches(bool includeRemote, std::function<void(QStringList)> callback) {
  QtConcurrent::run([this, includeRemote, callback]() {
    TRACK_CONCURRENT_TASK(QString("BPGitManager::getBranches(remote=%1)").arg(includeRemote ? "yes" : "no"));

    QStringList branches;

    // Get local branches
    auto localResult = executeCommand("git branch --format='%(refname:short)'", gitRoot, 10000);
    if (localResult.success) {
      branches = parseBranches(localResult.output);
    }

    // Get remote branches if requested
    if (includeRemote) {
      auto remoteResult = executeCommand("git branch -r --format='%(refname:short)'", gitRoot, 10000);
      if (remoteResult.success) {
        QStringList remoteBranches = parseBranches(remoteResult.output);
        for (const QString &branch : remoteBranches) {
          if (branch.startsWith("origin/") && !branch.contains("->")) {
            QString branchName = branch.mid(7); // Remove "origin/"
            if (!branches.contains(branchName)) {
              branches.append(branchName);
            }
          }
        }
      }
    }

    branches.sort();

    QMetaObject::invokeMethod(this, [this, branches, callback]() {
      emit branchesReady(branches);
      if (callback) {
        callback(branches);
      }
    }, Qt::QueuedConnection);
  });
}

QStringList BPGitManager::parseBranches(const QString &output) {
  QStringList branches;
  for (const QString &line : output.split('\n', QString::SkipEmptyParts)) {
    QString branch = line.trimmed();
    if (!branch.isEmpty()) {
      branches.append(branch);
    }
  }
  return branches;
}

void BPGitManager::hasUncommittedChanges(std::function<void(bool)> callback) {
  QtConcurrent::run([this, callback]() {
    TRACK_CONCURRENT_TASK("BPGitManager::hasUncommittedChanges");

    auto result = executeCommand("git status --porcelain", gitRoot, 5000);
    bool hasChanges = result.success && !result.output.isEmpty();

    QMetaObject::invokeMethod(this, [callback, hasChanges]() {
      if (callback) {
        callback(hasChanges);
      }
    }, Qt::QueuedConnection);
  });
}

void BPGitManager::hasUpdatesAvailable(std::function<void(bool)> callback) {
  QtConcurrent::run([this, callback]() {
    TRACK_CONCURRENT_TASK("BPGitManager::hasUpdatesAvailable");

    // First try to fetch quietly
    auto fetchResult = executeCommand("git fetch --dry-run 2>&1", gitRoot, 15000);

    // Check if there are commits ahead
    auto result = executeCommand("git rev-list HEAD..@{u} --count 2>/dev/null || echo '0'", gitRoot, 5000);
    bool hasUpdates = result.success && result.output.toInt() > 0;

    QMetaObject::invokeMethod(this, [callback, hasUpdates]() {
      if (callback) {
        callback(hasUpdates);
      }
    }, Qt::QueuedConnection);
  });
}
