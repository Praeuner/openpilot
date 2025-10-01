#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/bp_video_dialog.h"

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include <iostream>
#include <QDir>
#include <QHBoxLayout>
#include <QTime>
#include <QVBoxLayout>

#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/bp_media_control_button.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/bp_routes_panel.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/bp_video_widget.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/sunnypilot/ui.h"

namespace {
constexpr int kSeekStepMs = 10'000;          // 10 seconds
constexpr int kOverlayHideMs = 3'000;        // Controls auto-hide delay
constexpr int kKeepAwakeMs = 3'000;          // Reset display wake
constexpr int kWatchdogTimeoutMs = 100;      // UI watchdog threshold

QString formatPlaybackTime(qint64 millis) {
  const int seconds = static_cast<int>(millis / 1000);
  return QTime(0, 0).addSecs(seconds).toString("m:ss");
}

CameraKind cameraFromString(const QString &camera) {
  if (camera == "wide") {
    return CameraKind::kWide;
  }
  if (camera == "driver") {
    return CameraKind::kDriver;
  }
  if (camera == "lq") {
    return CameraKind::kFrontLq;
  }
  return CameraKind::kFront;
}
}  // namespace

BPRouteVideoDialog::BPRouteVideoDialog(const QString &route_base, QWidget *parent)
    : BPDialogBase(parent), routeBaseName(route_base) {
  setWindowTitle("Route Video Playback");

  if (auto *routes_panel = qobject_cast<BPRoutesPanel *>(parent)) {
    // Find the first segment directory for this route (format: base--0, base--1, etc.)
    QString routes_dir = routes_panel->getRoutesDir();
    QString first_segment_path;

    QDir dir(routes_dir);
    QStringList segment_filter;
    segment_filter << route_base + "--*";
    QStringList matching_dirs = dir.entryList(segment_filter, QDir::Dirs, QDir::Name);

    if (!matching_dirs.isEmpty()) {
      first_segment_path = routes_dir + "/" + matching_dirs.first();
    } else {
      // Fallback to base name if no segments found
      first_segment_path = routes_dir + "/" + route_base;
    }

    BPLog::bpDebugVideo() << "[bp.video.dialog] Constructor | Looking for route in: " << first_segment_path.toStdString() << std::endl;

    const auto info = routes_panel->getRouteInfo(first_segment_path);
    routeInfo.baseName = info.baseName;
    routeInfo.timestamp = info.timestamp;
    routeInfo.endTimestamp = info.endTimestamp;
    routeInfo.duration = info.duration;
    routeInfo.elapsedTime = info.elapsedTime;
    routeInfo.displayDate = info.displayDate;
    routeInfo.size = info.size;
    routeInfo.humanTime = info.humanTime;
    routeInfo.segments = info.segments;
    routeInfo.tripMiles = info.tripMiles;
    routeInfo.hasVideo = info.hasVideo;
    routeInfo.hasRLog = info.hasRLog;
    routeInfo.hasQLog = info.hasQLog;
    routeInfo.hasFrontVideo = info.hasFrontVideo;
    routeInfo.hasWideVideo = info.hasWideVideo;
    routeInfo.hasDriverVideo = info.hasDriverVideo;
    routeInfo.hasLQVideo = info.hasLQVideo;
    routeInfo.hasFrontHQVideo = info.hasFrontHQVideo;
    routeInfo.hasFrontLQVideo = info.hasFrontLQVideo;
    routeInfo.hasDriverHQVideo = info.hasDriverHQVideo;
    routeInfo.isStarred = info.isStarred;
    routeInfo.dateTime = info.dateTime;
  }

  // Debug: Log route info to diagnose time extraction
  BPLog::bpDebugVideo() << "[bp.video.dialog] Constructor | Route info loaded:" << std::endl;
  BPLog::bpDebugVideo() << "  - baseName: " << routeInfo.baseName.toStdString() << std::endl;
  BPLog::bpDebugVideo() << "  - timestamp: " << routeInfo.timestamp.toStdString() << std::endl;
  BPLog::bpDebugVideo() << "  - humanTime: " << routeInfo.humanTime.toStdString() << std::endl;
  BPLog::bpDebugVideo() << "  - dateTime valid: " << (routeInfo.dateTime.isValid() ? "true" : "false") << std::endl;
  BPLog::bpDebugVideo() << "  - dateTime: " << routeInfo.dateTime.toString().toStdString() << std::endl;
  BPLog::bpDebugVideo() << "  - displayDate: " << routeInfo.displayDate.toStdString() << std::endl;

  // Load fullscreen SVG icons
  fullscreenIcon = loadPixmap("../assets/offroad/icon_fullscreen.svg", QSize(80, 80));
  minimizeIcon = loadPixmap("../assets/offroad/icon_minimize.svg", QSize(80, 80));

  // Fallback to PNG only if SVG loading completely fails
  if (fullscreenIcon.isNull()) {
    fullscreenIcon = loadPixmap("../assets/offroad/icon_open_fullscreen.png", QSize(80, 80));
  }
  if (minimizeIcon.isNull()) {
    minimizeIcon = loadPixmap("../assets/offroad/icon_open_fullscreen.png", QSize(80, 80));
  }

  setupUI();
  attachInputHandlers();

  overlayFadeTimer = new QTimer(this);
  overlayFadeTimer->setInterval(kOverlayHideMs);
  overlayFadeTimer->setSingleShot(true);
  QObject::connect(overlayFadeTimer, &QTimer::timeout, this, &BPRouteVideoDialog::hideOverlayControls);

  keepAwakeTimer = new QTimer(this);
  keepAwakeTimer->setInterval(kKeepAwakeMs);
  QObject::connect(keepAwakeTimer, &QTimer::timeout, this, &BPRouteVideoDialog::keepDisplayAwake);

  QString routes_dir;
  if (auto *routes_panel = qobject_cast<BPRoutesPanel *>(parent)) {
    routes_dir = routes_panel->getRoutesDir();
  }

  videoController = std::make_unique<VideoController>(routes_dir, this);
  watchdog = std::make_unique<WatchdogDetector>(kWatchdogTimeoutMs, this);

  QObject::connect(videoController.get(), &VideoController::frameReady,
                   this, &BPRouteVideoDialog::handleFrameReady, Qt::QueuedConnection);
  QObject::connect(videoController.get(), &VideoController::stateChanged,
                   this, &BPRouteVideoDialog::handlePlaybackState, Qt::QueuedConnection);
  QObject::connect(videoController.get(), &VideoController::positionChanged,
                   this, &BPRouteVideoDialog::handlePositionChanged, Qt::QueuedConnection);
  QObject::connect(videoController.get(), &VideoController::bufferLevel,
                   this, &BPRouteVideoDialog::handleBufferLevel, Qt::QueuedConnection);
  QObject::connect(videoController.get(), &VideoController::segmentChanged,
                   this, &BPRouteVideoDialog::handleSegmentChanged, Qt::QueuedConnection);
  QObject::connect(videoController.get(), &VideoController::error,
                   this, &BPRouteVideoDialog::handleError, Qt::QueuedConnection);
  QObject::connect(videoController.get(), &VideoController::metricsUpdated,
                   this, &BPRouteVideoDialog::handlePlaybackMetrics, Qt::QueuedConnection);

  QObject::connect(watchdog.get(), &WatchdogDetector::watchdogWarning, this, [this](int blocked_ms) {
    if (playStatusLabel) {
      playStatusLabel->setText(QString("UI stalled (%1 ms)").arg(blocked_ms));
    }
  });

  // Initialize slider range based on estimated route duration (60s per segment)
  if (routeInfo.segments > 0) {
    totalDuration = routeInfo.segments * 60 * 1000;  // milliseconds
    positionSlider->setRange(0, static_cast<int>(totalDuration));
    BPLog::bpDebugVideo() << "[bp.video.dialog] Constructor | Set initial slider range: 0 - " << totalDuration << " ms" << std::endl;
  }

  QTimer::singleShot(0, this, [this]() { handleRouteLoaded(); });
}

BPRouteVideoDialog::~BPRouteVideoDialog() {
  if (videoController) {
    videoController->stop();
  }
}

void BPRouteVideoDialog::setupUI() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  setupFullWidthHeader();
  layout->addWidget(headerWidget);

  auto *content_widget = new QWidget(this);
  auto *content_layout = new QHBoxLayout(content_widget);
  content_layout->setContentsMargins(0, 0, 0, 0);
  content_layout->setSpacing(0);

  setupVideoDisplay();
  content_layout->addWidget(videoContainer, 85);

  setupCameraPanel();
  content_layout->addWidget(cameraPanel, 15);

  layout->addWidget(content_widget, 1);
}

void BPRouteVideoDialog::setupFullWidthHeader() {
  headerWidget = new QWidget(this);
  headerWidget->setFixedHeight(120);
  headerWidget->setStyleSheet("background: #202020;");

  auto *header_layout = new QHBoxLayout(headerWidget);
  header_layout->setContentsMargins(40, 15, 40, 15);
  header_layout->setSpacing(30);

  closeButton = new QPushButton("✕", headerWidget);
  closeButton->setFixedSize(90, 90);
  closeButton->setStyleSheet(R"(
    QPushButton {
      background: #444444;
      border: 2px solid #666666;
      border-radius: 45px;
      font-size: 50px;
      color: #FFD700;
      font-weight: bold;
    }
    QPushButton:hover { background: #555555; border: 2px solid #FFD700; }
    QPushButton:pressed { background: #333333; }
  )");
  QObject::connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

  auto *title_layout = new QVBoxLayout;
  title_layout->setSpacing(5);

  // Main title - show the date and time with comprehensive fallback logic
  QString display_date = routeInfo.displayDate;
  if (display_date.isEmpty()) {
    display_date = routeInfo.dateTime.toString("MMMM d, yyyy");
  }
  // Fix for empty or invalid date
  if (display_date.contains("0th") || display_date == "0, 0" || !routeInfo.dateTime.isValid()) {
    display_date = QDateTime::currentDateTime().toString("MMMM d, yyyy");
  }

  // Add time to the date - use time extraction priority for better reliability
  QString display_time;

  // Priority 1: humanTime from route info (should be the best preformatted version)
  if (!routeInfo.humanTime.isEmpty()) {
    display_time = routeInfo.humanTime;
    BPLog::bpDebugVideo() << "[bp.video.dialog] Time extraction | Using humanTime: " << display_time.toStdString() << std::endl;
  }
  // Priority 2: Direct dateTime formatting if humanTime is empty or the same as timestamp
  else if (routeInfo.dateTime.isValid()) {
    display_time = routeInfo.dateTime.toString("h:mm AP");
    BPLog::bpDebugVideo() << "[bp.video.dialog] Time extraction | Using dateTime formatted: " << display_time.toStdString() << std::endl;
  }
  // Priority 3: Try original timestamp if dateTime isn't valid
  else if (!routeInfo.timestamp.isEmpty()) {
    display_time = routeInfo.timestamp;
    BPLog::bpDebugVideo() << "[bp.video.dialog] Time extraction | Using timestamp: " << display_time.toStdString() << std::endl;
  }
  // Priority 4: Extract from route name (format: YYYY-MM-DD--HH-MM-SS) as last resort
  else {
    BPLog::bpDebugVideo() << "[bp.video.dialog] Time extraction | Parsing from routeBaseName: " << routeBaseName.toStdString() << std::endl;
    QStringList parts = routeBaseName.split("--");
    BPLog::bpDebugVideo() << "[bp.video.dialog] Time extraction | Split parts count: " << parts.size() << std::endl;
    if (parts.size() >= 2) {
      QString timePart = parts[1];
      BPLog::bpDebugVideo() << "[bp.video.dialog] Time extraction | Time part: " << timePart.toStdString() << std::endl;
      QStringList timeComponents = timePart.split("-");
      BPLog::bpDebugVideo() << "[bp.video.dialog] Time extraction | Time components count: " << timeComponents.size() << std::endl;
      if (timeComponents.size() >= 3) {
        QString hour = timeComponents[0];
        QString minute = timeComponents[1];
        int hourInt = hour.toInt();
        QString ampm = (hourInt >= 12) ? "PM" : "AM";
        if (hourInt > 12) hourInt -= 12;
        if (hourInt == 0) hourInt = 12;
        display_time = QString("%1:%2 %3").arg(hourInt).arg(minute, 2, '0').arg(ampm);
        BPLog::bpDebugVideo() << "[bp.video.dialog] Time extraction | Parsed time: " << display_time.toStdString() << std::endl;
      }
    }
  }

  // Final fallback
  if (display_time.isEmpty()) {
    display_time = "Unknown Time";
    BPLog::bpDebugVideo() << "[bp.video.dialog] Time extraction | All methods failed, using fallback" << std::endl;
  }

  BPLog::bpDebugVideo() << "[bp.video.dialog] setupFullWidthHeader | Route timestamp: " << routeInfo.timestamp.toStdString()
            << " | DateTime: " << routeInfo.dateTime.toString().toStdString()
            << " | Final displayTime: " << display_time.toStdString() << std::endl;

  QString fullTitle = QString("%1 at %2").arg(display_date, display_time);

  routeTitle = new QLabel(fullTitle, headerWidget);
  routeTitle->setStyleSheet("font-size: 44px; font-weight: 600; color: white; background: transparent;");

  auto *subtitle_label = new QLabel(routeBaseName, headerWidget);
  subtitle_label->setStyleSheet("font-size: 32px; color: #cccccc; background: transparent;");

  title_layout->addWidget(routeTitle);
  title_layout->addWidget(subtitle_label);

  auto *right_layout = new QVBoxLayout;
  right_layout->setSpacing(5);

  auto *size_label = new QLabel(routeInfo.size, headerWidget);
  size_label->setStyleSheet("font-size: 40px; color: #2196F3; font-weight: 600; background: transparent;");
  size_label->setAlignment(Qt::AlignRight);

  segmentLabel = new QLabel("Loading segments...", headerWidget);
  segmentLabel->setStyleSheet("font-size: 28px; color: #888; font-weight: 500; background: transparent;");
  segmentLabel->setAlignment(Qt::AlignRight);

  right_layout->addWidget(size_label);
  right_layout->addWidget(segmentLabel);

  header_layout->addWidget(closeButton);
  header_layout->addLayout(title_layout, 1);
  header_layout->addLayout(right_layout);
}

void BPRouteVideoDialog::setupVideoDisplay() {
  videoContainer = new QWidget(this);
  videoContainer->setStyleSheet("background: #000000;");

  auto *video_layout = new QVBoxLayout(videoContainer);
  video_layout->setContentsMargins(0, 0, 0, 0);
  video_layout->setSpacing(0);

  videoDisplay = new BPVideoWidget(videoContainer);
  videoDisplay->setMinimumSize(1400, 900);
  videoDisplay->setBackgroundColor(QColor("#000000"));
  videoDisplay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  QObject::connect(videoDisplay, &BPVideoWidget::clicked, this, &BPRouteVideoDialog::onVideoTap);

  video_layout->addWidget(videoDisplay, 1);

  setupOverlayControls();

  sliderContainer = new QWidget(videoContainer);
  sliderContainer->setObjectName("sliderContainer");
  sliderContainer->setStyleSheet(R"(
    QWidget#sliderContainer {
      background: rgba(0, 0, 0, 200);
      border: 2px solid rgba(255, 255, 255, 0.2);
      border-radius: 45px;
    }
  )");

  auto *slider_layout = new QHBoxLayout(sliderContainer);
  slider_layout->setContentsMargins(30, 20, 30, 10);
  slider_layout->setSpacing(15);

  positionSlider = new QSlider(Qt::Horizontal, sliderContainer);
  positionSlider->setFixedHeight(100);
  positionSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  positionSlider->setStyleSheet(R"(
    QSlider { background: transparent; }
    QSlider::groove:horizontal {
      background: rgba(255, 255, 255, 30);
      height: 24px; border-radius: 12px; margin: 38px 0;
    }
    QSlider::sub-page:horizontal {
      background: white; height: 24px; border-radius: 12px; margin: 38px 0;
    }
    QSlider::handle:horizontal {
      background: white; width: 100px; height: 100px; border-radius: 50px;
      border: 6px solid rgba(0, 0, 0, 0.1); margin: -38px 0;
    }
    QSlider::handle:horizontal:pressed,
    QSlider::handle:horizontal:hover {
      background: #2196F3; width: 100px; height: 100px; border-radius: 50px;
      border: 6px solid rgba(33, 150, 243, 0.8); margin: -38px 0;
    }
  )");

  timeLabel = new QLabel("0:00 / 0:00", sliderContainer);
  timeLabel->setFixedWidth(200);
  timeLabel->setAlignment(Qt::AlignCenter);
  timeLabel->setStyleSheet("color: white; font-size: 32px; font-weight: 600; background: transparent;");

  slider_layout->addWidget(positionSlider, 1);
  slider_layout->addWidget(timeLabel);

  sliderContainer->setFixedHeight(200);
  sliderContainer->setAttribute(Qt::WA_StyledBackground, true);
  sliderContainer->show();  // Show initially like the original
  sliderContainer->raise();

  // Create raised tab-style play status label (positioned above slider)
  playStatusLabel = new QLabel("Idle", videoContainer);
  playStatusLabel->setAlignment(Qt::AlignCenter);
  playStatusLabel->setFixedSize(600, 70);
  playStatusLabel->setStyleSheet(R"(
    QLabel {
      background: rgba(0, 0, 0, 220);
      color: #FFFFFF;
      font-size: 32px;
      font-weight: 700;
      border: 3px solid rgba(255, 255, 255, 0.3);
      border-bottom: none;
      border-top-left-radius: 20px;
      border-top-right-radius: 20px;
      padding: 8px;
    }
  )");
  playStatusLabel->show();
  playStatusLabel->raise();

  // CRITICAL ISSUE #2: Scrubbing spams worker thread with seek requests
  // WORKING: Slider interaction and time label updates
  // NOT WORKING:
  //   1. valueChanged emits constantly during drag (every pixel moved)
  //   2. 100ms throttle still sends ~10 seeks per second during scrubbing
  //   3. Each seek can trigger segment switches and cache invalidation
  //   4. Worker thread gets flooded, UI becomes unresponsive
  QObject::connect(positionSlider, &QSlider::sliderPressed, this, [this]() {
    isSeeking = true;
    watchdog->reset();
    if (videoController) {
      videoController->recordUiActivity();
    }
  });
  QObject::connect(positionSlider, &QSlider::sliderReleased, this, [this]() {
    isSeeking = false;
    handleSeekRequested(positionSlider->value());  // Only seek on release
    if (videoController) {
      videoController->recordUiActivity();
    }
  });
  QObject::connect(positionSlider, &QSlider::valueChanged, this, [this](int value) {
    updateTimeLabel(value, totalDuration);

    // ISSUE #2: This fires CONSTANTLY during scrubbing (every pixel)
    // Throttled to 100ms but still floods worker thread with seeks
    if (isSeeking) {
      // Throttle seek requests during drag to prevent spam
      static qint64 lastSeekTime = 0;
      qint64 currentTime_ms = QDateTime::currentMSecsSinceEpoch();
      if (currentTime_ms - lastSeekTime > 100) {
        handleSeekRequested(value);  // ❌ SPAMS worker thread ~10/sec
        lastSeekTime = currentTime_ms;
      }
    }

    if (videoController) {
      videoController->recordUiActivity();
    }
  });

  QTimer::singleShot(0, this, [this]() { updateOverlayPosition(); });
}

void BPRouteVideoDialog::setupOverlayControls() {
  controlsWidget = new QWidget(videoDisplay);
  controlsWidget->setStyleSheet("background: transparent;");
  controlsWidget->setFixedSize(700, 200);

  auto *layout = new QHBoxLayout(controlsWidget);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(80);
  layout->setAlignment(Qt::AlignCenter);

  auto rewind_button = new MediaControlButton(MediaControlButton::RewindArrow, controlsWidget);
  rewind_button->setFixedSize(150, 150);
  QObject::connect(rewind_button, &QPushButton::clicked, this, &BPRouteVideoDialog::seekBackward);

  playPauseButton = new MediaControlButton(MediaControlButton::Play, controlsWidget);
  playPauseButton->setFixedSize(200, 200);
  QObject::connect(playPauseButton, &QPushButton::clicked, this, &BPRouteVideoDialog::togglePlayback);

  auto forward_button = new MediaControlButton(MediaControlButton::ForwardArrow, controlsWidget);
  forward_button->setFixedSize(150, 150);
  QObject::connect(forward_button, &QPushButton::clicked, this, &BPRouteVideoDialog::seekForward);

  layout->addWidget(rewind_button);
  layout->addWidget(playPauseButton);
  layout->addWidget(forward_button);

  fullscreenToggleButton = new QPushButton(videoDisplay);
  fullscreenToggleButton->setFixedSize(120, 120);
  fullscreenToggleButton->move(20, 20);

  if (!fullscreenIcon.isNull()) {
    fullscreenToggleButton->setIcon(QIcon(fullscreenIcon));
    fullscreenToggleButton->setIconSize(QSize(80, 80));
    fullscreenToggleButton->setStyleSheet(R"(
      QPushButton {
        background: rgba(0, 0, 0, 200);
        border: none;
        border-radius: 60px;
      }
      QPushButton:hover {
        background: rgba(0, 0, 0, 240);
      }
      QPushButton:pressed {
        background: rgba(33, 150, 243, 200);
      }
    )");
  } else {
    fullscreenToggleButton->setText("⛶");
    fullscreenToggleButton->setStyleSheet(R"(
      QPushButton {
        background: rgba(0, 0, 0, 200);
        border: none;
        border-radius: 60px;
        color: white;
        font-size: 50px;
        font-weight: bold;
      }
      QPushButton:hover {
        background: rgba(0, 0, 0, 240);
      }
      QPushButton:pressed {
        background: rgba(33, 150, 243, 200);
      }
    )");
  }
  fullscreenToggleButton->setToolTip("Enter fullscreen");
  QObject::connect(fullscreenToggleButton, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleFullscreen);
  fullscreenToggleButton->show();  // Show initially since slider container is shown
  fullscreenToggleButton->raise();
}

void BPRouteVideoDialog::setupCameraPanel() {
  cameraPanel = new QWidget;
  cameraPanel->setStyleSheet("background: #2a2a2a; border-left: 1px solid #444444;");

  auto *panelLayout = new QVBoxLayout(cameraPanel);
  panelLayout->setContentsMargins(20, 20, 20, 20);
  panelLayout->setSpacing(15);

  auto *cameraTitle = new QLabel("Camera Views", cameraPanel);
  cameraTitle->setStyleSheet("font-size: 36px; font-weight: 600; color: white; margin-bottom: 10px;");
  panelLayout->addWidget(cameraTitle);

  QButtonGroup *cameraGroup = new QButtonGroup(this);

  auto make_button = [this, cameraGroup](const QString &label, const QString &key) {
    auto *button = new QPushButton(label, cameraPanel);
    button->setFixedHeight(80);
    button->setCheckable(true);
    button->setStyleSheet(buttonStyle("36px"));
    cameraGroup->addButton(button);
    QObject::connect(button, &QPushButton::clicked, this, [this, key]() { switchCamera(key); });
    return button;
  };

  frontCamButton = nullptr;
  if (routeInfo.hasFrontHQVideo) {
    frontCamButton = make_button("Front Camera", "front");
  } else if (routeInfo.hasFrontLQVideo) {
    frontCamButton = make_button("Front Camera (LQ)", "lq");
  }
  if (frontCamButton) panelLayout->addWidget(frontCamButton);

  wideCamButton = routeInfo.hasWideVideo ? make_button("Wide Camera", "wide") : nullptr;
  if (wideCamButton) panelLayout->addWidget(wideCamButton);

  driverCamButton = routeInfo.hasDriverHQVideo ? make_button("Driver Camera", "driver") : nullptr;
  if (driverCamButton) panelLayout->addWidget(driverCamButton);

  lqCamButton = routeInfo.hasFrontLQVideo && !frontCamButton ? make_button("Front LQ", "lq") : nullptr;
  if (lqCamButton) panelLayout->addWidget(lqCamButton);

  if (frontCamButton) {
    frontCamButton->setChecked(true);
    currentCameraType = frontCamButton->text().contains("(LQ)") ? "lq" : "front";
  } else if (wideCamButton) {
    wideCamButton->setChecked(true);
    currentCameraType = "wide";
  } else if (driverCamButton) {
    driverCamButton->setChecked(true);
    currentCameraType = "driver";
  }

  panelLayout->addStretch(1);

  setupActionButtons(panelLayout);
  updateCameraButtonStates();
}

void BPRouteVideoDialog::setupActionButtons(QVBoxLayout *parentLayout) {
  auto *actionsTitle = new QLabel("Actions", cameraPanel);
  actionsTitle->setStyleSheet("font-size: 36px; font-weight: 600; color: white; margin-bottom: 15px; margin-top: 20px;");
  parentLayout->addWidget(actionsTitle);

  auto *actionButtonsContainer = new QWidget(cameraPanel);
  auto *buttonLayout = new QHBoxLayout(actionButtonsContainer);
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->setSpacing(15);
  buttonLayout->setAlignment(Qt::AlignCenter);

  starButton = new QPushButton(cameraPanel);
  starButton->setFixedSize(100, 100);
  starButton->setText(routeInfo.isStarred ? "★" : "☆");
  starButton->setStyleSheet(R"(
    QPushButton {
      background: #444444;
      border: 3px solid #666666;
      border-radius: 50px;
      font-size: 60px;
      color: #FFD700;
      font-weight: bold;
    }
    QPushButton:hover { background: #555555; border: 3px solid #FFD700; }
    QPushButton:pressed { background: #333333; }
  )");
  QObject::connect(starButton, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleStar);

  deleteButton = new QPushButton("🗑", cameraPanel);
  deleteButton->setFixedSize(100, 100);
  deleteButton->setStyleSheet(R"(
    QPushButton {
      background: #d32f2f;
      border: 3px solid #b71c1c;
      border-radius: 50px;
      font-size: 50px;
      color: white;
      font-weight: bold;
    }
    QPushButton:hover { background: #c62828; border: 3px solid #FFD700; }
    QPushButton:pressed { background: #b71c1c; }
  )");
  QObject::connect(deleteButton, &QPushButton::clicked, this, &BPRouteVideoDialog::deleteRoute);

  buttonLayout->addWidget(starButton);
  buttonLayout->addWidget(deleteButton);

  parentLayout->addWidget(actionButtonsContainer);
}

void BPRouteVideoDialog::attachInputHandlers() {}

QString BPRouteVideoDialog::buttonStyle(const QString &size) {
  return QString(R"(
    QPushButton {
      background: rgba(33, 150, 243, 180);
      color: white;
      font-size: %1;
      border: 3px solid #1976D2;
      border-radius: 16px;
      font-weight: bold;
    }
    QPushButton:pressed {
      background: rgba(25, 118, 210, 220);
    }
    QPushButton:hover {
      background: rgba(30, 136, 229, 200);
      border: 3px solid #FFD700;
    }
  )").arg(size);
}

void BPRouteVideoDialog::handleRouteLoaded() {
  showLoadingIndicator();
  if (videoController) {
    videoController->setRoute(routeBaseName);
    videoController->setCamera(cameraFromString(currentCameraType));
    // Auto-start playback after loading like original
    videoController->play();
  }
}

// CRITICAL ISSUE #7: UI thread blocking during frame display
// WORKING: Frame display and position updates
// NOT WORKING:
//   1. displayFrame() calls makeCurrent/doneCurrent which can block 50-100ms
//   2. Blocking delays watchdog reset, triggers false "UI stalled" warnings
//   3. If this takes >50ms, next frame pump gets dropped (re-entry protection)
//   4. Causes stuttering playback when GPU is busy
void BPRouteVideoDialog::handleFrameReady(VisionBuf *buf, int width, int height, int64_t timestamp_ms) {
  watchdog->reset();
  hideLoadingIndicator();

  currentPosition = timestamp_ms;
  if (!isSeeking) {
    positionSlider->blockSignals(true);
    positionSlider->setValue(static_cast<int>(timestamp_ms));
    positionSlider->blockSignals(false);
  }

  // ISSUE #7: displayFrame() does SYNCHRONOUS OpenGL operations on UI thread
  // makeCurrent/doneCurrent can block waiting for GPU, causing 50-100ms stalls
  // If this takes >50ms, next frame pump will be dropped due to re-entry protection
  videoDisplay->displayFrame(buf, width, height);  // ❌ CAN BLOCK UI THREAD
  updateTimeLabel(currentPosition, totalDuration);

  if (videoController) {
    videoController->releaseFrame(buf);
  }
}

void BPRouteVideoDialog::handlePlaybackState(VideoState state) {
  watchdog->reset();

  switch (state) {
    case VideoState::kPlaying:
      isPlaying = true;
      playPauseButton->setIconType(MediaControlButton::Pause);
      if (playStatusLabel) {
        playStatusLabel->setText("Playing");
      }
      // Start overlay fade timer to hide controls during auto-play
      if (overlayFadeTimer) {
        overlayFadeTimer->start();
      }
      break;
    case VideoState::kPaused:
      isPlaying = false;
      playPauseButton->setIconType(MediaControlButton::Play);
      if (playStatusLabel) {
        playStatusLabel->setText("Paused");
      }
      // Stop overlay fade timer and show controls when paused
      if (overlayFadeTimer) {
        overlayFadeTimer->stop();
      }
      showOverlayControls();
      break;
    case VideoState::kLoading:
    case VideoState::kBuffering:
      showLoadingIndicator();
      break;
    case VideoState::kSeeking:
      showLoadingIndicator();
      if (playStatusLabel) {
        playStatusLabel->setText(QString("Seeking to %1...").arg(formatPlaybackTime(currentPosition)));
      }
      break;
    case VideoState::kEndOfRoute:
      isPlaying = false;
      playPauseButton->setIconType(MediaControlButton::Play);
      if (playStatusLabel) {
        playStatusLabel->setText("End of route");
      }
      break;
    case VideoState::kIdle:
    case VideoState::kError:
      hideLoadingIndicator();
      break;
  }

  updatePlaybackControls();
}

void BPRouteVideoDialog::handlePositionChanged(int64_t position_ms, int64_t duration_ms) {
  watchdog->reset();
  currentPosition = position_ms;
  totalDuration = duration_ms;

  positionSlider->blockSignals(true);
  positionSlider->setMaximum(static_cast<int>(duration_ms));
  if (!isSeeking) {
    positionSlider->setValue(static_cast<int>(position_ms));
  }
  positionSlider->blockSignals(false);

  updateTimeLabel(position_ms, duration_ms);
}

void BPRouteVideoDialog::handleBufferLevel(float level) {
  watchdog->reset();
  if (!playStatusLabel) {
    return;
  }
  const QString label = currentSegmentIndex >= 0
                            ? QString("Segment %1").arg(currentSegmentIndex + 1)
                            : QString("Buffer");
  playStatusLabel->setText(QString("%1: %2%").arg(label, QString::number(level, 'f', 1)));
}

void BPRouteVideoDialog::handleSegmentChanged(int current_segment, int total_segments) {
  watchdog->reset();
  currentSegmentIndex = current_segment;
  totalSegments = total_segments;
  segmentLabel->setText(QString("Segment %1 / %2").arg(current_segment + 1).arg(total_segments));
}

void BPRouteVideoDialog::handleError(const QString &message) {
  watchdog->reset();
  if (!playStatusLabel) {
      return;
    }
  playStatusLabel->setText(message);
  showLoadingIndicator();
}

void BPRouteVideoDialog::handleSeekRequested(int64_t position_ms) {
  if (!videoController) {
    return;
  }
  watchdog->reset();
  showLoadingIndicator();
  if (playStatusLabel) {
    playStatusLabel->setText(QString("Seeking to %1...").arg(formatPlaybackTime(position_ms)));
  }
  videoController->seekToMs(position_ms);
}

void BPRouteVideoDialog::handlePlaybackMetrics(int64_t buffered_ms, int total_frames) {
  Q_UNUSED(total_frames);
  if (!playStatusLabel) {
    return;
  }

  const double buffered_s = static_cast<double>(buffered_ms) / 1000.0;
  playStatusLabel->setText(QString("Buffered %1 s | %2 frames")
                               .arg(QString::number(buffered_s, 'f', 2))
                               .arg(total_frames));
}

void BPRouteVideoDialog::togglePlayback() {
  watchdog->reset();
  if (!videoController) {
    return;
  }
  if (isPlaying) {
    videoController->pause();
    } else {
    videoController->play();
  }
}

void BPRouteVideoDialog::seekForward() {
  watchdog->reset();
  const qint64 next = qMin(currentPosition + kSeekStepMs, totalDuration);
  positionSlider->setValue(static_cast<int>(next));
  handleSeekRequested(next);
}

void BPRouteVideoDialog::seekBackward() {
  watchdog->reset();
  const qint64 next = qMax<qint64>(0, currentPosition - kSeekStepMs);
  positionSlider->setValue(static_cast<int>(next));
  handleSeekRequested(next);
}

void BPRouteVideoDialog::toggleFullscreen() {
  watchdog->reset();
  isFullscreen = !isFullscreen;

  if (isFullscreen) {
    // Hide camera panel AND header widget for true fullscreen
    if (cameraPanel) cameraPanel->hide();
    if (headerWidget) headerWidget->hide();

    // Update fullscreen toggle button to show exit icon
    if (fullscreenToggleButton) {
      if (!minimizeIcon.isNull()) {
        fullscreenToggleButton->setIcon(QIcon(minimizeIcon));
        fullscreenToggleButton->setText("");  // Clear text if icon loads
        fullscreenToggleButton->setToolTip("Exit fullscreen");
      } else {
        fullscreenToggleButton->setText("◱");
        fullscreenToggleButton->setIcon(QIcon());  // Clear icon
        fullscreenToggleButton->setToolTip("Exit fullscreen");
      }
    }

    // Make video fill entire container
    videoDisplay->setGeometry(0, 0, videoContainer->width(), videoContainer->height());
  } else {
    // Restore normal view
    if (cameraPanel) cameraPanel->show();
    if (headerWidget) headerWidget->show();

    // Update fullscreen toggle button to show enter icon
    if (fullscreenToggleButton) {
      if (!fullscreenIcon.isNull()) {
        fullscreenToggleButton->setIcon(QIcon(fullscreenIcon));
        fullscreenToggleButton->setText("");  // Clear text if icon loads
        fullscreenToggleButton->setToolTip("Enter fullscreen");
      } else {
        fullscreenToggleButton->setText("⛶");
        fullscreenToggleButton->setIcon(QIcon());  // Clear icon
        fullscreenToggleButton->setToolTip("Enter fullscreen");
      }
    }

    // Restore normal video size
    videoDisplay->setGeometry(0, 0, videoContainer->width(), videoContainer->height());
  }

  // Update overlay position for new view
    updateOverlayPosition();

    // Force an additional update with slight delay to ensure videoDisplay has the new geometry
    QTimer::singleShot(10, [this]() {
      updateOverlayPosition();
  });
}

void BPRouteVideoDialog::switchCamera(const QString &camera_type) {
  watchdog->reset();
  currentCameraType = camera_type;
    updateCameraButtonStates();
  if (videoController) {
    videoController->setCamera(cameraFromString(camera_type));
  }
}

void BPRouteVideoDialog::deleteRoute() {
  watchdog->reset();
  // TODO: integrate route deletion workflow.
  BPLog::bpWarn() << "[bp.video.dialog] Delete route requested (TODO)" << std::endl;
}

void BPRouteVideoDialog::toggleStar() {
  watchdog->reset();
  routeInfo.isStarred = !routeInfo.isStarred;
  if (starButton) {
    starButton->setText(routeInfo.isStarred ? "★" : "☆");
  }

  if (auto *routes_panel = qobject_cast<BPRoutesPanel *>(parentWidget())) {
    routes_panel->setRouteStarred(routeBaseName, routeInfo.isStarred);
  }
}

void BPRouteVideoDialog::keepDisplayAwake() {
  watchdog->reset();
  Hardware::set_display_power(true);
}

void BPRouteVideoDialog::showOverlayControls() {
  controlsVisible = true;

  if (controlsWidget) {
    controlsWidget->show();
  }

  if (sliderContainer) {
    sliderContainer->show();
  }

  if (playStatusLabel) {
    playStatusLabel->show();
  }

  if (fullscreenToggleButton) {
    fullscreenToggleButton->show();
  }

  updateOverlayPosition();

  // Restart fade timer to hide again after 3 seconds if playing
  if (overlayFadeTimer && isPlaying) {
    overlayFadeTimer->stop();
    overlayFadeTimer->start();
  }
}

void BPRouteVideoDialog::hideOverlayControls() {
  if (isSeeking) {
    return;
  }

  controlsVisible = false;

  if (controlsWidget) {
    controlsWidget->hide();
  }

  if (sliderContainer) {
    sliderContainer->hide();
  }

  if (playStatusLabel) {
    playStatusLabel->hide();
  }

  if (fullscreenToggleButton) {
    fullscreenToggleButton->hide();
  }
}

void BPRouteVideoDialog::onVideoTap() {
  if (!controlsVisible) {
    // Show controls if they're hidden
    showOverlayControls();
  } else {
    // Toggle play/pause if controls are already visible
    togglePlayback();
  }
}

void BPRouteVideoDialog::updateOverlayPosition() {
  if (!videoDisplay) return;

  const int width = videoDisplay->width();
  const int height = videoDisplay->height();

  if (controlsWidget) {
    controlsWidget->move(
      (width - controlsWidget->width()) / 2,
      (height - controlsWidget->height()) / 2
    );
  }

  if (sliderContainer) {
    const int padding = isFullscreen ? 20 : 40;
    const int containerWidth = width - (2 * padding);
    sliderContainer->resize(containerWidth, sliderContainer->height());
    sliderContainer->move(padding, height - sliderContainer->height() - 40);
  }

  // Position play status label centered above slider container (raised tab style)
  if (playStatusLabel && sliderContainer) {
    const int sliderX = sliderContainer->x();
    const int sliderY = sliderContainer->y();
    const int sliderWidth = sliderContainer->width();

    // Center the label above the slider container
    const int labelX = sliderX + (sliderWidth - playStatusLabel->width()) / 2;
    const int labelY = sliderY - playStatusLabel->height() + 3; // Overlap by 3px to create tab effect

    playStatusLabel->move(labelX, labelY);
  }
}

void BPRouteVideoDialog::updateCameraButtonStates() {
  auto set_active = [](QPushButton *btn, bool active) {
    if (!btn) return;
    btn->setStyleSheet(active ?
                       "background: rgba(33, 150, 243, 255); color: white; font-size: 32px; border: 3px solid #FFD700; border-radius: 16px; font-weight: bold;"
                       :
                       "background: rgba(33, 150, 243, 180); color: white; font-size: 32px; border: 3px solid #1976D2; border-radius: 16px; font-weight: bold;");
  };

  set_active(frontCamButton, currentCameraType == "front");
  set_active(wideCamButton, currentCameraType == "wide");
  set_active(driverCamButton, currentCameraType == "driver");
  set_active(lqCamButton, currentCameraType == "lq");
}

void BPRouteVideoDialog::resetOverlayTimers() {
  if (!overlayFadeTimer) {
    return;
  }
  overlayFadeTimer->stop();
  overlayFadeTimer->start();
}

void BPRouteVideoDialog::updatePlaybackControls() {
  const bool enabled = (totalDuration > 0);
        if (playPauseButton) {
    playPauseButton->setEnabled(enabled);
  }
  if (positionSlider) {
    positionSlider->setEnabled(enabled);
  }
}

void BPRouteVideoDialog::resetPlaybackUi() {
  isPlaying = false;
  currentPosition = 0;
  totalDuration = 0;
  if (positionSlider) {
    positionSlider->setValue(0);
    positionSlider->setMaximum(0);
  }
  updateTimeLabel(0, 0);
        if (playPauseButton) {
    playPauseButton->setIconType(MediaControlButton::Play);
  }
}

void BPRouteVideoDialog::updateTimeLabel(qint64 position_ms, qint64 total_ms) {
  const QString current = formatPlaybackTime(position_ms);
  const QString total = total_ms > 0 ? formatPlaybackTime(total_ms) : "0:00";
  if (timeLabel) {
    timeLabel->setText(QString("%1 / %2").arg(current, total));
  }
}

void BPRouteVideoDialog::showLoadingIndicator() {
  if (!playStatusLabel) {
    return;
  }
  playStatusLabel->setText(currentSegmentIndex >= 0 ?
                           QString("Loading segment %1...").arg(currentSegmentIndex + 1) :
                           "Loading...");
}

void BPRouteVideoDialog::hideLoadingIndicator() {
  if (playStatusLabel) {
    playStatusLabel->setText(isPlaying ? "Playing" : "Idle");
  }
}

void BPRouteVideoDialog::keyPressEvent(QKeyEvent *event) {
  watchdog->reset();
  switch (event->key()) {
    case Qt::Key_Space:
      togglePlayback();
      return;
    case Qt::Key_Left:
      seekBackward();
      return;
    case Qt::Key_Right:
      seekForward();
      return;
    default:
      break;
  }
      BPDialogBase::keyPressEvent(event);
}

void BPRouteVideoDialog::showEvent(QShowEvent *event) {
  BPDialogBase::showEvent(event);
  if (keepAwakeTimer) {
  keepAwakeTimer->start();
  }
}

void BPRouteVideoDialog::hideEvent(QHideEvent *event) {
  BPDialogBase::hideEvent(event);
  if (keepAwakeTimer) {
    keepAwakeTimer->stop();
  }
  if (videoController) {
    videoController->pause();
  }
}

void BPRouteVideoDialog::resizeEvent(QResizeEvent *event) {
  BPDialogBase::resizeEvent(event);
  updateOverlayPosition();
}



