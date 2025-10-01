#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/video_metrics.h"

#include "selfdrive/ui/bluepilot/bp_logging.h"

namespace {
constexpr int kLowBufferLogCooldownMs = 1000;
constexpr int kWatchdogThresholdMs = 150;
constexpr int kSummaryIntervalMs = 30'000;
}

VideoMetrics &VideoMetrics::instance() {
  static VideoMetrics instance;
  return instance;
}

VideoMetrics::VideoMetrics() {
  last_low_buffer_log_.start();
  last_main_thread_activity_.start();
  summary_timer_.start();
}

void VideoMetrics::seekStarted() {
  seek_timer_.restart();
  ++seek_count_;
  BPLog::bpDebugVideo() << "[bp.video.metrics] seekStarted()" << std::endl;
}

void VideoMetrics::seekCompleted() {
  if (!seek_timer_.isValid()) {
    return;
  }

  const int elapsed = static_cast<int>(seek_timer_.elapsed());
  last_seek_ms_ = elapsed;
  total_seek_ms_.fetch_add(elapsed);
  const int seeks = qMax(seek_count_.load(), 1);
  BPLog::bpDebugVideo() << "[bp.video.metrics] seekCompleted | seek done: " << elapsed << " | ms avg:" << (total_seek_ms_.load() / seeks) << std::endl;
}

void VideoMetrics::segmentSwitchStarted() {
  segment_timer_.restart();
  BPLog::bpDebugVideo() << "[bp.video.metrics] segmentSwitchStarted()" << std::endl;
}

void VideoMetrics::segmentSwitchCompleted() {
  if (!segment_timer_.isValid()) {
    return;
  }

  const int elapsed = static_cast<int>(segment_timer_.elapsed());
  last_segment_switch_ms_ = elapsed;
  // BPLog::bpDebugVideo() << "[bp.video.metrics] segmentSwitchCompleted | segment switch: " << elapsed << "ms" << std::endl;
}

void VideoMetrics::frameDecoded() {
  ++frames_decoded_;
  // BPLog::bpDebugVideo() << "[bp.video.metrics] frameDecoded() | frame decoded: " << frames_decoded_.load() << std::endl;
}

void VideoMetrics::frameDisplayed() {
  ++frames_displayed_;
  // BPLog::bpDebugVideo() << "[bp.video.metrics] frameDisplayed() | frame displayed: " << frames_displayed_.load() << std::endl;
}

void VideoMetrics::frameDropped() {
  const int dropped = ++frames_dropped_;
  if (dropped % 10 == 0) {
    // BPLog::bpWarn() << "[bp.video.metrics] frameDropped() | dropped frames: " << dropped << std::endl;
  }
}

void VideoMetrics::setBufferLevel(float percent) {
  buffer_level_ = percent;
  if (percent < 20.0f && last_low_buffer_log_.elapsed() > kLowBufferLogCooldownMs) {
    BPLog::bpWarn() << "[bp.video.metrics] setBufferLevel() | low buffer: " << percent << std::endl;
    last_low_buffer_log_.restart();
  }
}

void VideoMetrics::logSummary() {
  const int seeks = seek_count_.load();
  const int avg_seek = seeks == 0 ? 0 : total_seek_ms_.load() / seeks;
  BPLog::bpDebugVideo() << "[bp.video.metrics] logSummary() | summary seeks: " << seeks << " | avg:" << avg_seek
           << " | frames: " << frames_displayed_.load() << "/" << frames_decoded_.load()
           << " | dropped: " << frames_dropped_.load()
           << " | buffer: " << buffer_level_.load() << std::endl;
}

void VideoMetrics::logSummaryIfDue() {
  if (summary_timer_.elapsed() < kSummaryIntervalMs) {
    return;
  }

  logSummary();
  summary_timer_.restart();
}

void VideoMetrics::mainThreadActivity() {
  last_main_thread_activity_.restart();
}

void VideoMetrics::checkWatchdog() {
  const int elapsed = static_cast<int>(last_main_thread_activity_.elapsed());
  if (elapsed > kWatchdogThresholdMs) {
    BPLog::bpWarn() << "[bp.video.metrics] checkWatchdog() | watchdog blocked: " << elapsed << "ms" << std::endl;
    emit watchdogWarning(elapsed);
  }
}
