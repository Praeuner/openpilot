// selfdrive/ui/bluepilot/performance_logger.h
#pragma once

#include <chrono>
#include <string>
#include <fstream>

#include "selfdrive/ui/bluepilot/bp_logging.h"

// Performance logger to track UI thread blocking
class PerformanceLogger {
public:
  PerformanceLogger(const std::string& operation, int threshold_ms = 50)
    : operation_(operation), threshold_ms_(threshold_ms),
      start_(std::chrono::steady_clock::now()) {}

  ~PerformanceLogger() {
    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();

    if (duration_ms > threshold_ms_) {
      BPLog::bpWarn() << "[bp.ui.perf] " << operation_ << " took " << duration_ms
                      << "ms (threshold: " << threshold_ms_ << "ms)" << std::endl;

      // Also log to a file for later analysis
      static std::ofstream perf_log("/data/ui_performance.log", std::ios::app);
      if (perf_log.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count();
        perf_log << now_ms << "," << operation_ << "," << duration_ms << "\n";
        perf_log.flush();
      }
    }
  }

private:
  std::string operation_;
  int threshold_ms_;
  std::chrono::steady_clock::time_point start_;
};

// Macro for easy usage - use counter to avoid name collisions
#define PERF_LOG_CONCAT_(x, y) x##y
#define PERF_LOG_CONCAT(x, y) PERF_LOG_CONCAT_(x, y)
#define PERF_LOG(name, threshold) PerformanceLogger PERF_LOG_CONCAT(__perf_log_, __COUNTER__)(name, threshold)
