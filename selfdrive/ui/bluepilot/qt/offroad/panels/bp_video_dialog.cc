// bp_video_dialog.cc - Route Video Playback Dialog Implementation
#include "bp_routes_panel.h"
#include "bp_utils.h"
#include "bp_video_types.h"
#include "bp_frame_reader.h"
#include "bp_video_widget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QKeyEvent>
#include <QDir>
#include <QFileInfo>
#include <QButtonGroup>
#include <QThread>
#include <QtConcurrent>
#include <chrono>
#include "third_party/libyuv/include/libyuv.h"
#include "selfdrive/ui/sunnypilot/ui.h"

BPRouteVideoDialog::BPRouteVideoDialog(const QString &routeBase, QWidget *parent)
    : BPDialogBase(parent), routeBaseName(routeBase) {

  qDebug() << "[VIDEO DEBUG] === BPRouteVideoDialog Constructor ===";
  qDebug() << "[VIDEO DEBUG] Route: " << routeBase;

  setWindowTitle("Route Video Playback");

  // Load route info
  QString routePath = static_cast<BPRoutesPanel*>(parent)->getRoutesDir() + "/" + routeBase;
  qDebug() << "[VIDEO DEBUG] Route path: " << routePath;
  routeInfo = static_cast<BPRoutesPanel*>(parent)->getRouteInfo(routePath);

  qDebug() << "[VIDEO DEBUG] Route info - Segments: " << routeInfo.segments
            << ", HasFrontHQ: " << routeInfo.hasFrontHQVideo
            << ", HasFrontLQ: " << routeInfo.hasFrontLQVideo
            << ", HasWide: " << routeInfo.hasWideVideo
            << ", HasDriverHQ: " << routeInfo.hasDriverHQVideo
            << ", HasLQ: " << routeInfo.hasLQVideo;

  // Initialize timers
  playbackTimer = new QTimer(this);
  positionTimer = new QTimer(this);
  positionTimer->setInterval(100); // Update position every 100ms

  // Keep display awake timer
  keepAwakeTimer = new QTimer(this);
  keepAwakeTimer->setInterval(3000); // Reset awake every 3 seconds (more aggressive)
  keepAwakeTimer->setSingleShot(false);

  connect(positionTimer, &QTimer::timeout, this, &BPRouteVideoDialog::updatePlaybackPosition);
  connect(keepAwakeTimer, &QTimer::timeout, this, &BPRouteVideoDialog::keepDisplayAwake);
  connect(playbackTimer, &QTimer::timeout, this, &BPRouteVideoDialog::updateVideoFrame);

  setupUI();

  // Load video segments in background thread to avoid blocking UI
  QtConcurrent::run([this]() {
    loadVideoSegments();
    // Load thumbnail and start playback after segments are loaded
    QMetaObject::invokeMethod(this, [this]() {
      loadThumbnail();
      // Auto-start playback after thumbnail is loaded, with safety checks
      if (frameReader && totalFrames > 0 && frameReader->width > 0 && frameReader->height > 0) {
        isPlaying = true;
        if (playPauseButton) {
          playPauseButton->setText("II");  // Pause bars
        }
        if (positionTimer) {
          positionTimer->start();
        }
        // Start playback timer on UI thread with validation
        if (playbackTimer) {
          playbackTimer->start(50); // 20fps for smoother playback
          qDebug() << "[VIDEO DEBUG] Playback timer started on UI thread at 20fps";
        } else {
          qDebug() << "[VIDEO DEBUG] Cannot start playback timer - timer not available";
        }
      } else {
        qDebug() << "[VIDEO DEBUG] Cannot start playback - invalid video data";
        // Just show thumbnail without starting playback
        isPlaying = false;
        if (playPauseButton) {
          playPauseButton->setText("▶");
        }
      }
    }, Qt::QueuedConnection);
  });
}

BPRouteVideoDialog::~BPRouteVideoDialog() {
  // Stop video playback first
  isPlaying = false;

  // Stop all timers
  if (playbackTimer) playbackTimer->stop();
  if (positionTimer) positionTimer->stop();
  if (keepAwakeTimer) keepAwakeTimer->stop();

  // Stop and wait for playback thread to finish (if any)
  if (playbackFuture.isRunning()) {
    playbackFuture.cancel();
    playbackFuture.waitForFinished(); // Wait for thread to finish
  }

  // Clean up video resources
  frameReader.reset();

  // Clear playlists
  currentPlaylist.clear();

  qDebug() << "[VIDEO DEBUG] BPRouteVideoDialog destroyed and cleaned up";
}

void BPRouteVideoDialog::setupUI() {
  // Main container for the modal (80/20 split)
  QHBoxLayout *mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Left side - Video display (85%)
  setupVideoDisplay();
  mainLayout->addWidget(videoContainer, 85);

  // Right side - Camera panel (15%) - made narrower
  setupCameraPanel();
  mainLayout->addWidget(cameraPanel, 15);
}

void BPRouteVideoDialog::setupVideoDisplay() {
  videoContainer = new QWidget;
  videoContainer->setStyleSheet("background: #000000;");

  QVBoxLayout *videoLayout = new QVBoxLayout(videoContainer);
  videoLayout->setContentsMargins(0, 0, 0, 0);
  videoLayout->setSpacing(0);

  // Header - redesigned for better information display
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

  // Title and subtitle section - redesigned layout
  QVBoxLayout *titleLayout = new QVBoxLayout;
  titleLayout->setSpacing(5);

  // Main title - show the date and time
  QString displayDate = routeInfo.displayDate;
  if (displayDate.isEmpty()) {
    displayDate = routeInfo.dateTime.toString("MMMM d, yyyy");
  }
  // Fix for empty or invalid date
  if (displayDate.contains("0th") || displayDate == "0, 0" || !routeInfo.dateTime.isValid()) {
    displayDate = QDateTime::currentDateTime().toString("MMMM d, yyyy");
  }
  
  // Add time to the date
  QString displayTime = routeInfo.timestamp;
  if (displayTime.isEmpty()) {
    displayTime = routeInfo.dateTime.toString("h:mmap");
  }
  QString fullTitle = QString("%1 • %2").arg(displayDate, displayTime);
  
  routeTitle = new QLabel(fullTitle);
  routeTitle->setStyleSheet("font-size: 48px; font-weight: 600; color: white;");

  // Subtitle - show route ID
  QString subtitleText = QString("%1 • %2").arg(displayTime, routeBaseName);
  QLabel *subtitleLabel = new QLabel(subtitleText);
  subtitleLabel->setStyleSheet("font-size: 32px; color: #cccccc;");

  titleLayout->addWidget(routeTitle);
  titleLayout->addWidget(subtitleLabel);

  // Right side - route size and segment info
  QVBoxLayout *rightInfoLayout = new QVBoxLayout;
  rightInfoLayout->setSpacing(5);

  // Route size
  QLabel *sizeLabel = new QLabel(routeInfo.size);
  sizeLabel->setStyleSheet("font-size: 32px; color: #2196F3; font-weight: 500;");
  sizeLabel->setAlignment(Qt::AlignRight);

  // Segment indicator
  segmentLabel = new QLabel("Loading segments...");
  segmentLabel->setStyleSheet("font-size: 28px; color: #888;");
  segmentLabel->setAlignment(Qt::AlignRight);

  rightInfoLayout->addWidget(sizeLabel);
  rightInfoLayout->addWidget(segmentLabel);

  // Fullscreen button - much larger
  fullscreenButton = new QPushButton("⛶");
  fullscreenButton->setFixedSize(120, 120);
  fullscreenButton->setStyleSheet(R"(
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
  connect(fullscreenButton, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleFullscreen);

  headerLayout->addWidget(closeButton);
  headerLayout->addLayout(titleLayout, 1);
  headerLayout->addLayout(rightInfoLayout);
  headerLayout->addWidget(fullscreenButton);

  videoLayout->addWidget(headerWidget);

  // Video display area - much larger for iOS style (taller)
  videoDisplay = new BPVideoWidget;
  videoDisplay->setMinimumSize(1400, 900); // Increased height for better aspect ratio
  videoDisplay->setBackgroundColor(QColor("#000000"));
  videoDisplay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  // Connect touch to play/pause
  connect(videoDisplay, &BPVideoWidget::clicked, this, &BPRouteVideoDialog::togglePlayback);

  videoLayout->addWidget(videoDisplay, 1);

  // Setup overlay controls (iOS style) - no bottom controls needed
  setupOverlayControls();
}

QString BPRouteVideoDialog::buttonStyle(const QString &size) {
  return QString(R"(
    QPushButton {
      background: rgba(33, 150, 243, 180);
      color: white;
      font-size: %1;
      border: 3px solid #1976D2;
      border-radius: %2;
      font-weight: bold;
    }
    QPushButton:pressed {
      background: rgba(25, 118, 210, 220);
    }
    QPushButton:hover {
      background: rgba(30, 136, 229, 200);
      border: 3px solid #FFD700;
    }
  )").arg(QString::number(size.left(size.indexOf("px")).toInt() * 0.6) + "px")
     .arg(QString::number(size.left(size.indexOf("px")).toInt() / 2) + "px");
}

void BPRouteVideoDialog::setupOverlayControls() {
  // Create centered control buttons overlay - scaled for 6" 2160x1080 display
  controlsWidget = new QWidget(videoContainer);
  controlsWidget->setObjectName("overlayControls");
  controlsWidget->setStyleSheet("background: transparent;");
  
  QHBoxLayout *controlsLayout = new QHBoxLayout(controlsWidget);
  controlsLayout->setContentsMargins(0, 0, 0, 0);
  controlsLayout->setSpacing(80);  // More spacing for 6" display
  controlsLayout->setAlignment(Qt::AlignCenter);
  
  // Rewind 10s button with -10s text
  QPushButton *rewindButton = new QPushButton("-10s");
  rewindButton->setFixedSize(150, 150);  // Larger for 6" touch
  rewindButton->setStyleSheet(R"(
    QPushButton {
      background: rgba(0, 0, 0, 180);
      color: white;
      font-size: 42px;
      font-weight: 600;
      border: none;
      border-radius: 75px;
    }
    QPushButton:pressed {
      background: rgba(0, 0, 0, 240);
    }
  )");
  connect(rewindButton, &QPushButton::clicked, [this]() {
    currentPosition = qMax(0LL, currentPosition - 10000);
    seekToPosition(currentPosition);
    positionSlider->setValue(currentPosition);
  });

  // Play/Pause button - LARGER central button  
  playPauseButton = new QPushButton("II");
  playPauseButton->setFixedSize(200, 200);  // Much larger for main action
  playPauseButton->setStyleSheet(R"(
    QPushButton {
      background: rgba(0, 0, 0, 180);
      color: white;
      font-size: 60px;
      font-weight: 300;
      letter-spacing: 15px;
      padding-left: 15px;
      border: none;
      border-radius: 100px;
    }
    QPushButton:pressed {
      background: rgba(0, 0, 0, 240);
    }
  )");
  connect(playPauseButton, &QPushButton::clicked, this, &BPRouteVideoDialog::togglePlayback);

  // Forward 10s button with +10s text
  QPushButton *forwardButton = new QPushButton("+10s");
  forwardButton->setFixedSize(150, 150);  // Larger for 6" touch
  forwardButton->setStyleSheet(rewindButton->styleSheet());
  connect(forwardButton, &QPushButton::clicked, [this]() {
    currentPosition = qMin(totalDuration, currentPosition + 10000);
    seekToPosition(currentPosition);
    positionSlider->setValue(currentPosition);
  });

  controlsLayout->addWidget(rewindButton);
  controlsLayout->addWidget(playPauseButton);
  controlsLayout->addWidget(forwardButton);
  
  // Position controls in center
  controlsWidget->setFixedSize(700, 200);
  updateOverlayPosition();
  controlsWidget->show();
  
  // Add fullscreen button overlay - top right corner
  QPushButton *fullscreenOverlay = new QPushButton("⤢");
  fullscreenOverlay->setParent(videoContainer);
  fullscreenOverlay->setFixedSize(100, 100);
  fullscreenOverlay->move(videoContainer->width() - 120, 20);
  fullscreenOverlay->setStyleSheet(R"(
    QPushButton {
      background: rgba(0, 0, 0, 150);
      color: white;
      font-size: 48px;
      border: none;
      border-radius: 50px;
    }
    QPushButton:pressed {
      background: rgba(0, 0, 0, 220);
    }
  )");
  connect(fullscreenOverlay, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleFullscreen);
  fullscreenOverlay->show();
  
  // Create bottom slider bar - FULL WIDTH at BOTTOM with time on RIGHT
  QWidget *sliderContainer = new QWidget(videoContainer);
  sliderContainer->setObjectName("sliderContainer");
  sliderContainer->setStyleSheet("background: transparent;");
  
  QHBoxLayout *sliderLayout = new QHBoxLayout(sliderContainer);
  sliderLayout->setContentsMargins(20, 10, 20, 20);
  sliderLayout->setSpacing(20);
  
  // Position slider - TOUCH FRIENDLY
  positionSlider = new QSlider(Qt::Horizontal);
  positionSlider->setFixedHeight(60);  // Large touch target
  positionSlider->setStyleSheet(R"(
    QSlider::groove:horizontal {
      background: rgba(255, 255, 255, 40);
      height: 8px;
      border-radius: 4px;
    }
    QSlider::sub-page:horizontal {
      background: white;
      height: 8px;
      border-radius: 4px;
    }
    QSlider::handle:horizontal {
      background: white;
      width: 36px;
      height: 36px;
      border-radius: 18px;
      margin: -14px 0;
    }
    QSlider::handle:horizontal:pressed {
      background: #2196F3;
      width: 42px;
      height: 42px;
      border-radius: 21px;
      margin: -17px 0;
    }
  )");
  
  // Time label on RIGHT of slider
  timeLabel = new QLabel("0:00 / 0:00");
  timeLabel->setFixedWidth(180);
  timeLabel->setStyleSheet(R"(
    color: white; 
    font-size: 28px;
    font-weight: 500;
    background: rgba(0, 0, 0, 150); 
    padding: 10px 15px; 
    border-radius: 8px;
  )");
  timeLabel->setAlignment(Qt::AlignCenter);
  
  sliderLayout->addWidget(positionSlider, 1);  // Slider takes most space
  sliderLayout->addWidget(timeLabel, 0);       // Time label fixed width
  
  // Position at very bottom
  sliderContainer->resize(videoContainer->width(), 100);
  sliderContainer->move(0, videoContainer->height() - 100);
  sliderContainer->show();
  sliderContainer->raise();
  
  // Slider connections
  connect(positionSlider, &QSlider::sliderPressed, [this]() {
    if (isPlaying && playbackTimer) {
      playbackTimer->stop();
    }
    positionTimer->stop();
    isSeeking = true;
  });
  
  connect(positionSlider, &QSlider::sliderReleased, [this]() {
    currentPosition = positionSlider->value();
    isSeeking = false;
    seekToPosition(currentPosition);
    if (isPlaying) {
      positionTimer->start();
      if (playbackTimer) {
        playbackTimer->start(50);
      }
    }
  });
  
  connect(positionSlider, &QSlider::valueChanged, [this](int value) {
    if (timeLabel) {
      int totalSecs = totalDuration / 1000;
      int currentSecs = value / 1000;
      QString currentTime = QTime(0, 0).addSecs(currentSecs).toString("m:ss");
      QString totalTime = QTime(0, 0).addSecs(totalSecs).toString("m:ss");
      timeLabel->setText(QString("%1 / %2").arg(currentTime, totalTime));
      
      if (isSeeking) {
        static qint64 lastSeekTime = 0;
        qint64 currentTime_ms = QDateTime::currentMSecsSinceEpoch();
        if (currentTime_ms - lastSeekTime > 100) {
          currentPosition = value;
          seekToPosition(currentPosition);
          lastSeekTime = currentTime_ms;
        }
      }
    }
  });
}

void BPRouteVideoDialog::updateOverlayPosition() {
  if (controlsWidget && videoContainer) {
    // Center controls in video
    int x = (videoContainer->width() - controlsWidget->width()) / 2;
    int y = (videoContainer->height() - controlsWidget->height()) / 2;
    controlsWidget->move(x, y);
  }
}

void BPRouteVideoDialog::resizeEvent(QResizeEvent *event) {
  BPDialogBase::resizeEvent(event);
  updateOverlayPosition();
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

  // Common button style for all available cameras - LARGER for 6" display
  QString buttonStyle = R"(
    QPushButton {
      background: #404040;
      color: white;
      font-size: 42px;
      border: 3px solid #555555;
      border-radius: 20px;
      text-align: center;
      font-weight: bold;
      padding: 15px;
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
  // Priority: HQ cameras first, then LQ only if no HQ available
  qDebug() << "[VIDEO DEBUG] === Camera Button Creation ===";
  qDebug() << "[VIDEO DEBUG] Checking hasFrontHQVideo: " << (routeInfo.hasFrontHQVideo ? "true" : "false");
  if (routeInfo.hasFrontHQVideo) {
    qDebug() << "[VIDEO DEBUG] Creating Front Camera button";
    frontCamButton = new QPushButton("Front Camera");
    frontCamButton->setFixedHeight(120);  // Larger for 6" touch display
    frontCamButton->setCheckable(true);
    frontCamButton->setStyleSheet(buttonStyle);
    cameraGroup->addButton(frontCamButton);
    connect(frontCamButton, &QPushButton::clicked, [this]() { switchCamera("front"); });
    panelLayout->addWidget(frontCamButton);
    qDebug() << "[VIDEO DEBUG] Front Camera button created and added";
  } else if (routeInfo.hasFrontLQVideo) {
    // Only show LQ if HQ is not available
    qDebug() << "[VIDEO DEBUG] Creating Front LQ Camera button (HQ not available)";
    frontCamButton = new QPushButton("Front Camera (LQ)");
    frontCamButton->setFixedHeight(120);  // Larger for 6" touch display
    frontCamButton->setCheckable(true);
    frontCamButton->setStyleSheet(buttonStyle);
    cameraGroup->addButton(frontCamButton);
    connect(frontCamButton, &QPushButton::clicked, [this]() { switchCamera("lq"); });
    panelLayout->addWidget(frontCamButton);
    qDebug() << "[VIDEO DEBUG] Front LQ Camera button created and added";
  } else {
    qDebug() << "[VIDEO DEBUG] Skipping Front Camera button - no video available";
  }

  qDebug() << "[VIDEO DEBUG] Checking hasWideVideo: " << (routeInfo.hasWideVideo ? "true" : "false");
  if (routeInfo.hasWideVideo) {
    qDebug() << "[VIDEO DEBUG] Creating Wide Camera button";
    wideCamButton = new QPushButton("Wide Camera");
    wideCamButton->setFixedHeight(120);  // Larger for 6" touch display
    wideCamButton->setCheckable(true);
    wideCamButton->setStyleSheet(buttonStyle);
    cameraGroup->addButton(wideCamButton);
    connect(wideCamButton, &QPushButton::clicked, [this]() { switchCamera("wide"); });
    panelLayout->addWidget(wideCamButton);
    qDebug() << "[VIDEO DEBUG] Wide Camera button created and added";
  } else {
    qDebug() << "[VIDEO DEBUG] Skipping Wide Camera button - no wide video";
  }

  if (routeInfo.hasDriverHQVideo) {
    driverCamButton = new QPushButton("Driver Camera");
    driverCamButton->setFixedHeight(120);  // Larger for 6" touch display
    driverCamButton->setCheckable(true);
    driverCamButton->setStyleSheet(buttonStyle);
    cameraGroup->addButton(driverCamButton);
    connect(driverCamButton, &QPushButton::clicked, [this]() { switchCamera("driver"); });
    panelLayout->addWidget(driverCamButton);
  }

  // Set default camera
  if (routeInfo.hasFrontHQVideo && frontCamButton) {
    frontCamButton->setChecked(true);
    currentCameraType = "front";
    qDebug() << "[VIDEO DEBUG] Set default camera to front";
  } else if (routeInfo.hasFrontLQVideo && frontCamButton) {
    // frontCamButton was created as LQ in this case
    frontCamButton->setChecked(true);
    currentCameraType = "lq";
    qDebug() << "[VIDEO DEBUG] Set default camera to front LQ";
  } else if (routeInfo.hasWideVideo && wideCamButton) {
    wideCamButton->setChecked(true);
    currentCameraType = "wide";
    qDebug() << "[VIDEO DEBUG] Set default camera to wide";
  } else if (routeInfo.hasDriverHQVideo && driverCamButton) {
    driverCamButton->setChecked(true);
    currentCameraType = "driver";
    qDebug() << "[VIDEO DEBUG] Set default camera to driver";
  }

  qDebug() << "[VIDEO DEBUG] Final camera selection: " << currentCameraType;

  panelLayout->addStretch();

  // Actions section - fixed
  setupActionButtons(panelLayout);

  panelLayout->addStretch();
}

void BPRouteVideoDialog::setupActionButtons(QVBoxLayout *parentLayout) {
  // Actions section title
  QLabel *actionsTitle = new QLabel("Actions");
  actionsTitle->setStyleSheet("font-size: 48px; font-weight: 600; color: white; margin-bottom: 20px; margin-top: 30px;");
  parentLayout->addWidget(actionsTitle);

  // Create horizontal flow layout container
  QWidget *actionButtonsContainer = new QWidget;
  QGridLayout *gridLayout = new QGridLayout(actionButtonsContainer);
  gridLayout->setContentsMargins(0, 0, 0, 0);
  gridLayout->setSpacing(20);

  // Star button - larger and touch-friendly
  starButton = new QPushButton;
  starButton->setFixedSize(100, 100);
  starButton->setText(routeInfo.isStarred ? "★" : "☆");
  starButton->setStyleSheet(R"(
    QPushButton {
      background: #444444;
      border: 3px solid #666666;
      border-radius: 50px;
      font-size: 56px;
      color: #FFD700;
      font-weight: bold;
    }
    QPushButton:hover {
      background: #555555;
      border: 3px solid #FFD700;
    }
    QPushButton:pressed {
      background: #333333;
    }
  )");
  connect(starButton, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleStar);
  
  // Delete button - larger and touch-friendly
  deleteButton = new QPushButton("🗑");
  deleteButton->setFixedSize(100, 100);
  deleteButton->setStyleSheet(R"(
    QPushButton {
      background: #d32f2f;
      border: 3px solid #b71c1c;
      border-radius: 50px;
      font-size: 48px;
      color: white;
      font-weight: bold;
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

  // Add buttons to grid - will wrap automatically
  gridLayout->addWidget(starButton, 0, 0);
  gridLayout->addWidget(deleteButton, 0, 1);
  
  // Add more action buttons here in future (share, export, etc)
  // They will automatically flow to next row when space runs out

  parentLayout->addWidget(actionButtonsContainer);
}

void BPRouteVideoDialog::loadVideoSegments() {
  qDebug() << "[VIDEO DEBUG] Loading video segments for camera: " << currentCameraType;
  currentPlaylist.clear();

  // Build playlist for current camera
  BPRoutesPanel *parent = static_cast<BPRoutesPanel*>(this->parent());
  QString routesDir = parent->getRoutesDir();
  qDebug() << "[VIDEO DEBUG] Routes directory: " << routesDir;

  // Find all segment directories for this route
  QDir dir(routesDir);
  QStringList segmentFilter;
  segmentFilter << routeBaseName + "--*";
  QStringList segmentDirs = dir.entryList(segmentFilter, QDir::Dirs, QDir::Name);

  qDebug() << "[VIDEO DEBUG] Found " << segmentDirs.size() << " segment directories for route " << routeBaseName;

  // Build ordered playlist by scanning each segment
  for (int i = 0; i < segmentDirs.size(); ++i) {
    QString videoPath = getVideoPath(currentCameraType, i);
    qDebug() << "[VIDEO DEBUG] Checking segment " << i << " video path: " << videoPath;

    if (QFile::exists(videoPath)) {
      currentPlaylist.append(videoPath);
      qDebug() << "[VIDEO DEBUG] Added segment " << i << " to playlist: " << videoPath;
    } else {
      qDebug() << "[VIDEO DEBUG] Video file not found for segment " << i << ": " << videoPath;
    }
  }

  qDebug() << "[VIDEO DEBUG] Total playlist size: " << currentPlaylist.size() << " segments";

  // Update segment indicator on UI thread
  QMetaObject::invokeMethod(this, [this]() {
    if (segmentLabel) {
      if (currentPlaylist.isEmpty()) {
        segmentLabel->setText("No segments");
        segmentLabel->setStyleSheet("font-size: 28px; color: #ff4444;");
      } else {
        segmentLabel->setText(QString("Segment: 1 of %1").arg(currentPlaylist.size()));
        segmentLabel->setStyleSheet("font-size: 28px; color: #888;");
      }
    }
  }, Qt::QueuedConnection);

  if (!currentPlaylist.isEmpty()) {
    currentSegment = 0;
    // Calculate total duration (rough estimate: 60 seconds per segment)
    totalDuration = currentPlaylist.size() * 60 * 1000; // milliseconds
    qDebug() << "[VIDEO DEBUG] Total estimated duration: " << totalDuration << " ms";
    positionSlider->setRange(0, totalDuration);
    playCurrentSegment();
  } else {
    qDebug() << "[VIDEO DEBUG] WARNING: No video segments found for " << currentCameraType;
    qDebug() << "[VIDEO] No " << currentCameraType << " video available";
  }
}

void BPRouteVideoDialog::loadThumbnail() {
  // Load and display the first frame as thumbnail
  qDebug() << "[VIDEO DEBUG] Loading first frame as thumbnail...";

  if (!frameReader) {
    qDebug() << "[VIDEO DEBUG] No frameReader available for thumbnail";
    return;
  }

  // Create a buffer for the first frame
  VisionBuf thumbnailBuf = {};
  size_t frame_size = frameReader->width * frameReader->height * 3 / 2; // YUV420 format
  thumbnailBuf.allocate(frame_size);
  thumbnailBuf.init_yuv(frameReader->width, frameReader->height, frameReader->width, frameReader->width * frameReader->height);

  // Decode the first frame (index 0)
  if (frameReader->get(0, &thumbnailBuf)) {
    qDebug() << "[VIDEO DEBUG] Successfully decoded first frame for thumbnail";
    // Display the first frame in the video widget
    videoDisplay->displayFrame(&thumbnailBuf, frameReader->width, frameReader->height);
    qDebug() << "[VIDEO] First frame loaded and displayed as thumbnail";
  } else {
    qDebug() << "[VIDEO DEBUG] Failed to decode first frame for thumbnail";

    // Fallback to static thumbnail if first frame fails
    BPRoutesPanel *parent = static_cast<BPRoutesPanel*>(this->parent());
    QString thumbnailPath = parent->getThumbnailPath(routeBaseName);

    if (QFile::exists(thumbnailPath)) {
      qDebug() << "[VIDEO DEBUG] Falling back to static thumbnail";
      // Note: We can't directly display a QPixmap in the OpenGL video widget
      // The static thumbnail would need to be converted to YUV format
      qDebug() << "[VIDEO] Static thumbnail available but video widget needs YUV data";
    }
  }

  // Clean up thumbnail buffer
  thumbnailBuf.free();
}

QString BPRouteVideoDialog::getVideoPath(const QString &cameraType, int segment) {
  BPRoutesPanel *parent = static_cast<BPRoutesPanel*>(this->parent());
  QString routesDir = parent->getRoutesDir();

  QString filename;

  // Map camera types to actual filenames
  if (cameraType == "front") {
    filename = "fcamera.hevc";
  } else if (cameraType == "wide") {
    filename = "ecamera.hevc";  // Wide camera files are ecamera.hevc
  } else if (cameraType == "driver") {
    filename = "dcamera.hevc";  // Driver camera files are dcamera.hevc
  } else if (cameraType == "lq") {
    filename = "qcamera.ts";
  }

  // Build segment directory path: routeBaseName + "--" + segment
  QString segmentDir = QString("%1--%2").arg(routeBaseName).arg(segment);
  QString videoPath = routesDir + "/" + segmentDir + "/" + filename;

  qDebug() << "[VIDEO DEBUG] Generated video path: " << videoPath;
  return videoPath;
}

void BPRouteVideoDialog::playCurrentSegment() {
  qDebug() << "[VIDEO DEBUG] === playCurrentSegment() called ===";
  qDebug() << "[VIDEO DEBUG] Current playlist size: " << currentPlaylist.size();
  qDebug() << "[VIDEO DEBUG] Current segment: " << currentSegment;
  qDebug() << "[VIDEO DEBUG] Current camera type: " << currentCameraType;

  if (currentSegment >= currentPlaylist.size()) {
    qDebug() << "[VIDEO DEBUG] ERROR: currentSegment (" << currentSegment << ") >= playlist size (" << currentPlaylist.size() << ")";
    return;
  }

  QString videoPath = currentPlaylist[currentSegment];
  qDebug() << "[VIDEO DEBUG] Playing segment " << currentSegment << ": " << videoPath;

  if (!QFile::exists(videoPath)) {
    qDebug() << "[VIDEO DEBUG] ERROR: Video file does not exist: " << videoPath;
    qDebug() << "[VIDEO] Video file not found";
    return;
  }

  QFileInfo fileInfo(videoPath);
  qDebug() << "[VIDEO DEBUG] File size: " << fileInfo.size() << " bytes";

  qDebug() << "[VIDEO DEBUG] === Loading video with FrameReader ===";
  qDebug() << "[VIDEO DEBUG] Video path: " << videoPath;
  qDebug() << "[VIDEO DEBUG] File exists: " << QFile::exists(videoPath);

  // Create FrameReader and load video
  qDebug() << "[VIDEO DEBUG] Creating FrameReader...";
  frameReader = std::make_unique<FrameReader>();

  // Determine camera type based on current selection
  CameraType cameraType = RoadCam;  // Default to front camera
  qDebug() << "[VIDEO DEBUG] Current camera type string: " << currentCameraType;
  if (currentCameraType == "driver") {
    cameraType = DriverCam;
    qDebug() << "[VIDEO DEBUG] Using DriverCam type";
  } else if (currentCameraType == "wide") {
    cameraType = WideRoadCam;
    qDebug() << "[VIDEO DEBUG] Using WideRoadCam type";
  } else {
    qDebug() << "[VIDEO DEBUG] Using RoadCam type (default)";
  }

  // Load video with hardware decoding enabled
  qDebug() << "[VIDEO DEBUG] Calling frameReader->load()...";
  std::atomic<bool> abort{false};
  if (!frameReader->load(cameraType, videoPath.toStdString(), false, &abort)) {
    qDebug() << "[VIDEO DEBUG] ERROR: Failed to load video with FrameReader";
    qDebug() << "[VIDEO] Failed to load video file";
    return;
  }

  qDebug() << "[VIDEO DEBUG] FrameReader load successful!";
  qDebug() << "[VIDEO DEBUG] Video loaded successfully - " << frameReader->getFrameCount()
            << " frames, " << frameReader->width << "x" << frameReader->height;

  // Clear display
  qDebug() << "[VIDEO] Starting video playback...";

  // Display first frame immediately to show video content
  qDebug() << "[VIDEO DEBUG] Displaying first frame immediately...";
  VisionBuf firstFrame = {};
  size_t frame_size = frameReader->width * frameReader->height * 3 / 2;
  firstFrame.allocate(frame_size);
  firstFrame.init_yuv(frameReader->width, frameReader->height, frameReader->width, frameReader->width * frameReader->height);

  if (frameReader->get(0, &firstFrame)) {
    qDebug() << "[VIDEO DEBUG] First frame decoded successfully";
    if (videoDisplay) {
      qDebug() << "[VIDEO DEBUG] Displaying first frame on video widget";
      videoDisplay->displayFrame(&firstFrame, frameReader->width, frameReader->height);
    } else {
      qDebug() << "[VIDEO DEBUG] ERROR: videoDisplay is null!";
    }
  } else {
    qDebug() << "[VIDEO DEBUG] ERROR: Failed to decode first frame";
  }
  firstFrame.free();

  // Initialize playback state
  currentFrameIndex = 1; // Start from frame 1 since frame 0 was already displayed
  totalFrames = frameReader->getFrameCount();

  qDebug() << "[VIDEO DEBUG] Video segment loaded, timer will start automatically";
}

void BPRouteVideoDialog::updateVideoFrame() {
  // Enhanced safety checks
  if (!isPlaying || !frameReader || !videoDisplay || !playbackTimer) {
    qDebug() << "[VIDEO DEBUG] updateVideoFrame - stopping timer due to safety check";
    if (playbackTimer) {
      playbackTimer->stop();
    }
    return;
  }

  // Check frame reader validity
  if (frameReader->width <= 0 || frameReader->height <= 0 || totalFrames <= 0) {
    qDebug() << "[VIDEO DEBUG] Invalid frame reader state - stopping playback";
    if (playbackTimer) {
      playbackTimer->stop();
    }
    return;
  }

  if (currentFrameIndex >= totalFrames) {
    // Video finished
    qDebug() << "[VIDEO DEBUG] Video finished, stopping timer";
    if (playbackTimer) {
      playbackTimer->stop();
    }
    onSegmentFinished();
    return;
  }

  // Allocate buffer for current frame with safety checks
  VisionBuf frameBuf = {};
  try {
    size_t frame_size = frameReader->width * frameReader->height * 3 / 2;
    if (frame_size == 0 || frame_size > 10*1024*1024) { // Sanity check: max 10MB per frame
      qDebug() << "[VIDEO DEBUG] Invalid frame size:" << frame_size;
      currentFrameIndex++;
      return;
    }

    frameBuf.allocate(frame_size);
    frameBuf.init_yuv(frameReader->width, frameReader->height, frameReader->width, frameReader->width * frameReader->height);

    // Decode current frame with error handling
    bool decode_result = frameReader->get(currentFrameIndex, &frameBuf);

    if (decode_result && frameBuf.addr) {
      // Display frame directly on UI thread
      videoDisplay->displayFrame(&frameBuf, frameReader->width, frameReader->height);

      // Update position safely
      currentPosition = (currentFrameIndex * 50); // 20fps = 50ms per frame
      if (positionSlider && !isSeeking) {
        positionSlider->setValue(currentPosition);
      }

      // Minimal debug logging for performance
      if (currentFrameIndex % 60 == 0) { // Log every 3 seconds at 20fps
        qDebug() << "[VIDEO DEBUG] Displayed frame" << currentFrameIndex << "/" << totalFrames;
      }
    } else {
      qDebug() << "[VIDEO DEBUG] Failed to decode frame" << currentFrameIndex;
    }

    // Always free the buffer
    frameBuf.free();

  } catch (const std::exception& e) {
    qDebug() << "[VIDEO DEBUG] Exception in updateVideoFrame:" << e.what();
    if (frameBuf.addr) {
      frameBuf.free();
    }
    // Stop playback on exception to prevent continuous crashes
    if (playbackTimer) {
      playbackTimer->stop();
    }
    isPlaying = false;
    if (playPauseButton) {
      playPauseButton->setText("▶");
    }
    return;
  }

  currentFrameIndex++;
}

void BPRouteVideoDialog::playbackVideoFrames() {
  // This method is now deprecated - using timer-based approach instead
  qDebug() << "[VIDEO DEBUG] playbackVideoFrames() called but using timer-based approach";
}

void BPRouteVideoDialog::onFrameDecoded(const DecodedFrame &frame, const VisionBuf *buf) {
  static int frameNumber = 0;
  frameNumber++;

  // Minimal logging for performance
  if (frameNumber % 50 == 0) {
    qDebug() << "[VIDEO] Displaying frame " << frameNumber;
  }

  // Update display directly with VisionBuf
  if (buf) {
    // Safe const cast since display doesn't modify the buffer
    videoDisplay->displayFrame(const_cast<VisionBuf*>(buf), frame.width, frame.height);
  }

  // Update timestamp/progress
  currentPosition = frame.timestamp_us / 1000;  // Convert to ms
  positionSlider->setValue(currentPosition);
}

void BPRouteVideoDialog::updateCameraButtonStates() {
  qDebug() << "[VIDEO DEBUG] Updating camera button states for: " << currentCameraType;

  // Clear all button selections first
  if (frontCamButton) frontCamButton->setChecked(false);
  if (wideCamButton) wideCamButton->setChecked(false);
  if (driverCamButton) driverCamButton->setChecked(false);
  if (lqCamButton) lqCamButton->setChecked(false);

  // Set the correct button as checked based on current camera type
  if (currentCameraType == "front" && frontCamButton) {
    frontCamButton->setChecked(true);
    qDebug() << "[VIDEO DEBUG] Set front camera button as selected";
  } else if (currentCameraType == "wide" && wideCamButton) {
    wideCamButton->setChecked(true);
    qDebug() << "[VIDEO DEBUG] Set wide camera button as selected";
  } else if (currentCameraType == "driver" && driverCamButton) {
    driverCamButton->setChecked(true);
    qDebug() << "[VIDEO DEBUG] Set driver camera button as selected";
  } else if (currentCameraType == "lq" && lqCamButton) {
    lqCamButton->setChecked(true);
    qDebug() << "[VIDEO DEBUG] Set LQ camera button as selected";
  }
}

void BPRouteVideoDialog::togglePlayback() {
  qDebug() << "[VIDEO DEBUG] togglePlayback() called - current state: " << (isPlaying ? "PLAYING" : "STOPPED");

  // Safety check - ensure we have valid video data before starting playback
  if (!isPlaying && (!frameReader || totalFrames <= 0 || frameReader->width <= 0 || frameReader->height <= 0)) {
    qDebug() << "[VIDEO DEBUG] Cannot start playback - invalid video data";
    return;
  }

  isPlaying = !isPlaying;

  if (isPlaying) {
    qDebug() << "[VIDEO DEBUG] Starting playback";
    if (playPauseButton) {
      playPauseButton->setText("II");  // Pause bars
    }
    if (playbackTimer) {
      playbackTimer->start(50); // 20fps
    }
    if (positionTimer) {
      positionTimer->start();
    }
    qDebug() << "[VIDEO DEBUG] Playback and position timers started";
  } else {
    qDebug() << "[VIDEO DEBUG] Stopping playback";
    if (playPauseButton) {
      playPauseButton->setText("▶");  // Play triangle
    }
    if (playbackTimer) {
      playbackTimer->stop();
    }
    if (positionTimer) {
      positionTimer->stop();
    }
    qDebug() << "[VIDEO DEBUG] Playback and position timers stopped";
    // Show thumbnail when paused
    qDebug() << "[VIDEO DEBUG] Loading thumbnail for paused state";
    loadThumbnail();
  }
}

void BPRouteVideoDialog::switchCamera(const QString &cameraType) {
  if (currentCameraType != cameraType) {
    qDebug() << "[VIDEO DEBUG] Switching camera from " << currentCameraType
              << " to " << cameraType;

    // Store current playback state
    bool wasPlaying = isPlaying;
    qint64 savedPosition = currentPosition;
    int savedSegment = currentSegment;

    qDebug() << "[VIDEO DEBUG] Saved state - Playing: " << wasPlaying
              << ", Position: " << savedPosition << ", Segment: " << savedSegment;

    // Stop current playback
    if (isPlaying) {
      isPlaying = false;
      if (positionTimer) {
        positionTimer->stop();
      }
    }

    // Stop and wait for playback thread to finish before switching
    if (playbackFuture.isRunning()) {
      playbackFuture.cancel();
      playbackFuture.waitForFinished(); // Wait for thread to finish
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

    qDebug() << "[VIDEO DEBUG] Restored position: " << currentPosition
              << ", segment: " << currentSegment;

    if (wasPlaying) {
      // Resume playback - wait for segment to load before starting timer
      isPlaying = true;
      playPauseButton->setText("II");  // Pause bars
      positionTimer->start();
      
      // Ensure frameReader is ready before starting playback timer
      QTimer::singleShot(100, this, [this]() {
        if (frameReader && totalFrames > 0 && frameReader->width > 0 && frameReader->height > 0) {
          if (playbackTimer && isPlaying) {
            playbackTimer->start(50);  // Restart playback timer after segment loads
            qDebug() << "[VIDEO DEBUG] Playback timer started after camera switch";
          }
        } else {
          qDebug() << "[VIDEO DEBUG] Cannot start playback timer - frameReader not ready";
          isPlaying = false;
          playPauseButton->setText("▶");
        }
      });
      qDebug() << "[VIDEO DEBUG] Resuming playback after camera switch";
    } else {
      // Show thumbnail for new camera
      loadThumbnail();
      qDebug() << "[VIDEO DEBUG] Loaded thumbnail for stopped playback";
    }
  }
}

void BPRouteVideoDialog::seekForward() {
  currentPosition = qMin(currentPosition + 10000, totalDuration); // +10 seconds
  seekToPosition(currentPosition);
  positionSlider->setValue(currentPosition);
}

void BPRouteVideoDialog::seekBackward() {
  currentPosition = qMax(currentPosition - 10000, (qint64)0); // -10 seconds
  seekToPosition(currentPosition);
  positionSlider->setValue(currentPosition);
}

void BPRouteVideoDialog::toggleFullscreen() {
  isFullscreen = !isFullscreen;

  if (isFullscreen) {
    // Hide camera panel and header for true fullscreen
    cameraPanel->hide();

    // Create fullscreen exit button - circular X in top left
    if (!fullscreenExitButton) {
      fullscreenExitButton = new QPushButton("✕");
      fullscreenExitButton->setFixedSize(80, 80);
      fullscreenExitButton->setStyleSheet(R"(
        QPushButton {
          background: rgba(0, 0, 0, 180);
          border: 3px solid #666666;
          border-radius: 40px;
          font-size: 40px;
          color: white;
          font-weight: bold;
        }
        QPushButton:hover {
          background: rgba(0, 0, 0, 220);
          border: 3px solid #FFD700;
        }
        QPushButton:pressed {
          background: rgba(0, 0, 0, 255);
        }
      )");
      fullscreenExitButton->setParent(videoContainer);
      connect(fullscreenExitButton, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleFullscreen);
    }

    // Position exit button in top left corner
    fullscreenExitButton->move(20, 20);
    fullscreenExitButton->show();
    fullscreenExitButton->raise();

    // Make video fill entire container
    videoDisplay->setGeometry(0, 0, videoContainer->width(), videoContainer->height());

    // Update overlay position for fullscreen
    updateOverlayPosition();

    fullscreenButton->setText("⛶");
  } else {
    // Exit fullscreen - restore normal layout
    cameraPanel->show();

    if (fullscreenExitButton) {
      fullscreenExitButton->hide();
    }

    // Restore normal video size
    videoDisplay->setGeometry(0, 160, videoContainer->width(), videoContainer->height() - 160);

    // Update overlay position for normal view
    updateOverlayPosition();

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
    // Update segment indicator
    if (segmentLabel) {
      segmentLabel->setText(QString("Segment: %1 of %2").arg(currentSegment + 1).arg(currentPlaylist.size()));
    }
    playCurrentSegment();
  } else {
    // Playback finished
    isPlaying = false;
    playPauseButton->setText("▶");  // Play triangle
    positionTimer->stop();
    // Show thumbnail when playback ends
    loadThumbnail();
    // Reset segment indicator
    if (segmentLabel) {
      segmentLabel->setText(QString("Segment: 1 of %1").arg(currentPlaylist.size()));
    }
  }
}

void BPRouteVideoDialog::seekToPosition(qint64 positionMs) {
  // Safety checks to prevent crashes
  if (!frameReader || currentPlaylist.isEmpty() || !videoDisplay) {
    qDebug() << "[VIDEO DEBUG] Seek aborted - missing components";
    return;
  }

  // Clamp position to valid range
  positionMs = qBound(0LL, positionMs, totalDuration);

  qDebug() << "[VIDEO DEBUG] Seeking to position: " << positionMs << "ms";

  // Calculate which segment and frame to seek to
  qint64 segmentDuration = 60 * 1000; // 60 seconds per segment in ms
  int targetSegment = positionMs / segmentDuration;
  qint64 positionInSegment = positionMs % segmentDuration;

  // Clamp segment to valid range
  targetSegment = qBound(0, targetSegment, currentPlaylist.size() - 1);

  // If we need to change segments, do that first
  if (targetSegment != currentSegment) {
    qDebug() << "[VIDEO DEBUG] Seeking to segment: " << targetSegment;

    // Validate target segment exists
    if (targetSegment < 0 || targetSegment >= currentPlaylist.size()) {
      qDebug() << "[VIDEO DEBUG] Invalid segment index: " << targetSegment;
      return;
    }

    currentSegment = targetSegment;

    // Stop current playback temporarily
    bool wasPlaying = isPlaying;
    if (isPlaying && playbackTimer) {
      playbackTimer->stop();
    }

    // Load the new segment
    playCurrentSegment();

    // Update segment indicator safely
    if (segmentLabel) {
      segmentLabel->setText(QString("Segment: %1 of %2").arg(currentSegment + 1).arg(currentPlaylist.size()));
    }

    // Restore playing state if it was playing
    if (wasPlaying && playPauseButton) {
      isPlaying = true;
      playPauseButton->setText("⏸");
      if (!isSeeking && playbackTimer) {
        playbackTimer->start(50); // Only restart timer if not still seeking
      }
    }
  }

  // Calculate target frame within current segment
  // Assuming 20fps (50ms per frame)
  int frameRate = 20;
  int targetFrameInSegment = (positionInSegment * frameRate) / 1000;

  // Clamp to valid frame range and ensure we have valid data
  if (frameReader && totalFrames > 0 && frameReader->width > 0 && frameReader->height > 0) {
    targetFrameInSegment = qBound(0, targetFrameInSegment, static_cast<int>(totalFrames) - 1);

    // Seek to the specific frame and display it immediately
    VisionBuf seekBuf = {};
    try {
      size_t frame_size = frameReader->width * frameReader->height * 3 / 2;
      seekBuf.allocate(frame_size);
      seekBuf.init_yuv(frameReader->width, frameReader->height, frameReader->width, frameReader->width * frameReader->height);

      if (frameReader->get(targetFrameInSegment, &seekBuf)) {
        videoDisplay->displayFrame(&seekBuf, frameReader->width, frameReader->height);
        currentFrameIndex = targetFrameInSegment;

        qDebug() << "[VIDEO DEBUG] Seeked to frame " << targetFrameInSegment << " in segment " << targetSegment;
      } else {
        qDebug() << "[VIDEO DEBUG] Failed to seek to frame " << targetFrameInSegment;
      }

      seekBuf.free();
    } catch (const std::exception& e) {
      qDebug() << "[VIDEO DEBUG] Exception during seek:" << e.what();
      if (seekBuf.addr) {
        seekBuf.free();
      }
    }
  } else {
    qDebug() << "[VIDEO DEBUG] Cannot seek - invalid frame reader state";
  }

  // Update position safely
  currentPosition = positionMs;
  if (!isSeeking && positionSlider) {
    positionSlider->setValue(currentPosition);
  }

  qDebug() << "[VIDEO DEBUG] Seek completed to segment " << targetSegment << ", frame " << targetFrameInSegment;
}

void BPRouteVideoDialog::updatePlaybackPosition() {
  static int updateCount = 0;
  updateCount++;

  if (isPlaying && !isSeeking) {
    currentPosition += 100; // Add 100ms
    positionSlider->setValue(currentPosition);

    // Update time label
    int totalSecs = totalDuration / 1000;
    int currentSecs = currentPosition / 1000;

    QString format = totalSecs >= 3600 ? "h:mm:ss" : "m:ss";
    QString currentTime = QTime(0, 0).addSecs(currentSecs).toString(format);
    QString totalTime = QTime(0, 0).addSecs(totalSecs).toString(format);

    timeLabel->setText(QString("%1 / %2").arg(currentTime, totalTime));

    // Only log every 50th update to reduce noise
    if (updateCount % 50 == 0) {
      qDebug() << "[VIDEO DEBUG] Position update: " << currentPosition << "/" << totalDuration
                << " (" << currentTime << "/" << totalTime << ")";
    }
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

void BPRouteVideoDialog::keepDisplayAwake() {
  // Reset interactivity timeout to keep display awake during video playback
  if (auto *dev = device()) {
    dev->resetInteractiveTimeout();
    qDebug() << "[VIDEO DEBUG] Reset display timeout to keep screen awake";
  }
  // Also send a dummy input event to keep system awake
  QApplication::sendPostedEvents();
}

void BPRouteVideoDialog::showEvent(QShowEvent *event) {
  BPDialogBase::showEvent(event);

  // Start the keep awake timer
  keepAwakeTimer->start();

  // Reset interactivity timeout to keep display awake during video playback
  keepDisplayAwake();

  // setupFullscreen() is now called before exec() in bp_routes_panel.cc for proper QCOM2 rotation
}

void BPRouteVideoDialog::hideEvent(QHideEvent *event) {
  BPDialogBase::hideEvent(event);

  // Stop the keep awake timer when modal is hidden
  if (keepAwakeTimer) {
    keepAwakeTimer->stop();
  }

  // Stop video playback when modal is hidden
  isPlaying = false;
  if (positionTimer) {
    positionTimer->stop();
  }

  qDebug() << "[VIDEO DEBUG] BPRouteVideoDialog hidden, stopped timers and playback";
}
