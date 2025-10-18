// selfdrive/ui/bluepilot/qt/offroad/panels/updater/bp_git_worker.h

#pragma once

#ifndef BP_GIT_WORKER_H
#define BP_GIT_WORKER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QThread>
#include <QMutex>

// Git operation results
struct GitResult {
  bool success;
  QString output;
  QString error;
  int exitCode;
  bool timedOut;
};

struct GitStatus {
  QString currentBranch;
  QString lastCommitHash;
  QString lastCommitMessage;
  QString lastCommitTime;
  bool hasLocalChanges;
  bool hasUpdatesAvailable;
  int commitsAhead;
  int commitsBehind;
};

// Worker class that runs all git operations in background thread
class BPGitWorker : public QObject {
  Q_OBJECT

public:
  explicit BPGitWorker(QObject *parent = nullptr);
  ~BPGitWorker();

  // Check if we should perform operations
  bool canPerformOperations() const;

public slots:
  // Background operations (all run in worker thread)
  void checkStatus();
  void checkForUpdates();
  void fetchBranches(bool includeRemote);
  void updateRepository();
  void switchBranch(const QString &branch);
  void resetRepository();
  void repairRepository();
  void unshallowRepository();
  void fetchCommitHistory(int count);

signals:
  // Status updates
  void statusReady(const GitStatus &status);
  void updatesCheckComplete(bool hasUpdates, int commitsAhead);
  void branchListReady(const QStringList &branches);
  void commitHistoryReady(const QStringList &commits);

  // Operation progress
  void operationStarted(const QString &operation);
  void operationProgress(const QString &message);
  void operationComplete(bool success, const QString &message);

  // Errors
  void errorOccurred(const QString &error);

  // State changes
  void canOperateChanged(bool canOperate);

private:
  QString workingDir;
  QMutex operationMutex;
  bool operationInProgress;

  // Helper methods
  GitResult executeCommand(const QString &command, int timeoutMs = 30000);
  bool isValidGitRepo() const;
  bool checkInternetConnectivity() const;
  bool checkSSHAccess() const;
  QString getCurrentBranch() const;
  bool hasUncommittedChanges() const;
};

#endif // BP_GIT_WORKER_H
