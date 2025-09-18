#pragma once

#include <vector>

#include "system/loggerd/decoder/decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class FfmpegDecoder : public VideoDecoder {
public:
  FfmpegDecoder(int width, int height, FrameCallback callback, bool is_h265);
  ~FfmpegDecoder();
  
  int decode_frame(uint8_t* data, int len, uint64_t timestamp_us, bool keyframe) override;
  void decoder_open() override;
  void decoder_close() override;
  void flush() override;

private:
  void decode_packet(AVPacket* pkt, uint64_t timestamp_us);

  bool is_open = false;
  bool is_hevc = false;
  
  const AVCodec *codec = nullptr;
  AVCodecContext *codec_ctx = nullptr;
  AVCodecParserContext *parser = nullptr;
  AVFrame *frame = nullptr;
  
  std::vector<uint8_t> yuv_buffer;
  int yuv_buffer_size = 0;
};
