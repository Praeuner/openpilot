// selfdrive/ui/bluepilot/qt/offroad/routes_panel/watchdog_detector.cc

#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/watchdog_detector.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/video_metrics.h"

namespace {
constexpr int kDefaultIntervalMs = 50;
}

WatchdogDetector::WatchdogDetector(int timeout_ms, QObject *parent)
    : QObject(parent), timeout_ms_(timeout_ms) {
  timer_ = new QTimer(this);
  timer_->setInterval(kDefaultIntervalMs);
  QObject::connect(timer_, &QTimer::timeout, this, &WatchdogDetector::check);
  timer_->start();
  last_reset_.start();
}

void WatchdogDetector::reset() {
  last_reset_.restart();
}

void WatchdogDetector::check() {
  const int elapsed = static_cast<int>(last_reset_.elapsed());
  if (elapsed > timeout_ms_) {
    emit watchdogWarning(elapsed);
    last_reset_.restart();
  }
}
