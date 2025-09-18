// bp_hardware_video_decoder.h
#pragma once

#include <QObject>
#include <QTimer>
#include <QVideoWidget>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QAtomicInt>
#include <QDialog>
#include <QTime>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

class BPHardwareVideoDecoder : public QObject {
  Q_OBJECT

public:
  explicit BPHardwareVideoDecoder(QObject *parent = nullptr);
  ~BPHardwareVideoDecoder();

  // Initialize decoder with video file path
  bool initialize(const QString &videoPath);

  // Start/stop playback
  void play();
  void pause();
  void stop();

  // Seek to position (in milliseconds)
  void seek(qint64 position);

  // Get current state
  bool isPlaying() const { return m_isPlaying; }
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
  // Hardware decoder initialization
  bool initHardwareDecoder();
  bool initCodecContext();

  // Frame decoding
  bool decodeFrame();
  void processFrame(AVFrame *frame);

  // Hardware device management
  bool createHardwareDevice();
  void cleanupHardwareDevice();

  // Thread-safe operations
  void decodeThread();

  // Member variables
  QString m_videoPath;
  QVideoWidget *m_videoWidget = nullptr;

  // FFmpeg contexts
  AVFormatContext *m_formatContext = nullptr;
  AVCodecContext *m_codecContext = nullptr;
  AVCodec *m_codec = nullptr;
  AVStream *m_videoStream = nullptr;

  // Hardware acceleration
  AVBufferRef *m_hwDeviceContext = nullptr;
  AVBufferRef *m_hwFrameContext = nullptr;
  AVPixelFormat m_hwPixelFormat = AV_PIX_FMT_NONE;

  // Make hwPixelFormat accessible to static callback
  friend enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);

public:
  // Public accessor for hardware pixel format
  AVPixelFormat getHwPixelFormat() const { return m_hwPixelFormat; }

  // Decoding state
  QAtomicInt m_isPlaying{0};
  QAtomicInt m_shouldStop{0};
  qint64 m_duration = 0;
  qint64 m_position = 0;
  int m_videoStreamIndex = -1;

  // Threading
  QThread *m_decodeThread = nullptr;
  QMutex m_mutex;
  QTimer *m_positionTimer = nullptr;

  // Frame buffers
  AVFrame *m_frame = nullptr;
  AVFrame *m_hwFrame = nullptr;
  AVPacket *m_packet = nullptr;

  // Platform-specific hardware device type
#ifdef __APPLE__
  static constexpr AVHWDeviceType HW_DEVICE_TYPE = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#else
  static constexpr AVHWDeviceType HW_DEVICE_TYPE = AV_HWDEVICE_TYPE_CUDA;
#endif
};

// Custom video widget that can display hardware-decoded frames
class BPHardwareVideoWidget : public QVideoWidget {
  Q_OBJECT

public:
  explicit BPHardwareVideoWidget(QWidget *parent = nullptr);
  ~BPHardwareVideoWidget();

  void setFrame(const QImage &frame);
  void clearFrame();

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QImage m_currentFrame;
  QMutex m_frameMutex;
};

// Hardware-accelerated video dialog
class BPHardwareVideoDialog : public QDialog {
  Q_OBJECT

public:
  explicit BPHardwareVideoDialog(const QString &videoPath, QWidget *parent = nullptr);
  ~BPHardwareVideoDialog();

private slots:
  void togglePlayback();
  void onPositionChanged(qint64 position);
  void onDurationChanged(qint64 duration);
  void onPlaybackFinished();
  void onErrorOccurred(const QString &error);
  void onSliderMoved(int position);

private:
  void setupUI();
  void updateTimeLabel();

  BPHardwareVideoDecoder *m_decoder = nullptr;
  BPHardwareVideoWidget *m_videoWidget = nullptr;
  QPushButton *m_playPauseButton = nullptr;
  QSlider *m_positionSlider = nullptr;
  QLabel *m_timeLabel = nullptr;
  QWidget *m_headerWidget = nullptr;
  QWidget *m_controlsWidget = nullptr;
};
