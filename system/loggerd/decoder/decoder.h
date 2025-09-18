#pragma once

#include <memory>
#include <vector>
#include <functional>

#include "msgq/visionipc/visionipc.h"
#include "common/queue.h"

// Decoded frame callback
struct DecodedFrame {
  uint8_t *y, *u, *v;  // YUV420 planes
  int width, height;
  int stride_y, stride_uv;
  uint64_t timestamp_us;
  int frame_id;
  bool keyframe;
};

using FrameCallback = std::function<void(const DecodedFrame&)>;

class VideoDecoder {
public:
  VideoDecoder(int width, int height, FrameCallback callback)
      : out_width(width), out_height(height), frame_callback(callback) {}
  virtual ~VideoDecoder() {}
  
  // Feed compressed data to decoder
  virtual int decode_frame(uint8_t* data, int len, uint64_t timestamp_us, bool keyframe) = 0;
  
  // Open/close decoder session
  virtual void decoder_open() = 0;
  virtual void decoder_close() = 0;
  
  // Flush any pending frames
  virtual void flush() = 0;

protected:
  int out_width, out_height;
  FrameCallback frame_callback;
  int frame_count = 0;
};
