#pragma once

#include <QObject>
#include <QTimer>
#include <QThread>
#include <QMutex>
#include <QAtomicInt>
#include <QImage>
#include <QVideoWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QTime>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

#include "third_party/libyuv/include/libyuv.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"
#include "third_party/libyuv/include/libyuv/convert.h"

class BPFFmpegDecoder : public QObject {
  Q_OBJECT

public:
  explicit BPFFmpegDecoder(QObject *parent = nullptr);
  ~BPFFmpegDecoder();

  // Initialize decoder with video file path
  bool initialize(const QString &videoPath);

  // Playback control
  void play();
  void pause();
  void stop();
  void seek(qint64 position);

  // State getters
  bool isPlaying() const { return m_isPlaying.loadAcquire(); }
  qint64 duration() const { return m_duration; }
  qint64 position() const { return m_position; }

  // Set video output widget
  void setVideoOutput(QVideoWidget *widget);

signals:
  void positionChanged(qint64 position);
  void durationChanged(qint64 duration);
  void playbackFinished();
  void errorOccurred(const QString &error);

private slots:
  void updatePosition();

private:
  // Decoder initialization
  bool initDecoder();
  void cleanupDecoder();

  // Frame decoding and processing
  bool decodeFrame();
  void processFrame(AVFrame *frame);
  QImage convertYUVToRGB(AVFrame *frame);

  // Threading
  void decodeLoop();

  // Member variables
  QString m_videoPath;
  QVideoWidget *m_videoWidget = nullptr;

  // FFmpeg contexts
  AVFormatContext *m_formatContext = nullptr;
  AVCodecContext *m_codecContext = nullptr;
  const AVCodec *m_codec = nullptr;
  AVStream *m_videoStream = nullptr;
  int m_videoStreamIndex = -1;

  // Hardware acceleration support
  AVBufferRef *m_hwDeviceContext = nullptr;
  AVPixelFormat m_hwPixelFormat = AV_PIX_FMT_NONE;
  bool m_useHardwareDecoding = false;

  // Frame buffers
  AVFrame *m_frame = nullptr;
  AVFrame *m_hwFrame = nullptr;
  AVFrame *m_swFrame = nullptr;
  AVPacket *m_packet = nullptr;

  // libyuv conversion buffers
  std::vector<uint8_t> m_rgbBuffer;

  // Playback state
  QAtomicInt m_isPlaying{0};
  QAtomicInt m_shouldStop{0};
  qint64 m_duration = 0;
  qint64 m_position = 0;

  // Threading
  std::unique_ptr<QThread> m_decodeThread;
  QMutex m_mutex;
  QTimer *m_positionTimer = nullptr;

  // Frame rate control
  static constexpr int TARGET_FPS = 20;
  static constexpr int FRAME_DELAY_MS = 1000 / TARGET_FPS; // 50ms
};

// Optimized video widget using non-blocking rendering
class BPFFmpegVideoWidget : public QVideoWidget {
  Q_OBJECT

public:
  explicit BPFFmpegVideoWidget(QWidget *parent = nullptr);
  ~BPFFmpegVideoWidget();

public slots:
  void setFrame(const QImage &frame);
  void clearFrame();

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QImage m_currentFrame;
  mutable QMutex m_frameMutex;
};