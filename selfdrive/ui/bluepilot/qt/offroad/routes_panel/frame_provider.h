#pragma once

#include <QMutex>
#include <QObject>
#include <deque>
#include <unordered_set>
#include <vector>
#include <QWaitCondition>
#include <QFuture>
#include <atomic>
#include <memory>

#include "bp_frame_reader.h"

/// @brief Decodes frames on a background worker and buffers them for playback.
class FrameProvider : public QObject {
  Q_OBJECT

public:
  FrameProvider();
  ~FrameProvider() override;

  void setReader(std::shared_ptr<FrameReader> reader);
  VisionBuf *getNextFrame();
  void seekToFrame(int frame_idx);
  void clear();
  float bufferLevel() const;
  int queuedFrames() const;
  int width() const;
  int height() const;
  int frameCount() const;
  void releaseFrame(VisionBuf *buf);

private:
  void startDecodeThread();
  void stopDecodeThread();
  void decodeLoop();
  void prepareBufferPoolLocked(int width, int height, int stride);
  VisionBuf *acquireBufferLocked();
  void releaseBufferLocked(VisionBuf *buf);
  void resetBufferPoolLocked();

  std::shared_ptr<FrameReader> current_reader_;
  mutable QMutex queue_mutex_;
  std::deque<VisionBuf *> frame_queue_;
  std::unordered_set<VisionBuf *> in_flight_buffers_;
  std::unordered_set<VisionBuf *> available_buffer_set_;
  std::vector<VisionBuf *> available_buffers_;
  std::vector<std::unique_ptr<VisionBuf>> buffer_storage_;
  QWaitCondition queue_not_full_;

  std::atomic<int> current_frame_idx_{0};
  std::atomic<int> target_frame_idx_{0};
  std::atomic<bool> should_stop_{false};

  QFuture<void> decode_future_;

  bool buffers_initialized_ = false;
  int buffer_width_ = 0;
  int buffer_height_ = 0;
  int buffer_stride_ = 0;
  size_t buffer_uv_offset_ = 0;
  size_t frame_payload_size_ = 0;
  int pool_allocation_limit_ = 12;

  static constexpr int kMaxQueueSize = 60;   // 3 seconds at 20 FPS
  static constexpr int kHighWatermark = 50;  // Pause decoder when queue is saturated
  static constexpr int kLowWatermark = 20;   // Resume decoding when enough frames consumed
};
