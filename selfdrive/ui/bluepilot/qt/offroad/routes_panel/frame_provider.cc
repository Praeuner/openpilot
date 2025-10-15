#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/frame_provider.h"

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include "selfdrive/ui/bluepilot/concurrent_tracker.h"
#include <QMutexLocker>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
#include <cstring>

#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/video_metrics.h"

namespace {
constexpr int kInitialPoolSize = 8;
}

FrameProvider::FrameProvider() = default;

FrameProvider::~FrameProvider() {
  clear();
}

void FrameProvider::setReader(std::shared_ptr<FrameReader> reader) {
  clear();

  {
    QMutexLocker locker(&queue_mutex_);
    current_reader_ = std::move(reader);
    current_frame_idx_ = 0;
    target_frame_idx_ = 0;
    if (current_reader_) {
      BPLog::bpDebugVideo() << "[bp.video.frame_provider] setReader | reader assigned width: "
               << current_reader_->width << " height: " << current_reader_->height
               << " frames: " << current_reader_->getFrameCount() << std::endl;
      const int stride = current_reader_->stride > 0 ? current_reader_->stride : current_reader_->width;
      prepareBufferPoolLocked(current_reader_->width, current_reader_->height, stride);
    } else {
      BPLog::bpWarn() << "[bp.video.frame_provider] setReader | reader reset to null" << std::endl;
    }
  }

  if (current_reader_) {
    BPLog::bpDebugVideo() << "[bp.video.frame_provider] setReader | set reader" << std::endl;
    startDecodeThread();
  }
}

VisionBuf *FrameProvider::getNextFrame() {
  QMutexLocker locker(&queue_mutex_);
  if (frame_queue_.empty()) {
    // BPLog::bpDebugVideo() << "[bp.video.frame_provider] getNextFrame | queue empty" << std::endl;
    return nullptr;
  }

  VisionBuf *frame = frame_queue_.front();
  frame_queue_.pop_front();
  queue_not_full_.wakeOne();
  return frame;
}

void FrameProvider::seekToFrame(int frame_idx) {
  QMutexLocker locker(&queue_mutex_);
  for (VisionBuf *buf : frame_queue_) {
    releaseBufferLocked(buf);
  }
  frame_queue_.clear();

  target_frame_idx_ = frame_idx;
  current_frame_idx_ = frame_idx;
  BPLog::bpDebugVideo() << "[bp.video.frame_provider] seekToFrame | seek to frame: " << frame_idx << std::endl;
  queue_not_full_.wakeAll();
}

void FrameProvider::clear() {
  stopDecodeThread();

  QMutexLocker locker(&queue_mutex_);
  BPLog::bpDebugVideo() << "[bp.video.frame_provider] clear | clear queue: " << frame_queue_.size() << std::endl;
  for (VisionBuf *buf : frame_queue_) {
    releaseBufferLocked(buf);
  }
  frame_queue_.clear();
  resetBufferPoolLocked();
  current_reader_.reset();
}

float FrameProvider::bufferLevel() const {
  QMutexLocker locker(&queue_mutex_);
  return static_cast<float>(frame_queue_.size()) / kMaxQueueSize * 100.0f;
}

int FrameProvider::queuedFrames() const {
  QMutexLocker locker(&queue_mutex_);
  return frame_queue_.size();
}

int FrameProvider::width() const {
  QMutexLocker locker(&queue_mutex_);
  return current_reader_ ? current_reader_->width : 0;
}

int FrameProvider::height() const {
  QMutexLocker locker(&queue_mutex_);
  return current_reader_ ? current_reader_->height : 0;
}

int FrameProvider::frameCount() const {
  QMutexLocker locker(&queue_mutex_);
  return current_reader_ ? static_cast<int>(current_reader_->getFrameCount()) : 0;
}

void FrameProvider::releaseFrame(VisionBuf *buf) {
  if (buf == nullptr) {
    return;
  }

  QMutexLocker locker(&queue_mutex_);
  releaseBufferLocked(buf);
}

void FrameProvider::startDecodeThread() {
  should_stop_ = false;
  decode_future_ = QtConcurrent::run([this]() {
    TRACK_CONCURRENT_TASK("FrameProvider::decodeLoop");
    decodeLoop();
  });
}

void FrameProvider::stopDecodeThread() {
  should_stop_ = true;
  queue_not_full_.wakeAll();
  if (decode_future_.isRunning()) {
    decode_future_.waitForFinished();
  }
}

// CRITICAL ISSUE #1: No auto-segment-transition logic
// WORKING: Decodes frames from current segment reader
// NOT WORKING:
//   1. When current_idx >= reader->getFrameCount(), loops forever at 50ms sleep
//   2. No signal sent to VideoControllerWorker to load next segment
//   3. Result: Video hangs at end of segment, pumpFrames() gets nullptr indefinitely
void FrameProvider::decodeLoop() {
  while (!should_stop_) {
    std::shared_ptr<FrameReader> reader;
    int current_idx = 0;
    int target_idx = 0;

    {
      QMutexLocker locker(&queue_mutex_);
      reader = current_reader_;
      if (!reader) {
        // BPLog::bpDebugVideo() << "[bp.video.frame_provider] decodeLoop | no reader, exit" << std::endl;
        return;
      }

      while (frame_queue_.size() >= kHighWatermark && !should_stop_) {
        queue_not_full_.wait(&queue_mutex_);
      }
      if (should_stop_) {
        return;
      }

      current_idx = current_frame_idx_.load();
      target_idx = target_frame_idx_.load();
      if (current_idx < target_idx - kMaxQueueSize) {
        current_idx = std::max(target_idx - kLowWatermark, 0);
        current_frame_idx_ = current_idx;
      }
    }

    if (!reader || reader->width <= 0 || reader->height <= 0) {
      // BPLog::bpWarn() << "[bp.video.frame_provider] decodeLoop | invalid reader dimensions" << std::endl;
      QThread::msleep(50);
      continue;
    }

    // ISSUE #1: When segment ends (current_idx >= frameCount), this loops forever
    // No mechanism to signal VideoControllerWorker to load next segment
    // pumpFrames() will get nullptr indefinitely, video hangs
    if (current_idx < 0 || current_idx >= static_cast<int>(reader->getFrameCount())) {
      // BPLog::bpDebugVideo() << "[bp.video.frame_provider] decodeLoop | frame index out of bounds: " << current_idx
              //  << " | total: " << reader->getFrameCount() << std::endl;
      QThread::msleep(50);  // ❌ LOOPS FOREVER - no segment transition
      continue;
    }

    VisionBuf *decoded_buf = nullptr;
    {
      QMutexLocker locker(&queue_mutex_);
      if (!buffers_initialized_) {
        prepareBufferPoolLocked(reader->width, reader->height, reader->stride > 0 ? reader->stride : reader->width);
      }
      decoded_buf = acquireBufferLocked();
      // if (decoded_buf == nullptr) {
        // BPLog::bpWarn() << "[bp.video.frame_provider] decodeLoop | buffer acquire failed" << std::endl;
      // }
    }

    if (decoded_buf == nullptr) {
      QThread::msleep(5);
      continue;
    }

    // Reset plane pointers for the pooled buffer before decoding.
    int stride = reader->stride > 0 ? reader->stride : buffer_stride_;
    size_t uv_offset = static_cast<size_t>(stride) * buffer_height_;
    decoded_buf->init_yuv(buffer_width_, buffer_height_, stride, uv_offset);

    const bool decode_ok = reader->get(current_idx, decoded_buf);
    if (decode_ok) {
      QMutexLocker locker(&queue_mutex_);
      if (frame_queue_.size() <= kMaxQueueSize) {
        frame_queue_.push_back(decoded_buf);
        VideoMetrics::instance().frameDecoded();
      } else {
        VideoMetrics::instance().frameDropped();
        releaseBufferLocked(decoded_buf);
      }
      current_frame_idx_++;
      queue_not_full_.wakeOne();
      if (frame_queue_.size() >= kHighWatermark) {
        queue_not_full_.wait(&queue_mutex_);
      }
    } else {
      // BPLog::bpDebugVideo() << "[bp.video.frame_provider] decodeLoop | reader failed: " << current_idx
      //          << " width:" << (reader ? reader->width : -1)
      //          << " height:" << (reader ? reader->height : -1)
      //          << " frames_available:" << (reader ? reader->getFrameCount() : 0)
      //          << std::endl;
      // if (reader) {
      //   BPLog::bpDebugVideo() << "[bp.video.frame_provider] decodeLoop | reader prev_idx:" << reader->prev_idx
      //            << " packets:" << reader->getFrameCount()
      //            << std::endl;
      // }
      QMutexLocker locker(&queue_mutex_);
      releaseBufferLocked(decoded_buf);
      QThread::msleep(10);
    }
  }
}

void FrameProvider::prepareBufferPoolLocked(int width, int height, int stride) {
  if (width <= 0 || height <= 0) {
    return;
  }

  const bool reuse_dimensions = buffers_initialized_ && width == buffer_width_
      && height == buffer_height_ && stride == buffer_stride_;
  if (reuse_dimensions) {
    return;
  }

  resetBufferPoolLocked();

  buffer_width_ = width;
  buffer_height_ = height;
  buffer_stride_ = stride;
  buffer_uv_offset_ = static_cast<size_t>(buffer_stride_) * buffer_height_;
  frame_payload_size_ = buffer_uv_offset_ + (buffer_stride_ * buffer_height_ / 2);

  const int desired_pool = std::min(pool_allocation_limit_, std::max(kInitialPoolSize, kLowWatermark));
  for (int i = 0; i < desired_pool; ++i) {
    auto holder = std::make_unique<VisionBuf>();
    holder->allocate(frame_payload_size_);
    holder->init_yuv(buffer_width_, buffer_height_, buffer_stride_, buffer_uv_offset_);
    VisionBuf *raw_ptr = holder.get();
    buffer_storage_.push_back(std::move(holder));
    available_buffers_.push_back(raw_ptr);
    available_buffer_set_.insert(raw_ptr);
  }

  buffers_initialized_ = true;

  BPLog::bpDebugVideo() << "[bp.video.frame_provider] prepareBufferPool | allocated buffers:" << available_buffers_.size()
           << " payload:" << frame_payload_size_ << std::endl;
}

VisionBuf *FrameProvider::acquireBufferLocked() {
  if (!buffers_initialized_) {
    return nullptr;
  }

  if (!available_buffers_.empty()) {
    VisionBuf *buf = available_buffers_.back();
    available_buffers_.pop_back();
    available_buffer_set_.erase(buf);
    in_flight_buffers_.insert(buf);
    return buf;
  }

  if (static_cast<int>(buffer_storage_.size()) < pool_allocation_limit_) {
    auto holder = std::make_unique<VisionBuf>();
    holder->allocate(frame_payload_size_);
    holder->init_yuv(buffer_width_, buffer_height_, buffer_stride_, buffer_uv_offset_);
    VisionBuf *raw_ptr = holder.get();
    in_flight_buffers_.insert(raw_ptr);
    buffer_storage_.push_back(std::move(holder));
    return raw_ptr;
  }

  // BPLog::bpWarn() << "[bp.video.frame_provider] acquireBuffer | exhausted pool" << std::endl;
  return nullptr;
}

void FrameProvider::releaseBufferLocked(VisionBuf *buf) {
  if (buf == nullptr) {
    return;
  }

  auto storage_it = std::find_if(buffer_storage_.begin(), buffer_storage_.end(),
      [buf](const std::unique_ptr<VisionBuf> &holder) { return holder.get() == buf; });
  if (storage_it == buffer_storage_.end()) {
    BPLog::bpWarn() << "[bp.video.frame_provider] releaseBuffer | buffer not owned" << std::endl;
    return;
  }

  auto it = in_flight_buffers_.find(buf);
  if (it == in_flight_buffers_.end()) {
    BPLog::bpWarn() << "[bp.video.frame_provider] releaseBuffer | unknown buffer" << std::endl;
    return;
  }

  in_flight_buffers_.erase(it);
  if (available_buffer_set_.insert(buf).second) {
    available_buffers_.push_back(buf);
  }
}

void FrameProvider::resetBufferPoolLocked() {
  for (VisionBuf *buf : frame_queue_) {
    releaseBufferLocked(buf);
  }
  frame_queue_.clear();

  for (VisionBuf *buf : in_flight_buffers_) {
    available_buffer_set_.insert(buf);
    available_buffers_.push_back(buf);
  }
  in_flight_buffers_.clear();

  available_buffer_set_.clear();
  available_buffers_.clear();
  buffer_storage_.clear();

  buffers_initialized_ = false;
  buffer_width_ = 0;
  buffer_height_ = 0;
  buffer_stride_ = 0;
  buffer_uv_offset_ = 0;
  frame_payload_size_ = 0;
}
