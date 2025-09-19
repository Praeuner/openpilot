#include "bp_routes_panel.h"
#include "system/loggerd/decoder/decoder.h"
#include "system/loggerd/decoder/ffmpeg_decoder.h"
#include "system/loggerd/decoder/v4l_decoder.h"
#include "bp_panel_dialogs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QScrollBar>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QSpacerItem>
#include <QTimer>
#include <QFuture>
#include <QtConcurrent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <iostream>

#ifdef QCOM2
#include <wayland-client.h>
#include <wayland-util.h>
#include <qpa/qplatformnativeinterface.h>
#endif

// Platform-specific paths
#ifdef QCOM2
  static const QString ROUTES_PATH = "/data/media/0/realdata/";
  static const QString THUMBNAIL_CACHE_PATH = "/data/media/0/realdata_thumbnails/";
#else
  static const QString ROUTES_PATH = QDir::homePath() + "/comma_data/media/0/realdata/";
  static const QString THUMBNAIL_CACHE_PATH = QDir::homePath() + "/comma_data/media/0/realdata_thumbnails/";
#endif

// Constants
static const int THUMBNAIL_WIDTH = 320;
static const int THUMBNAIL_HEIGHT = 180;
static const int ROUTES_PER_PAGE = 10;
static const int CARD_HEIGHT = 360;
static const int CARD_SPACING = 30;

// ====================== BPEnhancedVideoModal ======================

BPEnhancedVideoModal::BPEnhancedVideoModal(const QString &routeBase, const RouteInfo &route, QWidget *parent)
    : QDialog(parent), m_routeBase(routeBase), m_route(route), m_currentCamera("fcamera.hevc") {

  setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground, false);
  setAttribute(Qt::WA_AcceptTouchEvents, true);

  setupUI();
  setupDecoder();
  loadVideo(m_currentCamera);
}

BPEnhancedVideoModal::~BPEnhancedVideoModal() {
  stopPlayback();
}

void BPEnhancedVideoModal::setRoute(const RouteInfo &route) {
  m_route = route;
  m_routeBase = route.baseName;

  routeInfoLabel->setText(QString("Route: %1 • %2").arg(m_route.timestamp, m_route.duration));
  updateStarButton();

  loadVideo(m_currentCamera);
}

void BPEnhancedVideoModal::updateStarButton() {
  starButton->setText(m_route.isStarred ? "★" : "☆");
  starButton->setStyleSheet(starButton->styleSheet() +
    QString("QPushButton { color: %1; }").arg(m_route.isStarred ? "#FFD700" : "white"));
}

void BPEnhancedVideoModal::setupUI() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  setStyleSheet("background-color: #0f0f0f;");

  // Header
  header = new QWidget;
  header->setFixedHeight(100);
  header->setStyleSheet("background-color: #1a1a1a; border-bottom: 2px solid #2196F3;");

  QHBoxLayout *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(30, 15, 30, 15);

  routeInfoLabel = new QLabel(QString("Route: %1 • %2").arg(m_route.timestamp, m_route.duration));
  routeInfoLabel->setStyleSheet("color: #2196F3; font-size: 48px; font-weight: 600;");

  starButton = new QPushButton();
  starButton->setFixedSize(80, 80);
  starButton->setStyleSheet(R"(
    QPushButton {
      background: transparent;
      border: none;
      font-size: 48px;
    }
    QPushButton:hover {
      color: #FFD700;
    }
  )");
  connect(starButton, &QPushButton::clicked, this, &BPEnhancedVideoModal::onStarClicked);
  updateStarButton();

  closeButton = new QPushButton("✕");
  closeButton->setFixedSize(100, 100);
  closeButton->setStyleSheet(R"(
    QPushButton {
      background-color: #363636;
      color: white;
      font-size: 52px;
      font-weight: bold;
      border-radius: 50px;
      border: none;
    }
    QPushButton:hover {
      background-color: #F44336;
    }
    QPushButton:pressed {
      background-color: #D32F2F;
    }
  )");

  fullscreenButton = new QPushButton("⛶");
  fullscreenButton->setFixedSize(100, 100);
  fullscreenButton->setStyleSheet(R"(
    QPushButton {
      background-color: #363636;
      color: white;
      font-size: 48px;
      border-radius: 50px;
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
  headerLayout->addWidget(starButton);
  headerLayout->addStretch();
  headerLayout->addWidget(fullscreenButton);
  headerLayout->addSpacing(20);
  headerLayout->addWidget(closeButton);

  // Content area
  QWidget *contentArea = new QWidget;
  QHBoxLayout *contentLayout = new QHBoxLayout(contentArea);
  contentLayout->setContentsMargins(20, 20, 20, 20);
  contentLayout->setSpacing(20);

  // Left side: Video player
  videoContainer = new QWidget;
  videoContainer->setStyleSheet("background-color: #000000; border-radius: 12px;");
  QVBoxLayout *videoLayout = new QVBoxLayout(videoContainer);
  videoLayout->setContentsMargins(0, 0, 0, 0);
  videoLayout->setSpacing(0);

  // Video display
  videoDisplay = new QLabel;
  videoDisplay->setAlignment(Qt::AlignCenter);
  videoDisplay->setStyleSheet("background: black; color: white; font-size: 36px;");
  videoDisplay->setText("Loading video...");
  videoDisplay->setScaledContents(true);
  videoLayout->addWidget(videoDisplay, 1);

  // Video controls
  controlsWidget = new QWidget;
  controlsWidget->setFixedHeight(120);
  controlsWidget->setStyleSheet("background-color: #1a1a1a; border-top: 1px solid #333;");

  QHBoxLayout *controlsLayout = new QHBoxLayout(controlsWidget);
  controlsLayout->setContentsMargins(20, 10, 20, 10);

  playPauseButton = new QPushButton("▶");
  playPauseButton->setFixedSize(120, 120);
  playPauseButton->setStyleSheet(R"(
    QPushButton {
      background-color: #2196F3;
      color: white;
      font-size: 60px;
      border-radius: 60px;
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

  timeLabel = new QLabel("00:00 / 00:00");
  timeLabel->setStyleSheet("color: white; font-size: 40px; min-width: 280px;");
  timeLabel->setAlignment(Qt::AlignCenter);

  controlsLayout->addWidget(playPauseButton);
  controlsLayout->addSpacing(20);
  controlsLayout->addWidget(positionSlider, 1);
  controlsLayout->addSpacing(20);
  controlsLayout->addWidget(timeLabel);

  videoLayout->addWidget(controlsWidget);

  // Right side: Camera panel
  cameraPanel = new QWidget;
  cameraPanel->setFixedWidth(320);
  cameraPanel->setStyleSheet("background-color: #1a1a1a; border-radius: 12px; padding: 20px;");

  QVBoxLayout *cameraPanelLayout = new QVBoxLayout(cameraPanel);
  cameraPanelLayout->setContentsMargins(20, 20, 20, 20);
  cameraPanelLayout->setSpacing(15);

  QLabel *cameraHeader = new QLabel(tr("Camera View"));
  cameraHeader->setStyleSheet("color: #2196F3; font-size: 44px; font-weight: 600; padding-bottom: 10px;");
  cameraHeader->setAlignment(Qt::AlignCenter);
  cameraPanelLayout->addWidget(cameraHeader);

  cameraButtonLayout = new QVBoxLayout;
  cameraButtonLayout->setSpacing(12);

  // Create camera buttons based on available cameras
  if (m_route.hasFCamera) {
    createCameraButton(tr("Front Camera"), "fcamera.hevc", true);
  }
  if (m_route.hasECamera) {
    createCameraButton(tr("Wide Camera"), "ecamera.hevc", false);
  }
  if (m_route.hasDCamera) {
    createCameraButton(tr("Driver Camera"), "dcamera.hevc", false);
  }
  if (m_route.hasQCamera) {
    createCameraButton(tr("Low Quality"), "qcamera.ts", !m_route.hasFCamera);
  }

  cameraPanelLayout->addLayout(cameraButtonLayout);
  cameraPanelLayout->addStretch();

  deleteButton = new QPushButton(tr("🗑 Delete Route"));
  deleteButton->setMinimumHeight(100);
  deleteButton->setStyleSheet(R"(
    QPushButton {
      background-color: #F44336;
      color: white;
      font-size: 44px;
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
  )");
  cameraPanelLayout->addWidget(deleteButton);

  contentLayout->addWidget(videoContainer, 7);
  contentLayout->addWidget(cameraPanel, 3);

  mainLayout->addWidget(header);
  mainLayout->addWidget(contentArea, 1);

  // Connect signals
  connect(closeButton, &QPushButton::clicked, this, &BPEnhancedVideoModal::onCloseClicked);
  connect(fullscreenButton, &QPushButton::clicked, this, &BPEnhancedVideoModal::onFullscreenToggle);
  connect(playPauseButton, &QPushButton::clicked, this, &BPEnhancedVideoModal::togglePlayback);
  connect(deleteButton, &QPushButton::clicked, this, &BPEnhancedVideoModal::deleteRoute);

  // Setup timers
  playbackTimer = new QTimer(this);
  playbackTimer->setInterval(33); // ~30 FPS
  connect(playbackTimer, &QTimer::timeout, this, &BPEnhancedVideoModal::onPlaybackTimer);

  decodeTimer = new QTimer(this);
  decodeTimer->setInterval(10);
  connect(decodeTimer, &QTimer::timeout, this, &BPEnhancedVideoModal::onDecodeChunk);

  // Slider handling
  connect(positionSlider, &QSlider::sliderMoved, this, [this](int value) {
    m_currentPosition = value;
    updateTimeLabel();
  });
}

void BPEnhancedVideoModal::createCameraButton(const QString &label, const QString &file, bool isDefault) {
  QPushButton *btn = new QPushButton(label);
  btn->setMinimumHeight(100);

  QString baseStyle = R"(
    QPushButton {
      background-color: %1;
      color: white;
      font-size: 44px;
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
  )";

  if (isDefault) {
    btn->setStyleSheet(baseStyle.arg("#2196F3", "#1E88E5", "#1976D2"));
    m_currentCamera = file;
  } else {
    btn->setStyleSheet(baseStyle.arg("#363636", "#404040", "#505050"));
  }

  cameraButtons[file] = btn;
  cameraButtonLayout->addWidget(btn);

  connect(btn, &QPushButton::clicked, [this, file]() {
    switchCamera(file);
  });
}

void BPEnhancedVideoModal::switchCamera(const QString &cameraFile) {
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
    )";

    if (it.key() == cameraFile) {
      it.value()->setStyleSheet(baseStyle.arg("#2196F3", "#1E88E5", "#1976D2"));
    } else {
      it.value()->setStyleSheet(baseStyle.arg("#363636", "#404040", "#505050"));
    }
  }

  m_currentCamera = cameraFile;
  loadVideo(cameraFile);
}

void BPEnhancedVideoModal::setupDecoder() {
  // Frame callback
  auto frameCallback = [this](const DecodedFrame& frame) {
    QMetaObject::invokeMethod(this, [this, frame]() {
      onFrameDecoded(frame);
    }, Qt::QueuedConnection);
  };

  // Create appropriate decoder for platform
#ifdef QCOM2
  decoder = std::make_unique<V4LDecoder>(1280, 720, frameCallback, false);
#else
  decoder = std::make_unique<FfmpegDecoder>(1280, 720, frameCallback, false);
#endif
}

void BPEnhancedVideoModal::loadVideo(const QString &videoFile) {
  stopPlayback();

  m_currentCamera = videoFile;
  videoDisplay->setText("Loading segments...");

  // Get all segments for this video type
  m_segmentPaths = getAvailableSegments(videoFile);

  if (m_segmentPaths.isEmpty()) {
    videoDisplay->setText("No video files found");
    playPauseButton->setEnabled(false);
    return;
  }

  // Setup slider
  m_totalDuration = m_segmentPaths.size() * 60000; // Estimate 60 seconds per segment
  positionSlider->setMaximum(m_totalDuration);
  positionSlider->setValue(0);
  m_currentPosition = 0;
  m_currentSegmentIndex = 0;

  // Initialize decoder
  decoder->decoder_open();

  videoDisplay->setText(QString("Ready - %1 segments").arg(m_segmentPaths.size()));
  playPauseButton->setEnabled(true);

  updateTimeLabel();
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

  std::sort(segmentDirs.begin(), segmentDirs.end());

  for (const QString &segmentDir : segmentDirs) {
    QString videoPath = QString("%1/%2/%3").arg(routesDir, segmentDir, videoFile);
    if (QFile::exists(videoPath)) {
      segments.append(videoPath);
    }
  }

  return segments;
}

void BPEnhancedVideoModal::startPlayback() {
  if (m_segmentPaths.isEmpty()) return;

  isPlaying = true;
  isDecoding = true;
  playPauseButton->setText("⏸");

  playbackTimer->start();
  decodeTimer->start();

  qDebug() << "Started playback";
}

void BPEnhancedVideoModal::stopPlayback() {
  isPlaying = false;
  isDecoding = false;
  playPauseButton->setText("▶");

  if (playbackTimer) playbackTimer->stop();
  if (decodeTimer) decodeTimer->stop();

  if (decoder) {
    decoder->flush();
    decoder->decoder_close();
  }
}

void BPEnhancedVideoModal::togglePlayback() {
  if (isPlaying) {
    stopPlayback();
  } else {
    startPlayback();
  }
}

void BPEnhancedVideoModal::onPlaybackTimer() {
  if (isPlaying) {
    m_currentPosition += 33; // ~30 FPS increment

    if (m_currentPosition <= m_totalDuration) {
      positionSlider->setValue(m_currentPosition);
      updateTimeLabel();
    } else {
      stopPlayback();
    }
  }
}

void BPEnhancedVideoModal::onDecodeChunk() {
  if (!isDecoding || m_segmentPaths.isEmpty()) return;

  if (m_currentSegmentIndex >= m_segmentPaths.size()) {
    stopPlayback();
    videoDisplay->setText("Playback finished");
    return;
  }

  // Read chunk from current segment file and decode
  // This is simplified - real implementation would read actual video data
  // For now just advance through segments based on time

  qint64 segmentDuration = 60000; // 60 seconds per segment
  int targetSegment = m_currentPosition / segmentDuration;

  if (targetSegment != m_currentSegmentIndex) {
    m_currentSegmentIndex = targetSegment;
    qDebug() << "Moving to segment" << m_currentSegmentIndex;
  }
}

void BPEnhancedVideoModal::onFrameDecoded(const DecodedFrame &frame) {
  convertYUVToRGB(frame);

  if (!currentPixmap.isNull()) {
    QSize displaySize = videoDisplay->size();
    videoDisplay->setPixmap(currentPixmap.scaled(displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
}

void BPEnhancedVideoModal::convertYUVToRGB(const DecodedFrame &frame) {
  int width = frame.width;
  int height = frame.height;
  int rgbSize = width * height * 3;

  if (rgbBuffer.size() != rgbSize) {
    rgbBuffer.resize(rgbSize);
  }

  // Simple YUV420 to RGB conversion
  uint8_t *y = frame.y;
  uint8_t *u = frame.u;
  uint8_t *v = frame.v;
  uint8_t *rgb = rgbBuffer.data();

  for (int row = 0; row < height; row++) {
    for (int col = 0; col < width; col++) {
      int yVal = y[row * frame.stride_y + col];
      int uVal = u[(row / 2) * frame.stride_uv + (col / 2)] - 128;
      int vVal = v[(row / 2) * frame.stride_uv + (col / 2)] - 128;

      int r = yVal + (int)(1.402 * vVal);
      int g = yVal - (int)(0.344 * uVal + 0.714 * vVal);
      int b = yVal + (int)(1.772 * uVal);

      int idx = (row * width + col) * 3;
      rgb[idx] = std::clamp(r, 0, 255);
      rgb[idx + 1] = std::clamp(g, 0, 255);
      rgb[idx + 2] = std::clamp(b, 0, 255);
    }
  }

  QImage image(rgb, width, height, QImage::Format_RGB888);
  currentPixmap = QPixmap::fromImage(image);
}

void BPEnhancedVideoModal::updateTimeLabel() {
  int totalMs = m_totalDuration;
  int currentMs = m_currentPosition;

  int totalSec = totalMs / 1000;
  int currentSec = currentMs / 1000;

  QString timeText = QString("%1:%2 / %3:%4")
    .arg(currentSec / 60, 2, 10, QChar('0'))
    .arg(currentSec % 60, 2, 10, QChar('0'))
    .arg(totalSec / 60, 2, 10, QChar('0'))
    .arg(totalSec % 60, 2, 10, QChar('0'));

  timeLabel->setText(timeText);
}

void BPEnhancedVideoModal::deleteRoute() {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = tr("Delete Route");
  config.prompt = tr("Are you sure you want to delete this route?\n\nRoute: %1\nThis action cannot be undone.").arg(m_route.baseName);
  config.confirmText = tr("Delete");
  config.cancelText = tr("Cancel");
  config.confirmColor = "#F44336";

  auto confirmDialog = new BPConfirmationDialog(config, this);
  if (confirmDialog->exec() == QDialog::Accepted) {
    QString routesDir;
#ifdef QCOM2
    routesDir = "/data/media/0/realdata";
#else
    routesDir = QDir::homePath() + "/comma_data/media/0/realdata";
#endif

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
      emit routeDeleted(m_routeBase);
      accept();
    } else {
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

void BPEnhancedVideoModal::onStarClicked() {
  m_route.isStarred = !m_route.isStarred;
  updateStarButton();
  emit routeStarredChanged(m_routeBase, m_route.isStarred);
}

void BPEnhancedVideoModal::onCloseClicked() {
  stopPlayback();
  reject();
}

void BPEnhancedVideoModal::onFullscreenToggle() {
  m_isFullscreen = !m_isFullscreen;
  if (m_isFullscreen) {
    showFullScreen();
  } else {
    showNormal();
  }
}

void BPEnhancedVideoModal::setupFullscreen() {
  if (m_fullscreenApplied) return;
  m_fullscreenApplied = true;

#ifdef QCOM2
  setFixedSize(2160, 1080);
  show();
  applyQCOM2Rotation();
#else
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
      wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
      wl_surface_commit(s);
    }
  }
#endif
}

void BPEnhancedVideoModal::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);
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
    default:
      QDialog::keyPressEvent(event);
  }
}

void BPEnhancedVideoModal::resizeEvent(QResizeEvent *event) {
  QDialog::resizeEvent(event);
  if (event->size().width() < 1400) {
    if (cameraPanel) {
      cameraPanel->setFixedWidth(200);
    }
  } else {
    if (cameraPanel) {
      cameraPanel->setFixedWidth(250);
    }
  }
}

// ====================== RouteCardWidget ======================

RouteCardWidget::RouteCardWidget(const RouteInfo &route, QWidget *parent)
    : QWidget(parent), route(route), isPressed(false), isHovered(false) {
  setupUI();
}

void RouteCardWidget::setupUI() {
  setFixedHeight(CARD_HEIGHT);
  setCursor(Qt::PointingHandCursor);

  // Main layout
  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(20);

  // Thumbnail container
  QWidget *thumbnailContainer = new QWidget(this);
  thumbnailContainer->setFixedSize(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
  thumbnailContainer->setStyleSheet("background: #1a1a1a; border-radius: 8px;");

  thumbnailLabel = new QLabel(thumbnailContainer);
  thumbnailLabel->setObjectName("thumbnailLabel");
  thumbnailLabel->setAlignment(Qt::AlignCenter);
  thumbnailLabel->setFixedSize(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
  thumbnailLabel->setScaledContents(true);
  thumbnailLabel->setText("Loading...");
  thumbnailLabel->setStyleSheet("color: #666; font-size: 28px;");

  layout->addWidget(thumbnailContainer);

  // Info section
  QVBoxLayout *infoLayout = new QVBoxLayout();
  infoLayout->setSpacing(12);

  // Timestamp
  timestampLabel = new QLabel(route.timestamp, this);
  timestampLabel->setStyleSheet("color: white; font-size: 32px; font-weight: bold;");
  infoLayout->addWidget(timestampLabel);

  // Duration row
  QHBoxLayout *statsRow = new QHBoxLayout();
  statsRow->setSpacing(30);

  durationLabel = new QLabel(QString("⏱ %1").arg(route.duration), this);
  durationLabel->setStyleSheet("color: #2196F3; font-size: 26px; font-weight: 500;");
  statsRow->addWidget(durationLabel);

  statsRow->addStretch();
  infoLayout->addLayout(statsRow);

  // Metadata row
  QHBoxLayout *metaRow = new QHBoxLayout();
  metaRow->setSpacing(30);

  segmentsLabel = new QLabel(QString("%1 segments").arg(route.segments), this);
  segmentsLabel->setStyleSheet("color: #999; font-size: 22px;");
  metaRow->addWidget(segmentsLabel);

  sizeLabel = new QLabel(route.size, this);
  sizeLabel->setStyleSheet("color: #999; font-size: 22px;");
  metaRow->addWidget(sizeLabel);

  metaRow->addStretch();
  infoLayout->addLayout(metaRow);

  // Badges row for camera types and logs
  QHBoxLayout *badgeRow = new QHBoxLayout();
  badgeRow->setSpacing(15);

  if (route.hasFCamera) {
    QLabel *badge = new QLabel("Front - HQ", this);
    badge->setStyleSheet(R"(
      background: #2196F3;
      color: white;
      padding: 6px 12px;
      border-radius: 4px;
      font-size: 18px;
      font-weight: bold;
    )");
    badgeRow->addWidget(badge);
  }

  if (route.hasQCamera) {
    QLabel *badge = new QLabel("Front - LQ", this);
    badge->setStyleSheet(R"(
      background: #2196F3;
      color: white;
      padding: 6px 12px;
      border-radius: 4px;
      font-size: 18px;
      font-weight: bold;
    )");
    badgeRow->addWidget(badge);
  }

  if (route.hasECamera) {
    QLabel *badge = new QLabel("Wide", this);
    badge->setStyleSheet(R"(
      background: #2196F3;
      color: white;
      padding: 6px 12px;
      border-radius: 4px;
      font-size: 18px;
      font-weight: bold;
    )");
    badgeRow->addWidget(badge);
  }

  if (route.hasDCamera) {
    QLabel *badge = new QLabel("Driver", this);
    badge->setStyleSheet(R"(
      background: #2196F3;
      color: white;
      padding: 6px 12px;
      border-radius: 4px;
      font-size: 18px;
      font-weight: bold;
    )");
    badgeRow->addWidget(badge);
  }

  if (route.hasRLog || route.hasQLog) {
    QLabel *logBadge = new QLabel("Logs", this);
    logBadge->setStyleSheet(R"(
      background: #FF9800;
      color: white;
      padding: 6px 12px;
      border-radius: 4px;
      font-size: 18px;
      font-weight: bold;
    )");
    badgeRow->addWidget(logBadge);
  }

  badgeRow->addStretch();
  infoLayout->addLayout(badgeRow);

  infoLayout->addStretch();
  layout->addLayout(infoLayout, 1);

  // Elapsed time on right
  QLabel *elapsedLabel = new QLabel(route.elapsedTime, this);
  elapsedLabel->setStyleSheet("color: #666; font-size: 24px;");
  layout->addWidget(elapsedLabel);

  // Star button - positioned absolute top-right
  starButton = new QPushButton(this);
  starButton->setFixedSize(60, 60);
  starButton->setStyleSheet(R"(
    QPushButton {
      background: transparent;
      border: none;
      font-size: 40px;
      color: #666;
    }
    QPushButton:hover {
      color: #FFD700;
    }
    QPushButton:pressed {
      color: #FFA500;
    }
  )");
  connect(starButton, &QPushButton::clicked, this, &RouteCardWidget::onStarButtonClicked);

  // Position star button absolutely
  starButton->move(width() - 60, 20);
  starButton->raise();

  updateStarButton();
}

void RouteCardWidget::setStarred(bool starred) {
  route.isStarred = starred;
  updateStarButton();
}

void RouteCardWidget::updateStarButton() {
  if (route.isStarred) {
    starButton->setText("★");
    starButton->setStyleSheet(starButton->styleSheet() + "QPushButton { color: #FFD700; }");
  } else {
    starButton->setText("☆");
    starButton->setStyleSheet(starButton->styleSheet().replace("color: #FFD700;", "color: #666;"));
  }
}

void RouteCardWidget::onStarButtonClicked() {
  route.isStarred = !route.isStarred;
  updateStarButton();
  emit starToggled(route.baseName, route.isStarred);
}

void RouteCardWidget::setThumbnail(const QPixmap &pixmap) {
  if (thumbnailLabel) {
    thumbnailLabel->setPixmap(pixmap.scaled(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT,
                                            Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
}

void RouteCardWidget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && !starButton->geometry().contains(event->pos())) {
    isPressed = true;
    update();
  }
  QWidget::mousePressEvent(event);
}

void RouteCardWidget::mouseReleaseEvent(QMouseEvent *event) {
  if (isPressed && rect().contains(event->pos())) {
    emit clicked(route);
  }
  isPressed = false;
  update();
  QWidget::mouseReleaseEvent(event);
}

void RouteCardWidget::enterEvent(QEvent *event) {
  isHovered = true;
  update();
  QWidget::enterEvent(event);
}

void RouteCardWidget::leaveEvent(QEvent *event) {
  isHovered = false;
  update();
  QWidget::leaveEvent(event);
}

void RouteCardWidget::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Background with gradient
  QLinearGradient gradient(0, 0, 0, height());
  if (isPressed) {
    gradient.setColorAt(0, QColor(20, 20, 20));
    gradient.setColorAt(1, QColor(10, 10, 10));
  } else if (isHovered) {
    gradient.setColorAt(0, QColor(35, 35, 35));
    gradient.setColorAt(1, QColor(20, 20, 20));
  } else {
    gradient.setColorAt(0, QColor(26, 26, 26));
    gradient.setColorAt(1, QColor(15, 15, 15));
  }

  QPainterPath path;
  path.addRoundedRect(rect(), 12, 12);
  p.fillPath(path, gradient);

  // Border
  p.setPen(QPen(isHovered ? QColor(33, 150, 243, 100) : QColor(50, 50, 50), 1));
  p.drawPath(path);
}

// ====================== BPRoutesPanel ======================

BPRoutesPanel::BPRoutesPanel(QWidget *parent)
    : QWidget(parent),
      currentPage(0),
      routesPerPage(ROUTES_PER_PAGE),
      isLoading(false),
      allRoutesLoaded(false),
      ffmpegProcess(nullptr) {

  routesPath = ROUTES_PATH;
  thumbnailCachePath = THUMBNAIL_CACHE_PATH;

  // Ensure cache directory exists
  QDir().mkpath(thumbnailCachePath);

  // Find FFmpeg
  ffmpegPath = findFFmpegPath();

  setupUI();
  applyStyles();
}

BPRoutesPanel::~BPRoutesPanel() {
  // Clean up thumbnail watchers
  for (auto watcher : thumbnailWatchers.values()) {
    if (watcher) {
      watcher->cancel();
      watcher->waitForFinished();
      delete watcher;
    }
  }

  if (ffmpegProcess) {
    ffmpegProcess->terminate();
    ffmpegProcess->waitForFinished();
    delete ffmpegProcess;
  }
}

void BPRoutesPanel::setupUI() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Header
  QWidget *header = new QWidget(this);
  header->setFixedHeight(100);
  QHBoxLayout *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(30, 10, 30, 10);

  QLabel *titleLabel = new QLabel("Driving Routes", this);
  titleLabel->setStyleSheet("color: white; font-size: 48px; font-weight: bold;");
  headerLayout->addWidget(titleLabel);

  // Stats label (route count and disk space)
  statsLabel = new QLabel(this);
  statsLabel->setStyleSheet("color: #666; font-size: 32px; margin-left: 20px;");
  headerLayout->addWidget(statsLabel);

  headerLayout->addStretch();

  refreshBtn = new QPushButton("Refresh", this);
  connect(refreshBtn, &QPushButton::clicked, this, &BPRoutesPanel::refresh);
  headerLayout->addWidget(refreshBtn);

  clearCacheBtn = new QPushButton("Clear Cache", this);
  connect(clearCacheBtn, &QPushButton::clicked, this, &BPRoutesPanel::clearCache);
  headerLayout->addWidget(clearCacheBtn);

  mainLayout->addWidget(header);

  // Scroll area
  scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->installEventFilter(this);

  scrollContent = new QWidget();
  contentLayout = new QVBoxLayout(scrollContent);
  contentLayout->setContentsMargins(30, 20, 30, 30);
  contentLayout->setSpacing(CARD_SPACING);

  scrollArea->setWidget(scrollContent);
  mainLayout->addWidget(scrollArea);

  // Status label
  statusLabel = new QLabel("Loading routes...", this);
  statusLabel->setAlignment(Qt::AlignCenter);
  statusLabel->setStyleSheet("color: #666; font-size: 48px; padding: 50px;");
  contentLayout->addWidget(statusLabel);

  // Connect scroll detection
  connect(scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
          this, &BPRoutesPanel::onScrollPositionChanged);
}

void BPRoutesPanel::applyStyles() {
  setStyleSheet(R"(
    BPRoutesPanel {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
        stop: 0 #1a1a1a,
        stop: 1 #0f0f0f);
    }
    QScrollArea {
      background: transparent;
      border: none;
    }
    QScrollBar:vertical {
      background: #1a1a1a;
      width: 10px;
      border-radius: 5px;
    }
    QScrollBar::handle:vertical {
      background: #2196F3;
      border-radius: 5px;
      min-height: 30px;
    }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
      height: 0;
    }
    QPushButton {
      background: #2196F3;
      border: none;
      border-radius: 12px;
      color: white;
      font-size: 32px;
      font-weight: bold;
      padding: 15px 30px;
      min-width: 140px;
      min-height: 60px;
    }
    QPushButton:pressed {
      background: #1976D2;
    }
    QPushButton:hover {
      background: #42A5F5;
    }
  )");
}

void BPRoutesPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  if (allRoutes.empty()) {
    QTimer::singleShot(100, this, &BPRoutesPanel::loadRoutes);
  }
}

void BPRoutesPanel::loadRoutes() {
  isLoading = true;
  statusLabel->setText("Scanning routes...");

  // Load routes asynchronously
  QFuture<void> future = QtConcurrent::run([this]() {
    scanRoutes();
  });

  QFutureWatcher<void> *watcher = new QFutureWatcher<void>(this);
  connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
    watcher->deleteLater();

    if (allRoutes.empty()) {
      statusLabel->setText("No routes found");
      updateStats();
    } else {
      statusLabel->hide();

      // Sort routes by date (newest first)
      std::sort(allRoutes.begin(), allRoutes.end(),
                [](const RouteInfo &a, const RouteInfo &b) {
                  return a.date > b.date || (a.date == b.date && a.timestamp > b.timestamp);
                });

      updateStats();
      // Load first page
      loadMoreRoutes();
    }

    isLoading = false;
  });

  watcher->setFuture(future);
}

void BPRoutesPanel::scanRoutes() {
  QDir routesDir(routesPath);
  if (!routesDir.exists()) {
    qWarning() << "Routes directory not found:" << routesPath;
    return;
  }

  routesDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
  routesDir.setSorting(QDir::Name | QDir::Reversed);

  QStringList routesList = routesDir.entryList();

  // Group segments by base name
  QMap<QString, QStringList> routeSegments;
  for (const QString &entry : routesList) {
    // Extract base name (without segment number)
    QStringList parts = entry.split("--");
    if (parts.size() >= 3) {
      QString baseName = parts[0] + "--" + parts[1] + "--" + parts[2];
      routeSegments[baseName].append(entry);
    }
  }

  // Parse each route
  for (auto it = routeSegments.begin(); it != routeSegments.end(); ++it) {
    RouteInfo route = parseRoute(it.key());
    if (!route.baseName.isEmpty()) {
      route.segments = it.value().size();
      route.fullPath = routesPath + it.value().first();
      allRoutes.push_back(route);
    }
  }
}

RouteInfo BPRoutesPanel::parseRoute(const QString &routePath) {
  RouteInfo route;
  route.baseName = routePath;

  QString fullPath = routesPath + routePath + "--0";
  QDir routeDir(fullPath);
  if (!routeDir.exists()) {
    return route;
  }

  // Parse timestamp from directory name
  QStringList parts = routePath.split("--");
  if (parts.size() >= 3) {
    QString dateStr = parts[0];
    QString timeStr = parts[1] + "--" + parts[2];

    QDateTime startTime = QDateTime::fromString(dateStr + " " + timeStr.replace("--", ":"), "yyyy-MM-dd HH:mm:ss");
    if (startTime.isValid()) {
      route.date = startTime.date();
      route.timestamp = startTime.toString("MMM d, h:mm AP");

      // Calculate elapsed time
      QDateTime now = QDateTime::currentDateTime();
      qint64 elapsed = startTime.secsTo(now);

      if (elapsed < 3600) {
        route.elapsedTime = QString("%1 min ago").arg(elapsed / 60);
      } else if (elapsed < 86400) {
        route.elapsedTime = QString("%1 hr ago").arg(elapsed / 3600);
      } else if (elapsed < 604800) {
        route.elapsedTime = QString("%1 days ago").arg(elapsed / 86400);
      } else {
        route.elapsedTime = startTime.toString("MMM d");
      }
    }
  }

  // Check for video files
  route.hasFCamera = QFile::exists(fullPath + "/fcamera.hevc");
  route.hasDCamera = QFile::exists(fullPath + "/dcamera.hevc");
  route.hasECamera = QFile::exists(fullPath + "/ecamera.hevc");
  route.hasQCamera = QFile::exists(fullPath + "/qcamera.ts");
  route.hasVideo = route.hasFCamera || route.hasDCamera || route.hasECamera || route.hasQCamera;

  // Check for log files
  route.hasRLog = QFile::exists(fullPath + "/rlog.bz2");
  route.hasQLog = QFile::exists(fullPath + "/qlog.bz2");

  // Load star status
  route.isStarred = loadRouteStarStatus(route.baseName);

  // Get actual duration and size
  route.duration = getDurationFromRoute(fullPath);

  // Calculate total size across all segments
  qint64 totalBytes = 0;
  for (int i = 0; i < 100; i++) { // Max 100 segments
    QString segmentPath = routesPath + routePath + QString("--%1").arg(i);
    QDir segmentDir(segmentPath);
    if (!segmentDir.exists()) break;

    for (const QFileInfo &fileInfo : segmentDir.entryInfoList(QDir::Files)) {
      totalBytes += fileInfo.size();
    }
  }
  route.totalBytes = totalBytes;
  route.size = formatSize(totalBytes);

  // Get thumbnail path
  route.thumbnailPath = getThumbnailPath(route.baseName);

  return route;
}

QString BPRoutesPanel::getDurationFromRoute(const QString &routePath) const {
  // Try to estimate duration from segment count or file timestamps
  // Each segment is typically 60 seconds
  QDir routeDir(routePath);
  if (routeDir.exists()) {
    // Count segments for this route
    QString baseName = QFileInfo(routePath).fileName();
    baseName = baseName.left(baseName.lastIndexOf("--"));

    int segmentCount = 1;
    for (int i = 1; i < 100; i++) {
      if (!QDir(routesPath + baseName + QString("--%1").arg(i)).exists()) {
        segmentCount = i;
        break;
      }
    }

    int totalSeconds = segmentCount * 60; // Approximate 60 seconds per segment
    return formatDuration(totalSeconds);
  }

  return "0:00";
}

void BPRoutesPanel::updateStats() {
  if (allRoutes.empty()) {
    statsLabel->setText("0 routes • 0 GB");
    return;
  }

  // Calculate total disk space
  qint64 totalBytes = 0;
  for (const RouteInfo &route : allRoutes) {
    totalBytes += route.totalBytes;
  }

  QString sizeStr = formatSize(totalBytes);
  statsLabel->setText(QString("%1 routes • %2").arg(allRoutes.size()).arg(sizeStr));
}

bool BPRoutesPanel::hasVideoFiles(const QString &routePath) {
  return QFile::exists(routePath + "/fcamera.hevc") ||
         QFile::exists(routePath + "/dcamera.hevc") ||
         QFile::exists(routePath + "/ecamera.hevc") ||
         QFile::exists(routePath + "/qcamera.ts");
}

void BPRoutesPanel::addDateSection(const QDate &date) {
  if (loadedDates.contains(date.toString())) {
    return;
  }

  QWidget *sectionWidget = new QWidget(this);
  QVBoxLayout *sectionLayout = new QVBoxLayout(sectionWidget);
  sectionLayout->setContentsMargins(0, 20, 0, 10);

  // Format date with day name
  QString dayName = date.toString("dddd");
  QString dateStr = date.toString("MMMM d, yyyy");
  QLabel *dateLabel = new QLabel(QString("%1 - %2").arg(dayName, dateStr), this);
  dateLabel->setStyleSheet("color: #999; font-size: 36px; font-weight: bold;");
  sectionLayout->addWidget(dateLabel);

  contentLayout->addWidget(sectionWidget);
  dateSections[date] = sectionWidget;
  loadedDates.insert(date.toString());
}

void BPRoutesPanel::addRouteCard(const RouteInfo &route) {
  // Add date section if needed
  addDateSection(route.date);

  // Create route card
  RouteCardWidget *card = new RouteCardWidget(route, this);
  connect(card, &RouteCardWidget::clicked, this, &BPRoutesPanel::onRouteCardClicked);
  connect(card, &RouteCardWidget::starToggled, this, &BPRoutesPanel::onCardStarToggled);

  contentLayout->addWidget(card);
  routeCards[route.baseName] = card;

  // Initialize thumbnail
  QLabel *thumbnailLabel = card->findChild<QLabel*>("thumbnailLabel");
  if (thumbnailLabel) {
    initializeThumbnail(thumbnailLabel, route.baseName);
  }
}

void BPRoutesPanel::loadMoreRoutes() {
  if (isLoading || allRoutesLoaded) {
    return;
  }

  int startIdx = currentPage * routesPerPage;
  int endIdx = std::min(startIdx + routesPerPage, (int)allRoutes.size());

  for (int i = startIdx; i < endIdx; i++) {
    addRouteCard(allRoutes[i]);
    displayedRoutes.push_back(allRoutes[i]);
  }

  currentPage++;

  if (endIdx >= allRoutes.size()) {
    allRoutesLoaded = true;
  }
}

void BPRoutesPanel::onScrollPositionChanged() {
  // Check if we're near the bottom
  QScrollBar *vbar = scrollArea->verticalScrollBar();
  if (vbar->value() > vbar->maximum() - 200) {
    loadMoreRoutes();
  }
}

void BPRoutesPanel::initializeThumbnail(QLabel *thumbnailLabel, const QString &routeBase) {
  QString thumbnailPath = getThumbnailPath(routeBase);

  if (QFile::exists(thumbnailPath)) {
    // Load existing thumbnail
    QPixmap pixmap(thumbnailPath);
    if (!pixmap.isNull()) {
      thumbnailLabel->setPixmap(pixmap.scaled(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT,
                                              Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
  } else {
    // Generate thumbnail asynchronously
    auto watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, thumbnailLabel, watcher]() {
      QString path = watcher->result();
      if (!path.isEmpty() && QFile::exists(path)) {
        QPixmap pixmap(path);
        if (!pixmap.isNull()) {
          thumbnailLabel->setPixmap(pixmap.scaled(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT,
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
      }

      thumbnailWatchers.remove(watcher->property("routeBase").toString());
      watcher->deleteLater();
    });

    watcher->setProperty("routeBase", routeBase);
    thumbnailWatchers[routeBase] = watcher;

    QFuture<QString> future = QtConcurrent::run(this, &BPRoutesPanel::generateThumbnailAsync, routeBase);
    watcher->setFuture(future);
  }
}

QString BPRoutesPanel::generateThumbnailAsync(const QString &routeBase) {
  if (ffmpegPath.isEmpty()) {
    qWarning() << "FFmpeg not found";
    return QString();
  }

  // Try different video files in order of preference
  QStringList videoFiles = {"fcamera.hevc", "ecamera.hevc", "dcamera.hevc", "qcamera.ts"};
  QString videoPath;

  for (const QString &videoFile : videoFiles) {
    QString testPath = routesPath + routeBase + "--0/" + videoFile;
    if (QFile::exists(testPath)) {
      videoPath = testPath;
      break;
    }
  }

  if (videoPath.isEmpty()) {
    return QString();
  }

  QString thumbnailPath = getThumbnailPath(routeBase);

  QProcess ffmpeg;
  QStringList args;
  args << "-y"                           // Overwrite output
       << "-nostdin"                      // Non-interactive
       << "-i" << videoPath               // Input video
       << "-ss" << "00:00:01"            // Seek to 1 second
       << "-vframes" << "1"               // Extract single frame
       << "-an"                           // Disable audio
       << "-vf" << QString("scale=%1:%2").arg(THUMBNAIL_WIDTH).arg(THUMBNAIL_HEIGHT)
       << thumbnailPath;                 // Output

  ffmpeg.start(ffmpegPath, args);
  if (ffmpeg.waitForFinished(5000)) {  // 5 second timeout
    if (ffmpeg.exitCode() == 0) {
      return thumbnailPath;
    } else {
      qWarning() << "FFmpeg failed:" << ffmpeg.readAllStandardError();
    }
  } else {
    ffmpeg.kill();
    qWarning() << "FFmpeg timeout";
  }

  return QString();
}

QString BPRoutesPanel::getThumbnailPath(const QString &routeBase) const {
  return thumbnailCachePath + routeBase + ".jpg";
}

QString BPRoutesPanel::findFFmpegPath() const {
  QStringList candidates = {
    "/usr/bin/ffmpeg",
    "/usr/local/bin/ffmpeg",
    "/opt/homebrew/bin/ffmpeg",  // macOS M1
    "ffmpeg"  // System PATH
  };

  for (const QString &path : candidates) {
    if (QFile::exists(path) || QStandardPaths::findExecutable(path) != "") {
      return path;
    }
  }

  return QString();
}

void BPRoutesPanel::onRouteCardClicked(const RouteInfo &route) {
  if (!videoModal) {
    videoModal = std::make_unique<BPEnhancedVideoModal>(route.baseName, route, this);
    connect(videoModal.get(), &BPEnhancedVideoModal::routeDeleted,
            this, &BPRoutesPanel::onRouteDeleted);
    connect(videoModal.get(), &BPEnhancedVideoModal::routeStarredChanged,
            this, &BPRoutesPanel::onRouteStarredChanged);
  } else {
    // Update modal with new route
    videoModal->setRoute(route);
  }

  videoModal->exec();
}

void BPRoutesPanel::saveRouteStarStatus(const QString &routeBaseName, bool starred) {
  QString starFile = QString("%1%2.star").arg(thumbnailCachePath).arg(routeBaseName);
  if (starred) {
    QFile(starFile).open(QIODevice::WriteOnly);
  } else {
    QFile::remove(starFile);
  }
}

bool BPRoutesPanel::loadRouteStarStatus(const QString &routeBaseName) {
  return QFile::exists(QString("%1%2.star").arg(thumbnailCachePath).arg(routeBaseName));
}

void BPRoutesPanel::onRouteDeleted(const QString &routeBaseName) {
  removeRouteCard(routeBaseName);

  // Remove from allRoutes
  allRoutes.erase(std::remove_if(allRoutes.begin(), allRoutes.end(),
                                 [&routeBaseName](const RouteInfo &r) {
                                   return r.baseName == routeBaseName;
                                 }),
                  allRoutes.end());

  // Update stats
  updateStats();
}

void BPRoutesPanel::onRouteStarredChanged(const QString &routeBaseName, bool starred) {
  saveRouteStarStatus(routeBaseName, starred);

  // Update card if visible
  if (routeCards.contains(routeBaseName)) {
    routeCards[routeBaseName]->setStarred(starred);
  }

  // Update in allRoutes
  for (auto &route : allRoutes) {
    if (route.baseName == routeBaseName) {
      route.isStarred = starred;
      break;
    }
  }
}

void BPRoutesPanel::onCardStarToggled(const QString &routeBaseName, bool starred) {
  saveRouteStarStatus(routeBaseName, starred);

  // Update in allRoutes
  for (auto &route : allRoutes) {
    if (route.baseName == routeBaseName) {
      route.isStarred = starred;
      break;
    }
  }
}

void BPRoutesPanel::removeRouteCard(const QString &routeBaseName) {
  if (routeCards.contains(routeBaseName)) {
    routeCards[routeBaseName]->deleteLater();
    routeCards.remove(routeBaseName);
  }
}

void BPRoutesPanel::refresh() {
  // Clear existing routes
  allRoutes.clear();
  displayedRoutes.clear();
  currentPage = 0;
  allRoutesLoaded = false;

  // Clear UI
  while (contentLayout->count() > 0) {
    QLayoutItem *item = contentLayout->takeAt(0);
    if (item->widget()) {
      item->widget()->deleteLater();
    }
    delete item;
  }

  dateSections.clear();
  loadedDates.clear();
  routeCards.clear();

  // Cancel thumbnail watchers
  for (auto watcher : thumbnailWatchers.values()) {
    if (watcher) {
      watcher->cancel();
      watcher->waitForFinished();
      delete watcher;
    }
  }
  thumbnailWatchers.clear();

  // Reset status label
  statusLabel = new QLabel("Loading routes...", this);
  statusLabel->setAlignment(Qt::AlignCenter);
  statusLabel->setStyleSheet("color: #666; font-size: 48px; padding: 50px;");
  contentLayout->addWidget(statusLabel);

  // Reload
  loadRoutes();
}

void BPRoutesPanel::clearCache() {
  // Clear thumbnail cache
  QDir cacheDir(thumbnailCachePath);
  if (cacheDir.exists()) {
    for (const QString &file : cacheDir.entryList(QDir::Files)) {
      if (file.endsWith(".jpg")) { // Only delete thumbnails, keep star files
        cacheDir.remove(file);
      }
    }
  }

  refresh();
}

void BPRoutesPanel::cleanupThumbnailCache() {
  QDir cacheDir(thumbnailCachePath);
  if (!cacheDir.exists()) return;

  // Get all route base names
  QSet<QString> validRoutes;
  for (const RouteInfo &route : allRoutes) {
    validRoutes.insert(route.baseName);
  }

  // Remove orphaned thumbnails
  for (const QString &thumbnail : cacheDir.entryList(QStringList() << "*.jpg", QDir::Files)) {
    QString routeBase = QFileInfo(thumbnail).baseName();
    if (!validRoutes.contains(routeBase)) {
      cacheDir.remove(thumbnail);
    }
  }
}

QString BPRoutesPanel::formatDuration(int seconds) const {
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  int secs = seconds % 60;

  if (hours > 0) {
    return QString("%1h %2m").arg(hours).arg(minutes);
  } else if (minutes > 0) {
    return QString("%1m %2s").arg(minutes).arg(secs);
  } else {
    return QString("%1s").arg(secs);
  }
}

QString BPRoutesPanel::formatSize(qint64 bytes) const {
  const qint64 kb = 1024;
  const qint64 mb = kb * 1024;
  const qint64 gb = mb * 1024;

  if (bytes >= gb) {
    return QString("%1 GB").arg(bytes / (double)gb, 0, 'f', 1);
  } else if (bytes >= mb) {
    return QString("%1 MB").arg(bytes / (double)mb, 0, 'f', 1);
  } else if (bytes >= kb) {
    return QString("%1 KB").arg(bytes / kb);
  } else {
    return QString("%1 B").arg(bytes);
  }
}

bool BPRoutesPanel::eventFilter(QObject *obj, QEvent *event) {
  if (obj == scrollArea && event->type() == QEvent::Wheel) {
    // Handle smooth scrolling for touchpad/wheel
    return false;  // Let default handling work
  }
  return QWidget::eventFilter(obj, event);
}

void BPRoutesPanel::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);

  // Reposition star buttons on all cards when resizing
  for (auto card : routeCards.values()) {
    QPushButton *starBtn = card->findChild<QPushButton*>();
    if (starBtn) {
      starBtn->move(card->width() - 60, 20);
    }
  }

  // Adjust cards per page based on window height
  int visibleHeight = scrollArea->viewport()->height();
  int cardsVisible = visibleHeight / (CARD_HEIGHT + CARD_SPACING);
  routesPerPage = std::max(5, cardsVisible + 2);  // Load a bit more than visible
}
