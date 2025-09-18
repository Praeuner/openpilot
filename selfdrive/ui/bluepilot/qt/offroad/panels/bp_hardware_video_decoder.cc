// bp_hardware_video_decoder.cc
#include "bp_hardware_video_decoder.h"
#include <QApplication>
#include <QPainter>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QMetaObject>
#include <iostream>

// Static callback for hardware pixel format selection
enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
  BPHardwareVideoDecoder *decoder = static_cast<BPHardwareVideoDecoder*>(ctx->opaque);
  if (!decoder) {
    return AV_PIX_FMT_YUV420P;
  }

  const enum AVPixelFormat *p;
  for (p = pix_fmts; *p != -1; p++) {
    if (*p == decoder->getHwPixelFormat()) {
      return *p;
    }
  }

  qWarning() << "Hardware pixel format not supported, falling back to software";
  return AV_PIX_FMT_YUV420P;
}

BPHardwareVideoDecoder::BPHardwareVideoDecoder(QObject *parent)
    : QObject(parent) {
  // Initialize FFmpeg logging
  av_log_set_level(AV_LOG_WARNING);

  // Initialize frame and packet
  m_frame = av_frame_alloc();
  m_hwFrame = av_frame_alloc();
  m_packet = av_packet_alloc();

  // Setup position timer
  m_positionTimer = new QTimer(this);
  m_positionTimer->setInterval(100); // Update every 100ms
  connect(m_positionTimer, &QTimer::timeout, this, &BPHardwareVideoDecoder::updatePosition);
}

BPHardwareVideoDecoder::~BPHardwareVideoDecoder() {
  stop();

  // Cleanup FFmpeg resources
  if (m_frame) av_frame_free(&m_frame);
  if (m_hwFrame) av_frame_free(&m_hwFrame);
  if (m_packet) av_packet_free(&m_packet);

  cleanupHardwareDevice();

  if (m_codecContext) avcodec_free_context(&m_codecContext);
  if (m_formatContext) avformat_close_input(&m_formatContext);
}

bool BPHardwareVideoDecoder::initialize(const QString &videoPath) {
  m_videoPath = videoPath;

  // Open input file
  if (avformat_open_input(&m_formatContext, videoPath.toUtf8().constData(), nullptr, nullptr) < 0) {
    emit errorOccurred("Failed to open video file");
    return false;
  }

  // Find stream information
  if (avformat_find_stream_info(m_formatContext, nullptr) < 0) {
    emit errorOccurred("Failed to find stream information");
    return false;
  }

  // Find video stream
  m_videoStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (m_videoStreamIndex < 0) {
    emit errorOccurred("No video stream found");
    return false;
  }

  m_videoStream = m_formatContext->streams[m_videoStreamIndex];

  // Initialize codec context
  if (!initCodecContext()) {
    return false;
  }

  // Calculate duration
  if (m_videoStream->duration != AV_NOPTS_VALUE) {
    m_duration = (m_videoStream->duration * 1000 * av_q2d(m_videoStream->time_base));
  } else {
    m_duration = m_formatContext->duration / 1000; // Convert from microseconds to milliseconds
  }

  emit durationChanged(m_duration);

  std::cout << "Hardware video decoder initialized successfully" << std::endl;
  std::cout << "Duration: " << m_duration << "ms" << std::endl;

  return true;
}

bool BPHardwareVideoDecoder::initCodecContext() {
  // Find decoder
  m_codec = const_cast<AVCodec*>(avcodec_find_decoder(m_videoStream->codecpar->codec_id));
  if (!m_codec) {
    emit errorOccurred("Unsupported codec");
    return false;
  }

  // Allocate codec context
  m_codecContext = avcodec_alloc_context3(m_codec);
  if (!m_codecContext) {
    emit errorOccurred("Failed to allocate codec context");
    return false;
  }

  // Copy codec parameters
  if (avcodec_parameters_to_context(m_codecContext, m_videoStream->codecpar) < 0) {
    emit errorOccurred("Failed to copy codec parameters");
    return false;
  }

  // Try to initialize hardware decoder
  if (!initHardwareDecoder()) {
    qWarning() << "Hardware decoder initialization failed, using software decoder";
  }

  // Open codec
  if (avcodec_open2(m_codecContext, m_codec, nullptr) < 0) {
    emit errorOccurred("Failed to open codec");
    return false;
  }

  return true;
}

bool BPHardwareVideoDecoder::initHardwareDecoder() {
  // Find hardware configuration
  const AVCodecHWConfig *config = nullptr;
  for (int i = 0; (config = avcodec_get_hw_config(m_codec, i)) != nullptr; i++) {
    if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
        config->device_type == HW_DEVICE_TYPE) {
      m_hwPixelFormat = config->pix_fmt;
      break;
    }
  }

  if (!config) {
    qWarning() << "Hardware configuration not found for device type" << HW_DEVICE_TYPE;
    return false;
  }

  // Create hardware device context
  if (!createHardwareDevice()) {
    return false;
  }

  // Set hardware device context
  m_codecContext->hw_device_ctx = av_buffer_ref(m_hwDeviceContext);
  m_codecContext->opaque = this;
  m_codecContext->get_format = get_hw_format;

  std::cout << "Hardware decoder initialized successfully" << std::endl;
  return true;
}

bool BPHardwareVideoDecoder::createHardwareDevice() {
  int ret = av_hwdevice_ctx_create(&m_hwDeviceContext, HW_DEVICE_TYPE, nullptr, nullptr, 0);
  if (ret < 0) {
    char errorBuf[256];
    av_strerror(ret, errorBuf, sizeof(errorBuf));
    qWarning() << "Failed to create hardware device context:" << errorBuf;
    return false;
  }

  return true;
}

void BPHardwareVideoDecoder::cleanupHardwareDevice() {
  if (m_hwFrameContext) {
    av_buffer_unref(&m_hwFrameContext);
  }
  if (m_hwDeviceContext) {
    av_buffer_unref(&m_hwDeviceContext);
  }
}

void BPHardwareVideoDecoder::play() {
  if (m_isPlaying.loadAcquire()) {
    return;
  }

  m_isPlaying.storeRelease(1);
  m_shouldStop.storeRelease(0);

  // Start decode thread
  if (!m_decodeThread) {
    m_decodeThread = new QThread(this);
    connect(m_decodeThread, &QThread::started, this, [this]() {
      decodeThread();
    });
  }

  if (!m_decodeThread->isRunning()) {
    m_decodeThread->start();
  }

  m_positionTimer->start();

  std::cout << "Playback started" << std::endl;
}

void BPHardwareVideoDecoder::pause() {
  m_isPlaying.storeRelease(0);
  m_positionTimer->stop();

  std::cout << "Playback paused" << std::endl;
}

void BPHardwareVideoDecoder::stop() {
  m_shouldStop.storeRelease(1);
  m_isPlaying.storeRelease(0);

  if (m_decodeThread && m_decodeThread->isRunning()) {
    m_decodeThread->quit();
    m_decodeThread->wait();
  }

  m_positionTimer->stop();
  m_position = 0;

  // Seek to beginning
  if (m_formatContext && m_videoStream) {
    av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecContext);
  }

  std::cout << "Playback stopped" << std::endl;
}

void BPHardwareVideoDecoder::seek(qint64 position) {
  if (!m_formatContext || !m_videoStream) {
    return;
  }

  // Convert milliseconds to stream time base
  int64_t timestamp = (position * m_videoStream->time_base.den) / (m_videoStream->time_base.num * 1000);

  if (av_seek_frame(m_formatContext, m_videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
    qWarning() << "Failed to seek to position" << position;
    return;
  }

  avcodec_flush_buffers(m_codecContext);
  m_position = position;

  std::cout << "Seeked to position:" << position << "ms" << std::endl;
}

void BPHardwareVideoDecoder::setVideoOutput(QVideoWidget *widget) {
  m_videoWidget = widget;
}

void BPHardwareVideoDecoder::updatePosition() {
  if (m_isPlaying.loadAcquire()) {
    emit positionChanged(m_position);
  }
}

void BPHardwareVideoDecoder::decodeThread() {
  std::cout << "Decode thread started" << std::endl;

  while (!m_shouldStop.loadAcquire()) {
    if (!m_isPlaying.loadAcquire()) {
      QThread::msleep(10);
      continue;
    }

    if (!decodeFrame()) {
      // End of file or error
      break;
    }

    // Small delay to prevent excessive CPU usage
    QThread::msleep(1);
  }

  std::cout << "Decode thread finished" << std::endl;

  // Emit finished signal
  QMetaObject::invokeMethod(this, [this]() {
    emit playbackFinished();
  }, Qt::QueuedConnection);
}

bool BPHardwareVideoDecoder::decodeFrame() {
  int ret = av_read_frame(m_formatContext, m_packet);
  if (ret < 0) {
    if (ret == AVERROR_EOF) {
      return false; // End of file
    }
    return false; // Error
  }

  if (m_packet->stream_index != m_videoStreamIndex) {
    av_packet_unref(m_packet);
    return true; // Skip non-video packets
  }

  // Send packet to decoder
  ret = avcodec_send_packet(m_codecContext, m_packet);
  if (ret < 0) {
    av_packet_unref(m_packet);
    return false;
  }

  // Receive frame from decoder
  ret = avcodec_receive_frame(m_codecContext, m_frame);
  if (ret < 0) {
    av_packet_unref(m_packet);
    return ret == AVERROR(EAGAIN); // Need more packets
  }

  // Process the frame
  processFrame(m_frame);

  // Update position
  if (m_frame->pts != AV_NOPTS_VALUE) {
    m_position = (m_frame->pts * 1000 * av_q2d(m_videoStream->time_base));
  }

  av_packet_unref(m_packet);
  return true;
}

void BPHardwareVideoDecoder::processFrame(AVFrame *frame) {
  AVFrame *outputFrame = frame;

  // If hardware frame, transfer to CPU
  if (frame->format == m_hwPixelFormat) {
    int ret = av_hwframe_transfer_data(m_hwFrame, frame, 0);
    if (ret < 0) {
      qWarning() << "Failed to transfer hardware frame to CPU";
      return;
    }
    outputFrame = m_hwFrame;
  }

  // Convert frame to QImage and display
  if (m_videoWidget) {
    QImage image(outputFrame->data[0], outputFrame->width, outputFrame->height,
                 outputFrame->linesize[0], QImage::Format_RGB888);

    // Convert YUV to RGB if needed
    if (outputFrame->format == AV_PIX_FMT_YUV420P) {
      // Simple YUV to RGB conversion (this could be optimized)
      QImage rgbImage(outputFrame->width, outputFrame->height, QImage::Format_RGB888);
      // TODO: Implement proper YUV to RGB conversion
      image = rgbImage;
    }

    // Send to video widget
    QMetaObject::invokeMethod(m_videoWidget, "setFrame", Qt::QueuedConnection, Q_ARG(QImage, image));
  }
}

// BPHardwareVideoWidget implementation
BPHardwareVideoWidget::BPHardwareVideoWidget(QWidget *parent)
    : QVideoWidget(parent) {
  setMinimumHeight(200);
}

BPHardwareVideoWidget::~BPHardwareVideoWidget() {
}

void BPHardwareVideoWidget::setFrame(const QImage &frame) {
  QMutexLocker locker(&m_frameMutex);
  m_currentFrame = frame;
  update(); // Trigger repaint
}

void BPHardwareVideoWidget::clearFrame() {
  QMutexLocker locker(&m_frameMutex);
  m_currentFrame = QImage();
  update();
}

void BPHardwareVideoWidget::paintEvent(QPaintEvent *event) {
  QVideoWidget::paintEvent(event);

  QMutexLocker locker(&m_frameMutex);
  if (!m_currentFrame.isNull()) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Scale image to fit widget while maintaining aspect ratio
    QRect targetRect = rect();
    QSize imageSize = m_currentFrame.size();
    QSize scaledSize = imageSize.scaled(targetRect.size(), Qt::KeepAspectRatio);
    QRect drawRect = QRect(0, 0, scaledSize.width(), scaledSize.height());
    drawRect.moveCenter(targetRect.center());

    painter.drawImage(drawRect, m_currentFrame);
  }
}

// BPHardwareVideoDialog implementation
BPHardwareVideoDialog::BPHardwareVideoDialog(const QString &videoPath, QWidget *parent)
    : QDialog(parent) {
  setupUI();

  // Initialize decoder
  m_decoder = new BPHardwareVideoDecoder(this);
  if (!m_decoder->initialize(videoPath)) {
    qWarning() << "Failed to initialize hardware video decoder";
    return;
  }

  m_decoder->setVideoOutput(m_videoWidget);

  // Connect signals
  QObject::connect(m_decoder, &BPHardwareVideoDecoder::positionChanged, this, &BPHardwareVideoDialog::onPositionChanged);
  QObject::connect(m_decoder, &BPHardwareVideoDecoder::durationChanged, this, &BPHardwareVideoDialog::onDurationChanged);
  QObject::connect(m_decoder, &BPHardwareVideoDecoder::playbackFinished, this, &BPHardwareVideoDialog::onPlaybackFinished);
  QObject::connect(m_decoder, &BPHardwareVideoDecoder::errorOccurred, this, &BPHardwareVideoDialog::onErrorOccurred);

  // Start playback
  m_decoder->play();
}

BPHardwareVideoDialog::~BPHardwareVideoDialog() {
  if (m_decoder) {
    m_decoder->stop();
  }
}

void BPHardwareVideoDialog::setupUI() {
  setWindowTitle("Hardware Video Playback");
  setModal(true);
  resize(1200, 800);

  // Main layout
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Header
  m_headerWidget = new QWidget;
  m_headerWidget->setFixedHeight(100);
  m_headerWidget->setStyleSheet("background-color: #202020;");

  QHBoxLayout *headerLayout = new QHBoxLayout(m_headerWidget);
  headerLayout->setContentsMargins(30, 20, 30, 20);

  QPushButton *backBtn = new QPushButton("← Back");
  backBtn->setStyleSheet(R"(
    QPushButton {
      background-color: #404040;
      color: white;
      border: none;
      padding: 10px 20px;
      border-radius: 5px;
      font-size: 16px;
    }
    QPushButton:hover {
      background-color: #505050;
    }
  )");
  QObject::connect(backBtn, &QPushButton::clicked, this, &QDialog::reject);

  QLabel *titleLabel = new QLabel("Hardware Video Playback");
  titleLabel->setStyleSheet("font-size: 24px; font-weight: 600; color: white;");
  titleLabel->setAlignment(Qt::AlignCenter);

  headerLayout->addWidget(backBtn);
  headerLayout->addWidget(titleLabel, 1);
  headerLayout->addSpacing(backBtn->width());

  // Video widget
  m_videoWidget = new BPHardwareVideoWidget;

  // Controls
  m_controlsWidget = new QWidget;
  m_controlsWidget->setFixedHeight(100);
  m_controlsWidget->setStyleSheet("background-color: #333333;");

  QHBoxLayout *controlsLayout = new QHBoxLayout(m_controlsWidget);
  controlsLayout->setContentsMargins(20, 10, 20, 10);

  m_playPauseButton = new QPushButton("Pause");
  m_playPauseButton->setFixedSize(100, 80);
  m_playPauseButton->setStyleSheet(R"(
    QPushButton {
      background-color: #009688;
      color: white;
      border: none;
      border-radius: 5px;
      font-size: 16px;
      font-weight: bold;
    }
    QPushButton:hover {
      background-color: #00796B;
    }
  )");
  QObject::connect(m_playPauseButton, &QPushButton::clicked, this, &BPHardwareVideoDialog::togglePlayback);

  m_positionSlider = new QSlider(Qt::Horizontal);
  m_positionSlider->setStyleSheet(R"(
    QSlider::groove:horizontal {
      border: 1px solid #999999;
      height: 8px;
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #B1B1B1, stop:1 #c4c4c4);
      margin: 2px 0;
      border-radius: 4px;
    }
    QSlider::handle:horizontal {
      background: #009688;
      border: 1px solid #5c5c5c;
      width: 18px;
      margin: -2px 0;
      border-radius: 9px;
    }
    QSlider::handle:horizontal:hover {
      background: #00796B;
    }
  )");
  QObject::connect(m_positionSlider, &QSlider::sliderMoved, this, &BPHardwareVideoDialog::onSliderMoved);

  m_timeLabel = new QLabel("00:00 / 00:00");
  m_timeLabel->setStyleSheet("color: white; font-size: 16px;");
  m_timeLabel->setMinimumWidth(150);

  controlsLayout->addWidget(m_playPauseButton);
  controlsLayout->addWidget(m_positionSlider, 1);
  controlsLayout->addWidget(m_timeLabel);

  // Add to main layout
  mainLayout->addWidget(m_headerWidget);
  mainLayout->addWidget(m_videoWidget, 1);
  mainLayout->addWidget(m_controlsWidget);
}

void BPHardwareVideoDialog::togglePlayback() {
  if (m_decoder->isPlaying()) {
    m_decoder->pause();
    m_playPauseButton->setText("Play");
  } else {
    m_decoder->play();
    m_playPauseButton->setText("Pause");
  }
}

void BPHardwareVideoDialog::onPositionChanged(qint64 position) {
  m_positionSlider->setValue(position);
  updateTimeLabel();
}

void BPHardwareVideoDialog::onDurationChanged(qint64 duration) {
  m_positionSlider->setRange(0, duration);
  updateTimeLabel();
}

void BPHardwareVideoDialog::onPlaybackFinished() {
  m_playPauseButton->setText("Play");
  m_positionSlider->setValue(0);
  updateTimeLabel();
}

void BPHardwareVideoDialog::onErrorOccurred(const QString &error) {
  qWarning() << "Video decoder error:" << error;
  // Could show error dialog here
}

void BPHardwareVideoDialog::onSliderMoved(int position) {
  m_decoder->seek(position);
}

// Helper function to format time
static QString formatTime(int seconds) {
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  int secs = seconds % 60;

  if (hours > 0) {
    return QString("%1:%2:%3").arg(hours, 2, 10, QChar('0'))
                              .arg(minutes, 2, 10, QChar('0'))
                              .arg(secs, 2, 10, QChar('0'));
  } else {
    return QString("%1:%2").arg(minutes, 2, 10, QChar('0'))
                           .arg(secs, 2, 10, QChar('0'));
  }
}

void BPHardwareVideoDialog::updateTimeLabel() {
  qint64 duration = m_decoder->duration();
  qint64 position = m_decoder->position();

  // Convert milliseconds to seconds
  int durationSecs = duration / 1000;
  int positionSecs = position / 1000;

  m_timeLabel->setText(formatTime(positionSecs) + " / " + formatTime(durationSecs));
}
