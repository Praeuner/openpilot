#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>

/// @brief Periodically checks for UI thread stalls and raises warnings when the
///        watchdog threshold is exceeded.
class WatchdogDetector : public QObject {
  Q_OBJECT

public:
  explicit WatchdogDetector(int timeout_ms = 100, QObject *parent = nullptr);
  ~WatchdogDetector() override = default;

  void reset();

signals:
  void watchdogWarning(int blocked_ms);

private slots:
  void check();

private:
  QTimer *timer_ = nullptr;
  QElapsedTimer last_reset_;
  int timeout_ms_ = 0;
};
