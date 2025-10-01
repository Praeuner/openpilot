#pragma once

#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <memory>

#include "bp_frame_reader.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/video_controller.h"

class SegmentIndex;


/// @brief Manages a small cache of decoded segments for seamless playback.
///
/// Route directory layout recap (normalized by SegmentIndex):
///   routes_root/
///     <route-base>--0/
///       fcamera.hevc  (front HDR)
///       qcamera.ts    (front LQ legacy)
///       ecamera.hevc  (wide road)
///       dcamera.hevc  (driver camera)
///       ... additional loggerd outputs
///     <route-base>--1/
///     <route-base>--2/
///     ...
///
/// SegmentBuffer turns a `(segment_idx, CameraKind)` pair into a concrete video
/// file path.  The filenames are fixed; only the segment directory varies.
class SegmentBuffer {
public:
  explicit SegmentBuffer(std::shared_ptr<SegmentIndex> segment_index);
  ~SegmentBuffer();

  void setRoute(const QString &route_id);
  std::shared_ptr<FrameReader> getReader(int segment_idx, CameraKind camera);
  void preloadSegment(int segment_idx, CameraKind camera);
  void clearCache();
  bool hasSegment(int segment_idx, CameraKind camera);
  bool invalidate(int segment_idx, CameraKind camera, bool hw_only = false);

private:
  struct CachedEntry {
    std::shared_ptr<FrameReader> reader;
    CameraKind camera;
    bool hardware_decoder = false;
  };

  QString cacheKey(int segment_idx, CameraKind camera) const;
  QString videoPath(int segment_idx, CameraKind camera) const;
  CameraType mapCameraType(CameraKind camera) const;
  CachedEntry *lookupCacheLocked(const QString &key) const;

  QCache<QString, CachedEntry> cache_;
  QSet<QString> loading_segments_;
  QMutex cache_mutex_;
  QString route_id_;
  std::shared_ptr<SegmentIndex> segment_index_;

  static constexpr int kMaxCacheSize = 3;
};
