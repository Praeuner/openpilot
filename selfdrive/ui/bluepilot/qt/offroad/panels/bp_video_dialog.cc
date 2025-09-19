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
  // Use vertical layout on QCOM2 devices for better portrait orientation support
  bool useVerticalLayout = CommaTools::isCommaDevice();

  QBoxLayout *mainLayout;
  if (useVerticalLayout) {
    mainLayout = new QVBoxLayout(this);
  } else {
    mainLayout = new QHBoxLayout(this);
  }
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Top/Left side - Video display (main content)
  setupVideoDisplay();
  mainLayout->addWidget(videoContainer, 7);

  // Bottom/Right side - Camera panel (controls)
  setupCameraPanel();
  mainLayout->addWidget(cameraPanel, 3);
}

void BPRouteVideoDialog::setupVideoDisplay() {
  videoContainer = new QWidget;
  videoContainer->setStyleSheet("background: #000000;");

  QVBoxLayout *videoLayout = new QVBoxLayout(videoContainer);
  videoLayout->setContentsMargins(0, 0, 0, 0);
  videoLayout->setSpacing(0);

  // Header
  QWidget *headerWidget = new QWidget;
  headerWidget->setFixedHeight(80);
  headerWidget->setStyleSheet("background: #202020;");

  QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(20, 10, 20, 10);

  // Route title
  routeTitle = new QLabel(QString("Route: %1").arg(routeBaseName));
  routeTitle->setStyleSheet("font-size: 24px; font-weight: 600; color: white;");

  // Star button
  starButton = new QPushButton;
  starButton->setFixedSize(50, 50);
  starButton->setText(routeInfo.isStarred ? "★" : "☆");
  starButton->setStyleSheet(R"(
    QPushButton {
      background: transparent;
      border: none;
      font-size: 28px;
      color: #FFD700;
    }
    QPushButton:hover {
      background: rgba(255, 255, 255, 0.1);
      border-radius: 25px;
    }
  )");
  connect(starButton, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleStar);

  // Fullscreen button
  fullscreenButton = new QPushButton("⛶");
  fullscreenButton->setFixedSize(50, 50);
  fullscreenButton->setStyleSheet(starButton->styleSheet());
  connect(fullscreenButton, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleFullscreen);

  // Close button
  closeButton = new QPushButton("✕");
  closeButton->setFixedSize(50, 50);
  closeButton->setStyleSheet(starButton->styleSheet());
  connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

  headerLayout->addWidget(routeTitle, 1);
  headerLayout->addWidget(starButton);
  headerLayout->addWidget(fullscreenButton);
  headerLayout->addWidget(closeButton);

  videoLayout->addWidget(headerWidget);

  // Video display area
  videoDisplay = new QLabel;
  videoDisplay->setStyleSheet("background: #000000; border: none;");
  videoDisplay->setAlignment(Qt::AlignCenter);
  videoDisplay->setText("Loading video...");
  videoDisplay->setScaledContents(true);
  videoDisplay->setMinimumSize(800, 450);

  videoLayout->addWidget(videoDisplay, 1);

  // Controls
  setupControls();
  videoLayout->addWidget(controlsWidget);
}

void BPRouteVideoDialog::setupControls() {
  controlsWidget = new QWidget;
  controlsWidget->setFixedHeight(80);
  controlsWidget->setStyleSheet("background: #333333;");

  QHBoxLayout *controlsLayout = new QHBoxLayout(controlsWidget);
  controlsLayout->setContentsMargins(20, 10, 20, 10);
  controlsLayout->setSpacing(15);

  // Play/Pause button
  playPauseButton = new QPushButton("▶");
  playPauseButton->setFixedSize(60, 60);
  playPauseButton->setStyleSheet(R"(
    QPushButton {
      background: #2196F3;
      color: white;
      font-size: 24px;
      border: none;
      border-radius: 30px;
    }
    QPushButton:pressed {
      background: #1976D2;
    }
  )");
  connect(playPauseButton, &QPushButton::clicked, this, &BPRouteVideoDialog::togglePlayback);

  // Position slider
  positionSlider = new QSlider(Qt::Horizontal);
  positionSlider->setStyleSheet(R"(
    QSlider::groove:horizontal {
      border: 1px solid #999999;
      height: 8px;
      background: #555555;
      border-radius: 4px;
    }
    QSlider::handle:horizontal {
      background: #2196F3;
      border: 1px solid #1976D2;
      width: 18px;
      height: 18px;
      border-radius: 9px;
      margin: -5px 0;
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

  // Time label
  timeLabel = new QLabel("00:00 / 00:00");
  timeLabel->setStyleSheet("color: white; font-size: 18px; font-family: monospace;");
  timeLabel->setMinimumWidth(120);

  controlsLayout->addWidget(playPauseButton);
  controlsLayout->addWidget(positionSlider, 1);
  controlsLayout->addWidget(timeLabel);
}

void BPRouteVideoDialog::setupCameraPanel() {
  cameraPanel = new QWidget;
  bool isQCOM2 = CommaTools::isCommaDevice();
  if (isQCOM2) {
    cameraPanel->setFixedHeight(200); // Fixed height for vertical layout
    cameraPanel->setStyleSheet("background: #2a2a2a; border-top: 1px solid #444444;");
  } else {
    cameraPanel->setFixedWidth(250);
    cameraPanel->setStyleSheet("background: #2a2a2a; border-left: 1px solid #444444;");
  }

  QVBoxLayout *panelLayout = new QVBoxLayout(cameraPanel);
  panelLayout->setContentsMargins(15, 20, 15, 20);
  panelLayout->setSpacing(15);

  // Camera selection title
  QLabel *cameraTitle = new QLabel("Camera Views");
  cameraTitle->setStyleSheet("font-size: 20px; font-weight: 600; color: white; margin-bottom: 10px;");
  panelLayout->addWidget(cameraTitle);

  // Camera buttons
  QButtonGroup *cameraGroup = new QButtonGroup(this);

  frontCamButton = new QPushButton("Front Camera");
  wideCamButton = new QPushButton("Wide Camera");
  driverCamButton = new QPushButton("Driver Camera");
  lqCamButton = new QPushButton("LQ Camera");

  QList<QPushButton*> camButtons = {frontCamButton, wideCamButton, driverCamButton, lqCamButton};
  QStringList camTypes = {"front", "wide", "driver", "lq"};

  for (int i = 0; i < camButtons.size(); i++) {
    QPushButton *btn = camButtons[i];
    QString camType = camTypes[i];

    btn->setFixedHeight(50);
    btn->setCheckable(true);
    cameraGroup->addButton(btn);

    // Check if camera is available
    bool available = false;
    if (camType == "front") available = routeInfo.hasFrontVideo;
    else if (camType == "wide") available = routeInfo.hasWideVideo;
    else if (camType == "driver") available = routeInfo.hasDriverVideo;
    else if (camType == "lq") available = routeInfo.hasLQVideo;

    if (available) {
      btn->setStyleSheet(R"(
        QPushButton {
          background: #404040;
          color: white;
          font-size: 16px;
          border: 1px solid #555555;
          border-radius: 8px;
          text-align: left;
          padding-left: 15px;
        }
        QPushButton:checked {
          background: #2196F3;
          border: 1px solid #1976D2;
        }
        QPushButton:hover {
          background: #505050;
        }
        QPushButton:checked:hover {
          background: #1976D2;
        }
      )");

      connect(btn, &QPushButton::clicked, [this, camType]() {
        switchCamera(camType);
      });
    } else {
      btn->setEnabled(false);
      btn->setStyleSheet(R"(
        QPushButton {
          background: #2a2a2a;
          color: #666666;
          font-size: 16px;
          border: 1px solid #333333;
          border-radius: 8px;
          text-align: left;
          padding-left: 15px;
        }
      )");
    }

    panelLayout->addWidget(btn);
  }

  // Set default camera
  if (routeInfo.hasFrontVideo) {
    frontCamButton->setChecked(true);
    currentCameraType = "front";
  } else if (routeInfo.hasWideVideo) {
    wideCamButton->setChecked(true);
    currentCameraType = "wide";
  } else if (routeInfo.hasDriverVideo) {
    driverCamButton->setChecked(true);
    currentCameraType = "driver";
  } else if (routeInfo.hasLQVideo) {
    lqCamButton->setChecked(true);
    currentCameraType = "lq";
  }

  panelLayout->addStretch();

  // Delete route button
  deleteButton = new QPushButton("Delete Route");
  deleteButton->setFixedHeight(50);
  deleteButton->setStyleSheet(R"(
    QPushButton {
      background: #d32f2f;
      color: white;
      font-size: 16px;
      font-weight: 500;
      border: none;
      border-radius: 8px;
    }
    QPushButton:pressed {
      background: #b71c1c;
    }
  )");
  connect(deleteButton, &QPushButton::clicked, this, &BPRouteVideoDialog::deleteRoute);

  panelLayout->addWidget(deleteButton);
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
  // For now, show placeholder
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
  config.prompt = QString("Are you sure you want to delete route %1?\\n\\nThis action cannot be undone.").arg(routeBaseName);
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
  parent->setRouteStarred(routeBaseName, !currentlyStarred);

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