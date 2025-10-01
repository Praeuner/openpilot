// selfdrive/ui/bluepilot/qt/offroad/routes_panel/segment_buffer.cc

#include <QDir>
#include <QFile>
#include <QMutexLocker>
#include <QThread>
#include <QtConcurrent>
#include "selfdrive/ui/bluepilot/bp_logging.h"

#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/segment_buffer.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/bp_frame_reader.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/segment_index.h"

namespace {
constexpr int kWaitIterations = 100;  // 5 seconds max (100 * 50ms)
constexpr int kWaitSleepMs = 50;
}

SegmentBuffer::SegmentBuffer(std::shared_ptr<SegmentIndex> segment_index)
    : cache_(kMaxCacheSize), segment_index_(std::move(segment_index)) {
  cache_.setMaxCost(kMaxCacheSize);
}

SegmentBuffer::~SegmentBuffer() {
  clearCache();
}

void SegmentBuffer::setRoute(const QString &route_id) {
  QMutexLocker locker(&cache_mutex_);
  route_id_ = route_id;
  cache_.clear();
  loading_segments_.clear();
  BPLog::bpDebugVideo() << "[bp.video.segment_buffer] setRoute | route_id: " << route_id_.toStdString() << std::endl;
}

std::shared_ptr<FrameReader> SegmentBuffer::getReader(int segment_idx, CameraKind camera) {
  const QString key = cacheKey(segment_idx, camera);

  {
    QMutexLocker locker(&cache_mutex_);
    if (auto cached = lookupCacheLocked(key)) {
      if (cached->camera == camera) {
        BPLog::bpDebugVideo() << "[bp.video.segment_buffer] getReader | cache hit: " << key.toStdString()
                 << " | camera:" << VideoController::cameraToString(cached->camera).toStdString()
                 << " | hw:" << cached->hardware_decoder << std::endl;
        return cached->reader;
      }

      if (cached->hardware_decoder) {
        BPLog::bpDebugVideo() << "[bp.video.segment_buffer] getReader | cache evict due to HW decoder change" << std::endl;
        cache_.remove(key);
      }
    }

    if (loading_segments_.contains(key)) {
      BPLog::bpDebugVideo() << "[bp.video.segment_buffer] getReader | waiting for in-flight segment: " << key.toStdString() << std::endl;
      locker.unlock();
      for (int i = 0; i < kWaitIterations; ++i) {
        QThread::msleep(kWaitSleepMs);
        locker.relock();
        if (auto cached = lookupCacheLocked(key)) {
          return cached->reader;
        }
        if (!loading_segments_.contains(key)) {
          break;
        }
        locker.unlock();
      }
      locker.relock();
    }

    loading_segments_.insert(key);
  }

  BPLog::bpDebugVideo() << "[bp.video.segment_buffer] getReader | request: "
           << route_id_.toStdString() << " seg:" << segment_idx
           << " camera:" << VideoController::cameraToString(camera).toStdString() << std::endl;

  const QString path = videoPath(segment_idx, camera);
  if (path.isEmpty()) {
    QMutexLocker locker(&cache_mutex_);
    loading_segments_.remove(key);
    BPLog::bpWarn() << "[bp.video.segment_buffer] getReader | empty path: " << key.toStdString() << std::endl;
    return nullptr;
  }

  auto reader = std::make_shared<FrameReader>();
  std::atomic<bool> abort{false};
  BPLog::bpDebugVideo() << "[bp.video.segment_buffer] getReader | loading reader path: " << path.toStdString() << std::endl;

  const bool use_hw_decoder = true;
  if (!reader->load(mapCameraType(camera), path.toStdString(), !use_hw_decoder, &abort)) {
    QMutexLocker locker(&cache_mutex_);
    loading_segments_.remove(key);
    BPLog::bpWarn() << "[bp.video.segment_buffer] getReader | failed to load reader: " << path.toStdString() << std::endl;
    return nullptr;
  }

  QMutexLocker locker(&cache_mutex_);
  auto cache_entry = new CachedEntry{reader, camera, reader->uses_hw_decoder};
  cache_.insert(key, cache_entry, 1);
  loading_segments_.remove(key);
  BPLog::bpDebugVideo() << "[bp.video.segment_buffer] getReader | cached reader: " << key.toStdString() << std::endl;
  return reader;
}

SegmentBuffer::CachedEntry *SegmentBuffer::lookupCacheLocked(const QString &key) const {
  return cache_.object(key);
}

// CRITICAL ISSUE #4: Preloading is completely disabled
// WORKING: Nothing - this is a stub
// NOT WORKING: Everything - preloading is essential for smooth segment transitions
// WHY DISABLED: Hardware preload was removed due to QCOM V4L2 issues
// IMPACT: Every segment transition requires synchronous load, causing 1-2 second freezes
void SegmentBuffer::preloadSegment(int segment_idx, CameraKind camera) {
  Q_UNUSED(segment_idx);
  Q_UNUSED(camera);
  // ❌ COMPLETELY DISABLED - causes 1-2 second stalls on every segment transition
  // TODO: Implement background preloading with software decoder or fix HW decoder state
}

void SegmentBuffer::clearCache() {
  QMutexLocker locker(&cache_mutex_);
  cache_.clear();
  loading_segments_.clear();
}

bool SegmentBuffer::hasSegment(int segment_idx, CameraKind camera) {
  const QString key = cacheKey(segment_idx, camera);
  QMutexLocker locker(&cache_mutex_);
  return cache_.contains(key);
}

bool SegmentBuffer::invalidate(int segment_idx, CameraKind camera, bool hw_only) {
  const QString key = cacheKey(segment_idx, camera);
  QMutexLocker locker(&cache_mutex_);
  if (auto cached = cache_.object(key)) {
    if (!hw_only || cached->hardware_decoder) {
      cache_.remove(key);
      BPLog::bpDebugVideo() << "[bp.video.segment_buffer] invalidate | key: " << key.toStdString()
               << " | hw_only:" << hw_only << std::endl;
      return true;
    }
  }
  return false;
}

QString SegmentBuffer::cacheKey(int segment_idx, CameraKind camera) const {
  return QStringLiteral("%1_%2_%3").arg(route_id_).arg(segment_idx).arg(VideoController::cameraToString(camera));
}

QString SegmentBuffer::videoPath(int segment_idx, CameraKind camera) const {
  if (!segment_index_) {
    return QString();
  }

  const QString segment_path = segment_index_->getSegmentPath(segment_idx);
  if (segment_path.isEmpty()) {
    BPLog::bpWarn() << "[bp.video.segment_buffer] videoPath | missing segment path: " << segment_idx << std::endl;
    return QString();
  }

  QString filename;
  switch (camera) {
    case CameraKind::kFront:
      filename = "fcamera.hevc";
      break;
    case CameraKind::kWide:
      filename = "ecamera.hevc";
      break;
    case CameraKind::kDriver:
      filename = "dcamera.hevc";
      break;
    case CameraKind::kFrontLq:
      filename = "qcamera.ts";
      break;
  }

  const QString full_path = segment_path + "/" + filename;
  if (!QFile::exists(full_path)) {
    BPLog::bpWarn() << "[bp.video.segment_buffer] videoPath | video missing: " << full_path.toStdString() << std::endl;
    return QString();
  }
  return full_path;
}

CameraType SegmentBuffer::mapCameraType(CameraKind camera) const {
  switch (camera) {
    case CameraKind::kWide:
      return WideRoadCam;
    case CameraKind::kDriver:
      return DriverCam;
    default:
      return RoadCam;
  }
}
