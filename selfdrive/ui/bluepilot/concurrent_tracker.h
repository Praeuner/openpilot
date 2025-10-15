// selfdrive/ui/bluepilot/concurrent_tracker.h
// Simple RAII-based tracker for QtConcurrent tasks to help debug thread pool issues

#pragma once

#include <QDateTime>
#include <QString>
#include <QMutex>
#include <QMutexLocker>
#include <QMap>
#include <fstream>
#include <chrono>

#include "selfdrive/ui/bluepilot/bp_logging.h"

// Simple tracker for concurrent tasks - logs start/end to help debug thread pool saturation
class ConcurrentTaskTracker {
public:
  static ConcurrentTaskTracker& instance() {
    static ConcurrentTaskTracker instance;
    return instance;
  }

  // Log task start - returns task ID
  int taskStarted(const QString& taskName) {
    QMutexLocker locker(&mutex_);
    int taskId = nextTaskId_++;

    TaskInfo info;
    info.name = taskName;
    info.startTime = QDateTime::currentDateTime();
    activeTasks_[taskId] = info;

    logToFile("START", taskId, taskName, 0);

    BPLog::bpDebugGeneral() << "[concurrent.tracker] Task " << taskId << " started: "
                            << taskName.toStdString() << " (active: " << activeTasks_.size() << ")"
                            << std::endl;

    if (activeTasks_.size() > 5) {
      BPLog::bpWarn() << "[concurrent.tracker] WARNING: " << activeTasks_.size()
                      << " concurrent tasks running!" << std::endl;
    }

    return taskId;
  }

  // Log task completion
  void taskCompleted(int taskId) {
    QMutexLocker locker(&mutex_);

    if (!activeTasks_.contains(taskId)) {
      BPLog::bpWarn() << "[concurrent.tracker] Task " << taskId << " completed but was not tracked!" << std::endl;
      return;
    }

    TaskInfo info = activeTasks_[taskId];
    qint64 duration = info.startTime.msecsTo(QDateTime::currentDateTime());

    logToFile("END", taskId, info.name, duration);

    BPLog::bpDebugGeneral() << "[concurrent.tracker] Task " << taskId << " completed: "
                            << info.name.toStdString() << " in " << duration << "ms (active: "
                            << (activeTasks_.size() - 1) << ")" << std::endl;

    activeTasks_.remove(taskId);
  }

  // Get info about currently active tasks
  QString getActiveTasksInfo() {
    QMutexLocker locker(&mutex_);

    if (activeTasks_.isEmpty()) {
      return "No active concurrent tasks";
    }

    QString info = QString("Active concurrent tasks: %1\n").arg(activeTasks_.size());
    QDateTime now = QDateTime::currentDateTime();

    for (auto it = activeTasks_.constBegin(); it != activeTasks_.constEnd(); ++it) {
      qint64 runningTime = it->startTime.msecsTo(now);
      info += QString("  Task %1: %2 (running %3ms)\n")
                  .arg(it.key())
                  .arg(it->name)
                  .arg(runningTime);
    }

    return info;
  }

private:
  ConcurrentTaskTracker() : nextTaskId_(1) {
    BPLog::bpInfo() << "[concurrent.tracker] Initialized - logging to /data/concurrent_tasks.log" << std::endl;
  }

  ~ConcurrentTaskTracker() = default;
  ConcurrentTaskTracker(const ConcurrentTaskTracker&) = delete;
  ConcurrentTaskTracker& operator=(const ConcurrentTaskTracker&) = delete;

  void logToFile(const QString& event, int taskId, const QString& taskName, qint64 duration) {
    static std::ofstream logFile("/data/concurrent_tasks.log", std::ios::app);

    if (logFile.is_open()) {
      auto now = std::chrono::system_clock::now();
      auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()).count();

      logFile << now_ms << ","
              << event.toStdString() << ","
              << taskId << ","
              << taskName.toStdString() << ","
              << duration << ","
              << activeTasks_.size() << "\n";
      logFile.flush();
    }
  }

  struct TaskInfo {
    QString name;
    QDateTime startTime;
  };

  QMutex mutex_;
  int nextTaskId_;
  QMap<int, TaskInfo> activeTasks_;
};

// RAII guard to automatically track task lifecycle
class ConcurrentTaskGuard {
public:
  explicit ConcurrentTaskGuard(const QString& taskName)
    : taskId_(ConcurrentTaskTracker::instance().taskStarted(taskName)) {}

  ~ConcurrentTaskGuard() {
    ConcurrentTaskTracker::instance().taskCompleted(taskId_);
  }

  // Delete copy/move to prevent misuse
  ConcurrentTaskGuard(const ConcurrentTaskGuard&) = delete;
  ConcurrentTaskGuard& operator=(const ConcurrentTaskGuard&) = delete;

private:
  int taskId_;
};

// Convenience macro
#define TRACK_CONCURRENT_TASK(name) ConcurrentTaskGuard __concurrent_guard(name)
