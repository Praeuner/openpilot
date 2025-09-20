#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "system/loggerd/decoder/decoder.h"
#include "system/loggerd/decoder/v4l_decoder.h"

// Structure to hold thumbnail data
struct ThumbnailData {
  std::vector<uint8_t> rgb_data;  // RGB888 format
  int width;
  int height;
  bool success;
};

class ThumbnailDecoder {
public:
  ThumbnailDecoder(int video_width, int video_height, int thumb_width, int thumb_height);
  ~ThumbnailDecoder();

  // Generate thumbnail from video file
  // Returns thumbnail data structure
  ThumbnailData generateThumbnail(const std::string& videoPath);

private:
  // Callback from decoder when frame is ready
  void onFrameDecoded(const DecodedFrame& frame);

  // Convert NV12 to RGB and scale to thumbnail size
  void processFrame(const DecodedFrame& frame);

  // Fallback method when hardware decoder is not available
  ThumbnailData generateFallbackThumbnail(const std::string& videoPath);

  int thumb_width_;
  int thumb_height_;

  std::unique_ptr<V4LDecoder> decoder_;
  std::atomic<bool> frame_captured_{false};
  ThumbnailData captured_thumbnail_;

  // Synchronization for frame capture
  std::mutex frame_mutex_;
  std::condition_variable frame_cv_;
};