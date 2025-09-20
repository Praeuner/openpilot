// bp_video_dialog.cc - Route Video Playback Dialog Implementation
#include "bp_routes_panel.h"
#include "bp_utils.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QKeyEvent>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QImage>
#include <QButtonGroup>
#include <QThread>
#include <QtConcurrent>
#include <iostream>

BPRouteVideoDialog::BPRouteVideoDialog(const QString &routeBase, QWidget *parent)
    : BPDialogBase(parent), routeBaseName(routeBase) {

  std::cout << "[VIDEO DEBUG] === BPRouteVideoDialog Constructor ===" << std::endl;
  std::cout << "[VIDEO DEBUG] Route: " << routeBase.toStdString() << std::endl;

  setWindowTitle("Route Video Playback");

  // Load route info
  QString routePath = static_cast<BPRoutesPanel*>(parent)->getRoutesDir() + "/" + routeBase;
  std::cout << "[VIDEO DEBUG] Route path: " << routePath.toStdString() << std::endl;
  routeInfo = static_cast<BPRoutesPanel*>(parent)->getRouteInfo(routePath);

  std::cout << "[VIDEO DEBUG] Route info - Segments: " << routeInfo.segments
            << ", HasFront: " << routeInfo.hasFrontVideo
            << ", HasWide: " << routeInfo.hasWideVideo
            << ", HasDriver: " << routeInfo.hasDriverVideo
            << ", HasLQ: " << routeInfo.hasLQVideo << std::endl;

  // Initialize timers
  playbackTimer = new QTimer(this);
  positionTimer = new QTimer(this);
  positionTimer->setInterval(100); // Update position every 100ms

  connect(positionTimer, &QTimer::timeout, this, &BPRouteVideoDialog::updatePlaybackPosition);

  setupUI();
  loadVideoSegments();
  loadThumbnail();
}

BPRouteVideoDialog::~BPRouteVideoDialog() {
  if (decoder) {
    decoder->decoder_close();
  }
}

void BPRouteVideoDialog::setupUI() {
  // Main container for the modal (80/20 split)
  QHBoxLayout *mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Left side - Video display (80%)
  setupVideoDisplay();
  mainLayout->addWidget(videoContainer, 8);

  // Right side - Camera panel (20%)
  setupCameraPanel();
  mainLayout->addWidget(cameraPanel, 2);
}

void BPRouteVideoDialog::setupVideoDisplay() {
  videoContainer = new QWidget;
  videoContainer->setStyleSheet("background: #000000;");

  QVBoxLayout *videoLayout = new QVBoxLayout(videoContainer);
  videoLayout->setContentsMargins(0, 0, 0, 0);
  videoLayout->setSpacing(0);

  // Header - much larger for 6" display
  QWidget *headerWidget = new QWidget;
  headerWidget->setFixedHeight(160);
  headerWidget->setStyleSheet("background: #202020;");

  QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(40, 20, 40, 20);
  headerLayout->setSpacing(30);

  // Close button - moved to far left and much larger
  closeButton = new QPushButton("✕");
  closeButton->setFixedSize(120, 120);
  closeButton->setStyleSheet(R"(
    QPushButton {
      background: #444444;
      border: 2px solid #666666;
      border-radius: 60px;
      font-size: 60px;
      color: #FFD700;
      font-weight: bold;
    }
    QPushButton:hover {
      background: #555555;
      border: 2px solid #FFD700;
    }
    QPushButton:pressed {
      background: #333333;
    }
  )");
  connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

  // Route title - much larger - extract time from route name
  QString displayTime = routeBaseName;
  if (routeBaseName.contains("--")) {
    QStringList parts = routeBaseName.split("--");
    if (parts.size() >= 2) {
      QString timePart = parts[1]; // e.g., "14-30-25"
      QStringList timeParts = timePart.split("-");
      if (timeParts.size() >= 3) {
        QString hour = timeParts[0];
        QString minute = timeParts[1];
        int hourInt = hour.toInt();
        QString ampm = (hourInt >= 12) ? "PM" : "AM";
        if (hourInt > 12) hourInt -= 12;
        if (hourInt == 0) hourInt = 12;
        displayTime = QString("%1:%2 %3").arg(hourInt).arg(minute).arg(ampm);
      }
    }
  }
  routeTitle = new QLabel(QString("Route: %1").arg(displayTime));
  routeTitle->setStyleSheet("font-size: 52px; font-weight: 600; color: white; margin-left: 30px;");

  // Star button - much larger
  starButton = new QPushButton;
  starButton->setFixedSize(120, 120);
  starButton->setText(routeInfo.isStarred ? "★" : "☆");
  starButton->setStyleSheet(R"(
    QPushButton {
      background: #444444;
      border: 2px solid #666666;
      border-radius: 60px;
      font-size: 60px;
      color: #FFD700;
      font-weight: bold;
    }
    QPushButton:hover {
      background: #555555;
      border: 2px solid #FFD700;
    }
    QPushButton:pressed {
      background: #333333;
    }
  )");
  connect(starButton, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleStar);

  // Fullscreen button - much larger
  fullscreenButton = new QPushButton("⛶");
  fullscreenButton->setFixedSize(120, 120);
  fullscreenButton->setStyleSheet(starButton->styleSheet());
  connect(fullscreenButton, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleFullscreen);

  headerLayout->addWidget(closeButton);
  headerLayout->addWidget(routeTitle, 1);
  headerLayout->addWidget(starButton);
  headerLayout->addWidget(fullscreenButton);

  videoLayout->addWidget(headerWidget);

  // Video display area - much larger
  videoDisplay = new QLabel;
  videoDisplay->setStyleSheet("background: #000000; border: none; color: white; font-size: 96px; font-weight: bold;");
  videoDisplay->setAlignment(Qt::AlignCenter);
  videoDisplay->setText("Loading video...");
  videoDisplay->setScaledContents(true);
  videoDisplay->setMinimumSize(1400, 800);

  videoLayout->addWidget(videoDisplay, 1);

  // Controls
  setupControls();
  videoLayout->addWidget(controlsWidget);
}

void BPRouteVideoDialog::setupControls() {
  controlsWidget = new QWidget;
  controlsWidget->setFixedHeight(180);
  controlsWidget->setStyleSheet("background: #333333;");

  QHBoxLayout *controlsLayout = new QHBoxLayout(controlsWidget);
  controlsLayout->setContentsMargins(50, 25, 50, 25);
  controlsLayout->setSpacing(40);

  // Play/Pause button - much larger for touch
  playPauseButton = new QPushButton("▶");
  playPauseButton->setFixedSize(130, 130);
  playPauseButton->setStyleSheet(R"(
    QPushButton {
      background: #2196F3;
      color: white;
      font-size: 60px;
      border: 3px solid #1976D2;
      border-radius: 65px;
      font-weight: bold;
    }
    QPushButton:pressed {
      background: #1976D2;
    }
    QPushButton:hover {
      background: #1E88E5;
      border: 3px solid #FFD700;
    }
  )");
  connect(playPauseButton, &QPushButton::clicked, this, &BPRouteVideoDialog::togglePlayback);

  // Position slider - much larger for touch
  positionSlider = new QSlider(Qt::Horizontal);
  positionSlider->setFixedHeight(60);
  positionSlider->setStyleSheet(R"(
    QSlider::groove:horizontal {
      border: 2px solid #999999;
      height: 20px;
      background: #555555;
      border-radius: 10px;
    }
    QSlider::handle:horizontal {
      background: #2196F3;
      border: 3px solid #1976D2;
      width: 40px;
      height: 40px;
      border-radius: 20px;
      margin: -10px 0;
    }
    QSlider::handle:horizontal:hover {
      background: #FFD700;
      border: 3px solid #FFC107;
    }
  )");
  connect(positionSlider, &QSlider::sliderPressed, [this]() {
    positionTimer->stop();
  });
  connect(positionSlider, &QSlider::sliderReleased, [this]() {
    // Seek to new position
    currentPosition = positionSlider->value();
    if (isPlaying) {
      positionTimer->start();
    }
  });

  // Time label - much larger
  timeLabel = new QLabel("00:00 / 00:00");
  timeLabel->setStyleSheet("color: white; font-size: 42px; font-family: monospace; font-weight: bold;");
  timeLabel->setMinimumWidth(280);

  controlsLayout->addWidget(playPauseButton);
  controlsLayout->addWidget(positionSlider, 1);
  controlsLayout->addWidget(timeLabel);
}

void BPRouteVideoDialog::setupCameraPanel() {
  cameraPanel = new QWidget;
  // Optimized for 20% split with large touch targets
  cameraPanel->setStyleSheet("background: #2a2a2a; border-left: 1px solid #444444;");

  QVBoxLayout *panelLayout = new QVBoxLayout(cameraPanel);
  panelLayout->setContentsMargins(30, 40, 30, 40);
  panelLayout->setSpacing(25);

  // Camera selection title - much larger font for 6" display
  QLabel *cameraTitle = new QLabel("Camera Views");
  cameraTitle->setStyleSheet("font-size: 44px; font-weight: 600; color: white; margin-bottom: 20px;");
  panelLayout->addWidget(cameraTitle);

  // Camera buttons
  QButtonGroup *cameraGroup = new QButtonGroup(this);

  // Initialize button pointers to nullptr
  frontCamButton = nullptr;
  wideCamButton = nullptr;
  driverCamButton = nullptr;
  lqCamButton = nullptr;

  // Common button style for all available cameras
  QString buttonStyle = R"(
    QPushButton {
      background: #404040;
      color: white;
      font-size: 32px;
      border: 3px solid #555555;
      border-radius: 15px;
      text-align: center;
      font-weight: bold;
    }
    QPushButton:checked {
      background: #2196F3;
      border: 3px solid #1976D2;
      color: white;
    }
    QPushButton:hover {
      background: #505050;
      border: 3px solid #FFD700;
    }
    QPushButton:checked:hover {
      background: #1976D2;
      border: 3px solid #FFD700;
    }
  )";

  // Only create and add buttons for available cameras
  if (routeInfo.hasFrontHQVideo) {
    frontCamButton = new QPushButton("Front Camera");
    frontCamButton->setFixedHeight(90);
    frontCamButton->setCheckable(true);
    frontCamButton->setStyleSheet(buttonStyle);
    cameraGroup->addButton(frontCamButton);
    connect(frontCamButton, &QPushButton::clicked, [this]() { switchCamera("front"); });
    panelLayout->addWidget(frontCamButton);
  }

  if (routeInfo.hasWideVideo) {
    wideCamButton = new QPushButton("Wide Camera");
    wideCamButton->setFixedHeight(90);
    wideCamButton->setCheckable(true);
    wideCamButton->setStyleSheet(buttonStyle);
    cameraGroup->addButton(wideCamButton);
    connect(wideCamButton, &QPushButton::clicked, [this]() { switchCamera("wide"); });
    panelLayout->addWidget(wideCamButton);
  }

  if (routeInfo.hasDriverHQVideo) {
    driverCamButton = new QPushButton("Driver Camera");
    driverCamButton->setFixedHeight(90);
    driverCamButton->setCheckable(true);
    driverCamButton->setStyleSheet(buttonStyle);
    cameraGroup->addButton(driverCamButton);
    connect(driverCamButton, &QPushButton::clicked, [this]() { switchCamera("driver"); });
    panelLayout->addWidget(driverCamButton);
  }

  if (routeInfo.hasLQVideo) {
    lqCamButton = new QPushButton("LQ Camera");
    lqCamButton->setFixedHeight(90);
    lqCamButton->setCheckable(true);
    lqCamButton->setStyleSheet(buttonStyle);
    cameraGroup->addButton(lqCamButton);
    connect(lqCamButton, &QPushButton::clicked, [this]() { switchCamera("lq"); });
    panelLayout->addWidget(lqCamButton);
  }

  // Set default camera
  if (routeInfo.hasFrontHQVideo && frontCamButton) {
    frontCamButton->setChecked(true);
    currentCameraType = "front";
    std::cout << "[VIDEO DEBUG] Set default camera to front" << std::endl;
  } else if (routeInfo.hasWideVideo && wideCamButton) {
    wideCamButton->setChecked(true);
    currentCameraType = "wide";
    std::cout << "[VIDEO DEBUG] Set default camera to wide" << std::endl;
  } else if (routeInfo.hasDriverHQVideo && driverCamButton) {
    driverCamButton->setChecked(true);
    currentCameraType = "driver";
    std::cout << "[VIDEO DEBUG] Set default camera to driver" << std::endl;
  } else if (routeInfo.hasLQVideo && lqCamButton) {
    lqCamButton->setChecked(true);
    currentCameraType = "lq";
    std::cout << "[VIDEO DEBUG] Set default camera to lq" << std::endl;
  }

  std::cout << "[VIDEO DEBUG] Final camera selection: " << currentCameraType.toStdString() << std::endl;

  panelLayout->addStretch();

  // Actions section
  QLabel *actionsTitle = new QLabel("Actions");
  actionsTitle->setStyleSheet("font-size: 44px; font-weight: 600; color: white; margin-bottom: 20px; margin-top: 30px;");
  panelLayout->addWidget(actionsTitle);

  // Delete route button - much larger
  deleteButton = new QPushButton("🗑 Delete Route");
  deleteButton->setFixedHeight(90);
  deleteButton->setStyleSheet(R"(
    QPushButton {
      background: #d32f2f;
      color: white;
      font-size: 32px;
      font-weight: bold;
      border: 3px solid #b71c1c;
      border-radius: 15px;
      text-align: center;
    }
    QPushButton:pressed {
      background: #b71c1c;
    }
    QPushButton:hover {
      background: #c62828;
      border: 3px solid #FFD700;
    }
  )");
  connect(deleteButton, &QPushButton::clicked, this, &BPRouteVideoDialog::deleteRoute);
  panelLayout->addWidget(deleteButton);

  // Future action buttons can be added here
  panelLayout->addStretch();
}

void BPRouteVideoDialog::loadVideoSegments() {
  std::cout << "[VIDEO DEBUG] Loading video segments for camera: " << currentCameraType.toStdString() << std::endl;
  currentPlaylist.clear();

  // Build playlist for current camera
  BPRoutesPanel *parent = static_cast<BPRoutesPanel*>(this->parent());
  QString routeDir = parent->getRoutesDir() + "/" + routeBaseName;
  std::cout << "[VIDEO DEBUG] Route directory: " << routeDir.toStdString() << std::endl;

  // The routeBaseName is already the correct segment directory (e.g., "0000009b--d7712fe77a--0")
  // Just look for the video file directly in this directory
  QString videoPath = getVideoPath(currentCameraType, 0);  // segment parameter not used anyway
  std::cout << "[VIDEO DEBUG] Checking video path: " << videoPath.toStdString() << std::endl;
  if (QFile::exists(videoPath)) {
    currentPlaylist.append(videoPath);
    std::cout << "[VIDEO DEBUG] Added video file to playlist: " << videoPath.toStdString() << std::endl;
  } else {
    std::cout << "[VIDEO DEBUG] Video file not found: " << videoPath.toStdString() << std::endl;
  }

  std::cout << "[VIDEO DEBUG] Total playlist size: " << currentPlaylist.size() << " segments" << std::endl;

  if (!currentPlaylist.isEmpty()) {
    currentSegment = 0;
    // Calculate total duration (rough estimate: 60 seconds per segment)
    totalDuration = currentPlaylist.size() * 60 * 1000; // milliseconds
    std::cout << "[VIDEO DEBUG] Total estimated duration: " << totalDuration << " ms" << std::endl;
    positionSlider->setRange(0, totalDuration);
    playCurrentSegment();
  } else {
    std::cout << "[VIDEO DEBUG] WARNING: No video segments found for " << currentCameraType.toStdString() << std::endl;
    videoDisplay->setText(QString("No %1 Video Available").arg(currentCameraType.toUpper()));
    videoDisplay->setStyleSheet("background: #000000; border: none; color: #FF6B6B; font-size: 120px; font-weight: bold;");
  }
}

void BPRouteVideoDialog::loadThumbnail() {
  // Load and display thumbnail as initial frame
  BPRoutesPanel *parent = static_cast<BPRoutesPanel*>(this->parent());
  QString thumbnailPath = parent->getThumbnailPath(routeBaseName);

  if (QFile::exists(thumbnailPath)) {
    QPixmap pixmap(thumbnailPath);
    if (!pixmap.isNull()) {
      // Scale thumbnail to fit the video display while maintaining aspect ratio
      QPixmap scaledPixmap = pixmap.scaled(videoDisplay->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
      videoDisplay->setPixmap(scaledPixmap);
      videoDisplay->setText(""); // Clear any text
      return;
    }
  }

  // If no thumbnail exists, try to generate one
  if (!thumbnailPath.isEmpty()) {
    // Use the existing thumbnail generation system
    parent->initializeThumbnail(videoDisplay, routeBaseName);
  } else {
    // Fallback message with larger font
    std::cout << "[VIDEO DEBUG] No thumbnail available, showing fallback message" << std::endl;
    videoDisplay->setText("No Video Available");
    videoDisplay->setStyleSheet("background: #000000; border: none; color: #FFD700; font-size: 120px; font-weight: bold;");
  }
}

QString BPRouteVideoDialog::getVideoPath(const QString &cameraType, int segment) {
  BPRoutesPanel *parent = static_cast<BPRoutesPanel*>(this->parent());
  QString routeDir = parent->getRoutesDir() + "/" + routeBaseName;

  QString filename;

  // The actual video files are just named fcamera.hevc, dcamera.hevc, etc.
  // directly in the segment directory
  if (cameraType == "front") {
    filename = "fcamera.hevc";
  } else if (cameraType == "wide") {
    filename = "dcamera.hevc";  // Wide camera files are actually dcamera.hevc
  } else if (cameraType == "driver") {
    filename = "ecamera.hevc";  // Driver camera files are ecamera.hevc
  } else if (cameraType == "lq") {
    filename = "qcamera.ts";
  }

  QString videoPath = routeDir + "/" + filename;
  std::cout << "[VIDEO DEBUG] Generated video path: " << videoPath.toStdString() << std::endl;
  return videoPath;
}

void BPRouteVideoDialog::playCurrentSegment() {
  std::cout << "[VIDEO DEBUG] playCurrentSegment() called" << std::endl;

  if (currentSegment >= currentPlaylist.size()) {
    std::cout << "[VIDEO DEBUG] ERROR: currentSegment (" << currentSegment << ") >= playlist size (" << currentPlaylist.size() << ")" << std::endl;
    return;
  }

  QString videoPath = currentPlaylist[currentSegment];
  std::cout << "[VIDEO DEBUG] Playing segment " << currentSegment << ": " << videoPath.toStdString() << std::endl;

  if (!QFile::exists(videoPath)) {
    std::cout << "[VIDEO DEBUG] ERROR: Video file does not exist: " << videoPath.toStdString() << std::endl;
    videoDisplay->setText("Video file not found");
    return;
  }

  QFileInfo fileInfo(videoPath);
  std::cout << "[VIDEO DEBUG] File size: " << fileInfo.size() << " bytes" << std::endl;

  bool isH265 = videoPath.endsWith(".hevc");
  std::cout << "[VIDEO DEBUG] Video format: " << (isH265 ? "H.265/HEVC" : "H.264/TS") << std::endl;

#ifdef QCOM2
  std::cout << "[VIDEO DEBUG] Using V4L decoder (QCOM2 platform)" << std::endl;
  decoder = std::make_unique<V4LDecoder>(1920, 1080,
    [this](const DecodedFrame &frame) { onFrameDecoded(frame); }, isH265);
#else
  std::cout << "[VIDEO DEBUG] Using FFmpeg decoder (non-QCOM2 platform)" << std::endl;
  decoder = std::make_unique<FfmpegDecoder>(1920, 1080,
    [this](const DecodedFrame &frame) { onFrameDecoded(frame); }, isH265);
#endif

  try {
    decoder->decoder_open();

#ifdef QCOM2
    V4LDecoder* v4l_decoder = static_cast<V4LDecoder*>(decoder.get());
    if (!v4l_decoder->is_decoder_available()) {
      std::cout << "[VIDEO DEBUG] ERROR: V4L decoder device not available" << std::endl;
      videoDisplay->setText("Hardware decoder not available.\nPlease check device permissions.");

      // Fall back to FFmpeg decoder
      std::cout << "[VIDEO DEBUG] Falling back to FFmpeg software decoder" << std::endl;
      decoder = std::make_unique<FfmpegDecoder>(1920, 1080,
        [this](const DecodedFrame &frame) { onFrameDecoded(frame); }, isH265);
      decoder->decoder_open();
    } else if (!v4l_decoder->is_decoder_open()) {
      std::cout << "[VIDEO DEBUG] ERROR: V4L decoder failed to open" << std::endl;
      videoDisplay->setText("Hardware decoder failed to initialize");
      return;
    } else {
      std::cout << "[VIDEO DEBUG] V4L decoder opened successfully" << std::endl;
    }
#endif

  } catch (const std::exception& e) {
    std::cout << "[VIDEO DEBUG] ERROR: Decoder failed to open: " << e.what() << std::endl;
    videoDisplay->setText("Video decoder failed to initialize");
    return;
  }

  // Clear display
  videoDisplay->clear();
  videoDisplay->setStyleSheet("background: #000000; border: none;");

  // Start feeding video data in background thread
  QtConcurrent::run([this, videoPath, isH265]() {
    feedVideoToDecoder(videoPath, isH265);
  });
}

void BPRouteVideoDialog::feedVideoToDecoder(const QString &videoPath, bool isH265) {
  QFile videoFile(videoPath);
  if (!videoFile.open(QIODevice::ReadOnly)) {
    std::cout << "[VIDEO DEBUG] ERROR: Failed to open video file for reading" << std::endl;
    QMetaObject::invokeMethod(this, [this]() {
      videoDisplay->setText("Failed to open video file");
    }, Qt::QueuedConnection);
    return;
  }

  // Feed data in chunks appropriate for hardware decoder
  const int CHUNK_SIZE = isH265 ? (256 * 1024) : (188 * 100);  // 256KB for HEVC, 100 TS packets for H264
  QByteArray buffer;
  uint64_t timestamp_us = 0;
  int totalBytesRead = 0;
  int frameCount = 0;
  bool firstChunk = true;

  // For HEVC, we need to accumulate NAL units until we have a complete access unit
  QByteArray nalBuffer;
  const uint8_t NAL_START_CODE[] = {0x00, 0x00, 0x00, 0x01};

  // Check if decoder is still valid - V4L decoder stays open internally
  while (!videoFile.atEnd() && decoder) {
    buffer = videoFile.read(CHUNK_SIZE);
    if (buffer.isEmpty()) break;

    totalBytesRead += buffer.size();

    if (isH265) {
      // For HEVC, we need to handle NAL units properly
      nalBuffer.append(buffer);

      // Look for NAL unit boundaries
      int pos = 0;
      while (pos < nalBuffer.size() - 4) {
        // Find next NAL start code
        int nextNal = nalBuffer.indexOf(QByteArray((char*)NAL_START_CODE, 4), pos + 4);
        if (nextNal == -1) {
          // No more NAL units in buffer, keep remaining data for next iteration
          if (pos > 0) {
            nalBuffer = nalBuffer.mid(pos);
          }
          break;
        }

        // Extract NAL unit
        QByteArray nalUnit = nalBuffer.mid(pos, nextNal - pos);

        // Check NAL unit type (6th bit of 5th byte)
        if (nalUnit.size() > 5) {
          uint8_t nalType = (nalUnit[4] >> 1) & 0x3F;

          // VPS=32, SPS=33, PPS=34, IDR=19-20, CRA=21
          bool isKeyframe = firstChunk ||
                           (nalType >= 19 && nalType <= 21) ||
                           (nalType >= 32 && nalType <= 34);

          // Feed NAL unit to decoder
          int result = decoder->decode_frame(
            reinterpret_cast<uint8_t*>(nalUnit.data()),
            nalUnit.size(),
            timestamp_us,
            isKeyframe
          );

          if (result != 0) {
            std::cout << "[VIDEO DEBUG] Decode error at byte " << totalBytesRead
                      << ", NAL type " << (int)nalType << ": " << result << std::endl;
          }

          firstChunk = false;
          frameCount++;
          timestamp_us += 33333;  // ~30fps
        }

        pos = nextNal;
      }

    } else {
      // For TS files, feed chunks directly
      int result = decoder->decode_frame(
        reinterpret_cast<uint8_t*>(buffer.data()),
        buffer.size(),
        timestamp_us,
        firstChunk
      );

      if (result != 0) {
        std::cout << "[VIDEO DEBUG] TS decode error at byte " << totalBytesRead << ": " << result << std::endl;
      }

      firstChunk = false;
      timestamp_us += (buffer.size() / 188) * 1000;  // Approximate timing based on TS packets
    }

    // Small delay to avoid overwhelming the decoder
    if (frameCount % 10 == 0) {
      QThread::msleep(10);
    }

    // Update progress periodically
    if (totalBytesRead % (1024 * 1024) == 0) {
      std::cout << "[VIDEO DEBUG] Fed " << (totalBytesRead / (1024 * 1024)) << " MB to decoder" << std::endl;
    }
  }

  videoFile.close();
  std::cout << "[VIDEO DEBUG] Finished feeding video - " << totalBytesRead << " bytes, " << frameCount << " chunks" << std::endl;

  // Flush decoder
  if (decoder) {
    decoder->flush();
  }

  // Signal completion
  QMetaObject::invokeMethod(this, &BPRouteVideoDialog::onSegmentFinished, Qt::QueuedConnection);
}

void BPRouteVideoDialog::onFrameDecoded(const DecodedFrame &frame) {
  static int frameNumber = 0;
  frameNumber++;

  // Only process every Nth frame for display to reduce CPU usage
  if (frameNumber % 3 != 0) {
    return;  // Skip 2 out of 3 frames
  }

  std::cout << "[VIDEO DEBUG] Displaying frame " << frameNumber
            << " - Size: " << frame.width << "x" << frame.height << std::endl;

  // Scale for display
  int display_width = qMin(frame.width, 1280);
  int display_height = (display_width * frame.height) / frame.width;

  QImage image(display_width, display_height, QImage::Format_RGB888);

  // NV12 to RGB conversion
  // Frame data is already in NV12 format from V4L decoder
  for (int y = 0; y < display_height; y++) {
    for (int x = 0; x < display_width; x++) {
      int src_x = (x * frame.width) / display_width;
      int src_y = (y * frame.height) / display_height;

      // Y component
      int Y = frame.y[src_y * frame.stride_y + src_x];

      // UV components (interleaved in NV12)
      int uv_x = src_x & ~1;  // Round down to even
      int uv_y = src_y / 2;
      int uv_offset = uv_y * frame.stride_uv + uv_x;

      int U = frame.u[uv_offset] - 128;      // U at even positions
      int V = frame.u[uv_offset + 1] - 128;  // V at odd positions

      // YUV to RGB conversion
      int R = qBound(0, (int)(Y + 1.402 * V), 255);
      int G = qBound(0, (int)(Y - 0.344 * U - 0.714 * V), 255);
      int B = qBound(0, (int)(Y + 1.772 * U), 255);

      image.setPixel(x, y, qRgb(R, G, B));
    }
  }

  // Update display
  QPixmap pixmap = QPixmap::fromImage(image);
  videoDisplay->setPixmap(pixmap.scaled(videoDisplay->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

  // Update timestamp/progress
  currentPosition = frame.timestamp_us / 1000;  // Convert to ms
  positionSlider->setValue(currentPosition);
}

void BPRouteVideoDialog::updateCameraButtonStates() {
  std::cout << "[VIDEO DEBUG] Updating camera button states for: " << currentCameraType.toStdString() << std::endl;

  // Clear all button selections first
  if (frontCamButton) frontCamButton->setChecked(false);
  if (wideCamButton) wideCamButton->setChecked(false);
  if (driverCamButton) driverCamButton->setChecked(false);
  if (lqCamButton) lqCamButton->setChecked(false);

  // Set the correct button as checked based on current camera type
  if (currentCameraType == "front" && frontCamButton) {
    frontCamButton->setChecked(true);
    std::cout << "[VIDEO DEBUG] Set front camera button as selected" << std::endl;
  } else if (currentCameraType == "wide" && wideCamButton) {
    wideCamButton->setChecked(true);
    std::cout << "[VIDEO DEBUG] Set wide camera button as selected" << std::endl;
  } else if (currentCameraType == "driver" && driverCamButton) {
    driverCamButton->setChecked(true);
    std::cout << "[VIDEO DEBUG] Set driver camera button as selected" << std::endl;
  } else if (currentCameraType == "lq" && lqCamButton) {
    lqCamButton->setChecked(true);
    std::cout << "[VIDEO DEBUG] Set LQ camera button as selected" << std::endl;
  }
}

void BPRouteVideoDialog::togglePlayback() {
  std::cout << "[VIDEO DEBUG] togglePlayback() called - current state: " << (isPlaying ? "PLAYING" : "STOPPED") << std::endl;

  isPlaying = !isPlaying;

  if (isPlaying) {
    std::cout << "[VIDEO DEBUG] Starting playback" << std::endl;
    playPauseButton->setText("⏸");
    positionTimer->start();
    std::cout << "[VIDEO DEBUG] Position timer started" << std::endl;
  } else {
    std::cout << "[VIDEO DEBUG] Stopping playback" << std::endl;
    playPauseButton->setText("▶");
    positionTimer->stop();
    std::cout << "[VIDEO DEBUG] Position timer stopped" << std::endl;
    // Show thumbnail when paused
    std::cout << "[VIDEO DEBUG] Loading thumbnail for paused state" << std::endl;
    loadThumbnail();
  }
}

void BPRouteVideoDialog::switchCamera(const QString &cameraType) {
  if (currentCameraType != cameraType) {
    std::cout << "[VIDEO DEBUG] Switching camera from " << currentCameraType.toStdString()
              << " to " << cameraType.toStdString() << std::endl;

    // Store current playback state
    bool wasPlaying = isPlaying;
    qint64 savedPosition = currentPosition;
    int savedSegment = currentSegment;

    std::cout << "[VIDEO DEBUG] Saved state - Playing: " << wasPlaying
              << ", Position: " << savedPosition << ", Segment: " << savedSegment << std::endl;

    // Stop current playback
    if (isPlaying) {
      isPlaying = false;
      positionTimer->stop();
    }

    // Close current decoder
    if (decoder) {
      decoder->decoder_close();
      decoder.reset();
    }

    // Switch camera type
    currentCameraType = cameraType;

    // Update button states to reflect current selection
    updateCameraButtonStates();

    // Reload video segments for new camera
    loadVideoSegments();

    // Restore playback state
    currentPosition = savedPosition;
    currentSegment = savedSegment;
    positionSlider->setValue(currentPosition);

    std::cout << "[VIDEO DEBUG] Restored position: " << currentPosition
              << ", segment: " << currentSegment << std::endl;

    if (wasPlaying) {
      // Resume playback
      isPlaying = true;
      playPauseButton->setText("⏸");
      positionTimer->start();
      playCurrentSegment();
      std::cout << "[VIDEO DEBUG] Resumed playback" << std::endl;
    } else {
      // Show thumbnail for new camera
      loadThumbnail();
      std::cout << "[VIDEO DEBUG] Loaded thumbnail for stopped playback" << std::endl;
    }
  }
}

void BPRouteVideoDialog::seekForward() {
  currentPosition = qMin(currentPosition + 10000, totalDuration); // +10 seconds
  positionSlider->setValue(currentPosition);
}

void BPRouteVideoDialog::seekBackward() {
  currentPosition = qMax(currentPosition - 10000, (qint64)0); // -10 seconds
  positionSlider->setValue(currentPosition);
}

void BPRouteVideoDialog::toggleFullscreen() {
  isFullscreen = !isFullscreen;

  if (isFullscreen) {
    cameraPanel->hide();
    fullscreenButton->setText("⛶");
  } else {
    cameraPanel->show();
    fullscreenButton->setText("⛶");
  }
}

void BPRouteVideoDialog::deleteRoute() {
  // Show confirmation dialog
  BPConfirmationDialog::ConfirmConfig config;
  config.title = "Delete Route";
  config.prompt = QString("Are you sure you want to delete route %1?\n\nThis action cannot be undone.").arg(routeBaseName);
  config.confirmText = "Delete";
  config.cancelText = "Cancel";
  config.confirmColor = "#d32f2f";

  BPConfirmationDialog *confirmDialog = BPConfirmationDialog::showConfirmation(config, this);
  connect(confirmDialog, &BPConfirmationDialog::confirmed, [this](bool accepted) {
    if (accepted) {
      // Delete the route directory
      BPRoutesPanel *parent = static_cast<BPRoutesPanel*>(this->parent());
      QString routeDir = parent->getRoutesDir() + "/" + routeBaseName;

      // TODO: Implement safe route deletion
      // QDir(routeDir).removeRecursively();

      // Close dialog and refresh parent
      accept();
      parent->handleRefresh();
    }
  });
}

void BPRouteVideoDialog::toggleStar() {
  BPRoutesPanel *parent = static_cast<BPRoutesPanel*>(this->parent());
  bool currentlyStarred = parent->isRouteStarred(routeBaseName);

  // Use the main panel's star toggle method to ensure UI synchronization
  parent->handleRouteStarToggle(routeBaseName);

  // Update the modal's star button and route info
  starButton->setText(!currentlyStarred ? "★" : "☆");
  routeInfo.isStarred = !currentlyStarred;
}

void BPRouteVideoDialog::onSegmentFinished() {
  currentSegment++;
  if (currentSegment < currentPlaylist.size()) {
    playCurrentSegment();
  } else {
    // Playback finished
    isPlaying = false;
    playPauseButton->setText("▶");
    positionTimer->stop();
    // Show thumbnail when playback ends
    loadThumbnail();
  }
}

void BPRouteVideoDialog::updatePlaybackPosition() {
  if (isPlaying) {
    currentPosition += 100; // Add 100ms
    positionSlider->setValue(currentPosition);

    // Update time label
    int totalSecs = totalDuration / 1000;
    int currentSecs = currentPosition / 1000;

    QString format = totalSecs >= 3600 ? "h:mm:ss" : "m:ss";
    QString currentTime = QTime(0, 0).addSecs(currentSecs).toString(format);
    QString totalTime = QTime(0, 0).addSecs(totalSecs).toString(format);

    timeLabel->setText(QString("%1 / %2").arg(currentTime, totalTime));
  }
}

void BPRouteVideoDialog::keyPressEvent(QKeyEvent *event) {
  switch (event->key()) {
    case Qt::Key_Space:
      togglePlayback();
      break;
    case Qt::Key_Escape:
      reject();
      break;
    case Qt::Key_F:
      toggleFullscreen();
      break;
    case Qt::Key_Left:
      seekBackward();
      break;
    case Qt::Key_Right:
      seekForward();
      break;
    default:
      BPDialogBase::keyPressEvent(event);
  }
}

void BPRouteVideoDialog::showEvent(QShowEvent *event) {
  BPDialogBase::showEvent(event);
  // setupFullscreen() is now called before exec() in bp_routes_panel.cc for proper QCOM2 rotation
}
