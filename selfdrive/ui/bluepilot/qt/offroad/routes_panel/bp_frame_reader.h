#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <map>
#include <tuple>

#include "msgq/visionipc/visionbuf.h"

#ifndef __APPLE__
#include "qcom_decoder.h"
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
}

enum CameraType {
  RoadCam = 0,
  DriverCam,
  WideRoadCam
};

// Forward declarations
class VideoDecoder;
class FrameReader;

// Simplified FrameReader for BluePilot routes panel
class FrameReader {
public:
  FrameReader();
  ~FrameReader();

  bool load(CameraType type, const std::string &file, bool no_hw_decoder = false, std::atomic<bool> *abort = nullptr);
  bool get(int idx, VisionBuf *buf);
  size_t getFrameCount() const { return packets_info.size(); }

  int width = 0, height = 0, stride = 0;
  bool uses_hw_decoder = false;

  // These members need to be accessible by VideoDecoder classes
  AVFormatContext *input_ctx = nullptr;
  int prev_idx = -1;
  struct PacketInfo {
    int flags;
    int64_t pos;
  };
  std::vector<PacketInfo> packets_info;

private:
  VideoDecoder *decoder_ = nullptr;
  int video_stream_idx_ = -1;
};

// Base VideoDecoder class
class VideoDecoder {
public:
  virtual ~VideoDecoder() = default;
  virtual bool open(AVCodecParameters *codecpar, bool hw_decoder) = 0;
  virtual bool decode(FrameReader *reader, int idx, VisionBuf *buf) = 0;
  int width = 0, height = 0, stride = 0;
};

// FFmpeg software decoder
class FFmpegVideoDecoder : public VideoDecoder {
public:
  FFmpegVideoDecoder();
  ~FFmpegVideoDecoder() override;
  bool open(AVCodecParameters *codecpar, bool hw_decoder) override;
  bool decode(FrameReader *reader, int idx, VisionBuf *buf) override;

private:
  bool copyBuffer(AVFrame *f, VisionBuf *buf);

  AVFrame *av_frame_;
  AVCodecContext *decoder_ctx = nullptr;
};

#ifndef __APPLE__
// QCOM hardware decoder
class QcomVideoDecoder : public VideoDecoder {
public:
  QcomVideoDecoder() {};
  ~QcomVideoDecoder() override {};
  bool open(AVCodecParameters *codecpar, bool hw_decoder) override;
  bool decode(FrameReader *reader, int idx, VisionBuf *buf) override;

private:
  MsmVidc msm_vidc = MsmVidc();
};
#endif
