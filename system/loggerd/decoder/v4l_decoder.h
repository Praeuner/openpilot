#pragma once

#include <thread>
#include <queue>
#include <sys/mman.h>

#include "common/queue.h"
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
  bool is_decoder_open() const { return is_open; }
  bool is_decoder_available() const { return fd >= 0; }

private:
  static void decode_handler(V4LDecoder *d);
  
  int fd = -1;
  bool is_open = false;
  bool is_hevc = false;
  
  int stride = 0;
  int compressed_buf_size = 0;
  int frame_size = 0;
  int out_width = 0;
  int out_height = 0;
  
  std::thread decode_thread;
  std::queue<uint64_t> timestamp_queue;
  
  // Use mmap'd buffers instead of VisionBuf
  void* buf_in[BUF_IN_COUNT];
  void* buf_out[BUF_OUT_COUNT];
  SafeQueue<int> free_buf_in;
};
