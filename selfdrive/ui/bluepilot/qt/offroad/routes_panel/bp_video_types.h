#pragma once

#include <cstdint>
#include <functional>

// Video frame structure for decoded frames
struct DecodedFrame {
  uint8_t *y, *u, *v;  // YUV420 planes
  int width, height;
  int stride_y, stride_uv;
  uint64_t timestamp_us;
  int frame_id;
  bool keyframe;
};

// Callback function type for frame processing
using FrameCallback = std::function<void(const DecodedFrame&)>;