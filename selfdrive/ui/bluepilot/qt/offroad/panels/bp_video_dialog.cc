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
#include <iostream>

BPRouteVideoDialog::BPRouteVideoDialog(const QString &routeBase, QWidget *parent)
    : BPDialogBase(parent), routeBaseName(routeBase) {

  setWindowTitle("Route Video Playback");

  // Load route info
  QString routePath = static_cast<BPRoutesPanel*>(parent)->getRoutesDir() + "/" + routeBase;
  routeInfo = static_cast<BPRoutesPanel*>(parent)->getRouteInfo(routePath);

  // Initialize timers
  playbackTimer = new QTimer(this);
  positionTimer = new QTimer(this);
  positionTimer->setInterval(100); // Update position every 100ms

  connect(positionTimer, &QTimer::timeout, this, &BPRouteVideoDialog::updatePlaybackPosition);


  setupUI();
  loadVideoSegments();
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

  // Route title - much larger
  routeTitle = new QLabel(QString("Route: %1").arg(routeBaseName));
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
  videoDisplay->setStyleSheet("background: #000000; border: none; color: white; font-size: 48px;");
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
  if (routeInfo.hasFrontVideo) {
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

  if (routeInfo.hasDriverVideo) {
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
  if (routeInfo.hasFrontVideo && frontCamButton) {
    frontCamButton->setChecked(true);
    currentCameraType = "front";
  } else if (routeInfo.hasWideVideo && wideCamButton) {
    wideCamButton->setChecked(true);
    currentCameraType = "wide";
  } else if (routeInfo.hasDriverVideo && driverCamButton) {
    driverCamButton->setChecked(true);
    currentCameraType = "driver";
  } else if (routeInfo.hasLQVideo && lqCamButton) {
    lqCamButton->setChecked(true);
    currentCameraType = "lq";
  }

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
  currentPlaylist.clear();

  // Build playlist for current camera
  BPRoutesPanel *parent = static_cast<BPRoutesPanel*>(this->parent());
  QString routeDir = parent->getRoutesDir() + "/" + routeBaseName;

  for (int segment = 0; segment < routeInfo.segments; segment++) {
    QString videoPath = getVideoPath(currentCameraType, segment);
    if (QFile::exists(videoPath)) {
      currentPlaylist.append(videoPath);
    }
  }

  if (!currentPlaylist.isEmpty()) {
    currentSegment = 0;
    // Calculate total duration (rough estimate: 60 seconds per segment)
    totalDuration = currentPlaylist.size() * 60 * 1000; // milliseconds
    positionSlider->setRange(0, totalDuration);
    playCurrentSegment();
  }
}

QString BPRouteVideoDialog::getVideoPath(const QString &cameraType, int segment) {
  BPRoutesPanel *parent = static_cast<BPRoutesPanel*>(this->parent());
  QString routeDir = parent->getRoutesDir() + "/" + routeBaseName;

  QString segmentStr = QString::number(segment);
  QString filename;

  if (cameraType == "front") {
    filename = QString("%1--%2--fcamera.hevc").arg(routeBaseName, segmentStr);
  } else if (cameraType == "wide") {
    filename = QString("%1--%2--dcamera.hevc").arg(routeBaseName, segmentStr);
  } else if (cameraType == "driver") {
    filename = QString("%1--%2--ecamera.hevc").arg(routeBaseName, segmentStr);
  } else if (cameraType == "lq") {
    filename = QString("%1--%2--qcamera.ts").arg(routeBaseName, segmentStr);
  }

  return routeDir + "/" + filename;
}

void BPRouteVideoDialog::playCurrentSegment() {
  if (currentSegment >= currentPlaylist.size()) {
    return;
  }

  QString videoPath = currentPlaylist[currentSegment];

  // Initialize hardware decoder
  bool isH265 = videoPath.endsWith(".hevc");

#ifdef QCOM2
  // Use V4L decoder on QCOM2 devices
  decoder = std::make_unique<V4LDecoder>(1920, 1080,
    [this](const DecodedFrame &frame) { onFrameDecoded(frame); }, isH265);
#else
  // Use FFmpeg decoder on other platforms
  decoder = std::make_unique<FfmpegDecoder>(1920, 1080,
    [this](const DecodedFrame &frame) { onFrameDecoded(frame); }, isH265);
#endif

  decoder->decoder_open();

  // TODO: Load and feed video data to decoder
  // This would require reading the HEVC/TS file and feeding it frame by frame
  // For now, show placeholder with larger text
  videoDisplay->setText(QString("Playing: %1\\nSegment %2/%3")
    .arg(currentCameraType.toUpper())
    .arg(currentSegment + 1)
    .arg(currentPlaylist.size()));
}

void BPRouteVideoDialog::onFrameDecoded(const DecodedFrame &frame) {
  // Convert YUV420 to RGB and display
  QImage image(frame.width, frame.height, QImage::Format_RGB888);

  // Simple YUV420 to RGB conversion (this is a simplified version)
  for (int y = 0; y < frame.height; y++) {
    for (int x = 0; x < frame.width; x++) {
      int Y = frame.y[y * frame.stride_y + x];
      int U = frame.u[(y/2) * frame.stride_uv + (x/2)] - 128;
      int V = frame.v[(y/2) * frame.stride_uv + (x/2)] - 128;

      int R = qBound(0, (int)(Y + (1.370705 * V)), 255);
      int G = qBound(0, (int)(Y - (0.698001 * V) - (0.337633 * U)), 255);
      int B = qBound(0, (int)(Y + (1.732446 * U)), 255);

      image.setPixel(x, y, qRgb(R, G, B));
    }
  }

  // Display frame
  QPixmap pixmap = QPixmap::fromImage(image);
  videoDisplay->setPixmap(pixmap);
}

void BPRouteVideoDialog::togglePlayback() {
  isPlaying = !isPlaying;

  if (isPlaying) {
    playPauseButton->setText("⏸");
    positionTimer->start();
  } else {
    playPauseButton->setText("▶");
    positionTimer->stop();
  }
}

void BPRouteVideoDialog::switchCamera(const QString &cameraType) {
  if (currentCameraType != cameraType) {
    currentCameraType = cameraType;
    loadVideoSegments();
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