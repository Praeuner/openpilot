// bp_video_dialog.cc - Route Video Playback Dialog Implementation
#include "bp_routes_panel.h"
#include "bp_utils.h"
#include "bp_video_types.h"
#include "bp_frame_reader.h"
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
  frameReader.reset();
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

  std::cout << "[VIDEO DEBUG] Loading video with FrameReader: " << videoPath.toStdString() << std::endl;

  // Create FrameReader and load video
  frameReader = std::make_unique<FrameReader>();

  // Determine camera type based on current selection
  CameraType cameraType = RoadCam;  // Default to front camera
  if (currentCameraType == "driver") {
    cameraType = DriverCam;
  } else if (currentCameraType == "wide") {
    cameraType = WideRoadCam;
  }

  // Load video with hardware decoding enabled
  std::atomic<bool> abort{false};
  if (!frameReader->load(cameraType, videoPath.toStdString(), false, &abort)) {
    std::cout << "[VIDEO DEBUG] ERROR: Failed to load video with FrameReader" << std::endl;
    videoDisplay->setText("Failed to load video file");
    return;
  }

  std::cout << "[VIDEO DEBUG] Video loaded successfully - " << frameReader->getFrameCount()
            << " frames, " << frameReader->width << "x" << frameReader->height << std::endl;

  // Clear display
  videoDisplay->clear();
  videoDisplay->setStyleSheet("background: #000000; border: none;");

  // Start video playback in background thread
  QtConcurrent::run([this]() {
    playbackVideoFrames();
  });
}

void BPRouteVideoDialog::playbackVideoFrames() {
  if (!frameReader) {
    std::cout << "[VIDEO DEBUG] ERROR: No frameReader available" << std::endl;
    return;
  }

  size_t totalFrames = frameReader->getFrameCount();
  std::cout << "[VIDEO DEBUG] Starting playback of " << totalFrames << " frames" << std::endl;

  // Allocate buffer for decoded frames
  VisionBuf frameBuf;
  frameBuf.allocate(frameReader->width * frameReader->height * 3 / 2); // NV12 format
  frameBuf.init_yuv(frameReader->width, frameReader->height, frameReader->width, frameReader->width * frameReader->height);

  // Play through all frames
  for (size_t frameIdx = 0; frameIdx < totalFrames && frameReader; frameIdx++) {
    if (!isPlaying) {
      QThread::msleep(100);
      continue;  // Pause if not playing
    }

    // Decode frame
    if (frameReader->get(frameIdx, &frameBuf)) {
      std::cout << "[VIDEO DEBUG] Successfully decoded frame " << frameIdx << "/" << totalFrames << std::endl;

      // Convert VisionBuf to DecodedFrame and display
      DecodedFrame frame;
      frame.y = frameBuf.y;
      frame.u = frame.v = frameBuf.uv;  // NV12 format
      frame.width = frameReader->width;
      frame.height = frameReader->height;
      frame.stride_y = frameBuf.stride;
      frame.stride_uv = frameBuf.stride;
      frame.timestamp_us = frameIdx * 33333;  // 30fps
      frame.frame_id = frameIdx;
      frame.keyframe = false;

      // Display frame on main thread
      QMetaObject::invokeMethod(this, [this, frame]() {
        onFrameDecoded(frame);
      }, Qt::QueuedConnection);

    } else {
      std::cout << "[VIDEO DEBUG] Failed to decode frame " << frameIdx << std::endl;
    }

    // Control playback speed (30fps = ~33ms per frame)
    QThread::msleep(33);
  }

  frameBuf.free();
  std::cout << "[VIDEO DEBUG] Finished playback" << std::endl;

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

    // Close current frameReader
    frameReader.reset();

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
