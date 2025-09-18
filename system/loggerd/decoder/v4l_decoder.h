#pragma once

#include <thread>
#include <queue>

#include "common/queue.h"
#include "msgq/visionipc/visionbuf.h"
#include "system/loggerd/decoder/decoder.h"

#define BUF_IN_COUNT 10
#define BUF_OUT_COUNT 8

class V4LDecoder : public VideoDecoder {
public:
  V4LDecoder(int width, int height, FrameCallback callback, bool is_h265);
  ~V4LDecoder();
  
  int decode_frame(uint8_t* data, int len, uint64_t timestamp_us, bool keyframe) override;
  void decoder_open() override;
  void decoder_close() override;
  void flush() override;

private:
  static void decode_handler(V4LDecoder *d);
  
  int fd = -1;
  bool is_open = false;
  bool is_hevc = false;
  
  int stride = 0;
  int compressed_buf_size = 0;
  
  std::thread decode_thread;
  std::queue<uint64_t> timestamp_queue;
  
  VisionBuf buf_in[BUF_IN_COUNT];
  VisionBuf buf_out[BUF_OUT_COUNT];
  SafeQueue<int> free_buf_in;
};
