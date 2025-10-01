#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/seek_controller.h"

#include "selfdrive/ui/bluepilot/bp_logging.h"

SeekController::SeekController(QObject *parent) : QObject(parent) {
  coalesce_timer_ = new QTimer(this);
  coalesce_timer_->setSingleShot(true);
  coalesce_timer_->setInterval(kCoalesceDelayMs);
  QObject::connect(coalesce_timer_, &QTimer::timeout, this, &SeekController::executePendingSeek);
}

void SeekController::requestSeek(int64_t target_ms) {
  pending_seek_ms_ = target_ms;
  has_pending_seek_ = true;
  coalesce_timer_->start();
  BPLog::bpDebugVideo() << "[bp.video.seek] request" << target_ms << "ms (coalescing)" << std::endl;
}

void SeekController::executePendingSeek() {
  if (!has_pending_seek_) {
    return;
  }
  BPLog::bpDebugVideo() << "[bp.video.seek] execute" << pending_seek_ms_ << "ms" << std::endl;
  emit seekReady(pending_seek_ms_);
  has_pending_seek_ = false;
}
