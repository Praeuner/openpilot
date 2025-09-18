// bp_enhanced_video_modal.cc
#include "bp_routes_panel.h"
#include "bp_ffmpeg_decoder.h"
#include "bp_panel_dialogs.h"
#include <QKeyEvent>
#include <QResizeEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QtConcurrent>
#include <QTouchEvent>
#include <QScroller>

#ifdef QCOM2
#include <wayland-client.h>
#include <wayland-util.h>
#include <qpa/qplatformnativeinterface.h>
#endif

// Enhanced Video Modal Implementation
BPEnhancedVideoModal::BPEnhancedVideoModal(const QString &routeBase, const BPRoutesPanel::RouteInfo &route, QWidget *parent)
    : QDialog(parent), m_routeBase(routeBase), m_route(route), m_currentCamera("fcamera.hevc") {

  // Set fullscreen window flags
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground, false);
  setAttribute(Qt::WA_AcceptTouchEvents, true);

  // Setup UI
  setupUI();

  // Load default camera (front camera)
  loadVideoFile(m_currentCamera);
}

BPEnhancedVideoModal::~BPEnhancedVideoModal() {
  if (hwDecoder) {
    hwDecoder->stop();
    delete hwDecoder;
  }
}

void BPEnhancedVideoModal::setupUI() {
  // Main layout
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Set background color
  setStyleSheet("background-color: #0f0f0f;");

  // Header with close button (80px height)
  QWidget *header = new QWidget;
  header->setFixedHeight(80);
  header->setStyleSheet("background-color: #1a1a1a; border-bottom: 2px solid #2196F3;");

  QHBoxLayout *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(30, 15, 30, 15);

  // Route info in header
  routeInfoLabel = new QLabel(QString("Route: %1 • %2").arg(m_route.timestamp, m_route.duration));
  routeInfoLabel->setStyleSheet("color: #2196F3; font-size: 36px; font-weight: 600;");

  // Close button (top right) - using BP button style
  closeButton = new QPushButton("✕");
  closeButton->setFixedSize(80, 80);
  closeButton->setStyleSheet(R"(
    QPushButton {
      background-color: #363636;
      color: white;
      font-size: 40px;
      font-weight: bold;
      border-radius: 40px;
      border: none;
    }
    QPushButton:hover {
      background-color: #F44336;
    }
    QPushButton:pressed {
      background-color: #D32F2F;
    }
  )");

  // Fullscreen toggle button - BP style
  fullscreenButton = new QPushButton("⛶");
  fullscreenButton->setFixedSize(80, 80);
  fullscreenButton->setStyleSheet(R"(
    QPushButton {
      background-color: #363636;
      color: white;
      font-size: 36px;
      border-radius: 40px;
      border: none;
    }
    QPushButton:hover {
      background-color: #404040;
    }
    QPushButton:pressed {
      background-color: #505050;
    }
  )");

  headerLayout->addWidget(routeInfoLabel);
  headerLayout->addStretch();
  headerLayout->addWidget(fullscreenButton);
  headerLayout->addSpacing(20);
  headerLayout->addWidget(closeButton);

  // Content area with split layout
  QWidget *contentArea = new QWidget;
  QHBoxLayout *contentLayout = new QHBoxLayout(contentArea);
  contentLayout->setContentsMargins(20, 20, 20, 20);
  contentLayout->setSpacing(20);

  // Left side: Video player container
  videoContainer = new QWidget;
  videoContainer->setStyleSheet("background-color: #000000; border-radius: 12px;");
  QVBoxLayout *videoLayout = new QVBoxLayout(videoContainer);
  videoLayout->setContentsMargins(0, 0, 0, 0);
  videoLayout->setSpacing(0);

  // Hardware video widget
  hwVideoWidget = new BPFFmpegVideoWidget;
  hwVideoWidget->setMinimumHeight(400);
  videoLayout->addWidget(hwVideoWidget, 1);

  // Video controls
  QWidget *controlsWidget = new QWidget;
  controlsWidget->setFixedHeight(100);
  controlsWidget->setStyleSheet("background-color: #1a1a1a; border-top: 1px solid #333;");

  QHBoxLayout *controlsLayout = new QHBoxLayout(controlsWidget);
  controlsLayout->setContentsMargins(20, 10, 20, 10);

  // Play/Pause button - BP primary button style
  playPauseButton = new QPushButton("⏸");
  playPauseButton->setFixedSize(100, 100);
  playPauseButton->setStyleSheet(R"(
    QPushButton {
      background-color: #2196F3;
      color: white;
      font-size: 48px;
      border-radius: 50px;
      border: none;
    }
    QPushButton:hover {
      background-color: #1E88E5;
    }
    QPushButton:pressed {
      background-color: #1976D2;
    }
    QPushButton:disabled {
      background-color: #202020;
      color: #666666;
    }
  )");

  // Position slider
  positionSlider = new QSlider(Qt::Horizontal);
  positionSlider->setStyleSheet(R"(
    QSlider::groove:horizontal {
      height: 8px;
      background: #333;
      border-radius: 4px;
    }
    QSlider::handle:horizontal {
      width: 20px;
      height: 20px;
      background: #2196F3;
      border-radius: 10px;
      margin-top: -6px;
      margin-bottom: -6px;
    }
    QSlider::sub-page:horizontal {
      background: #2196F3;
      border-radius: 4px;
    }
  )");

  // Time label
  timeLabel = new QLabel("00:00 / 00:00");
  timeLabel->setStyleSheet("color: white; font-size: 28px; min-width: 200px;");
  timeLabel->setAlignment(Qt::AlignCenter);

  controlsLayout->addWidget(playPauseButton);
  controlsLayout->addSpacing(20);
  controlsLayout->addWidget(positionSlider, 1);
  controlsLayout->addSpacing(20);
  controlsLayout->addWidget(timeLabel);

  videoLayout->addWidget(controlsWidget);

  // Right side: Camera selection panel (fixed width)
  cameraPanel = new QWidget;
  cameraPanel->setFixedWidth(250);
  cameraPanel->setStyleSheet("background-color: #1a1a1a; border-radius: 12px; padding: 20px;");

  QVBoxLayout *cameraPanelLayout = new QVBoxLayout(cameraPanel);
  cameraPanelLayout->setContentsMargins(20, 20, 20, 20);
  cameraPanelLayout->setSpacing(15);

  // Camera selection header
  QLabel *cameraHeader = new QLabel(tr("Camera View"));
  cameraHeader->setStyleSheet("color: #2196F3; font-size: 32px; font-weight: 600; padding-bottom: 10px;");
  cameraHeader->setAlignment(Qt::AlignCenter);
  cameraPanelLayout->addWidget(cameraHeader);

  // Camera buttons container
  cameraButtonLayout = new QVBoxLayout;
  cameraButtonLayout->setSpacing(12);

  // Create camera buttons
  createCameraButton(tr("Front Camera"), "fcamera.hevc", true);
  createCameraButton(tr("Wide Camera"), "ecamera.hevc", false);
  createCameraButton(tr("Driver Camera"), "dcamera.hevc", false);
  createCameraButton(tr("Low Quality"), "qcamera.ts", false);

  cameraPanelLayout->addLayout(cameraButtonLayout);
  cameraPanelLayout->addStretch();

  // Delete button at bottom of camera panel - BP danger button style
  deleteButton = new QPushButton(tr("🗑 Delete Route"));
  deleteButton->setMinimumHeight(80);
  deleteButton->setStyleSheet(R"(
    QPushButton {
      background-color: #F44336;
      color: white;
      font-size: 32px;
      font-weight: 600;
      border-radius: 40px;
      border: none;
      padding: 15px 30px;
    }
    QPushButton:hover {
      background-color: #D32F2F;
    }
    QPushButton:pressed {
      background-color: #B71C1C;
    }
    QPushButton:disabled {
      background-color: #202020;
      color: #666666;
    }
  )");
  cameraPanelLayout->addWidget(deleteButton);

  // Add panels to content layout (70/30 split)
  contentLayout->addWidget(videoContainer, 7);
  contentLayout->addWidget(cameraPanel, 3);

  // Add all to main layout
  mainLayout->addWidget(header);
  mainLayout->addWidget(contentArea, 1);

  // Connect signals
  connect(closeButton, &QPushButton::clicked, this, &BPEnhancedVideoModal::onCloseClicked);
  connect(fullscreenButton, &QPushButton::clicked, this, &BPEnhancedVideoModal::onFullscreenToggle);
  connect(playPauseButton, &QPushButton::clicked, this, &BPEnhancedVideoModal::togglePlayback);
  connect(deleteButton, &QPushButton::clicked, this, &BPEnhancedVideoModal::deleteRoute);

  // Setup hardware video decoder
  hwDecoder = new BPFFmpegDecoder(this);
  hwDecoder->setVideoOutput(hwVideoWidget);

  connect(hwDecoder, &BPFFmpegDecoder::positionChanged, this, &BPEnhancedVideoModal::updatePosition);
  connect(hwDecoder, &BPFFmpegDecoder::durationChanged, this, &BPEnhancedVideoModal::updateDuration);
  connect(hwDecoder, &BPFFmpegDecoder::errorOccurred, this, &BPEnhancedVideoModal::onDecoderError);

  // Custom slider handling for multi-segment seeking
  connect(positionSlider, &QSlider::sliderMoved, this, [this](int value) {
    if (m_segmentPaths.isEmpty()) {
      // Single segment, use normal seeking
      if (hwDecoder) {
        hwDecoder->seek(value);
      }
    } else {
      // Multi-segment, find target segment and position
      qint64 segmentDuration = m_totalDuration / m_segmentPaths.size();
      int targetSegment = value / segmentDuration;
      qint64 positionInSegment = value % segmentDuration;

      seekToSegment(targetSegment, positionInSegment);
    }
  });
}

void BPEnhancedVideoModal::createCameraButton(const QString &label, const QString &file, bool isDefault) {
  QPushButton *btn = new QPushButton(label);
  btn->setMinimumHeight(80);

  // BP button styling with selection state
  QString baseStyle = R"(
    QPushButton {
      background-color: %1;
      color: white;
      font-size: 32px;
      font-weight: 500;
      border-radius: 40px;
      border: none;
      text-align: center;
      padding: 15px 30px;
    }
    QPushButton:hover {
      background-color: %2;
    }
    QPushButton:pressed {
      background-color: %3;
    }
    QPushButton:disabled {
      background-color: #202020;
      color: #666666;
    }
  )";

  if (isDefault) {
    // Active/selected button - blue like BP primary
    btn->setStyleSheet(baseStyle.arg("#2196F3", "#1E88E5", "#1976D2"));
  } else {
    // Inactive button - standard BP button gray
    btn->setStyleSheet(baseStyle.arg("#363636", "#404040", "#505050"));
  }

  cameraButtons[file] = btn;
  cameraButtonLayout->addWidget(btn);

  connect(btn, &QPushButton::clicked, [this, file]() {
    switchCamera(file);
  });
}

void BPEnhancedVideoModal::switchCamera(const QString &cameraFile) {
  // Update button styles with BP styling
  for (auto it = cameraButtons.begin(); it != cameraButtons.end(); ++it) {
    QString baseStyle = R"(
      QPushButton {
        background-color: %1;
        color: white;
        font-size: 32px;
        font-weight: 500;
        border-radius: 40px;
        border: none;
        text-align: center;
        padding: 15px 30px;
      }
      QPushButton:hover {
        background-color: %2;
      }
      QPushButton:pressed {
        background-color: %3;
      }
      QPushButton:disabled {
        background-color: #202020;
        color: #666666;
      }
    )";

    if (it.key() == cameraFile) {
      // Active/selected button - blue like BP primary
      it.value()->setStyleSheet(baseStyle.arg("#2196F3", "#1E88E5", "#1976D2"));
    } else {
      // Inactive button - standard BP button gray
      it.value()->setStyleSheet(baseStyle.arg("#363636", "#404040", "#505050"));
    }
  }

  m_currentCamera = cameraFile;
  loadVideoFile(cameraFile);
}

void BPEnhancedVideoModal::loadVideoFile(const QString &videoFile) {
  m_currentCamera = videoFile;

  // Setup streaming concatenation for instant playback
  setupStreamingConcatenation(videoFile);
}

void BPEnhancedVideoModal::togglePlayback() {
  if (!hwDecoder) return;

  if (hwDecoder->isPlaying()) {
    // Pause current playback
    hwDecoder->pause();
    playPauseButton->setText("▶");

    // Stop segment timer to prevent automatic transitions
    if (m_segmentTimer) {
      m_segmentTimer->stop();
    }
  } else {
    // Resume or start playback
    hwDecoder->play();
    playPauseButton->setText("⏸");

    // If we're not playing and have segments, start from current position
    if (m_segmentPaths.isEmpty()) {
      // No segments loaded, setup streaming for current camera
      setupStreamingConcatenation(m_currentCamera);
    }
  }
}

void BPEnhancedVideoModal::updatePosition(qint64 position) {
  // Calculate total position across all segments
  qint64 totalPosition = m_segmentStartTime + position;
  positionSlider->setValue(totalPosition);
  updateTimeLabel();
}

void BPEnhancedVideoModal::updateDuration(qint64 duration) {
  // For first segment, estimate total duration based on number of segments
  if (m_currentSegmentIndex == 0 && !m_segmentPaths.isEmpty()) {
    m_totalDuration = duration * m_segmentPaths.size();
    positionSlider->setRange(0, m_totalDuration);
  }
  updateTimeLabel();
}

void BPEnhancedVideoModal::updateTimeLabel() {
  if (!hwDecoder) return;

  // Use total duration for multi-segment routes
  int totalDuration = (m_totalDuration > 0 ? m_totalDuration : hwDecoder->duration()) / 1000;
  int currentPosition = (m_segmentStartTime + hwDecoder->position()) / 1000;

  QString format = totalDuration >= 3600 ? "hh:mm:ss" : "mm:ss";
  QString timeText = QTime(0, 0).addSecs(currentPosition).toString(format) +
                     " / " +
                     QTime(0, 0).addSecs(totalDuration).toString(format);
  timeLabel->setText(timeText);
}

void BPEnhancedVideoModal::deleteRoute() {
  // Confirm deletion
  BPConfirmationDialog::ConfirmConfig config;
  config.title = tr("Delete Route");
  config.prompt = tr("Are you sure you want to delete this route?\n\nRoute: %1\nThis action cannot be undone.").arg(m_route.baseName);
  config.confirmText = tr("Delete");
  config.cancelText = tr("Cancel");
  config.confirmColor = "#F44336";

  auto confirmDialog = new BPConfirmationDialog(config, this);
  if (confirmDialog->exec() == QDialog::Accepted) {
    // Get routes directory
    QString routesDir;
#ifdef QCOM2
    routesDir = "/data/media/0/realdata";
#else
    routesDir = QDir::homePath() + "/comma_data/media/0/realdata";
#endif

    // Delete all segments for this route
    QDir baseDir(routesDir);
    QStringList segments = baseDir.entryList(QStringList() << m_routeBase + "--*", QDir::Dirs);

    bool success = true;
    for (const QString &segment : segments) {
      QDir segmentDir(baseDir.filePath(segment));
      if (!segmentDir.removeRecursively()) {
        success = false;
      }
    }

    if (success) {
      // Show success message
      BPConfirmationDialog::ConfirmConfig successConfig;
      successConfig.title = tr("Success");
      successConfig.prompt = tr("Route deleted successfully.");
      successConfig.confirmText = tr("OK");
      successConfig.confirmColor = "#4CAF50";
      BPConfirmationDialog::showMessage(successConfig, this);

      // Close the modal
      accept();
    } else {
      // Show error message
      BPConfirmationDialog::ConfirmConfig errorConfig;
      errorConfig.title = tr("Error");
      errorConfig.prompt = tr("Failed to delete route completely. Some files may remain.");
      errorConfig.confirmText = tr("OK");
      errorConfig.confirmColor = "#FF0000";
      BPConfirmationDialog::showMessage(errorConfig, this);
    }
  }
  delete confirmDialog;
}

void BPEnhancedVideoModal::onCloseClicked() {
  if (hwDecoder) {
    hwDecoder->stop();
  }

  // Clean up temporary files
  cleanupTempFiles();

  reject();
}

void BPEnhancedVideoModal::onFullscreenToggle() {
  m_isFullscreen = !m_isFullscreen;

  if (m_isFullscreen) {
    fullscreenButton->setText("⛶");
    showFullScreen();
  } else {
    fullscreenButton->setText("⛶");
    showNormal();
  }
}

void BPEnhancedVideoModal::setupFullscreen() {
  if (m_fullscreenApplied) return;
  m_fullscreenApplied = true;

#ifdef QCOM2
  // Set size for QCOM2 portrait mode
  setFixedSize(2160, 1080);
  show();

  // Apply rotation for portrait mode
  applyQCOM2Rotation();
#else
  // For desktop, use screen size
  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen) {
    QRect screenGeometry = screen->geometry();
    setGeometry(screenGeometry);
  }
#endif

  showFullScreen();
}

void BPEnhancedVideoModal::applyQCOM2Rotation() {
#ifdef QCOM2
  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  if (native && windowHandle()) {
    wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", windowHandle()));
    if (s) {
      // Rotate 270 degrees for portrait mode
      wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
      wl_surface_commit(s);
    }
  }
#endif
}

void BPEnhancedVideoModal::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);

  // Set up fullscreen on first show
  setupFullscreen();
}

void BPEnhancedVideoModal::keyPressEvent(QKeyEvent *event) {
  switch (event->key()) {
    case Qt::Key_Escape:
      onCloseClicked();
      break;
    case Qt::Key_Space:
      togglePlayback();
      break;
    case Qt::Key_F:
    case Qt::Key_F11:
      onFullscreenToggle();
      break;
    case Qt::Key_Left:
      seekRelative(-5000); // Rewind 5 seconds
      break;
    case Qt::Key_Right:
      seekRelative(5000); // Forward 5 seconds
      break;
    default:
      QDialog::keyPressEvent(event);
  }
}

void BPEnhancedVideoModal::resizeEvent(QResizeEvent *event) {
  QDialog::resizeEvent(event);

  // Adjust UI elements based on size if needed
  if (event->size().width() < 1400) {
    // Compact mode for smaller screens
    if (cameraPanel) {
      cameraPanel->setFixedWidth(200);
    }
  } else {
    // Normal mode
    if (cameraPanel) {
      cameraPanel->setFixedWidth(250);
    }
  }
}

// Helper functions for video concatenation
QString BPEnhancedVideoModal::prepareConcatenatedVideo(const QString &videoFile) {
  QString routesDir;
#ifdef QCOM2
  routesDir = "/data/media/0/realdata";
#else
  routesDir = QDir::homePath() + "/comma_data/media/0/realdata";
#endif

  // Create temp file list for concatenation
  QString tempListPath = QDir::tempPath() + "/" + m_routeBase + "_" + videoFile + "_concat.txt";
  QFile listFile(tempListPath);

  if (!listFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return QString();
  }

  QTextStream stream(&listFile);
  int segmentCount = 0;

  // Build list of video files
  QDir baseDir(routesDir);
  QStringList segments = baseDir.entryList(QStringList() << m_routeBase + "--*", QDir::Dirs);
  std::sort(segments.begin(), segments.end());

  for (const QString &segment : segments) {
    QString videoPath = QString("%1/%2/%3").arg(routesDir, segment, videoFile);
    if (QFile::exists(videoPath)) {
      stream << "file '" << videoPath << "'\n";
      segmentCount++;
    }
  }

  listFile.close();

  if (segmentCount == 0) {
    QFile::remove(tempListPath);
    return QString();
  }

  // Store temp file path for cleanup
  m_tempFiles.append(tempListPath);

  // Return the list file path - the hardware decoder should handle concat protocol
  return tempListPath;
}

int BPEnhancedVideoModal::getTotalSegments(const QString &routeBase) {
  QString routesDir;
#ifdef QCOM2
  routesDir = "/data/media/0/realdata";
#else
  routesDir = QDir::homePath() + "/comma_data/media/0/realdata";
#endif

  QDir baseDir(routesDir);
  QStringList segments = baseDir.entryList(QStringList() << routeBase + "--*", QDir::Dirs);
  return segments.size();
}

void BPEnhancedVideoModal::cleanupTempFiles() {
  for (const QString &tempFile : m_tempFiles) {
    QFile::remove(tempFile);
  }
  m_tempFiles.clear();
}

void BPEnhancedVideoModal::onDecoderError(const QString &error) {
  // Update UI to show error state
  playPauseButton->setText("✕");
  playPauseButton->setEnabled(false);

  // Show error message
  BPConfirmationDialog::ConfirmConfig config;
  config.title = tr("Playback Error");
  config.prompt = tr("Video playback error: %1").arg(error);
  config.confirmText = tr("OK");
  config.confirmColor = "#FF0000";
  BPConfirmationDialog::showMessage(config, this);
}

// Streaming concatenation implementation
void BPEnhancedVideoModal::setupStreamingConcatenation(const QString &videoFile) {
  // Get all available segments for this video type
  m_segmentPaths = getAvailableSegments(videoFile);

  if (m_segmentPaths.isEmpty()) {
    // Show error - no segments found
    BPConfirmationDialog::ConfirmConfig config;
    config.title = tr("Video Error");
    config.prompt = tr("No video segments found for: %1").arg(videoFile);
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    return;
  }

  // Reset playback state
  m_currentSegmentIndex = 0;
  m_totalDuration = 0;
  m_segmentStartTime = 0;

  // Setup segment transition timer
  if (!m_segmentTimer) {
    m_segmentTimer = new QTimer(this);
    connect(m_segmentTimer, &QTimer::timeout, this, &BPEnhancedVideoModal::playNextSegment);
  }

  // Connect to playback finished signal for seamless transitions
  connect(hwDecoder, &BPFFmpegDecoder::playbackFinished, this, &BPEnhancedVideoModal::onSegmentFinished, Qt::UniqueConnection);

  // Start playing the first segment immediately
  playNextSegment();
}

QVector<QString> BPEnhancedVideoModal::getAvailableSegments(const QString &videoFile) {
  QString routesDir;
#ifdef QCOM2
  routesDir = "/data/media/0/realdata";
#else
  routesDir = QDir::homePath() + "/comma_data/media/0/realdata";
#endif

  QVector<QString> segments;
  QDir baseDir(routesDir);
  QStringList segmentDirs = baseDir.entryList(QStringList() << m_routeBase + "--*", QDir::Dirs);

  // Sort segments by number to ensure correct order
  std::sort(segmentDirs.begin(), segmentDirs.end());

  for (const QString &segmentDir : segmentDirs) {
    QString videoPath = QString("%1/%2/%3").arg(routesDir, segmentDir, videoFile);
    if (QFile::exists(videoPath)) {
      segments.append(videoPath);
    }
  }

  return segments;
}

void BPEnhancedVideoModal::playNextSegment() {
  if (m_currentSegmentIndex >= m_segmentPaths.size()) {
    // All segments played, stop
    playPauseButton->setText("▶");
    return;
  }

  QString currentSegmentPath = m_segmentPaths[m_currentSegmentIndex];

  if (hwDecoder) {
    // Stop current playback
    hwDecoder->stop();

    // Initialize with the current segment
    if (hwDecoder->initialize(currentSegmentPath)) {
      // Successfully initialized, update UI
      playPauseButton->setText("⏸");
      playPauseButton->setEnabled(true);

      // Start playback immediately
      hwDecoder->play();

      std::cout << "Playing segment " << m_currentSegmentIndex + 1 << " of " << m_segmentPaths.size()
                << ": " << currentSegmentPath.toStdString() << std::endl;
    } else {
      // Failed to load this segment, try next one
      std::cout << "Failed to load segment: " << currentSegmentPath.toStdString() << std::endl;
      m_currentSegmentIndex++;

      // Use a single-shot timer to try the next segment
      QTimer::singleShot(100, this, &BPEnhancedVideoModal::playNextSegment);
    }
  }
}

void BPEnhancedVideoModal::onSegmentFinished() {
  // Move to next segment
  m_currentSegmentIndex++;

  // Update total duration tracking
  if (hwDecoder) {
    m_segmentStartTime += hwDecoder->duration();
  }

  // Play next segment immediately for seamless playback
  if (m_currentSegmentIndex < m_segmentPaths.size()) {
    // Small delay to ensure smooth transition
    QTimer::singleShot(50, this, &BPEnhancedVideoModal::playNextSegment);
  } else {
    // All segments finished
    playPauseButton->setText("▶");
    std::cout << "All segments completed for route: " << m_routeBase.toStdString() << std::endl;
  }
}

void BPEnhancedVideoModal::seekRelative(qint64 offsetMs) {
  if (!hwDecoder || m_segmentPaths.isEmpty()) return;

  qint64 currentTotalPosition = m_segmentStartTime + hwDecoder->position();
  qint64 newPosition = currentTotalPosition + offsetMs;

  // Clamp to valid range
  newPosition = qMax(0LL, qMin(newPosition, m_totalDuration));

  // Find which segment this position belongs to
  qint64 segmentDuration = m_totalDuration / m_segmentPaths.size(); // Approximate
  int targetSegment = newPosition / segmentDuration;
  qint64 positionInSegment = newPosition % segmentDuration;

  // Clamp segment index
  targetSegment = qMax(0, qMin(targetSegment, m_segmentPaths.size() - 1));

  if (targetSegment != m_currentSegmentIndex) {
    // Need to switch segments
    seekToSegment(targetSegment, positionInSegment);
  } else {
    // Seek within current segment
    hwDecoder->seek(hwDecoder->position() + offsetMs);
  }
}

void BPEnhancedVideoModal::seekToSegment(int segmentIndex, qint64 positionMs) {
  if (segmentIndex < 0 || segmentIndex >= m_segmentPaths.size()) return;

  // Update segment tracking
  m_currentSegmentIndex = segmentIndex;
  qint64 segmentDuration = m_totalDuration / m_segmentPaths.size(); // Approximate
  m_segmentStartTime = segmentIndex * segmentDuration;

  // Stop current playback
  if (hwDecoder) {
    hwDecoder->stop();
  }

  // Load and play the target segment
  QString targetSegmentPath = m_segmentPaths[segmentIndex];
  if (hwDecoder && hwDecoder->initialize(targetSegmentPath)) {
    hwDecoder->play();

    // Seek to position within the segment
    if (positionMs > 0) {
      QTimer::singleShot(100, [this, positionMs]() {
        if (hwDecoder) {
          hwDecoder->seek(positionMs);
        }
      });
    }

    std::cout << "Seeked to segment " << segmentIndex + 1 << " at position " << positionMs << "ms" << std::endl;
  }
}
