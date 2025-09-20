#include "system/loggerd/decoder/thumbnail_decoder.h"

#include <chrono>
#include <fstream>
#include <cstring>

#include "common/swaglog.h"
#include "third_party/libyuv/include/libyuv.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"
#include "third_party/libyuv/include/libyuv/scale.h"

ThumbnailDecoder::ThumbnailDecoder(int video_width, int video_height, int thumb_width, int thumb_height)
    : thumb_width_(thumb_width), thumb_height_(thumb_height) {

  // Create decoder with callback to capture first frame
  auto callback = [this](const DecodedFrame& frame) {
    onFrameDecoded(frame);
  };

  // Assume H.265 for now (can be made configurable)
  decoder_ = std::make_unique<V4LDecoder>(video_width, video_height, callback, true);
}

ThumbnailDecoder::~ThumbnailDecoder() {
  if (decoder_ && decoder_->is_decoder_open()) {
    decoder_->decoder_close();
  }
}

void ThumbnailDecoder::onFrameDecoded(const DecodedFrame& frame) {
  std::lock_guard<std::mutex> lock(frame_mutex_);

  // Only capture the first frame
  if (frame_captured_.exchange(true)) {
    return;
  }

  LOGD("Thumbnail decoder: Captured frame %d (%dx%d)", frame.frame_id, frame.width, frame.height);

  // Process the frame and create thumbnail
  processFrame(frame);

  // Signal that frame is ready
  frame_cv_.notify_one();

  // Close decoder after first frame to save resources
  decoder_->decoder_close();
}

void ThumbnailDecoder::processFrame(const DecodedFrame& frame) {
  // Initialize thumbnail data
  captured_thumbnail_.width = thumb_width_;
  captured_thumbnail_.height = thumb_height_;
  captured_thumbnail_.success = false;

  // Create RGB buffer for thumbnail
  int rgb_size = thumb_width_ * thumb_height_ * 3;
  captured_thumbnail_.rgb_data.resize(rgb_size);

  // The V4L decoder provides NV12 data with separate Y and UV pointers
  // But UV is interleaved in NV12 format
  // We need to convert to I420 first, then to RGB

  // Create I420 buffers
  int y_size = frame.width * frame.height;
  int uv_size = y_size / 4;
  std::vector<uint8_t> i420_u(uv_size);
  std::vector<uint8_t> i420_v(uv_size);

  // Split interleaved UV to separate U and V planes
  uint8_t* nv12_uv = frame.u;  // Interleaved UV
  for (int i = 0; i < uv_size; i++) {
    i420_u[i] = nv12_uv[i * 2];      // U values at even positions
    i420_v[i] = nv12_uv[i * 2 + 1];  // V values at odd positions
  }

  // Now we have I420 format, convert to RGB at original size
  int orig_rgb_size = frame.width * frame.height * 3;
  std::vector<uint8_t> orig_rgb(orig_rgb_size);

  int ret = libyuv::I420ToRGB24(
      frame.y, frame.stride_y,              // Y plane
      i420_u.data(), frame.stride_y / 2,    // U plane
      i420_v.data(), frame.stride_y / 2,    // V plane
      orig_rgb.data(), frame.width * 3,     // RGB output
      frame.width, frame.height);

  if (ret != 0) {
    LOGE("Failed to convert I420 to RGB: %d", ret);
    return;
  }

  // Scale RGB to thumbnail size using I420 intermediate for better quality
  // First convert RGB back to I420 at thumbnail size
  int thumb_y_size = thumb_width_ * thumb_height_;
  int thumb_uv_size = thumb_y_size / 4;
  std::vector<uint8_t> thumb_y(thumb_y_size);
  std::vector<uint8_t> thumb_u(thumb_uv_size);
  std::vector<uint8_t> thumb_v(thumb_uv_size);

  // Scale I420 directly from original I420
  ret = libyuv::I420Scale(
      frame.y, frame.stride_y,
      i420_u.data(), frame.stride_y / 2,
      i420_v.data(), frame.stride_y / 2,
      frame.width, frame.height,
      thumb_y.data(), thumb_width_,
      thumb_u.data(), thumb_width_ / 2,
      thumb_v.data(), thumb_width_ / 2,
      thumb_width_, thumb_height_,
      libyuv::kFilterBox);

  if (ret != 0) {
    LOGE("Failed to scale I420: %d", ret);
    return;
  }

  // Convert scaled I420 to RGB
  ret = libyuv::I420ToRGB24(
      thumb_y.data(), thumb_width_,
      thumb_u.data(), thumb_width_ / 2,
      thumb_v.data(), thumb_width_ / 2,
      captured_thumbnail_.rgb_data.data(), thumb_width_ * 3,
      thumb_width_, thumb_height_);

  if (ret == 0) {
    captured_thumbnail_.success = true;
    LOGD("Successfully processed thumbnail: %dx%d", thumb_width_, thumb_height_);
  } else {
    LOGE("Failed to convert thumbnail to RGB: %d", ret);
  }
}

ThumbnailData ThumbnailDecoder::generateThumbnail(const std::string& videoPath) {
  // Reset state
  frame_captured_ = false;
  captured_thumbnail_ = ThumbnailData();
  captured_thumbnail_.width = thumb_width_;
  captured_thumbnail_.height = thumb_height_;
  captured_thumbnail_.success = false;

  // Check if video file exists
  std::ifstream videoFile(videoPath, std::ios::binary);
  if (!videoFile.good()) {
    LOGE("Thumbnail decoder: Video file not found: %s", videoPath.c_str());
    return captured_thumbnail_;
  }

  // Check if hardware decoder is available
  if (!decoder_->is_decoder_available()) {
    LOGE("Thumbnail decoder: Hardware decoder not available, using fallback");
    return generateFallbackThumbnail(videoPath);
  }

  // Open decoder
  decoder_->decoder_open();
  if (!decoder_->is_decoder_open()) {
    LOGE("Thumbnail decoder: Failed to open hardware decoder, using fallback");
    return generateFallbackThumbnail(videoPath);
  }

  // Read first chunk of video file (enough to get first keyframe)
  const int chunk_size = 1024 * 1024; // 1MB
  std::vector<uint8_t> videoData(chunk_size);

  videoFile.read(reinterpret_cast<char*>(videoData.data()), chunk_size);
  std::streamsize bytesRead = videoFile.gcount();
  videoFile.close();

  if (bytesRead == 0) {
    LOGE("Thumbnail decoder: Failed to read video data");
    decoder_->decoder_close();
    return captured_thumbnail_;
  }

  // Feed data to decoder
  int ret = decoder_->decode_frame(
      videoData.data(),
      bytesRead,
      0,    // timestamp
      true  // keyframe hint
  );

  if (ret < 0) {
    LOGE("Thumbnail decoder: Failed to decode frame");
    decoder_->decoder_close();
    return captured_thumbnail_;
  }

  // Wait for frame to be decoded (max 2 seconds)
  std::unique_lock<std::mutex> lock(frame_mutex_);
  bool success = frame_cv_.wait_for(lock, std::chrono::seconds(2),
                                    [this] { return frame_captured_.load(); });

  if (!success) {
    LOGE("Thumbnail decoder: Timeout waiting for decoded frame");
  }

  decoder_->decoder_close();
  return captured_thumbnail_;
}

ThumbnailData ThumbnailDecoder::generateFallbackThumbnail(const std::string& videoPath) {
  ThumbnailData fallback_thumbnail;
  fallback_thumbnail.width = thumb_width_;
  fallback_thumbnail.height = thumb_height_;
  fallback_thumbnail.success = true;

  // Create a simple placeholder thumbnail - dark blue gradient
  int rgb_size = thumb_width_ * thumb_height_ * 3;
  fallback_thumbnail.rgb_data.resize(rgb_size);

  for (int y = 0; y < thumb_height_; y++) {
    for (int x = 0; x < thumb_width_; x++) {
      int idx = (y * thumb_width_ + x) * 3;

      // Create a subtle gradient from dark blue at top to slightly lighter at bottom
      uint8_t blue_value = 40 + (y * 20) / thumb_height_;  // 40-60 range

      fallback_thumbnail.rgb_data[idx] = 20;         // R - dark red
      fallback_thumbnail.rgb_data[idx + 1] = 30;     // G - dark green
      fallback_thumbnail.rgb_data[idx + 2] = blue_value; // B - gradient blue
    }
  }

  LOGD("Generated fallback thumbnail: %dx%d", thumb_width_, thumb_height_);
  return fallback_thumbnail;
}