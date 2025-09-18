#include "bp_ffmpeg_decoder.h"
#include <QApplication>
#include <QDebug>
#include <iostream>
#include <algorithm>

BPFFmpegDecoder::BPFFmpegDecoder(QObject *parent)
    : QObject(parent) {
  // Initialize FFmpeg logging
  av_log_set_level(AV_LOG_WARNING);

  // Allocate frame and packet
  m_frame = av_frame_alloc();
  m_hwFrame = av_frame_alloc();
  m_swFrame = av_frame_alloc();
  m_packet = av_packet_alloc();

  // Setup position timer
  m_positionTimer = new QTimer(this);
  m_positionTimer->setInterval(100); // Update every 100ms
  connect(m_positionTimer, &QTimer::timeout, this, &BPFFmpegDecoder::updatePosition);
}

BPFFmpegDecoder::~BPFFmpegDecoder() {
  stop();
  cleanupDecoder();

  // Cleanup FFmpeg resources
  if (m_frame) av_frame_free(&m_frame);
  if (m_hwFrame) av_frame_free(&m_hwFrame);
  if (m_swFrame) av_frame_free(&m_swFrame);
  if (m_packet) av_packet_free(&m_packet);
}

bool BPFFmpegDecoder::initialize(const QString &videoPath) {
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
  m_videoStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &m_codec, 0);
  if (m_videoStreamIndex < 0) {
    emit errorOccurred("No video stream found");
    return false;
  }

  m_videoStream = m_formatContext->streams[m_videoStreamIndex];

  // Initialize decoder
  if (!initDecoder()) {
    return false;
  }

  // Calculate duration
  if (m_videoStream->duration != AV_NOPTS_VALUE) {
    m_duration = (m_videoStream->duration * 1000 * av_q2d(m_videoStream->time_base));
  } else if (m_formatContext->duration != AV_NOPTS_VALUE) {
    m_duration = m_formatContext->duration / 1000; // Convert from microseconds
  } else {
    m_duration = 0;
  }

  emit durationChanged(m_duration);

  std::cout << "FFmpeg decoder initialized successfully" << std::endl;
  std::cout << "Codec: " << m_codec->name << std::endl;
  std::cout << "Resolution: " << m_codecContext->width << "x" << m_codecContext->height << std::endl;
  std::cout << "Duration: " << m_duration << "ms" << std::endl;
  std::cout << "Hardware decoding: " << (m_useHardwareDecoding ? "enabled" : "disabled") << std::endl;

  return true;
}

bool BPFFmpegDecoder::initDecoder() {
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

  // Try hardware acceleration (following loggerd pattern)
  const AVCodecHWConfig *hw_config = nullptr;
  for (int i = 0; (hw_config = avcodec_get_hw_config(m_codec, i)) != nullptr; i++) {
    if (hw_config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
      // Try VAAPI first (Intel/AMD), then others
      if (hw_config->device_type == AV_HWDEVICE_TYPE_VAAPI ||
          hw_config->device_type == AV_HWDEVICE_TYPE_CUDA ||
          hw_config->device_type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX) {

        int ret = av_hwdevice_ctx_create(&m_hwDeviceContext, hw_config->device_type, nullptr, nullptr, 0);
        if (ret >= 0) {
          m_codecContext->hw_device_ctx = av_buffer_ref(m_hwDeviceContext);
          m_hwPixelFormat = hw_config->pix_fmt;
          m_useHardwareDecoding = true;
          std::cout << "Hardware acceleration enabled: " << av_hwdevice_get_type_name(hw_config->device_type) << std::endl;
          break;
        } else {
          char error_buf[256];
          av_strerror(ret, error_buf, sizeof(error_buf));
          qWarning() << "Failed to create hardware device" << av_hwdevice_get_type_name(hw_config->device_type) << ":" << error_buf;
        }
      }
    }
  }

  if (!m_useHardwareDecoding) {
    std::cout << "Using software decoding" << std::endl;
  }

  // Open codec
  if (avcodec_open2(m_codecContext, m_codec, nullptr) < 0) {
    emit errorOccurred("Failed to open codec");
    return false;
  }

  return true;
}

void BPFFmpegDecoder::cleanupDecoder() {
  if (m_hwDeviceContext) {
    av_buffer_unref(&m_hwDeviceContext);
  }
  if (m_codecContext) {
    avcodec_free_context(&m_codecContext);
  }
  if (m_formatContext) {
    avformat_close_input(&m_formatContext);
  }
}

void BPFFmpegDecoder::play() {
  if (m_isPlaying.loadAcquire()) {
    return;
  }

  m_isPlaying.storeRelease(1);
  m_shouldStop.storeRelease(0);

  // Start decode thread
  if (!m_decodeThread) {
    m_decodeThread = std::make_unique<QThread>();
    connect(m_decodeThread.get(), &QThread::started, this, &BPFFmpegDecoder::decodeLoop);
  }

  if (!m_decodeThread->isRunning()) {
    moveToThread(m_decodeThread.get());
    m_decodeThread->start();
  }

  m_positionTimer->start();
  std::cout << "Playback started" << std::endl;
}

void BPFFmpegDecoder::pause() {
  m_isPlaying.storeRelease(0);
  m_positionTimer->stop();
  std::cout << "Playback paused" << std::endl;
}

void BPFFmpegDecoder::stop() {
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

void BPFFmpegDecoder::seek(qint64 position) {
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
}

void BPFFmpegDecoder::setVideoOutput(QVideoWidget *widget) {
  m_videoWidget = widget;
}

void BPFFmpegDecoder::updatePosition() {
  if (m_isPlaying.loadAcquire()) {
    emit positionChanged(m_position);
  }
}

void BPFFmpegDecoder::decodeLoop() {
  std::cout << "Decode thread started" << std::endl;

  qint64 lastFrameTime = 0;

  while (!m_shouldStop.loadAcquire()) {
    if (!m_isPlaying.loadAcquire()) {
      QThread::msleep(10);
      continue;
    }

    // Frame rate limiting
    qint64 currentTime = QTime::currentTime().msecsSinceStartOfDay();
    if (lastFrameTime > 0 && (currentTime - lastFrameTime) < FRAME_DELAY_MS) {
      QThread::msleep(FRAME_DELAY_MS - (currentTime - lastFrameTime));
    }

    if (!decodeFrame()) {
      // End of file
      break;
    }

    lastFrameTime = QTime::currentTime().msecsSinceStartOfDay();
  }

  std::cout << "Decode thread finished" << std::endl;

  // Emit finished signal
  QMetaObject::invokeMethod(this, [this]() {
    emit playbackFinished();
  }, Qt::QueuedConnection);
}

bool BPFFmpegDecoder::decodeFrame() {
  int ret = av_read_frame(m_formatContext, m_packet);
  if (ret < 0) {
    return false; // End of file or error
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

  // Process the decoded frame
  processFrame(m_frame);

  // Update position
  if (m_frame->pts != AV_NOPTS_VALUE) {
    m_position = (m_frame->pts * 1000 * av_q2d(m_videoStream->time_base));
  }

  av_packet_unref(m_packet);
  return true;
}

void BPFFmpegDecoder::processFrame(AVFrame *frame) {
  AVFrame *outputFrame = frame;

  // Handle hardware frame transfer (like in ffmpeg_encoder.cc)
  if (m_useHardwareDecoding && frame->format == m_hwPixelFormat) {
    int ret = av_hwframe_transfer_data(m_swFrame, frame, 0);
    if (ret < 0) {
      qWarning() << "Failed to transfer hardware frame to CPU";
      return;
    }
    outputFrame = m_swFrame;
  }

  // Convert to RGB and send to widget
  if (m_videoWidget) {
    QImage image = convertYUVToRGB(outputFrame);
    QMetaObject::invokeMethod(m_videoWidget, "setFrame", Qt::QueuedConnection, Q_ARG(QImage, image));
  }
}

QImage BPFFmpegDecoder::convertYUVToRGB(AVFrame *frame) {
  // Use libyuv for efficient conversion (following loggerd pattern)
  int width = frame->width;
  int height = frame->height;

  // Resize RGB buffer if needed
  size_t rgb_size = width * height * 3;
  if (m_rgbBuffer.size() != rgb_size) {
    m_rgbBuffer.resize(rgb_size);
  }

  uint8_t *rgb_data = m_rgbBuffer.data();

  // Convert using libyuv (much faster than our manual conversion)
  if (frame->format == AV_PIX_FMT_YUV420P) {
    // Convert I420 to RGB24 (RGB888)
    libyuv::I420ToRGB24(
      frame->data[0], frame->linesize[0],  // Y plane
      frame->data[1], frame->linesize[1],  // U plane
      frame->data[2], frame->linesize[2],  // V plane
      rgb_data, width * 3,                 // RGB output
      width, height
    );
  } else if (frame->format == AV_PIX_FMT_NV12) {
    // Convert NV12 to I420 first, then I420 to RGB24
    std::vector<uint8_t> i420_buffer(width * height * 3 / 2);
    uint8_t *i420_y = i420_buffer.data();
    uint8_t *i420_u = i420_y + width * height;
    uint8_t *i420_v = i420_u + width * height / 4;

    libyuv::NV12ToI420(
      frame->data[0], frame->linesize[0],  // Y plane
      frame->data[1], frame->linesize[1],  // UV plane
      i420_y, width,                       // I420 Y
      i420_u, width / 2,                   // I420 U
      i420_v, width / 2,                   // I420 V
      width, height
    );

    libyuv::I420ToRGB24(
      i420_y, width,       // Y plane
      i420_u, width / 2,   // U plane
      i420_v, width / 2,   // V plane
      rgb_data, width * 3, // RGB output
      width, height
    );
  } else {
    // Fallback for unsupported formats
    qWarning() << "Unsupported pixel format:" << frame->format;
    return QImage(width, height, QImage::Format_RGB888); // Return black frame
  }

  // Create QImage from RGB data
  return QImage(rgb_data, width, height, width * 3, QImage::Format_RGB888).copy();
}

// BPFFmpegVideoWidget implementation
BPFFmpegVideoWidget::BPFFmpegVideoWidget(QWidget *parent)
    : QVideoWidget(parent) {
  setMinimumHeight(200);
}

BPFFmpegVideoWidget::~BPFFmpegVideoWidget() {
}

void BPFFmpegVideoWidget::setFrame(const QImage &frame) {
  // Non-blocking frame update
  if (m_frameMutex.tryLock(5)) { // 5ms timeout
    m_currentFrame = frame;
    m_frameMutex.unlock();
    update(); // Trigger repaint
  }
}

void BPFFmpegVideoWidget::clearFrame() {
  if (m_frameMutex.tryLock(5)) { // 5ms timeout
    m_currentFrame = QImage();
    m_frameMutex.unlock();
    update();
  }
}

void BPFFmpegVideoWidget::paintEvent(QPaintEvent *event) {
  QVideoWidget::paintEvent(event);

  // Non-blocking frame rendering
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
    QSize imageSize = frameToRender.size();
    QSize scaledSize = imageSize.scaled(targetRect.size(), Qt::KeepAspectRatio);
    QRect drawRect = QRect(0, 0, scaledSize.width(), scaledSize.height());
    drawRect.moveCenter(targetRect.center());

    painter.drawImage(drawRect, frameToRender);
  }
}

// MOC file will be generated by build system