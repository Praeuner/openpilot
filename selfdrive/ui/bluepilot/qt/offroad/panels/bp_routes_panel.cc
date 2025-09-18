#include "bp_routes_panel.h"

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
#include <QSlider>
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
#include <QTextStream>
#include <QDebug>
#include <algorithm>
#include <cmath>

// Include custom decoder
#ifdef QCOM2
  #include "system/loggerd/decoder/v4l_decoder.h"
#else
  #include "system/loggerd/decoder/ffmpeg_decoder.h"
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
static const int CARD_HEIGHT = 280;
static const int CARD_SPACING = 20;
static const int DECODE_BUFFER_SIZE = 1024 * 1024; // 1MB buffer for reading video chunks

// ====================== BPEnhancedVideoModal ======================

BPEnhancedVideoModal::BPEnhancedVideoModal(QWidget *parent) 
    : QDialog(parent), 
      currentVideoType(VideoType::FCamera),
      isPlaying(false),
      videoFile(nullptr),
      decodeBuffer(nullptr),
      currentPosition(0),
      totalDuration(0) {
  setupUI();
  
  // Allocate decode buffer
  decodeBuffer = new uint8_t[DECODE_BUFFER_SIZE];
  
  // Setup decoding timer for frame updates
  decodingTimer = new QTimer(this);
  decodingTimer->setInterval(50); // 20 FPS playback
  connect(decodingTimer, &QTimer::timeout, this, &BPEnhancedVideoModal::onDecodingTimer);
  
  // Hide controls timer
  hideControlsTimer = new QTimer(this);
  hideControlsTimer->setSingleShot(true);
  hideControlsTimer->setInterval(3000);
  connect(hideControlsTimer, &QTimer::timeout, this, [this]() {
    if (isPlaying) {
      controlsWidget->hide();
    }
  });
}

BPEnhancedVideoModal::~BPEnhancedVideoModal() {
  stopDecoding();
  
  if (decodeBuffer) {
    delete[] decodeBuffer;
  }
  
  if (videoFile && videoFile->isOpen()) {
    videoFile->close();
    delete videoFile;
  }
  
  if (!tempVideoPath.isEmpty() && QFile::exists(tempVideoPath)) {
    QFile::remove(tempVideoPath);
  }
}

void BPEnhancedVideoModal::setupUI() {
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setModal(true);
  
  // Full screen on QCOM2
#ifdef QCOM2
  showFullScreen();
#else
  resize(1280, 720);
#endif
  
  // Main layout
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);
  
  // Video display widget (we'll paint frames here)
  videoWidget = new QWidget(this);
  videoWidget->setStyleSheet("background: black;");
  videoWidget->setMinimumSize(640, 480);
  mainLayout->addWidget(videoWidget);
  
  // Controls overlay
  controlsWidget = new QWidget(this);
  controlsWidget->setStyleSheet(R"(
    QWidget {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
        stop: 0 transparent,
        stop: 0.7 transparent,
        stop: 1 rgba(0, 0, 0, 200));
    }
    QPushButton {
      background: rgba(33, 150, 243, 180);
      border: none;
      border-radius: 8px;
      color: white;
      font-size: 18px;
      font-weight: bold;
      padding: 15px 25px;
      min-width: 100px;
      min-height: 50px;
    }
    QPushButton:pressed {
      background: rgba(25, 118, 210, 200);
    }
    QPushButton:checked {
      background: rgba(25, 118, 210, 255);
      border: 2px solid white;
    }
    QSlider::groove:horizontal {
      background: rgba(255, 255, 255, 30);
      height: 8px;
      border-radius: 4px;
    }
    QSlider::handle:horizontal {
      background: #2196F3;
      width: 20px;
      height: 20px;
      border-radius: 10px;
      margin: -6px 0;
    }
    QSlider::sub-page:horizontal {
      background: #2196F3;
      border-radius: 4px;
    }
    QLabel {
      color: white;
      font-size: 16px;
      font-weight: 500;
    }
  )");
  
  QVBoxLayout *controlsLayout = new QVBoxLayout(controlsWidget);
  controlsLayout->setContentsMargins(30, 0, 30, 30);
  
  // Top controls - Camera selection
  QHBoxLayout *topLayout = new QHBoxLayout();
  topLayout->addStretch();
  
  fcameraBtn = new QPushButton("Front", this);
  fcameraBtn->setCheckable(true);
  fcameraBtn->setChecked(true);
  connect(fcameraBtn, &QPushButton::clicked, this, &BPEnhancedVideoModal::onCameraButtonClicked);
  topLayout->addWidget(fcameraBtn);
  
  dcameraBtn = new QPushButton("Driver", this);
  dcameraBtn->setCheckable(true);
  connect(dcameraBtn, &QPushButton::clicked, this, &BPEnhancedVideoModal::onCameraButtonClicked);
  topLayout->addWidget(dcameraBtn);
  
  ecameraBtn = new QPushButton("Wide", this);
  ecameraBtn->setCheckable(true);
  connect(ecameraBtn, &QPushButton::clicked, this, &BPEnhancedVideoModal::onCameraButtonClicked);
  topLayout->addWidget(ecameraBtn);
  
  qcameraBtn = new QPushButton("Preview", this);
  qcameraBtn->setCheckable(true);
  connect(qcameraBtn, &QPushButton::clicked, this, &BPEnhancedVideoModal::onCameraButtonClicked);
  topLayout->addWidget(qcameraBtn);
  
  QPushButton *closeBtn = new QPushButton("✕", this);
  closeBtn->setStyleSheet("min-width: 50px; font-size: 24px; padding: 10px;");
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
  topLayout->addWidget(closeBtn);
  
  controlsLayout->addLayout(topLayout);
  controlsLayout->addStretch();
  
  // Bottom controls - Playback
  QHBoxLayout *bottomLayout = new QHBoxLayout();
  
  playPauseBtn = new QPushButton("▶", this);
  connect(playPauseBtn, &QPushButton::clicked, this, &BPEnhancedVideoModal::onPlayPauseClicked);
  bottomLayout->addWidget(playPauseBtn);
  
  timeLabel = new QLabel("00:00", this);
  bottomLayout->addWidget(timeLabel);
  
  seekSlider = new QSlider(Qt::Horizontal, this);
  connect(seekSlider, &QSlider::sliderMoved, this, &BPEnhancedVideoModal::onSeekSliderMoved);
  bottomLayout->addWidget(seekSlider, 1);
  
  durationLabel = new QLabel("00:00", this);
  bottomLayout->addWidget(durationLabel);
  
  controlsLayout->addLayout(bottomLayout);
  
  // Position controls as overlay
  controlsWidget->setParent(this);
  controlsWidget->raise();
}

void BPEnhancedVideoModal::setRoute(const RouteInfo &route) {
  currentRoute = route;
  loadVideo(currentVideoType);
}

void BPEnhancedVideoModal::loadVideo(VideoType type) {
  stopDecoding();
  currentVideoType = type;
  
  // Update button states
  fcameraBtn->setChecked(type == VideoType::FCamera);
  dcameraBtn->setChecked(type == VideoType::DCamera);
  ecameraBtn->setChecked(type == VideoType::ECamera);
  qcameraBtn->setChecked(type == VideoType::QCamera);
  
  // Get video path
  QString videoPath = getVideoPath(type);
  
  if (currentRoute.segments > 1) {
    // Concatenate segments for continuous playback
    tempVideoPath = QDir::temp().absoluteFilePath(QString("bp_route_%1.mp4").arg(currentRoute.baseName));
    concatenateSegments(tempVideoPath);
    videoPath = tempVideoPath;
  }
  
  // Open video file
  if (videoFile) {
    videoFile->close();
    delete videoFile;
  }
  
  videoFile = new QFile(videoPath);
  if (!videoFile->open(QIODevice::ReadOnly)) {
    qWarning() << "Failed to open video file:" << videoPath;
    return;
  }
  
  // Get file size for duration estimation
  totalDuration = videoFile->size();
  seekSlider->setMaximum(totalDuration / 1000); // Convert to milliseconds
  
  // Create decoder with callback
  bool isHevc = (type != VideoType::QCamera);
  
#ifdef QCOM2
  decoder = std::make_unique<V4LDecoder>(1920, 1080,
    [this](const DecodedFrame& frame) {
      onFrameDecoded(frame.y, frame.u, frame.v, frame.width, frame.height,
                     frame.stride_y, frame.stride_uv, frame.timestamp_us);
    }, isHevc);
#else
  decoder = std::make_unique<FfmpegDecoder>(1920, 1080, 
    [this](const DecodedFrame& frame) {
      onFrameDecoded(frame.y, frame.u, frame.v, frame.width, frame.height, 
                     frame.stride_y, frame.stride_uv, frame.timestamp_us);
    }, isHevc);
#endif
  
  decoder->decoder_open();
  
  if (isPlaying) {
    startDecoding();
  }
}

void BPEnhancedVideoModal::startDecoding() {
  if (!decoder || !videoFile || !videoFile->isOpen()) return;
  
  decodingTimer->start();
  isPlaying = true;
  playPauseBtn->setText("⏸");
  hideControlsTimer->start();
}

void BPEnhancedVideoModal::stopDecoding() {
  decodingTimer->stop();
  
  if (decoder) {
    decoder->decoder_close();
    decoder.reset();
  }
}

void BPEnhancedVideoModal::onDecodingTimer() {
  if (!decoder || !videoFile || !videoFile->isOpen()) return;
  
  // Read chunk from file
  qint64 bytesRead = videoFile->read((char*)decodeBuffer, DECODE_BUFFER_SIZE);
  if (bytesRead <= 0) {
    // End of file
    pause();
    return;
  }
  
  // Decode frame
  decoder->decode_frame(decodeBuffer, bytesRead, currentPosition, false);
  
  currentPosition += bytesRead;
  
  // Update UI
  int seconds = currentPosition / (1000000); // Approximate
  int minutes = seconds / 60;
  seconds %= 60;
  timeLabel->setText(QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0')));
  
  if (!seekSlider->isSliderDown()) {
    seekSlider->setValue(currentPosition / 1000);
  }
}

void BPEnhancedVideoModal::onFrameDecoded(const uint8_t *y, const uint8_t *u, const uint8_t *v,
                                          int width, int height, int stride_y, int stride_uv,
                                          uint64_t timestamp_us) {
  // Convert YUV to RGB for display
  QImage frame(width, height, QImage::Format_RGB888);
  
  for (int row = 0; row < height; row++) {
    for (int col = 0; col < width; col++) {
      int y_val = y[row * stride_y + col];
      int u_val = u[(row/2) * stride_uv + (col/2)] - 128;
      int v_val = v[(row/2) * stride_uv + (col/2)] - 128;
      
      int r = y_val + 1.402 * v_val;
      int g = y_val - 0.344 * u_val - 0.714 * v_val;
      int b = y_val + 1.772 * u_val;
      
      r = std::max(0, std::min(255, r));
      g = std::max(0, std::min(255, g));
      b = std::max(0, std::min(255, b));
      
      frame.setPixel(col, row, qRgb(r, g, b));
    }
  }
  
  currentFrame = frame.scaled(videoWidget->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  videoWidget->update();
}

void BPEnhancedVideoModal::paintEvent(QPaintEvent *event) {
  QDialog::paintEvent(event);
  
  if (!currentFrame.isNull() && videoWidget) {
    QPainter p(videoWidget);
    p.fillRect(videoWidget->rect(), Qt::black);
    
    // Center the frame
    int x = (videoWidget->width() - currentFrame.width()) / 2;
    int y = (videoWidget->height() - currentFrame.height()) / 2;
    p.drawImage(x, y, currentFrame);
  }
}


QString BPEnhancedVideoModal::getVideoPath(VideoType type) const {
  QString videoFileName;
  switch (type) {
    case VideoType::FCamera:
      videoFileName = "fcamera.hevc";
      break;
    case VideoType::DCamera:
      videoFileName = "dcamera.hevc";
      break;
    case VideoType::ECamera:
      videoFileName = "ecamera.hevc";
      break;
    case VideoType::QCamera:
      videoFileName = "qcamera.ts";
      break;
  }
  
  // Return path to first segment
  return currentRoute.fullPath + "/" + videoFileName;
}

void BPEnhancedVideoModal::concatenateSegments(const QString &outputPath) {
  // Create file list for FFmpeg concat
  QTemporaryFile listFile;
  if (!listFile.open()) return;
  
  QTextStream stream(&listFile);
  for (int i = 0; i < currentRoute.segments; i++) {
    QString segmentPath = currentRoute.fullPath.replace("--0", QString("--%1").arg(i)) + "/" +
                         (currentVideoType == VideoType::QCamera ? "qcamera.ts" : "fcamera.hevc");
    stream << "file '" << segmentPath << "'\n";
  }
  stream.flush();
  
  // Run FFmpeg concat
  QProcess ffmpeg;
  QStringList args;
  args << "-f" << "concat" << "-safe" << "0" << "-i" << listFile.fileName()
       << "-c" << "copy" << outputPath;
  
  ffmpeg.start("ffmpeg", args);
  ffmpeg.waitForFinished();
}

void BPEnhancedVideoModal::onPlayPauseClicked() {
  if (isPlaying) {
    pause();
  } else {
    play();
  }
}

void BPEnhancedVideoModal::play() {
  startDecoding();
}

void BPEnhancedVideoModal::pause() {
  decodingTimer->stop();
  isPlaying = false;
  playPauseBtn->setText("▶");
  hideControlsTimer->stop();
  controlsWidget->show();
}

void BPEnhancedVideoModal::onCameraButtonClicked() {
  QPushButton *btn = qobject_cast<QPushButton*>(sender());
  if (!btn) return;
  
  VideoType newType = currentVideoType;
  if (btn == fcameraBtn) newType = VideoType::FCamera;
  else if (btn == dcameraBtn) newType = VideoType::DCamera;
  else if (btn == ecameraBtn) newType = VideoType::ECamera;
  else if (btn == qcameraBtn) newType = VideoType::QCamera;
  
  if (newType != currentVideoType) {
    loadVideo(newType);
    emit cameraChanged(newType);
  }
}

void BPEnhancedVideoModal::onSeekSliderMoved(int value) {
  if (videoFile && videoFile->isOpen()) {
    qint64 newPos = (qint64)value * 1000; // Convert milliseconds to bytes (approximate)
    videoFile->seek(newPos);
    currentPosition = newPos;
  }
}

void BPEnhancedVideoModal::keyPressEvent(QKeyEvent *event) {
  switch (event->key()) {
    case Qt::Key_Space:
      onPlayPauseClicked();
      break;
    case Qt::Key_Escape:
      close();
      break;
    case Qt::Key_F:
      if (isFullScreen()) {
        showNormal();
      } else {
        showFullScreen();
      }
      break;
    default:
      QDialog::keyPressEvent(event);
  }
}

void BPEnhancedVideoModal::mousePressEvent(QMouseEvent *event) {
  updateControlsVisibility();
  QDialog::mousePressEvent(event);
}

void BPEnhancedVideoModal::updateControlsVisibility() {
  if (controlsWidget->isHidden()) {
    controlsWidget->show();
    if (isPlaying) {
      hideControlsTimer->start();
    }
  } else {
    controlsWidget->hide();
  }
}

void BPEnhancedVideoModal::resizeEvent(QResizeEvent *event) {
  QDialog::resizeEvent(event);
  if (controlsWidget) {
    controlsWidget->resize(size());
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
  
  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(20);
  
  // Thumbnail container
  QWidget *thumbnailContainer = new QWidget(this);
  thumbnailContainer->setFixedSize(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
  thumbnailContainer->setStyleSheet("background: #1a1a1a; border-radius: 8px;");
  
  thumbnailLabel = new QLabel(thumbnailContainer);
  thumbnailLabel->setAlignment(Qt::AlignCenter);
  thumbnailLabel->setFixedSize(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
  thumbnailLabel->setScaledContents(true);
  thumbnailLabel->setText("Loading...");
  thumbnailLabel->setStyleSheet("color: #666; font-size: 18px;");
  
  layout->addWidget(thumbnailContainer);
  
  // Info section
  QVBoxLayout *infoLayout = new QVBoxLayout();
  infoLayout->setSpacing(8);
  
  // Timestamp
  timestampLabel = new QLabel(route.timestamp, this);
  timestampLabel->setStyleSheet("color: white; font-size: 22px; font-weight: bold;");
  infoLayout->addWidget(timestampLabel);
  
  // Duration and distance row
  QHBoxLayout *statsRow = new QHBoxLayout();
  statsRow->setSpacing(20);
  
  durationLabel = new QLabel(QString("⏱ %1").arg(route.duration), this);
  durationLabel->setStyleSheet("color: #2196F3; font-size: 18px; font-weight: 500;");
  statsRow->addWidget(durationLabel);
  
  distanceLabel = new QLabel(QString("📍 %1 mi").arg(route.tripMiles, 0, 'f', 1), this);
  distanceLabel->setStyleSheet("color: #4CAF50; font-size: 18px; font-weight: 500;");
  statsRow->addWidget(distanceLabel);
  
  statsRow->addStretch();
  infoLayout->addLayout(statsRow);
  
  // Metadata row
  QHBoxLayout *metaRow = new QHBoxLayout();
  metaRow->setSpacing(20);
  
  segmentsLabel = new QLabel(QString("%1 segments").arg(route.segments), this);
  segmentsLabel->setStyleSheet("color: #999; font-size: 16px;");
  metaRow->addWidget(segmentsLabel);
  
  sizeLabel = new QLabel(route.size, this);
  sizeLabel->setStyleSheet("color: #999; font-size: 16px;");
  metaRow->addWidget(sizeLabel);
  
  // Badges for available data
  if (route.hasVideo) {
    QLabel *videoBadge = new QLabel("VIDEO", this);
    videoBadge->setStyleSheet(R"(
      background: #4CAF50;
      color: white;
      padding: 4px 8px;
      border-radius: 4px;
      font-size: 12px;
      font-weight: bold;
    )");
    metaRow->addWidget(videoBadge);
  }
  
  if (route.hasRLog) {
    QLabel *logBadge = new QLabel("LOGS", this);
    logBadge->setStyleSheet(R"(
      background: #FF9800;
      color: white;
      padding: 4px 8px;
      border-radius: 4px;
      font-size: 12px;
      font-weight: bold;
    )");
    metaRow->addWidget(logBadge);
  }
  
  metaRow->addStretch();
  infoLayout->addLayout(metaRow);
  
  infoLayout->addStretch();
  layout->addLayout(infoLayout, 1);
  
  // Elapsed time on right
  QLabel *elapsedLabel = new QLabel(route.elapsedTime, this);
  elapsedLabel->setStyleSheet("color: #666; font-size: 16px;");
  layout->addWidget(elapsedLabel);
}

void RouteCardWidget::setThumbnail(const QPixmap &pixmap) {
  thumbnailLabel->setPixmap(pixmap.scaled(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, 
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void RouteCardWidget::mousePressEvent(QMouseEvent *event) {
  isPressed = true;
  update();
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
  
  // Platform detection
#ifdef QCOM2
  isQCOM2 = true;
#else
  isQCOM2 = false;
#endif
  
  routesPath = ROUTES_PATH;
  thumbnailCachePath = THUMBNAIL_CACHE_PATH;
  
  // Ensure cache directory exists
  QDir().mkpath(thumbnailCachePath);
  
  // Find FFmpeg
  ffmpegPath = findFFmpegPath();
  
  setupUI();
  applyStyles();
  
  // Create video modal
  videoModal = std::make_unique<BPEnhancedVideoModal>(this);
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
  header->setFixedHeight(80);
  QHBoxLayout *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(30, 0, 30, 0);
  
  QLabel *titleLabel = new QLabel("Driving Routes", this);
  titleLabel->setStyleSheet("color: white; font-size: 32px; font-weight: bold;");
  headerLayout->addWidget(titleLabel);
  
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
  statusLabel->setStyleSheet("color: #666; font-size: 20px; padding: 50px;");
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
      border-radius: 8px;
      color: white;
      font-size: 18px;
      font-weight: bold;
      padding: 12px 24px;
      min-width: 120px;
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
    } else {
      statusLabel->hide();
      
      // Sort routes by date (newest first)
      std::sort(allRoutes.begin(), allRoutes.end(), 
                [](const RouteInfo &a, const RouteInfo &b) {
                  return a.date > b.date || (a.date == b.date && a.timestamp > b.timestamp);
                });
      
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
  route.hasVideo = hasVideoFiles(fullPath);
  
  // Check for log files
  route.hasRLog = QFile::exists(fullPath + "/rlog.bz2");
  route.hasQLog = QFile::exists(fullPath + "/qlog.bz2");
  
  // Get duration and size (simplified for now)
  route.duration = formatDuration(300); // Placeholder
  route.size = formatSize(1024 * 1024 * 100); // Placeholder
  route.tripMiles = 5.2; // Placeholder
  
  // Get thumbnail path
  route.thumbnailPath = getThumbnailPath(route.baseName);
  
  return route;
}

bool BPRoutesPanel::hasVideoFiles(const QString &routePath) {
  return QFile::exists(routePath + "/fcamera.hevc") ||
         QFile::exists(routePath + "/qcamera.ts");
}

void BPRoutesPanel::addDateSection(const QDate &date) {
  if (loadedDates.contains(date.toString())) {
    return;
  }
  
  QWidget *sectionWidget = new QWidget(this);
  QVBoxLayout *sectionLayout = new QVBoxLayout(sectionWidget);
  sectionLayout->setContentsMargins(0, 20, 0, 10);
  
  QLabel *dateLabel = new QLabel(date.toString("MMMM d, yyyy"), this);
  dateLabel->setStyleSheet("color: #999; font-size: 24px; font-weight: bold;");
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
  
  contentLayout->addWidget(card);
  
  // Initialize thumbnail
  QLabel *thumbnailLabel = card->findChild<QLabel*>();
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
  
  QString videoPath = routesPath + routeBase + "--0/fcamera.hevc";
  if (!QFile::exists(videoPath)) {
    videoPath = routesPath + routeBase + "--0/qcamera.ts";
    if (!QFile::exists(videoPath)) {
      return QString();
    }
  }
  
  QString thumbnailPath = getThumbnailPath(routeBase);
  
  QProcess ffmpeg;
  QStringList args;
  args << "-y"                           // Overwrite output
       << "-nostdin"                      // Non-interactive
       << "-i" << videoPath               // Input video
       << "-vframes" << "1"               // Extract single frame
       << "-an"                           // Disable audio
       << "-vf" << QString("scale=%1:%2").arg(THUMBNAIL_WIDTH).arg(THUMBNAIL_HEIGHT)
       << "-strict" << "unofficial"      // Allow non-standard YUV
       << "-pix_fmt" << "yuvj420p"       // Full-range YUV
       << thumbnailPath;                 // Output
  
  ffmpeg.start(ffmpegPath, args);
  if (ffmpeg.waitForFinished(5000)) {  // 5 second timeout
    if (ffmpeg.exitCode() == 0) {
      return thumbnailPath;
    }
  } else {
    ffmpeg.kill();
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
  if (videoModal) {
    videoModal->setRoute(route);
    videoModal->exec();
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
  
  // Reload
  loadRoutes();
}

void BPRoutesPanel::clearCache() {
  // Clear thumbnail cache
  QDir cacheDir(thumbnailCachePath);
  if (cacheDir.exists()) {
    for (const QString &file : cacheDir.entryList(QDir::Files)) {
      cacheDir.remove(file);
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
  
  // Adjust cards per page based on window height
  int visibleHeight = scrollArea->viewport()->height();
  int cardsVisible = visibleHeight / (CARD_HEIGHT + CARD_SPACING);
  routesPerPage = std::max(5, cardsVisible + 2);  // Load a bit more than visible
}
