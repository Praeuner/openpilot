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
  // Try each hardware device type in order of preference
  for (const auto& deviceType : HW_DEVICE_TYPES) {
    if (deviceType == AV_HWDEVICE_TYPE_NONE) {
      qWarning() << "No hardware acceleration available, using software decoder";
      return false;
    }

    // Find hardware configuration for this device type
    const AVCodecHWConfig *config = nullptr;
    for (int i = 0; (config = avcodec_get_hw_config(m_codec, i)) != nullptr; i++) {
      if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
          config->device_type == deviceType) {
        m_hwPixelFormat = config->pix_fmt;
        break;
      }
    }

    if (!config) {
      qWarning() << "Hardware configuration not found for device type" << deviceType;
      continue;
    }

    // Try to create hardware device context for this type
    if (createHardwareDevice(deviceType)) {
      // Set hardware device context
      m_codecContext->hw_device_ctx = av_buffer_ref(m_hwDeviceContext);
      m_codecContext->opaque = this;
      m_codecContext->get_format = get_hw_format;

      std::cout << "Hardware decoder initialized successfully with device type " << deviceType << std::endl;
      return true;
    }
  }

  qWarning() << "Failed to initialize any hardware decoder, using software decoder";
  return false;
}

bool BPHardwareVideoDecoder::createHardwareDevice(AVHWDeviceType deviceType) {
  int ret = av_hwdevice_ctx_create(&m_hwDeviceContext, deviceType, nullptr, nullptr, 0);
  if (ret < 0) {
    char errorBuf[256];
    av_strerror(ret, errorBuf, sizeof(errorBuf));
    qWarning() << "Failed to create hardware device context for type" << deviceType << ":" << errorBuf;
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

  // Frame rate limiting (target ~20 FPS for playback)
  const int frameDelay = 50; // 50ms = 20 FPS
  qint64 lastFrameTime = 0;

  while (!m_shouldStop.loadAcquire()) {
    if (!m_isPlaying.loadAcquire()) {
      QThread::msleep(10);
      continue;
    }

    qint64 currentTime = QTime::currentTime().msecsSinceStartOfDay();

    // Frame rate limiting to prevent overwhelming the UI
    if (lastFrameTime > 0 && (currentTime - lastFrameTime) < frameDelay) {
      QThread::msleep(frameDelay - (currentTime - lastFrameTime));
    }

    if (!decodeFrame()) {
      // End of file or error
      break;
    }

    lastFrameTime = QTime::currentTime().msecsSinceStartOfDay();

    // Yield to other threads periodically
    QThread::yieldCurrentThread();
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

QImage BPHardwareVideoDecoder::convertYUV420PToRGB(AVFrame *frame) {
  QImage image(frame->width, frame->height, QImage::Format_RGB888);

  const uint8_t *yPlane = frame->data[0];
  const uint8_t *uPlane = frame->data[1];
  const uint8_t *vPlane = frame->data[2];

  const int yStride = frame->linesize[0];
  const int uStride = frame->linesize[1];
  const int vStride = frame->linesize[2];

  uint8_t *rgbData = image.bits();
  const int rgbStride = image.bytesPerLine();

  // Optimized YUV420P to RGB conversion (process rows to reduce cache misses)
  for (int y = 0; y < frame->height; y++) {
    const uint8_t *yRow = yPlane + y * yStride;
    const uint8_t *uRow = uPlane + (y / 2) * uStride;
    const uint8_t *vRow = vPlane + (y / 2) * vStride;
    uint8_t *rgbRow = rgbData + y * rgbStride;

    for (int x = 0; x < frame->width; x++) {
      // Get Y, U, V values
      int Y = yRow[x];
      int U = uRow[x / 2] - 128;
      int V = vRow[x / 2] - 128;

      // Convert to RGB using integer math (faster than floating point)
      int R = Y + ((1436 * V) >> 10);
      int G = Y - ((352 * U + 731 * V) >> 10);
      int B = Y + ((1814 * U) >> 10);

      // Clamp values to 0-255 (branchless)
      R = (R < 0) ? 0 : (R > 255) ? 255 : R;
      G = (G < 0) ? 0 : (G > 255) ? 255 : G;
      B = (B < 0) ? 0 : (B > 255) ? 255 : B;

      // Store RGB values
      rgbRow[x * 3 + 0] = R;
      rgbRow[x * 3 + 1] = G;
      rgbRow[x * 3 + 2] = B;
    }

    // Yield periodically to prevent blocking too long
    if ((y & 15) == 0) {  // Every 16 rows
      QThread::yieldCurrentThread();
    }
  }

  return image;
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
    QImage image;

    // Handle different pixel formats
    if (outputFrame->format == AV_PIX_FMT_YUV420P) {
      // Convert YUV420P to RGB
      image = convertYUV420PToRGB(outputFrame);
    } else if (outputFrame->format == AV_PIX_FMT_RGB24) {
      // Direct RGB24 format
      image = QImage(outputFrame->data[0], outputFrame->width, outputFrame->height,
                     outputFrame->linesize[0], QImage::Format_RGB888);
    } else if (outputFrame->format == AV_PIX_FMT_BGRA) {
      // BGRA format (common on macOS)
      image = QImage(outputFrame->data[0], outputFrame->width, outputFrame->height,
                     outputFrame->linesize[0], QImage::Format_ARGB32);
    } else {
      // Fallback: try to interpret as RGB888
      qWarning() << "Unsupported pixel format:" << outputFrame->format << ", trying RGB888 fallback";
      image = QImage(outputFrame->width, outputFrame->height, QImage::Format_RGB888);
      image.fill(Qt::black); // Fill with black for unsupported formats
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
  // Use tryLock to avoid blocking if paint is happening
  if (m_frameMutex.tryLock(5)) { // 5ms timeout
    m_currentFrame = frame;
    m_frameMutex.unlock();
    update(); // Trigger repaint
  }
}

void BPHardwareVideoWidget::clearFrame() {
  if (m_frameMutex.tryLock(5)) { // 5ms timeout
    m_currentFrame = QImage();
    m_frameMutex.unlock();
    update();
  }
}

void BPHardwareVideoWidget::paintEvent(QPaintEvent *event) {
  QVideoWidget::paintEvent(event);

  // Try to get the frame without blocking
  QImage frameToRender;
  if (m_frameMutex.tryLock(1)) { // 1ms timeout for paint
    frameToRender = m_currentFrame;
    m_frameMutex.unlock();
  }

  if (!frameToRender.isNull()) {
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

  // Center the modal dialog on screen
  centerOnScreen();

  // Set modal properties
  setModal(true);
  setWindowModality(Qt::ApplicationModal);

  // Enable keyboard focus
  setFocusPolicy(Qt::StrongFocus);
  setFocus();

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
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

  // Optimize for QCOM2 device (portrait 1080x2160)
  // Use most of the screen but leave some margin for touch interaction
  resize(1000, 1800);

  // SidebarBP-inspired styling
  setStyleSheet(R"(
    BPHardwareVideoDialog {
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1a1a1a, stop:1 #0f0f0f);
      border: 3px solid #404040;
      border-radius: 20px;
    }
  )");

  // Main layout with horizontal split for video and camera stack
  QHBoxLayout *mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Header with SidebarBP card styling
  m_headerWidget = new QWidget;
  m_headerWidget->setFixedHeight(120); // Larger for touch
  m_headerWidget->setStyleSheet(R"(
    QWidget {
      background-color: #242424;
      border-bottom: 3px solid #404040;
      border-top-left-radius: 20px;
      border-top-right-radius: 20px;
    }
  )");

  QHBoxLayout *headerLayout = new QHBoxLayout(m_headerWidget);
  headerLayout->setContentsMargins(30, 20, 30, 20); // SidebarBP spacing

  // Close button with SidebarBP danger button styling
  QPushButton *closeBtn = new QPushButton("✕");
  closeBtn->setFixedSize(80, 80); // Much larger for touch
  closeBtn->setStyleSheet(R"(
    QPushButton {
      background-color: #F44336;
      color: white;
      border: 2px solid transparent;
      border-radius: 12px;
      font-size: 36px;
      font-weight: bold;
    }
    QPushButton:hover {
      background-color: #D32F2F;
      border-color: #FFCDD2;
    }
    QPushButton:pressed {
      background-color: #C62828;
    }
  )");
  QObject::connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

  // Title with SidebarBP font styling
  QLabel *titleLabel = new QLabel("Route Video Playback");
  titleLabel->setStyleSheet("font-size: 48px; font-weight: 600; color: white;"); // SidebarBP font weight
  titleLabel->setAlignment(Qt::AlignCenter);

  // Minimize button with SidebarBP action button styling
  QPushButton *minimizeBtn = new QPushButton("−");
  minimizeBtn->setFixedSize(80, 80);
  minimizeBtn->setStyleSheet(R"(
    QPushButton {
      background-color: #2196F3;
      color: white;
      border: 2px solid transparent;
      border-radius: 12px;
      font-size: 36px;
      font-weight: bold;
    }
    QPushButton:hover {
      background-color: #1976D2;
      border-color: #64B5F6;
    }
    QPushButton:pressed {
      background-color: #1565C0;
    }
  )");
  QObject::connect(minimizeBtn, &QPushButton::clicked, this, &QWidget::showMinimized);

  headerLayout->addWidget(closeBtn);
  headerLayout->addWidget(titleLabel, 1);
  headerLayout->addWidget(minimizeBtn);

  // Left side: Video player with header and controls
  QWidget *leftSide = new QWidget;
  QVBoxLayout *leftLayout = new QVBoxLayout(leftSide);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(0);

  leftLayout->addWidget(m_headerWidget);

  // Video widget optimized for QCOM2 portrait mode
  m_videoWidget = new BPHardwareVideoWidget;
  m_videoWidget->setMinimumHeight(1200); // Larger for portrait mode

  // Controls with SidebarBP card styling
  m_controlsWidget = new QWidget;
  m_controlsWidget->setFixedHeight(180); // Much larger for touch
  m_controlsWidget->setStyleSheet(R"(
    QWidget {
      background-color: #242424;
      border-top: 3px solid #404040;
      border-bottom-left-radius: 20px;
      border-bottom-right-radius: 20px;
    }
  )");

  QHBoxLayout *controlsLayout = new QHBoxLayout(m_controlsWidget);
  controlsLayout->setContentsMargins(30, 30, 30, 30); // SidebarBP spacing

  // Play/Pause button with SidebarBP action button styling
  m_playPauseButton = new QPushButton("⏸ Pause");
  m_playPauseButton->setFixedSize(200, 120); // Much larger for touch
  m_playPauseButton->setStyleSheet(R"(
    QPushButton {
      background-color: #2196F3;
      color: white;
      border: 2px solid transparent;
      border-radius: 12px;
      font-size: 36px;
      font-weight: 600;
    }
    QPushButton:hover {
      background-color: #1976D2;
      border-color: #64B5F6;
    }
    QPushButton:pressed {
      background-color: #1565C0;
    }
  )");
  QObject::connect(m_playPauseButton, &QPushButton::clicked, this, &BPHardwareVideoDialog::togglePlayback);

  // Touch-friendly position slider with SidebarBP colors
  m_positionSlider = new QSlider(Qt::Horizontal);
  m_positionSlider->setMinimumHeight(60); // Larger touch area
  m_positionSlider->setStyleSheet(R"(
    QSlider::groove:horizontal {
      border: none;
      height: 20px;
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #404040, stop:1 #2a2a2a);
      margin: 10px 0;
      border-radius: 10px;
    }
    QSlider::handle:horizontal {
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2196F3, stop:1 #1976D2);
      border: 3px solid #64B5F6;
      width: 40px;
      margin: -10px 0;
      border-radius: 20px;
    }
    QSlider::handle:horizontal:hover {
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #42A5F5, stop:1 #2196F3);
      border-color: #90CAF9;
    }
    QSlider::handle:horizontal:pressed {
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1565C0, stop:1 #0D47A1);
    }
  )");
  QObject::connect(m_positionSlider, &QSlider::sliderMoved, this, &BPHardwareVideoDialog::onSliderMoved);

  // Time label with SidebarBP card styling
  m_timeLabel = new QLabel("00:00 / 00:00");
  m_timeLabel->setStyleSheet(R"(
    QLabel {
      color: white;
      font-size: 32px;
      font-weight: 600;
      background-color: #242424;
      padding: 15px 25px;
      border-radius: 12px;
      border: 2px solid #404040;
    }
  )");
  m_timeLabel->setMinimumWidth(250);
  m_timeLabel->setMinimumHeight(80);
  m_timeLabel->setAlignment(Qt::AlignCenter);

  controlsLayout->addWidget(m_playPauseButton);
  controlsLayout->addWidget(m_positionSlider, 1);
  controlsLayout->addWidget(m_timeLabel);

  leftLayout->addWidget(m_videoWidget, 1);
  leftLayout->addWidget(m_controlsWidget);

  // Right side: Camera stack panel
  QWidget *cameraStackPanel = new QWidget;
  cameraStackPanel->setFixedWidth(200); // Fixed width for camera stack
  cameraStackPanel->setStyleSheet(R"(
    QWidget {
      background-color: #242424;
      border-left: 3px solid #404040;
    }
  )");

  QVBoxLayout *cameraLayout = new QVBoxLayout(cameraStackPanel);
  cameraLayout->setContentsMargins(20, 20, 20, 20);
  cameraLayout->setSpacing(15);

  // Camera selection buttons
  QLabel *cameraLabel = new QLabel("Camera Views");
  cameraLabel->setStyleSheet("font-size: 24px; font-weight: 600; color: white; margin-bottom: 10px;");
  cameraLayout->addWidget(cameraLabel);

  // Camera buttons
  QPushButton *fcameraBtn = new QPushButton("Front Camera");
  QPushButton *dcameraBtn = new QPushButton("Driver Camera");
  QPushButton *ecameraBtn = new QPushButton("Wide Camera");

  // Style camera buttons
  QString cameraButtonStyle = R"(
    QPushButton {
      background-color: #2196F3;
      color: white;
      border: 2px solid transparent;
      border-radius: 8px;
      font-size: 18px;
      font-weight: 600;
      padding: 15px 10px;
    }
    QPushButton:hover {
      background-color: #1976D2;
      border-color: #64B5F6;
    }
    QPushButton:pressed {
      background-color: #1565C0;
    }
  )";

  fcameraBtn->setStyleSheet(cameraButtonStyle);
  dcameraBtn->setStyleSheet(cameraButtonStyle);
  ecameraBtn->setStyleSheet(cameraButtonStyle);

  cameraLayout->addWidget(fcameraBtn);
  cameraLayout->addWidget(dcameraBtn);
  cameraLayout->addWidget(ecameraBtn);
  cameraLayout->addStretch();

  // Delete button at bottom of camera stack
  QPushButton *deleteBtn = new QPushButton("🗑 Delete Route");
  deleteBtn->setStyleSheet(R"(
    QPushButton {
      background-color: #F44336;
      color: white;
      border: 2px solid transparent;
      border-radius: 8px;
      font-size: 18px;
      font-weight: 600;
      padding: 15px 10px;
    }
    QPushButton:hover {
      background-color: #D32F2F;
      border-color: #FFCDD2;
    }
    QPushButton:pressed {
      background-color: #C62828;
    }
  )");
  cameraLayout->addWidget(deleteBtn);

  // Add both sides to main layout
  mainLayout->addWidget(leftSide, 1); // Video side takes most space
  mainLayout->addWidget(cameraStackPanel); // Camera stack on right
}

void BPHardwareVideoDialog::togglePlayback() {
  if (m_decoder->isPlaying()) {
    m_decoder->pause();
    m_playPauseButton->setText("▶ Play");
  } else {
    m_decoder->play();
    m_playPauseButton->setText("⏸ Pause");
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
  m_playPauseButton->setText("▶ Play");
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

void BPHardwareVideoDialog::centerOnScreen() {
  // Optimize for QCOM2 device (portrait 1080x2160)
  // Center the modal with appropriate margins for touch interaction

  // For QCOM2 portrait 1080x2160 resolution, center with margins
  int screenWidth = 1080;
  int screenHeight = 2160;

  // Calculate center position with margins
  int marginX = 40;  // 40px margin on each side (smaller for portrait)
  int marginY = 80;  // 80px margin top/bottom

  int x = marginX;
  int y = marginY;

  // Ensure the dialog fits within screen bounds
  int maxWidth = screenWidth - (2 * marginX);
  int maxHeight = screenHeight - (2 * marginY);

  if (width() > maxWidth) {
    resize(maxWidth, height());
  }
  if (height() > maxHeight) {
    resize(width(), maxHeight);
  }

  // Move to position
  move(x, y);
}

void BPHardwareVideoDialog::keyPressEvent(QKeyEvent *event) {
  switch (event->key()) {
    case Qt::Key_Space:
      // Spacebar toggles play/pause
      togglePlayback();
      event->accept();
      break;

    case Qt::Key_Escape:
      // Escape closes the dialog
      reject();
      event->accept();
      break;

    case Qt::Key_Left:
      // Left arrow seeks backward 10 seconds
      if (m_decoder) {
        qint64 currentPos = m_decoder->position();
        qint64 newPos = qMax(0LL, currentPos - 10000);
        m_decoder->seek(newPos);
      }
      event->accept();
      break;

    case Qt::Key_Right:
      // Right arrow seeks forward 10 seconds
      if (m_decoder) {
        qint64 currentPos = m_decoder->position();
        qint64 duration = m_decoder->duration();
        qint64 newPos = qMin(duration, currentPos + 10000);
        m_decoder->seek(newPos);
      }
      event->accept();
      break;

    case Qt::Key_F:
      // F key toggles fullscreen (if supported)
      if (isFullScreen()) {
        showNormal();
      } else {
        showFullScreen();
      }
      event->accept();
      break;

    default:
      QDialog::keyPressEvent(event);
      break;
  }
}

bool BPHardwareVideoDialog::event(QEvent *event) {
  // Handle touch events for better touch screen support
  if (event->type() == QEvent::TouchBegin ||
      event->type() == QEvent::TouchUpdate ||
      event->type() == QEvent::TouchEnd) {
    // Convert touch events to mouse events for better compatibility
    QTouchEvent *touchEvent = static_cast<QTouchEvent*>(event);
    if (!touchEvent->touchPoints().isEmpty()) {
      const QTouchEvent::TouchPoint &touchPoint = touchEvent->touchPoints().first();

      // Create corresponding mouse event
      QMouseEvent *mouseEvent = nullptr;
      switch (event->type()) {
        case QEvent::TouchBegin:
          mouseEvent = new QMouseEvent(QEvent::MouseButtonPress,
                                     touchPoint.pos().toPoint(),
                                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
          break;
        case QEvent::TouchUpdate:
          mouseEvent = new QMouseEvent(QEvent::MouseMove,
                                     touchPoint.pos().toPoint(),
                                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
          break;
        case QEvent::TouchEnd:
          mouseEvent = new QMouseEvent(QEvent::MouseButtonRelease,
                                     touchPoint.pos().toPoint(),
                                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
          break;
        default:
          break;
      }

      if (mouseEvent) {
        QCoreApplication::postEvent(this, mouseEvent);
        event->accept();
        return true;
      }
    }
  }

  // Handle other events normally
  return QDialog::event(event);
}
