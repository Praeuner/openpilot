#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/video_controller.h"

#include <algorithm>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/frame_provider.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/segment_buffer.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/segment_index.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/seek_controller.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/video_metrics.h"

namespace {
constexpr int kFrameIntervalMs = 50;

// Register queued enums once so worker/UI cross-thread signals work everywhere
struct RegisterMetaTypes {
  RegisterMetaTypes() {
    qRegisterMetaType<VideoState>("VideoState");
    qRegisterMetaType<CameraKind>("CameraKind");
    qRegisterMetaType<VisionBuf*>("VisionBuf*");
    qRegisterMetaType<int64_t>("int64_t");
    qRegisterMetaType<uint64_t>("uint64_t");
  }
};

[[maybe_unused]] const RegisterMetaTypes kRegisterVideoTypes{};
}

VideoController::VideoController(const QString &routes_dir, QObject *parent)
    : QObject(parent) {
  BPLog::bpDebugVideo() << "[bp.video.controller] VideoController | init: " << routes_dir.toStdString() << std::endl;
  worker_thread_ = new QThread(this);
  worker_ = new VideoControllerWorker(routes_dir);
  worker_->moveToThread(worker_thread_);

  seek_controller_ = new SeekController(this);
  QObject::connect(seek_controller_, &SeekController::seekReady,
                   this, [this](int64_t position_ms) {
                     if (!worker_) {
                       return;
                     }
                     QMetaObject::invokeMethod(static_cast<QObject*>(worker_), "handleSeekToMs",
                                              Qt::QueuedConnection,
                                              Q_ARG(qint64, static_cast<qint64>(position_ms)));
                   }, Qt::QueuedConnection);
  QObject::connect(this, &VideoController::frameReleased,
                   worker_, &VideoControllerWorker::handleReleaseFrame, Qt::QueuedConnection);

  QObject::connect(worker_, &VideoControllerWorker::stateChanged,
                   this, &VideoController::stateChanged, Qt::QueuedConnection);
  QObject::connect(worker_, &VideoControllerWorker::frameReady,
                   this, &VideoController::frameReady, Qt::QueuedConnection);
  QObject::connect(worker_, &VideoControllerWorker::positionChanged,
                   this, &VideoController::positionChanged, Qt::QueuedConnection);
  QObject::connect(worker_, &VideoControllerWorker::bufferLevel,
                   this, &VideoController::bufferLevel, Qt::QueuedConnection);
  QObject::connect(worker_, &VideoControllerWorker::segmentChanged,
                   this, &VideoController::segmentChanged, Qt::QueuedConnection);
  QObject::connect(worker_, &VideoControllerWorker::error,
                   this, &VideoController::error, Qt::QueuedConnection);
  QObject::connect(worker_, &VideoControllerWorker::playbackMetrics,
                   this, &VideoController::metricsUpdated, Qt::QueuedConnection);

  BPLog::bpDebugVideo() << "[bp.video.controller] VideoController | start worker" << std::endl;
  worker_thread_->start();
}

VideoController::~VideoController() {
  BPLog::bpDebugVideo() << "[bp.video.controller] VideoController | shutdown" << std::endl;
  if (worker_) {
    BPLog::bpDebugVideo() << "[bp.video.controller] VideoController | request worker stop" << std::endl;
    QMetaObject::invokeMethod(static_cast<QObject*>(worker_), "handleStop", Qt::QueuedConnection);
  }
  if (worker_thread_) {
    worker_thread_->quit();
    worker_thread_->wait();
    worker_thread_ = nullptr;
    worker_ = nullptr;
  }
}

QString VideoController::cameraToString(CameraKind camera) {
  switch (camera) {
    case CameraKind::kFront:
      return "front";
    case CameraKind::kWide:
      return "wide";
    case CameraKind::kDriver:
      return "driver";
    case CameraKind::kFrontLq:
      return "frontlq";
  }
  return "front";
}

QString VideoController::stateToString(VideoState state) {
  switch (state) {
    case VideoState::kIdle:
      return "Idle";
    case VideoState::kLoading:
      return "Loading";
    case VideoState::kBuffering:
      return "Buffering";
    case VideoState::kPlaying:
      return "Playing";
    case VideoState::kPaused:
      return "Paused";
    case VideoState::kSeeking:
      return "Seeking";
    case VideoState::kEndOfRoute:
      return "EndOfRoute";
    case VideoState::kError:
      return "Error";
  }
  return "Unknown";
}

void VideoController::setRoute(const QString &route_id) {
  if (!worker_) {
    return;
  }
  BPLog::bpDebugVideo() << "[bp.video.controller] setRoute | route_id: " << route_id.toStdString() << std::endl;
  QMetaObject::invokeMethod(static_cast<QObject*>(worker_), "handleSetRoute", Qt::QueuedConnection, Q_ARG(QString, route_id));
  VideoMetrics::instance().mainThreadActivity();
}

void VideoController::setCamera(CameraKind camera) {
  if (!worker_) {
    return;
  }
  BPLog::bpDebugVideo() << "[bp.video.controller] setCamera | camera: " << cameraToString(camera).toStdString() << std::endl;
  QMetaObject::invokeMethod(static_cast<QObject*>(worker_), "handleSetCamera", Qt::QueuedConnection,
                           Q_ARG(CameraKind, camera));
  VideoMetrics::instance().mainThreadActivity();
}

void VideoController::play() {
  if (!worker_) {
    return;
  }
  BPLog::bpDebugVideo() << "[bp.video.controller] play" << std::endl;
  QMetaObject::invokeMethod(static_cast<QObject*>(worker_), "handlePlay", Qt::QueuedConnection);
  VideoMetrics::instance().mainThreadActivity();
}

void VideoController::pause() {
  if (!worker_) {
    return;
  }
  BPLog::bpDebugVideo() << "[bp.video.controller] pause" << std::endl;
  QMetaObject::invokeMethod(static_cast<QObject*>(worker_), "handlePause", Qt::QueuedConnection);
  VideoMetrics::instance().mainThreadActivity();
}

void VideoController::stop() {
  if (!worker_) {
    return;
  }
  BPLog::bpDebugVideo() << "[bp.video.controller] stop" << std::endl;
  QMetaObject::invokeMethod(static_cast<QObject*>(worker_), "handleStop", Qt::QueuedConnection);
  VideoMetrics::instance().mainThreadActivity();
}

void VideoController::seekToMs(int64_t position_ms) {
  if (seek_controller_) {
    BPLog::bpDebugVideo() << "[bp.video.controller] seekToMs | position_ms: " << position_ms << std::endl;
    seek_controller_->requestSeek(position_ms);
  }
  VideoMetrics::instance().mainThreadActivity();
}

void VideoController::recordUiActivity() {
  BPLog::bpDebugVideo() << "[bp.video.controller] recordUiActivity" << std::endl;
  VideoMetrics::instance().mainThreadActivity();
}

void VideoController::releaseFrame(VisionBuf *buf) {
  if (!worker_ || buf == nullptr) {
    return;
  }
  emit frameReleased(buf);
}

VideoControllerWorker::VideoControllerWorker(const QString &routes_dir)
    : routes_dir_(routes_dir) {
  BPLog::bpDebugVideo() << "[bp.video.worker] VideoControllerWorker | route_dir: " << routes_dir_.toStdString() << std::endl;
  segment_index_ = std::make_shared<SegmentIndex>();
  segment_buffer_ = std::make_shared<SegmentBuffer>(segment_index_);
  frame_provider_ = std::make_shared<FrameProvider>();

  frame_pump_timer_ = new QTimer(this);
  frame_pump_timer_->setTimerType(Qt::PreciseTimer);
  frame_pump_timer_->setInterval(kFrameIntervalMs);
  QObject::connect(frame_pump_timer_, &QTimer::timeout,
                   this, &VideoControllerWorker::pumpFrames, Qt::QueuedConnection);
  BPLog::bpDebugVideo() << "[bp.video.worker] VideoControllerWorker | pump interval(ms): " << kFrameIntervalMs << std::endl;
}

VideoControllerWorker::~VideoControllerWorker() {
  BPLog::bpDebugVideo() << "[bp.video.worker] VideoControllerWorker | destroyed" << std::endl;
  should_stop_ = true;
  if (frame_pump_timer_ != nullptr) {
    frame_pump_timer_->stop();
  }
  if (frame_provider_) {
    frame_provider_->clear();
  }
}

void VideoControllerWorker::handleSetRoute(const QString &route_id) {
  if (route_id.isEmpty()) {
    emit error(QStringLiteral("Route id empty"));
    return;
  }

  setState(VideoState::kLoading);
  VideoMetrics::instance().segmentSwitchStarted();

  if (!segment_index_->loadRoute(route_id, routes_dir_)) {
    BPLog::bpWarn() << "[bp.video.worker] handleSetRoute | failed to load route: " << route_id.toStdString() << std::endl;
    setState(VideoState::kError);
    emit error(QStringLiteral("Failed to load route: %1").arg(route_id));
    return;
  }

  segment_buffer_->setRoute(route_id);
  current_route_id_ = route_id;
  current_segment_idx_ = 0;
  current_position_ms_ = 0;
  current_frame_idx_ = 0;

  loadSegment(current_segment_idx_);

  if (auto_start_after_load_) {
    BPLog::bpDebugVideo() << "[bp.video.worker] handleSetRoute | auto start" << std::endl;
    auto_start_after_load_ = false;
    handlePlay();
  } else {
    setState(VideoState::kIdle);
  }
}

// CRITICAL ISSUE: Camera switching invalidates HW decoders and blocks worker thread
// WORKING: Detects camera change and reloads current segment with new camera
// NOT WORKING:
//   1. Invalidates hardware decoders causing 2-5 second stalls
//   2. Doesn't preserve current playback position/timestamp
//   3. loadSegment() blocks worker thread synchronously
//   4. Frame queue is cleared, losing buffered frames
void VideoControllerWorker::handleSetCamera(CameraKind camera) {
  BPLog::bpDebugVideo() << "[bp.video.worker] handleSetCamera | camera: " << VideoController::cameraToString(camera).toStdString() << std::endl;
  if (current_camera_ == camera) {
    BPLog::bpDebugVideo() << "[bp.video.worker] handleSetCamera | camera unchanged" << std::endl;
    return;
  }

  const CameraKind previous_camera = current_camera_;
  current_camera_ = camera;

  if (current_route_id_.isEmpty()) {
    return;
  }

  VideoMetrics::instance().segmentSwitchStarted();

  // ISSUE #3: Invalidating hardware decoders forces full reload on next getReader()
  // This causes 2-5 second stalls as decoder must be re-initialized from scratch
  // Was added to work around QCOM V4L2 state issues, but kills performance
  if (segment_buffer_) {
    segment_buffer_->invalidate(current_segment_idx_, previous_camera, /*hw_only=*/true);  // ❌ NUKES HW DECODER
    segment_buffer_->invalidate(current_segment_idx_, current_camera_, /*hw_only=*/false);
  }

  // ISSUE #5: loadSegment() is SYNCHRONOUS - blocks worker thread for 1-2 seconds
  // Also clears frame queue and resets position to start of segment (loses current time)
  loadSegment(current_segment_idx_);  // ❌ BLOCKS worker thread, LOSES position
}

void VideoControllerWorker::handlePlay() {
  if (current_state_ == VideoState::kLoading) {
    auto_start_after_load_ = true;
    BPLog::bpDebugVideo() << "[bp.video.worker] handlePlay | play deferred" << std::endl;
    return;
  }

  if (current_state_ == VideoState::kPlaying) {
    BPLog::bpDebugVideo() << "[bp.video.worker] handlePlay | already playing" << std::endl;
    return;
  }

  if (!frame_provider_) {
    BPLog::bpWarn() << "[bp.video.worker] handlePlay | frame provider unavailable" << std::endl;
    emit error(QStringLiteral("Frame provider unavailable"));
    return;
  }

  should_stop_ = false;
  BPLog::bpDebugVideo() << "[bp.video.worker] handlePlay | start pump, queue:"
           << (frame_provider_ ? frame_provider_->queuedFrames() : -1)
           << " reader:" << (frame_provider_ && frame_provider_->width() > 0 ? "ready" : "unset")
           << std::endl;
  frame_pump_timer_->start();
  setState(VideoState::kPlaying);
}

void VideoControllerWorker::handlePause() {
  if (current_state_ != VideoState::kPlaying) {
    BPLog::bpDebugVideo() << "[bp.video.worker] handlePause | pause ignored state: " << VideoController::stateToString(current_state_).toStdString() << std::endl;
    return;
  }

  frame_pump_timer_->stop();
  setState(VideoState::kPaused);
}

void VideoControllerWorker::handleStop() {
  should_stop_ = true;
  BPLog::bpDebugVideo() << "[bp.video.worker] handleStop" << std::endl;
  frame_pump_timer_->stop();

  if (frame_provider_) {
    frame_provider_->clear();
  }

  current_position_ms_ = 0;
  current_frame_idx_ = 0;
  setState(VideoState::kIdle);
}

// CRITICAL ISSUE: Seek handling blocks worker thread and floods with requests
// WORKING: Converts global timestamp to segment+local, updates frame provider
// NOT WORKING:
//   1. Called repeatedly during scrubbing (every 100ms) - floods worker thread
//   2. loadSegment() is SYNCHRONOUS and blocks worker for 1-2 seconds
//   3. Preload is disabled (does nothing) - see segment_buffer.cc
//   4. No state to indicate "seeking in progress" - can conflict with playback
void VideoControllerWorker::handleSeekToMs(int64_t position_ms) {
  if (current_state_ == VideoState::kLoading || current_route_id_.isEmpty()) {
    return;
  }

  // ISSUE #2: This gets called REPEATEDLY during scrubbing (every 100ms throttle)
  // Each call can trigger segment switches and cache invalidation
  auto [segment_idx, local_ms] = segment_index_->lookupGlobalMs(position_ms);
  pending_seek_segment_ = segment_idx;
  pending_seek_ms_ = position_ms;
  pending_seek_local_ms_ = local_ms;
  has_pending_seek_ = true;

  // ISSUE #4: preloadSegment() is completely disabled (does nothing)
  // Backward seeks have no preloading, causing 1-2 second stalls
  if (segment_idx < current_segment_idx_) {
    BPLog::bpDebugVideo() << "[bp.video.worker] handleSeekToMs | preload backward segment: " << segment_idx << std::endl;
    segment_buffer_->preloadSegment(segment_idx, current_camera_);  // ❌ NO-OP
  }

  // ISSUE #5: loadSegment() is SYNCHRONOUS - opens files, inits decoder, scans packets
  // Blocks worker thread for 1-2 seconds, preventing frame pumping
  if (segment_idx != current_segment_idx_) {
    BPLog::bpDebugVideo() << "[bp.video.worker] handleSeekToMs | seek segment switch: " << segment_idx << std::endl;
    loadSegment(segment_idx);  // ❌ BLOCKS worker thread
  }

  current_position_ms_ = position_ms;
  const int frame_idx = static_cast<int>(position_ms / kFrameIntervalMs);
  current_frame_idx_ = frame_idx;

  if (frame_provider_) {
    frame_provider_->seekToFrame(frame_idx);
  }

  emit positionChanged(current_position_ms_, segment_index_->totalDurationMs());
  VideoMetrics::instance().seekCompleted();
}

void VideoControllerWorker::handleReleaseFrame(VisionBuf *buf) {
  if (frame_provider_) {
    frame_provider_->releaseFrame(buf);
  }
}

// CRITICAL ISSUE: Frame pump timer fires every 50ms to deliver frames to UI thread
// WORKING: Gets next decoded frame from queue and emits to UI
// NOT WORKING:
//   1. Re-entry protection drops frames if UI thread is slow (causes stuttering)
//   2. No auto-segment-transition when frames run out (video hangs at segment end)
//   3. Position updates assume continuous playback (breaks during seeks/switches)
void VideoControllerWorker::pumpFrames() {
  // ISSUE #6: Re-entry protection - if previous frame emit is still processing on UI thread,
  // this silently drops the current frame. With slow OpenGL operations, this causes stuttering.
  if (is_pumping_.exchange(true)) {
    // BPLog::bpDebugVideo() << "[bp.video.worker] pumpFrames | pump re-entry" << std::endl;
    return;  // ❌ DROPS FRAME - UI thread too slow
  }

  struct PumpingReset {
    explicit PumpingReset(std::atomic<bool> &flag) : flag_(flag) {}
    ~PumpingReset() { flag_.store(false); }
    std::atomic<bool> &flag_;
  } reset(is_pumping_);

  if (should_stop_ || current_state_ != VideoState::kPlaying || !frame_provider_) {
    // BPLog::bpDebugVideo() << "[bp.video.worker] pumpFrames | early exit: " << should_stop_ << " | " << VideoController::stateToString(current_state_).toStdString() << std::endl;
    return;
  }

  VisionBuf *frame = frame_provider_->getNextFrame();
  if (!frame) {
    // ISSUE #1: When segment ends, FrameProvider returns nullptr indefinitely.
    // No logic here to detect end-of-segment and auto-load next segment.
    // Result: Video hangs, decode loop in FrameProvider spins waiting for frames.
    // BPLog::bpWarn() << "[bp.video.worker] pumpFrames | no frame | segment: " << current_segment_idx_
    //          << " state: " << VideoController::stateToString(current_state_).toStdString()
    //          << " queue: " << (frame_provider_ ? frame_provider_->queuedFrames() : -1)
    //          << std::endl;
    // if (frame_provider_) {
    //   BPLog::bpDebugVideo() << "[bp.video.worker] pumpFrames | frame_info width:" << frame_provider_->width()
    //            << " height:" << frame_provider_->height()
    //            << " current_idx:" << current_frame_idx_
    //            << " pending_seek:" << has_pending_seek_
    //            << " pending_seg:" << pending_seek_segment_
    //            << " pending_local_ms:" << pending_seek_local_ms_
    //            << std::endl;
    // }
    emit playbackMetrics(static_cast<int64_t>(frame_provider_->queuedFrames()) * kFrameIntervalMs,
                         frame_provider_->frameCount());
    return;
  }

  // BPLog::bpDebugVideo() << "[bp.video.worker] pumpFrames | emit frame: " << current_position_ms_ << std::endl;
  const int width = frame_provider_->width();
  const int height = frame_provider_->height();
  emit frameReady(frame, width, height, current_position_ms_);
  VideoMetrics::instance().frameDisplayed();

  VideoMetrics::instance().logSummaryIfDue();

  current_position_ms_ += static_cast<int64_t>(kFrameIntervalMs / playback_rate_);
  current_frame_idx_++;

  emit bufferLevel(frame_provider_->bufferLevel());
  emit playbackMetrics(static_cast<int64_t>(frame_provider_->queuedFrames()) * kFrameIntervalMs,
                       frame_provider_->frameCount());
  emit positionChanged(current_position_ms_, segment_index_->totalDurationMs());
}

void VideoControllerWorker::setState(VideoState new_state) {
  if (current_state_ == new_state) {
    return;
  }
  BPLog::bpDebugVideo() << "[bp.video.worker] setState | state: " << VideoController::stateToString(current_state_).toStdString()
           << "->" << VideoController::stateToString(new_state).toStdString() << std::endl;
  current_state_ = new_state;
  emit stateChanged(new_state);
}

void VideoControllerWorker::loadSegment(int segment_idx) {
  if (current_route_id_.isEmpty()) {
    BPLog::bpWarn() << "[bp.video.worker] loadSegment no route" << std::endl;
    emit error(QStringLiteral("No route loaded"));
    return;
  }

  auto reader = segment_buffer_->getReader(segment_idx, current_camera_);
  if (!reader) {
    BPLog::bpWarn() << "[bp.video.worker] loadSegment | failed segment: " << segment_idx << std::endl;
    setState(VideoState::kError);
    emit error(QStringLiteral("Failed to load segment %1").arg(segment_idx));
    return;
  }

  frame_provider_->setReader(reader);
  current_segment_idx_ = segment_idx;
  current_frame_idx_ = 0;
  current_position_ms_ = segment_index_->getGlobalMs(segment_idx, 0);

  emit segmentChanged(segment_idx, segment_index_->totalSegments());

  if (has_pending_seek_) {
    const int target_segment = segment_index_->segmentForMs(pending_seek_ms_);
    if (target_segment != current_segment_idx_) {
      loadSegment(target_segment);
      return;
    }

    has_pending_seek_ = false;
    if (frame_provider_) {
      const int frame_idx = static_cast<int>(pending_seek_ms_ / kFrameIntervalMs);
      current_frame_idx_ = frame_idx;
      current_position_ms_ = pending_seek_ms_;
      frame_provider_->seekToFrame(frame_idx);
    }
    setState(VideoState::kPaused);
  }

  preloadSegment(segment_idx + 1);
  VideoMetrics::instance().segmentSwitchCompleted();
}

void VideoControllerWorker::preloadSegment(int segment_idx) {
  if (!segment_buffer_ || segment_idx <= current_segment_idx_) {
    BPLog::bpDebugVideo() << "[bp.video.worker] preloadSegment | skip: " << segment_idx << " | current: " << current_segment_idx_ << std::endl;
    return;
  }
  if (segment_idx >= segment_index_->totalSegments()) {
    return;
  }
  BPLog::bpDebugVideo() << "[bp.video.worker] preloadSegment | preloading segments: " << segment_idx << std::endl;
  segment_buffer_->preloadSegment(segment_idx, current_camera_);
  if (segment_idx + 1 < segment_index_->totalSegments()) {
    segment_buffer_->preloadSegment(segment_idx + 1, current_camera_);
  }
}
