// selfdrive/ui/bluepilot/qt/offroad/software/bp_git_manager.h
// Safe, non-blocking git operations manager

#pragma once

#include <QObject>
#include <QString>
#include <QProcess>
#include <QDateTime>
#include <QtConcurrent>
#include <functional>

/**
 * BPGitManager - Async git operations that never block UI thread
 *
 * All operations use QtConcurrent::run() and return results via signals.
 * Includes timeouts, error handling, and validation.
 *
 * Usage:
 *   gitManager->getRepoStatus([](const RepoStatus &status) {
 *     // Update UI with status
 *   });
 */
class BPGitManager : public QObject {
  Q_OBJECT

public:
  struct RepoStatus {
    QString branch;
    QString commit;
    QString commitMessage;
    QString commitDate;
    bool hasLocalChanges;
    bool hasUpdatesAvailable;
    bool isValid;
    QString error;
  };

  struct CommandResult {
    bool success;
    QString output;
    QString error;
    int exitCode;
    bool timedOut;
  };

  explicit BPGitManager(QObject *parent = nullptr);

  // Async operations (non-blocking, use callbacks)
  void getRepoStatus(std::function<void(const RepoStatus&)> callback);
  void getBranches(bool includeRemote, std::function<void(QStringList)> callback);
  void hasUncommittedChanges(std::function<void(bool)> callback);
  void hasUpdatesAvailable(std::function<void(bool)> callback);

  // Static utility methods (can be called directly)
  static CommandResult executeCommand(const QString &command, const QString &workingDir = "", int timeoutMs = 30000);
  static bool isValidGitRepo(const QString &path = "");
  static bool hasSubmodules(const QString &path = "");
  static QString getGitRoot();

signals:
  void repoStatusReady(const RepoStatus &status);
  void branchesReady(const QStringList &branches);
  void operationError(const QString &error);

private:
  QString gitRoot;

  // Internal helpers
  RepoStatus parseRepoStatus(const QString &workingDir);
  QStringList parseBranches(const QString &output);
};
