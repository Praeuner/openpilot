#include "bp_frame_reader.h"
#include <QDebug>
#include "common/util.h"
#include "common/swaglog.h"
#include "system/hardware/hw.h"
#include "third_party/libyuv/include/libyuv.h"

#ifdef __APPLE__
#define HW_DEVICE_TYPE AV_HWDEVICE_TYPE_VIDEOTOOLBOX
#define HW_PIX_FMT AV_PIX_FMT_VIDEOTOOLBOX
#else
#define HW_DEVICE_TYPE AV_HWDEVICE_TYPE_CUDA
#define HW_PIX_FMT AV_PIX_FMT_CUDA
#endif

namespace {

struct DecoderManager {
  VideoDecoder *acquire(CameraType type, AVCodecParameters *codecpar, bool hw_decoder) {
    auto key = std::tuple(type, codecpar->width, codecpar->height);
    std::unique_lock lock(mutex_);
    if (auto it = decoders_.find(key); it != decoders_.end()) {
      return it->second.get();
    }

    std::unique_ptr<VideoDecoder> decoder;
    #ifndef __APPLE__
    if (Hardware::TICI() && hw_decoder) {
      decoder = std::make_unique<QcomVideoDecoder>();
      qDebug() << "Using QCOM hardware decoder";
    } else
    #endif
    {
      decoder = std::make_unique<FFmpegVideoDecoder>();
      qDebug() << "Using FFmpeg decoder" << (hw_decoder ? "with HW acceleration" : "software only");
    }

    if (!decoder->open(codecpar, hw_decoder)) {
      decoder.reset(nullptr);
    }
    decoders_[key] = std::move(decoder);
    return decoders_[key].get();
  }

private:
  std::mutex mutex_;
  std::map<std::tuple<CameraType, int, int>, std::unique_ptr<VideoDecoder>> decoders_;
};

DecoderManager decoder_manager;

}  // namespace

// FrameReader Implementation
FrameReader::FrameReader() {
  av_log_set_level(AV_LOG_QUIET);
}

FrameReader::~FrameReader() {
  if (input_ctx) {
    avformat_close_input(&input_ctx);
  }
}

bool FrameReader::load(CameraType type, const std::string &file, bool no_hw_decoder, std::atomic<bool> *abort) {
  if (avformat_open_input(&input_ctx, file.c_str(), nullptr, nullptr) != 0 ||
      avformat_find_stream_info(input_ctx, nullptr) < 0) {
    qWarning() << "Failed to open input file or find video stream:" << file.c_str();
    return false;
  }

  video_stream_idx_ = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (video_stream_idx_ == -1) {
    qWarning() << "No video stream found in file";
    return false;
  }

  decoder_ = decoder_manager.acquire(type, input_ctx->streams[video_stream_idx_]->codecpar, !no_hw_decoder);
  if (!decoder_) {
    return false;
  }
  width = decoder_->width;
  height = decoder_->height;

  // Build packet info for seeking
  packets_info.clear();
  AVPacket pkt;
  while (av_read_frame(input_ctx, &pkt) >= 0) {
    if (pkt.stream_index == video_stream_idx_) {
      packets_info.push_back({pkt.flags, pkt.pos});
    }
    av_packet_unref(&pkt);
  }

  av_seek_frame(input_ctx, video_stream_idx_, 0, AVSEEK_FLAG_BACKWARD);
  return true;
}

bool FrameReader::get(int idx, VisionBuf *buf) {
  if (!decoder_ || idx < 0 || idx >= packets_info.size()) {
    return false;
  }
  return decoder_->decode(this, idx, buf);
}

// FFmpegVideoDecoder Implementation
FFmpegVideoDecoder::FFmpegVideoDecoder() {
  av_frame_ = av_frame_alloc();
}

FFmpegVideoDecoder::~FFmpegVideoDecoder() {
  if (decoder_ctx) {
    avcodec_free_context(&decoder_ctx);
  }
  if (av_frame_) {
    av_frame_free(&av_frame_);
  }
}

bool FFmpegVideoDecoder::open(AVCodecParameters *codecpar, bool hw_decoder) {
  const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
  if (!codec) {
    qWarning() << "Codec not found";
    return false;
  }

  decoder_ctx = avcodec_alloc_context3(codec);
  if (!decoder_ctx) {
    qWarning() << "Could not allocate video codec context";
    return false;
  }

  if (avcodec_parameters_to_context(decoder_ctx, codecpar) < 0) {
    qWarning() << "Could not copy codec parameters to decoder context";
    return false;
  }

  if (avcodec_open2(decoder_ctx, codec, nullptr) < 0) {
    qWarning() << "Could not open codec";
    return false;
  }

  width = decoder_ctx->width;
  height = decoder_ctx->height;
  return true;
}

bool FFmpegVideoDecoder::decode(FrameReader *reader, int idx, VisionBuf *buf) {
  int from_idx = idx;
  if (idx != reader->prev_idx + 1) {
    // seeking to the nearest key frame
    for (int i = idx; i >= 0; --i) {
      if (reader->packets_info[i].flags & AV_PKT_FLAG_KEY) {
        from_idx = i;
        break;
      }
    }

    auto pos = reader->packets_info[from_idx].pos;
    int ret = avformat_seek_file(reader->input_ctx, 0, pos, pos, pos, AVSEEK_FLAG_BYTE);
    if (ret < 0) {
      qWarning() << "Failed to seek to byte position" << pos;
      return false;
    }
  }
  reader->prev_idx = idx;

  AVPacket pkt;
  while (av_read_frame(reader->input_ctx, &pkt) >= 0) {
    // Skip non-video packets
    int video_stream_idx = av_find_best_stream(reader->input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (pkt.stream_index != video_stream_idx) {
      av_packet_unref(&pkt);
      continue;
    }

    if (avcodec_send_packet(decoder_ctx, &pkt) < 0) {
      av_packet_unref(&pkt);
      continue;
    }

    while (avcodec_receive_frame(decoder_ctx, av_frame_) == 0) {
      if (from_idx++ == idx) {
        bool ret = copyBuffer(av_frame_, buf);
        av_packet_unref(&pkt);
        return ret;
      }
    }
    av_packet_unref(&pkt);
  }
  return false;
}

bool FFmpegVideoDecoder::copyBuffer(AVFrame *f, VisionBuf *buf) {
  // Convert frame to NV12 and copy to VisionBuf
  if (f->format == AV_PIX_FMT_YUV420P) {
    // Direct copy for YUV420P
    uint8_t *y_plane = (uint8_t *)buf->y;
    uint8_t *uv_plane = (uint8_t *)buf->uv;

    // Copy Y plane
    for (int i = 0; i < height; i++) {
      memcpy(y_plane + i * buf->stride, f->data[0] + i * f->linesize[0], width);
    }

    // Convert U and V planes to interleaved UV (NV12)
    for (int i = 0; i < height / 2; i++) {
      for (int j = 0; j < width / 2; j++) {
        uv_plane[i * buf->stride + j * 2] = f->data[1][i * f->linesize[1] + j];     // U
        uv_plane[i * buf->stride + j * 2 + 1] = f->data[2][i * f->linesize[2] + j]; // V
      }
    }
    return true;
  }
  return false;
}

#ifndef __APPLE__
// QcomVideoDecoder Implementation
bool QcomVideoDecoder::open(AVCodecParameters *codecpar, bool hw_decoder) {
  if (codecpar->codec_id != AV_CODEC_ID_HEVC) {
    qWarning() << "QCOM hardware decoder only supports HEVC codec";
    return false;
  }
  width = codecpar->width;
  height = codecpar->height;

  if (!msm_vidc.init(VIDEO_DEVICE, width, height, V4L2_PIX_FMT_HEVC)) {
    qWarning() << "Failed to initialize QCOM hardware decoder";
    return false;
  }
  return true;
}

bool QcomVideoDecoder::decode(FrameReader *reader, int idx, VisionBuf *buf) {
  int from_idx = idx;
  if (idx != reader->prev_idx + 1) {
    // seeking to the nearest key frame
    for (int i = idx; i >= 0; --i) {
      if (reader->packets_info[i].flags & AV_PKT_FLAG_KEY) {
        from_idx = i;
        break;
      }
    }

    auto pos = reader->packets_info[from_idx].pos;
    int ret = avformat_seek_file(reader->input_ctx, 0, pos, pos, pos, AVSEEK_FLAG_BYTE);
    if (ret < 0) {
      qWarning() << "Failed to seek to byte position" << pos;
      return false;
    }
  }
  reader->prev_idx = idx;
  bool result = false;
  AVPacket pkt;
  msm_vidc.avctx = reader->input_ctx;
  for (int i = from_idx; i <= idx; ++i) {
    if (av_read_frame(reader->input_ctx, &pkt) == 0) {
      result = msm_vidc.decodeFrame(&pkt, buf) && (i == idx);
      av_packet_unref(&pkt);
    }
  }
  return result;
}
#endif