#pragma once

#include "bp_frame_reader.h"

#include <QObject>
#include <QThread>
#include <atomic>
#include <memory>


class QTimer;
class VideoControllerWorker;
class SeekController;

/// @brief Playback state for the video controller.
enum class VideoState {
  kIdle = 0,
  kLoading,
  kBuffering,
  kPlaying,
  kPaused,
  kSeeking,
  kEndOfRoute,
  kError
};

/// @brief Supported camera streams for playback.
enum class CameraKind {
  kFront,
  kWide,
  kDriver,
  kFrontLq
};

class VideoController : public QObject {
  Q_OBJECT

public:
  explicit VideoController(const QString &routes_dir, QObject *parent = nullptr);
  ~VideoController() override;

  static QString cameraToString(CameraKind camera);
  static QString stateToString(VideoState state);

public slots:
  void setRoute(const QString &route_id);
  void setCamera(CameraKind camera);
  void play();
  void pause();
  void stop();
  void seekToMs(int64_t position_ms);
  void recordUiActivity();
  void releaseFrame(VisionBuf *buf);

signals:
  void frameReady(VisionBuf *buf, int width, int height, int64_t timestamp_ms);
  void stateChanged(VideoState state);
  void positionChanged(int64_t position_ms, int64_t duration_ms);
  void bufferLevel(float percentage);
  void segmentChanged(int current_segment, int total_segments);
  void error(const QString &message);
  void frameReleased(VisionBuf *buf);
  void metricsUpdated(int64_t buffered_ms, int total_frames);

private:
  VideoControllerWorker *worker_ = nullptr;
  QThread *worker_thread_ = nullptr;
  class SeekController *seek_controller_ = nullptr;
};

class VideoControllerWorker : public QObject {
  Q_OBJECT

public:
  explicit VideoControllerWorker(const QString &routes_dir);
  ~VideoControllerWorker() override;

public slots:
  void handleSetRoute(const QString &route_id);
  void handleSetCamera(CameraKind camera);
  void handlePlay();
  void handlePause();
  void handleStop();
  void handleSeekToMs(int64_t position_ms);
  void handleReleaseFrame(VisionBuf *buf);

signals:
  void stateChanged(VideoState state);
  void frameReady(VisionBuf *buf, int width, int height, int64_t timestamp_ms);
  void positionChanged(int64_t position_ms, int64_t duration_ms);
  void bufferLevel(float percentage);
  void segmentChanged(int current_segment, int total_segments);
  void error(const QString &message);
  void playbackMetrics(int64_t buffered_ms, int total_frames);

private slots:
  void pumpFrames();

private:
  void setState(VideoState new_state);
  void loadSegment(int segment_idx);
  void preloadSegment(int segment_idx);

  QString routes_dir_;
  VideoState current_state_ = VideoState::kIdle;

  std::shared_ptr<class SegmentIndex> segment_index_;
  std::shared_ptr<class SegmentBuffer> segment_buffer_;
  std::shared_ptr<class FrameProvider> frame_provider_;

  QString current_route_id_;
  CameraKind current_camera_ = CameraKind::kFront;
  int current_segment_idx_ = 0;
  int current_frame_idx_ = 0;
  int64_t current_position_ms_ = 0;
  double playback_rate_ = 1.0;

  // Seek management
  int pending_seek_segment_ = -1;
  int64_t pending_seek_ms_ = -1;
  int64_t pending_seek_local_ms_ = -1;
  bool has_pending_seek_ = false;

  QTimer *frame_pump_timer_ = nullptr;
  std::atomic<bool> should_stop_{false};
  std::atomic<bool> is_pumping_{false};
  bool auto_start_after_load_ = false;
};

