#pragma once

#include <QElapsedTimer>
#include <QtGlobal>
#include <QObject>
#include <QString>
#include <atomic>

/// @brief Collects metrics for the BluePilot video pipeline.
///
/// Threading: all invocations are thread-safe. Callers may invoke the public
/// methods from either UI or worker threads.
class VideoMetrics : public QObject {
  Q_OBJECT

public:
  static VideoMetrics &instance();

  void seekStarted();
  void seekCompleted();
  void segmentSwitchStarted();
  void segmentSwitchCompleted();
  void frameDecoded();
  void frameDisplayed();
  void frameDropped();
  void setBufferLevel(float percent);
  void logSummary();
  void logSummaryIfDue();
  void mainThreadActivity();
  void checkWatchdog();

signals:
  void watchdogWarning(int blockedMs);

private:
  VideoMetrics();

  QElapsedTimer seek_timer_;
  QElapsedTimer segment_timer_;
  QElapsedTimer last_low_buffer_log_;
  QElapsedTimer last_main_thread_activity_;
  QElapsedTimer summary_timer_;

  std::atomic<int> seek_count_{0};
  std::atomic<int> total_seek_ms_{0};
  std::atomic<int> last_seek_ms_{0};
  std::atomic<int> last_segment_switch_ms_{0};
  std::atomic<int> frames_decoded_{0};
  std::atomic<int> frames_displayed_{0};
  std::atomic<int> frames_dropped_{0};
  std::atomic<float> buffer_level_{0.0f};
};
