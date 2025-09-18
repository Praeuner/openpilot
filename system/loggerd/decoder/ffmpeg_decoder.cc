#include "system/loggerd/decoder/ffmpeg_decoder.h"

#include <cassert>
#include <cstdlib>

#include "third_party/libyuv/include/libyuv.h"
#include "common/swaglog.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
}

FfmpegDecoder::FfmpegDecoder(int width, int height, FrameCallback callback, bool is_h265)
    : VideoDecoder(width, height, callback), is_hevc(is_h265) {

  // Initialize FFmpeg decoder
  AVCodecID codec_id = is_hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
  codec = avcodec_find_decoder(codec_id);
  if (!codec) {
    LOGE("Failed to find decoder for %s", is_hevc ? "HEVC" : "H264");
    return;
  }

  codec_ctx = avcodec_alloc_context3(codec);
  if (!codec_ctx) {
    LOGE("Failed to allocate codec context");
    return;
  }

  codec_ctx->width = width;
  codec_ctx->height = height;
  codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  codec_ctx->thread_count = 4;  // Use multiple threads for decoding
  codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

  // Allocate frame for decoded output
  frame = av_frame_alloc();
  if (!frame) {
    LOGE("Failed to allocate frame");
    avcodec_free_context(&codec_ctx);
    return;
  }

  // Allocate buffers for YUV conversion if needed
  yuv_buffer_size = width * height * 3 / 2;
  yuv_buffer.resize(yuv_buffer_size);
}

FfmpegDecoder::~FfmpegDecoder() {
  decoder_close();

  if (frame) {
    av_frame_free(&frame);
  }
  if (codec_ctx) {
    avcodec_free_context(&codec_ctx);
  }
  if (parser) {
    av_parser_close(parser);
  }
}

void FfmpegDecoder::decoder_open() {
  if (is_open || !codec_ctx) return;

  int ret = avcodec_open2(codec_ctx, codec, nullptr);
  if (ret < 0) {
    LOGE("Failed to open codec: %d", ret);
    return;
  }

  // Initialize parser for H264/HEVC stream parsing
  parser = av_parser_init(codec->id);
  if (!parser) {
    LOGE("Failed to create parser");
    // Note: avcodec_close() is deprecated in newer FFmpeg versions
    // The codec context will be freed in destructor
    return;
  }

  is_open = true;
  frame_count = 0;
}

int FfmpegDecoder::decode_frame(uint8_t* data, int len, uint64_t timestamp_us, bool keyframe) {
  if (!is_open || !data || len <= 0) return -1;

  AVPacket pkt = {};
  pkt.data = data;
  pkt.size = len;
  pkt.pts = timestamp_us;
  pkt.dts = timestamp_us;
  if (keyframe) {
    pkt.flags |= AV_PKT_FLAG_KEY;
  }

  // Parse the data if we have a parser
  if (parser) {
    uint8_t *parse_data = data;
    int parse_len = len;

    while (parse_len > 0) {
      uint8_t *out_data = nullptr;
      int out_size = 0;

      int consumed = av_parser_parse2(parser, codec_ctx, &out_data, &out_size,
                                      parse_data, parse_len,
                                      timestamp_us, timestamp_us, 0);

      if (consumed < 0) {
        LOGE("Parse error");
        break;
      }

      parse_data += consumed;
      parse_len -= consumed;

      if (out_size > 0) {
        AVPacket parsed_pkt = {};
        parsed_pkt.data = out_data;
        parsed_pkt.size = out_size;
        parsed_pkt.pts = timestamp_us;
        parsed_pkt.dts = timestamp_us;
        if (parser->key_frame) {
          parsed_pkt.flags |= AV_PKT_FLAG_KEY;
        }

        decode_packet(&parsed_pkt, timestamp_us);
      }
    }
  } else {
    // Decode without parser
    decode_packet(&pkt, timestamp_us);
  }

  return 0;
}

void FfmpegDecoder::decode_packet(AVPacket* pkt, uint64_t timestamp_us) {
  int ret = avcodec_send_packet(codec_ctx, pkt);
  if (ret < 0 && ret != AVERROR(EAGAIN)) {
    LOGE("Send packet error: %d", ret);
    return;
  }

  while (ret >= 0) {
    ret = avcodec_receive_frame(codec_ctx, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    } else if (ret < 0) {
      LOGE("Receive frame error: %d", ret);
      break;
    }

    // Got a decoded frame - convert and callback
    if (frame_callback) {
      uint8_t *y_plane = yuv_buffer.data();
      uint8_t *u_plane = y_plane + out_width * out_height;
      uint8_t *v_plane = u_plane + (out_width * out_height) / 4;

      // Check if we need to convert format or scale
      if (frame->format == AV_PIX_FMT_YUV420P &&
          frame->width == out_width && frame->height == out_height) {
        // Direct copy if format matches
        memcpy(y_plane, frame->data[0], out_width * out_height);
        memcpy(u_plane, frame->data[1], (out_width * out_height) / 4);
        memcpy(v_plane, frame->data[2], (out_width * out_height) / 4);
      } else if (frame->format == AV_PIX_FMT_NV12) {
        // Convert NV12 to I420
        libyuv::NV12ToI420(frame->data[0], frame->linesize[0],
                          frame->data[1], frame->linesize[1],
                          y_plane, out_width,
                          u_plane, out_width/2,
                          v_plane, out_width/2,
                          frame->width, frame->height);
      } else {
        // Handle other formats or scaling
        // For now, assume YUV420P and scale if needed
        if (frame->width != out_width || frame->height != out_height) {
          libyuv::I420Scale(frame->data[0], frame->linesize[0],
                           frame->data[1], frame->linesize[1],
                           frame->data[2], frame->linesize[2],
                           frame->width, frame->height,
                           y_plane, out_width,
                           u_plane, out_width/2,
                           v_plane, out_width/2,
                           out_width, out_height,
                           libyuv::kFilterBilinear);
        }
      }

      DecodedFrame decoded = {
        .y = y_plane,
        .u = u_plane,
        .v = v_plane,
        .width = out_width,
        .height = out_height,
        .stride_y = out_width,
        .stride_uv = out_width / 2,
        .timestamp_us = timestamp_us,
        .frame_id = frame_count++,
        .keyframe = static_cast<bool>(frame->flags & AV_FRAME_FLAG_KEY)
      };

      frame_callback(decoded);
    }
  }
}

void FfmpegDecoder::flush() {
  if (!is_open) return;

  // Flush decoder by sending NULL packet
  decode_packet(nullptr, 0);
}

void FfmpegDecoder::decoder_close() {
  if (!is_open) return;

  flush();

  // Note: avcodec_close() is deprecated in newer FFmpeg versions
  // The codec context will be freed in destructor via avcodec_free_context()

  is_open = false;
}
