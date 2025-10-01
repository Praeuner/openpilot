// bp_video_dialog.cc - Route Video Playback Dialog Implementation
#include "bp_video_dialog.h"
#include "bp_routes_panel.h"
#include "../panels/bp_utils.h"
#include "bp_video_types.h"
#include "bp_frame_reader.h"
#include "bp_video_widget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QIcon>
#include <QTimer>
#include <QKeyEvent>
#include <QDir>
#include <QFileInfo>
#include <QButtonGroup>
#include <QThread>
#include <QtConcurrent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <chrono>
#include <iostream>
#include "third_party/libyuv/include/libyuv.h"
#include "selfdrive/ui/sunnypilot/ui.h"
#include "selfdrive/ui/qt/util.h"

// Custom button class for drawing perfect media control icons
class MediaControlButton : public QPushButton {
  Q_OBJECT

public:
  enum IconType {
    Play,
    Pause,
    RewindArrow,
    ForwardArrow
  };

  MediaControlButton(IconType type, QWidget *parent = nullptr)
    : QPushButton(parent), iconType(type) {
    setStyleSheet(R"(
      QPushButton {
        background: rgba(0, 0, 0, 180);
        border: none;
        border-radius: 75px;
      }
      QPushButton:pressed {
        background: rgba(0, 0, 0, 240);
      }
    )");
  }

  void setIconType(IconType type) {
    iconType = type;
    update(); // Trigger a repaint
  }

protected:
  void paintEvent(QPaintEvent *event) override {
    QPushButton::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255));

    QRect rect = this->rect();
    int centerX = rect.width() / 2;
    int centerY = rect.height() / 2;

    switch (iconType) {
      case Play: {
        // Draw perfect centered play triangle
        int size = rect.width() / 4;
        QPolygon triangle;
        triangle << QPoint(centerX - size/2, centerY - size)
                 << QPoint(centerX - size/2, centerY + size)
                 << QPoint(centerX + size, centerY);
        painter.drawPolygon(triangle);
        break;
      }
      case Pause: {
        // Draw perfect centered pause bars
        int barWidth = rect.width() / 10;
        int barHeight = rect.height() / 3;
        int spacing = barWidth;

        QRect leftBar(centerX - spacing/2 - barWidth, centerY - barHeight/2, barWidth, barHeight);
        QRect rightBar(centerX + spacing/2, centerY - barHeight/2, barWidth, barHeight);

        painter.drawRect(leftBar);
        painter.drawRect(rightBar);
        break;
      }
      case RewindArrow: {
        // Draw circular arrow pointing left (true mirror of +10s direction) for -10s button
        painter.setPen(QPen(QColor(255, 255, 255), 6, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);

        // Draw curved arrow - opposite direction using same approach as ForwardArrow
        int radius = rect.width() / 4;
        QRect arcRect(centerX - radius, centerY - radius, radius * 2, radius * 2);
        painter.drawArc(arcRect, 135 * 16, -270 * 16); // Negative arc span like ForwardArrow

        // Draw arrow head pointing left
        QPolygon arrowHead;
        int arrowSize = 12;
        arrowHead << QPoint(centerX - radius, centerY - radius/4)
                  << QPoint(centerX - radius - arrowSize, centerY)
                  << QPoint(centerX - radius, centerY + radius/4);
        painter.setBrush(QColor(255, 255, 255));
        painter.drawPolygon(arrowHead);

        // Draw "10" in center
        painter.setPen(QColor(255, 255, 255));
        painter.setFont(QFont("Arial", 24, QFont::Bold));
        painter.drawText(rect, Qt::AlignCenter, "10");
        break;
      }
      case ForwardArrow: {
        // Draw circular arrow pointing right with "10" in center
        painter.setPen(QPen(QColor(255, 255, 255), 6, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);

        // Draw curved arrow
        int radius = rect.width() / 4;
        QRect arcRect(centerX - radius, centerY - radius, radius * 2, radius * 2);
        painter.drawArc(arcRect, 315 * 16, -270 * 16); // 270 degree arc going other way

        // Draw arrow head pointing right
        QPolygon arrowHead;
        int arrowSize = 12;
        arrowHead << QPoint(centerX + radius, centerY - radius/4)
                  << QPoint(centerX + radius + arrowSize, centerY)
                  << QPoint(centerX + radius, centerY + radius/4);
        painter.setBrush(QColor(255, 255, 255));
        painter.drawPolygon(arrowHead);

        // Draw "10" in center
        painter.setPen(QColor(255, 255, 255));
        painter.setFont(QFont("Arial", 24, QFont::Bold));
        painter.drawText(rect, Qt::AlignCenter, "10");
        break;
      }
    }
  }

private:
  IconType iconType;
};

// Include the MOC file for Qt's meta-object system
#include "bp_video_dialog.moc"

// ===== FrameBufferPool Implementation =====
FrameBufferPool::FrameBufferPool(size_t maxBuffers) : maxBuffers(maxBuffers) {}

FrameBufferPool::~FrameBufferPool() {
    clear();
}

VisionBuf* FrameBufferPool::acquire(size_t newFrameSize) {
    std::lock_guard<std::mutex> lock(mutex);

    // If frame size changed, clear old buffers
    if (this->frameSize != newFrameSize) {
        while (!available.empty()) {
            available.pop();
        }
        this->frameSize = newFrameSize;
    }

    if (available.empty()) {
        // Create new buffer
        auto buf = std::make_unique<VisionBuf>();
        buf->allocate(newFrameSize);
        return buf.release();
    }

    auto buf = std::move(available.front());
    available.pop();
    return buf.release();
}

void FrameBufferPool::release(VisionBuf* buffer) {
    if (!buffer) return;

    std::lock_guard<std::mutex> lock(mutex);
    if (available.size() < maxBuffers) {
        available.push(std::unique_ptr<VisionBuf>(buffer));
    } else {
        // Pool is full, delete the buffer
        delete buffer;
    }
}

void FrameBufferPool::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    while (!available.empty()) {
        available.pop();
    }
}

// ===== SegmentCache Implementation =====
SegmentCache::SegmentCache(int maxCacheSize) {
    cache.setMaxCost(maxCacheSize);
    loaderPool = new QThreadPool();
    loaderPool->setMaxThreadCount(2); // Limit concurrent segment loads
}

SegmentCache::~SegmentCache() {
    shutdown.store(true);
    cancelPendingLoads();
    loaderPool->waitForDone(3000); // Wait up to 3 seconds
    delete loaderPool;
}

std::shared_ptr<FrameReader> SegmentCache::getSegment(int segmentIndex) {
    QMutexLocker lock(&cacheMutex);
    if (auto cached = cache.object(segmentIndex)) {
        return *cached;
    }
    return nullptr;
}

void SegmentCache::preloadSegment(int segmentIndex, const QString& videoPath, CameraType cameraType) {
    if (shutdown.load() || hasSegment(segmentIndex)) {
        return;
    }

    QtConcurrent::run(loaderPool, [this, segmentIndex, videoPath, cameraType]() {
        if (shutdown.load()) return;

        auto frameReader = std::make_shared<FrameReader>();
        std::atomic<bool> abort{false};

        if (frameReader->load(cameraType, videoPath.toStdString(), false, &abort)) {
            QMutexLocker lock(&cacheMutex);
            if (!shutdown.load()) {
                cache.insert(segmentIndex, new std::shared_ptr<FrameReader>(frameReader));
            }
        }
    });
}

void SegmentCache::clearCache() {
    QMutexLocker lock(&cacheMutex);
    cache.clear();
}

bool SegmentCache::hasSegment(int segmentIndex) const {
    QMutexLocker lock(&cacheMutex);
    return cache.contains(segmentIndex);
}

void SegmentCache::cancelPendingLoads() {
    shutdown.store(true);
}

BPRouteVideoDialog::BPRouteVideoDialog(const QString &routeBase, QWidget *parent)
    : BPDialogBase(parent), routeBaseName(routeBase) {

  std::cout << "[VIDEO DIALOG] === BPRouteVideoDialog Constructor ===" << std::endl;
  std::cout << "[VIDEO DIALOG] Route: " << routeBase.toStdString() << std::endl;

  setWindowTitle("Route Video Playback");

  // Initialize new components
  segmentCache = std::make_unique<SegmentCache>(5);  // Cache up to 5 segments
  bufferPool = std::make_unique<FrameBufferPool>(10);  // Pool of 10 buffers
  stopAllOperations.store(false);

  // Load route info
  QString routePath = static_cast<BPRoutesPanel*>(parent)->getRoutesDir() + "/" + routeBase;
  std::cout << "[VIDEO DIALOG] Route path: " << routePath.toStdString() << std::endl;
  // Copy fields from BPRoutesPanel::RouteInfo to local RouteInfo
  auto sourceRouteInfo = static_cast<BPRoutesPanel*>(parent)->getRouteInfo(routePath);
  routeInfo.baseName = sourceRouteInfo.baseName;
  routeInfo.timestamp = sourceRouteInfo.timestamp;
  routeInfo.endTimestamp = sourceRouteInfo.endTimestamp;
  routeInfo.duration = sourceRouteInfo.duration;
  routeInfo.elapsedTime = sourceRouteInfo.elapsedTime;
  routeInfo.displayDate = sourceRouteInfo.displayDate;
  routeInfo.humanTime = sourceRouteInfo.humanTime;
  routeInfo.segments = sourceRouteInfo.segments;
  routeInfo.size = sourceRouteInfo.size;
  routeInfo.tripMiles = sourceRouteInfo.tripMiles;
  routeInfo.hasVideo = sourceRouteInfo.hasVideo;
  routeInfo.hasRLog = sourceRouteInfo.hasRLog;
  routeInfo.hasQLog = sourceRouteInfo.hasQLog;
  routeInfo.hasFrontVideo = sourceRouteInfo.hasFrontVideo;
  routeInfo.hasWideVideo = sourceRouteInfo.hasWideVideo;
  routeInfo.hasDriverVideo = sourceRouteInfo.hasDriverVideo;
  routeInfo.hasLQVideo = sourceRouteInfo.hasLQVideo;
  routeInfo.hasFrontHQVideo = sourceRouteInfo.hasFrontHQVideo;
  routeInfo.hasFrontLQVideo = sourceRouteInfo.hasFrontLQVideo;
  routeInfo.hasDriverHQVideo = sourceRouteInfo.hasDriverHQVideo;
  routeInfo.isStarred = sourceRouteInfo.isStarred;
  routeInfo.dateTime = sourceRouteInfo.dateTime;

  showDebugPlayerOutput(QString("Route info - Segments: %1, HasFrontHQ: %2, HasFrontLQ: %3, HasWide: %4, HasDriverHQ: %5, HasLQ: %6").arg(routeInfo.segments).arg(routeInfo.hasFrontHQVideo).arg(routeInfo.hasFrontLQVideo).arg(routeInfo.hasWideVideo).arg(routeInfo.hasDriverHQVideo).arg(routeInfo.hasLQVideo));

  showDebugPlayerOutput(QString("Route time info - timestamp: %1, dateTime: %2, displayDate: %3, size: %4")
                        .arg(routeInfo.timestamp).arg(routeInfo.dateTime.toString()).arg(routeInfo.displayDate).arg(routeInfo.size));

  // Load fullscreen SVG icons (now white colored)
  fullscreenIcon = loadPixmap("../assets/offroad/icon_fullscreen.svg", QSize(80, 80));
  minimizeIcon = loadPixmap("../assets/offroad/icon_minimize.svg", QSize(80, 80));

  // Fallback to PNG only if SVG loading completely fails
  if (fullscreenIcon.isNull()) {
    fullscreenIcon = loadPixmap("../assets/offroad/icon_open_fullscreen.png", QSize(80, 80));
  }

  // Skip fallback icon creation during constructor to avoid UI thread blocking
  if (minimizeIcon.isNull()) {
    // Set a simple default instead of using QPainter during constructor
    minimizeIcon = loadPixmap("../assets/offroad/icon_open_fullscreen.png", QSize(80, 80));
  }

  // Initialize timers
  playbackTimer = new QTimer(this);
  positionTimer = new QTimer(this);
  positionTimer->setInterval(100); // Update position every 100ms

  // Overlay auto-hide timer - hide controls after 3 seconds during playback
  overlayFadeTimer = new QTimer(this);
  overlayFadeTimer->setInterval(3000); // Hide after 3 seconds
  overlayFadeTimer->setSingleShot(true);
  connect(overlayFadeTimer, &QTimer::timeout, this, &BPRouteVideoDialog::hideOverlayControls);

  showDebugPlayerOutput(QString("Initialized overlayFadeTimer - interval: %1ms, single shot: %2")
                       .arg(overlayFadeTimer->interval()).arg(overlayFadeTimer->isSingleShot()));

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
          playPauseButton->setIconType(MediaControlButton::Pause);  // Pause icon
        }
        if (positionTimer) {
          positionTimer->start();
        }
        // Start playback timer on UI thread with validation
        if (playbackTimer) {
          playbackTimer->start(50); // 20fps for smoother playback
          showDebugPlayerOutput("Playback timer started on UI thread at 20fps");
        } else {
          showDebugPlayerOutput("Cannot start playback timer - timer not available");
        }

        // Start overlay fade timer to hide controls during auto-play
        if (overlayFadeTimer) {
          overlayFadeTimer->start();
          showDebugPlayerOutput(QString("Started overlay fade timer for auto-play - timer interval: %1ms").arg(overlayFadeTimer->interval()));
        } else {
          showDebugPlayerOutput("ERROR: overlayFadeTimer is null!");
        }
      } else {
        showDebugPlayerOutput("Cannot start playback - invalid video data");
        // Just show thumbnail without starting playback
        isPlaying = false;
        if (playPauseButton) {
          playPauseButton->setIconType(MediaControlButton::Play);
        }
      }
    }, Qt::QueuedConnection);
  });
}

BPRouteVideoDialog::~BPRouteVideoDialog() {
  // Signal all operations to stop immediately
  stopAllOperations.store(true);

  // Stop video playback first
  isPlaying = false;

  // Stop all timers immediately
  for (auto* timer : {playbackTimer, positionTimer, keepAwakeTimer, overlayFadeTimer}) {
    if (timer) {
      timer->stop();
      timer->deleteLater();
    }
  }

  // Cancel any active async operations
  if (activeSeekWatcher && activeSeekWatcher->isRunning()) {
    activeSeekWatcher->cancel();
    activeSeekWatcher->waitForFinished();
  }
  if (segmentLoadWatcher && segmentLoadWatcher->isRunning()) {
    segmentLoadWatcher->cancel();
    segmentLoadWatcher->waitForFinished();
  }

  // Stop and wait for playback thread to finish (if any)
  if (playbackFuture.isRunning()) {
    playbackFuture.cancel();
    playbackFuture.waitForFinished(); // Wait for thread to finish
  }

  // Clean up resources in detached thread to prevent blocking
  std::thread([frameReaderPtr = frameReader,
               segCache = std::move(segmentCache),
               bufPool = std::move(bufferPool)]() mutable {
    // Cleanup happens independently
    segCache.reset();
    bufPool.reset();
  }).detach();

  frameReader.reset();

  // Clear playlists
  currentPlaylist.clear();

  showDebugPlayerOutput("BPRouteVideoDialog destroyed and cleaned up safely");
}

void BPRouteVideoDialog::setupUI() {
  // Main vertical layout to allow full-width header
  QVBoxLayout *mainVerticalLayout = new QVBoxLayout(this);
  mainVerticalLayout->setContentsMargins(0, 0, 0, 0);
  mainVerticalLayout->setSpacing(0);

  // Full-width header
  setupFullWidthHeader();
  mainVerticalLayout->addWidget(headerWidget);

  // Content area with video and camera panel
  QWidget *contentWidget = new QWidget;
  QHBoxLayout *contentLayout = new QHBoxLayout(contentWidget);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);

  // Left side - Video display (85%)
  setupVideoDisplay();
  contentLayout->addWidget(videoContainer, 85);

  // Right side - Camera panel (15%) - made narrower
  setupCameraPanel();
  contentLayout->addWidget(cameraPanel, 15);

  mainVerticalLayout->addWidget(contentWidget, 1);
}

void BPRouteVideoDialog::setupFullWidthHeader() {
  // Full-width header - moved out of video container
  headerWidget = new QWidget;
  headerWidget->setFixedHeight(120);  // Reduced height
  headerWidget->setStyleSheet("background: #202020;");

  QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(40, 15, 40, 15);
  headerLayout->setSpacing(30);

  // Close button - adjusted for reduced header height
  closeButton = new QPushButton("✕");
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

  // Add time to the date - use time extraction priority for better reliability
  QString displayTime;

  // Priority 1: humanTime from route info (should be the best preformatted version)
  if (!routeInfo.humanTime.isEmpty()) {
    displayTime = routeInfo.humanTime;
  }
  // Priority 2: Direct dateTime formatting if humanTime is empty or the same as timestamp
  else if (routeInfo.dateTime.isValid()) {
    displayTime = routeInfo.dateTime.toString("h:mm AP");
  }
  // Priority 3: Try original timestamp if dateTime isn't valid
  else if (!routeInfo.timestamp.isEmpty()) {
    displayTime = routeInfo.timestamp;
  }
  // Priority 4: Extract from route name (format: YYYY-MM-DD--HH-MM-SS) as last resort
  else {
    QStringList parts = routeBaseName.split("--");
    if (parts.size() >= 2) {
      QString timePart = parts[1];
      QStringList timeComponents = timePart.split("-");
      if (timeComponents.size() >= 3) {
        QString hour = timeComponents[0];
        QString minute = timeComponents[1];
        int hourInt = hour.toInt();
        QString ampm = (hourInt >= 12) ? "PM" : "AM";
        if (hourInt > 12) hourInt -= 12;
        if (hourInt == 0) hourInt = 12;
        displayTime = QString("%1:%2 %3").arg(hourInt).arg(minute, 2, '0').arg(ampm);
      }
    }
  }

  // Final fallback
  if (displayTime.isEmpty()) {
    displayTime = "Unknown Time";
  }

  // Debug: Check what we have for time
  std::cout << "[VIDEO DIALOG] Route timestamp: " << routeInfo.timestamp.toStdString()
            << ", DateTime: " << routeInfo.dateTime.toString().toStdString()
            << ", Final displayTime: " << displayTime.toStdString() << std::endl;

  QString fullTitle = QString("%1 at %2").arg(displayDate, displayTime);

  routeTitle = new QLabel(fullTitle);
  routeTitle->setStyleSheet("font-size: 44px; font-weight: 600; color: white; background: transparent;");

  // Subtitle - show only route ID
  QLabel *subtitleLabel = new QLabel(routeBaseName);
  subtitleLabel->setStyleSheet("font-size: 32px; color: #cccccc; background: transparent;");

  titleLayout->addWidget(routeTitle);
  titleLayout->addWidget(subtitleLabel);

  // Right side - route size and segment info
  QVBoxLayout *rightInfoLayout = new QVBoxLayout;
  rightInfoLayout->setSpacing(5);

  // Route size - adjusted for reduced header
  QLabel *sizeLabel = new QLabel(routeInfo.size);
  sizeLabel->setStyleSheet("font-size: 40px; color: #2196F3; font-weight: 600; background: transparent;");
  sizeLabel->setAlignment(Qt::AlignRight);

  // Segment indicator - adjusted for reduced header with smaller font size
  segmentLabel = new QLabel("Loading segments...");
  segmentLabel->setStyleSheet("font-size: 28px; color: #888; font-weight: 500; background: transparent;");
  segmentLabel->setAlignment(Qt::AlignRight);

  rightInfoLayout->addWidget(sizeLabel);
  rightInfoLayout->addWidget(segmentLabel);

  headerLayout->addWidget(closeButton);
  headerLayout->addLayout(titleLayout, 1);
  headerLayout->addLayout(rightInfoLayout);
}

void BPRouteVideoDialog::setupVideoDisplay() {
  videoContainer = new QWidget;
  videoContainer->setStyleSheet("background: #000000;");

  QVBoxLayout *videoLayout = new QVBoxLayout(videoContainer);
  videoLayout->setContentsMargins(0, 0, 0, 0);
  videoLayout->setSpacing(0);

  // Video display area - much larger for iOS style (taller)
  videoDisplay = new BPVideoWidget;
  videoDisplay->setMinimumSize(1400, 900); // Increased height for better aspect ratio
  videoDisplay->setBackgroundColor(QColor("#000000"));
  videoDisplay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  // Connect touch to play/pause or show controls depending on overlay visibility
  connect(videoDisplay, &BPVideoWidget::clicked, this, &BPRouteVideoDialog::onVideoTap);

  videoLayout->addWidget(videoDisplay, 1);

  // Setup overlay controls (iOS style) - no bottom controls needed
  setupOverlayControls();

  // Setup status overlay for loading and buffering feedback
  setupStatusOverlay();

  // Create slider container with proper layout management
  QWidget *sliderContainer = new QWidget(videoContainer);
  sliderContainer->setObjectName("sliderContainer");
  sliderContainer->setStyleSheet(R"(
    QWidget#sliderContainer {
      background: rgba(0, 0, 0, 200);
      border: 2px solid rgba(255, 255, 255, 0.2);
      border-radius: 45px;
    }
  )");

  QHBoxLayout *sliderLayout = new QHBoxLayout(sliderContainer);
  sliderLayout->setContentsMargins(30, 30, 30, 30);
  sliderLayout->setSpacing(20);

  // Position slider - FULL WIDTH with proper expansion
  positionSlider = new QSlider(Qt::Horizontal);
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
    QSlider::handle:horizontal:pressed, QSlider::handle:horizontal:hover {
      background: #2196F3; width: 100px; height: 100px; border-radius: 50px;
      border: 6px solid rgba(33, 150, 243, 0.8); margin: -38px 0;
    }
  )");

  // Time label with fixed width
  timeLabel = new QLabel("0:00 / 0:00");
  timeLabel->setFixedWidth(200);
  timeLabel->setStyleSheet("color: white; font-size: 32px; font-weight: 600; background: transparent;");
  timeLabel->setAlignment(Qt::AlignCenter);

  sliderLayout->addWidget(positionSlider, 1);  // Expands to fill
  sliderLayout->addWidget(timeLabel, 0);       // Fixed width

  sliderContainer->setFixedHeight(160);
  sliderContainer->show();
  sliderContainer->raise();
  sliderContainer->setAttribute(Qt::WA_StyledBackground, true);

  // Initialize positioning after layout setup - simple and clean
  QTimer::singleShot(0, [this]() { updateOverlayPosition(); });

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
  // Create controls container
  controlsWidget = new QWidget(videoDisplay);
  controlsWidget->setStyleSheet("background: transparent;");
  controlsWidget->setFixedSize(700, 200);

  QHBoxLayout *layout = new QHBoxLayout(controlsWidget);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(80);
  layout->setAlignment(Qt::AlignCenter);

  // Control buttons
  auto rewindButton = new MediaControlButton(MediaControlButton::RewindArrow);
  rewindButton->setFixedSize(150, 150);
  connect(rewindButton, &QPushButton::clicked, [this]() {
    currentPosition = qMax(0LL, currentPosition - 10000);
    seekToPosition(currentPosition);
    positionSlider->setValue(currentPosition);
  });

  playPauseButton = new MediaControlButton(MediaControlButton::Pause);
  playPauseButton->setFixedSize(200, 200);
  connect(playPauseButton, &QPushButton::clicked, this, &BPRouteVideoDialog::togglePlayback);

  auto forwardButton = new MediaControlButton(MediaControlButton::ForwardArrow);
  forwardButton->setFixedSize(150, 150);
  connect(forwardButton, &QPushButton::clicked, [this]() {
    currentPosition = qMin(totalDuration, currentPosition + 10000);
    seekToPosition(currentPosition);
    positionSlider->setValue(currentPosition);
  });

  layout->addWidget(rewindButton);
  layout->addWidget(playPauseButton);
  layout->addWidget(forwardButton);

  // Add fullscreen toggle button overlay
  fullscreenToggleButton = new QPushButton();
  fullscreenToggleButton->setParent(videoDisplay);
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
  connect(fullscreenToggleButton, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleFullscreen);
  fullscreenToggleButton->show();
  fullscreenToggleButton->raise();
}

void BPRouteVideoDialog::setupStatusOverlay() {
  // Create status overlay container
  statusOverlay = new QWidget(videoDisplay);
  statusOverlay->setStyleSheet(R"(
    QWidget {
      background: rgba(0, 0, 0, 0.8);
      border-radius: 15px;
      border: 2px solid rgba(255, 255, 255, 0.2);
    }
  )");
  statusOverlay->setFixedSize(400, 200);
  statusOverlay->hide(); // Initially hidden

  QVBoxLayout *overlayLayout = new QVBoxLayout(statusOverlay);
  overlayLayout->setAlignment(Qt::AlignCenter);
  overlayLayout->setSpacing(15);

  // Status indicator (spinner or icon)
  statusIndicator = new QLabel();
  statusIndicator->setAlignment(Qt::AlignCenter);
  statusIndicator->setStyleSheet("color: white; font-size: 48px; background: transparent;");
  statusIndicator->setText("◐"); // Default spinner
  overlayLayout->addWidget(statusIndicator);

  // Loading label
  loadingLabel = new QLabel();
  loadingLabel->setAlignment(Qt::AlignCenter);
  loadingLabel->setStyleSheet("color: white; font-size: 28px; font-weight: 600; background: transparent;");
  loadingLabel->setText("Loading...");
  overlayLayout->addWidget(loadingLabel);

  // Progress bars
  seekProgress = new QProgressBar();
  seekProgress->setStyleSheet(R"(
    QProgressBar {
      border: 2px solid #2196F3;
      border-radius: 10px;
      text-align: center;
      background: rgba(255, 255, 255, 0.1);
      color: white;
      font-weight: bold;
    }
    QProgressBar::chunk {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                  stop:0 #2196F3, stop:1 #1976D2);
      border-radius: 8px;
    }
  )");
  seekProgress->setFixedHeight(30);
  seekProgress->hide();
  overlayLayout->addWidget(seekProgress);

  loadingProgress = new QProgressBar();
  loadingProgress->setStyleSheet(R"(
    QProgressBar {
      border: 2px solid #4CAF50;
      border-radius: 10px;
      text-align: center;
      background: rgba(255, 255, 255, 0.1);
      color: white;
      font-weight: bold;
    }
    QProgressBar::chunk {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                  stop:0 #4CAF50, stop:1 #388E3C);
      border-radius: 8px;
    }
  )");
  loadingProgress->setFixedHeight(30);
  loadingProgress->hide();
  overlayLayout->addWidget(loadingProgress);

  // Position overlay in center of video display
  statusOverlay->move(
    (videoDisplay->width() - statusOverlay->width()) / 2,
    (videoDisplay->height() - statusOverlay->height()) / 2
  );
}

void BPRouteVideoDialog::updateOverlayPosition() {
  if (!videoDisplay) return;

  // Center control buttons
  if (controlsWidget) {
    controlsWidget->move(
      (videoDisplay->width() - controlsWidget->width()) / 2,
      (videoDisplay->height() - controlsWidget->height()) / 2
    );
  }

  // Center status overlay
  if (statusOverlay) {
    statusOverlay->move(
      (videoDisplay->width() - statusOverlay->width()) / 2,
      (videoDisplay->height() - statusOverlay->height()) / 2
    );
  }

  // Position slider container at bottom with dynamic width
  if (QWidget *sliderContainer = videoContainer->findChild<QWidget*>("sliderContainer")) {
    int padding = isFullscreen ? 20 : 40;
    int containerWidth = videoDisplay->width() - (2 * padding);
    int containerX = videoDisplay->x() + padding;
    int containerY = videoDisplay->y() + videoDisplay->height() - 180;

    sliderContainer->resize(containerWidth, 160);
    sliderContainer->move(containerX, containerY);
  }
}

void BPRouteVideoDialog::resizeEvent(QResizeEvent *event) {
  BPDialogBase::resizeEvent(event);
  updateOverlayPosition();
}

void BPRouteVideoDialog::setupCameraPanel() {
  cameraPanel = new QWidget;
  // Reduced height with smaller panel
  cameraPanel->setStyleSheet("background: #2a2a2a; border-left: 1px solid #444444;");

  QVBoxLayout *panelLayout = new QVBoxLayout(cameraPanel);
  panelLayout->setContentsMargins(20, 20, 20, 20);
  panelLayout->setSpacing(15);

  // Camera selection title - adjusted for smaller panel
  QLabel *cameraTitle = new QLabel("Camera Views");
  cameraTitle->setStyleSheet("font-size: 36px; font-weight: 600; color: white; margin-bottom: 10px;");
  panelLayout->addWidget(cameraTitle);

  // Camera buttons
  QButtonGroup *cameraGroup = new QButtonGroup(this);

  // Initialize button pointers to nullptr
  frontCamButton = nullptr;
  wideCamButton = nullptr;
  driverCamButton = nullptr;
  lqCamButton = nullptr;

  // Common button style for all available cameras - adjusted for smaller panel
  QString buttonStyle = R"(
    QPushButton {
      background: #404040;
      color: white;
      font-size: 34px;
      border: 3px solid #555555;
      border-radius: 20px;
      text-align: center;
      font-weight: bold;
      padding: 10px;
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
  showDebugPlayerOutput("=== Camera Button Creation ===");
  showDebugPlayerOutput(QString("Checking hasFrontHQVideo: %1").arg(routeInfo.hasFrontHQVideo ? "true" : "false"));
  if (routeInfo.hasFrontHQVideo) {
    showDebugPlayerOutput("Creating Front Camera button");
    frontCamButton = new QPushButton("Front Camera");
    frontCamButton->setFixedHeight(80);  // Adjusted for smaller panel
    frontCamButton->setCheckable(true);
    frontCamButton->setStyleSheet(buttonStyle);
    cameraGroup->addButton(frontCamButton);
    connect(frontCamButton, &QPushButton::clicked, [this]() { switchCamera("front"); });
    panelLayout->addWidget(frontCamButton);
    showDebugPlayerOutput("Front Camera button created and added");
  } else if (routeInfo.hasFrontLQVideo) {
    // Only show LQ if HQ is not available
    showDebugPlayerOutput("Creating Front LQ Camera button (HQ not available)");
    frontCamButton = new QPushButton("Front Camera (LQ)");
    frontCamButton->setFixedHeight(80);  // Adjusted for smaller panel
    frontCamButton->setCheckable(true);
    frontCamButton->setStyleSheet(buttonStyle);
    cameraGroup->addButton(frontCamButton);
    connect(frontCamButton, &QPushButton::clicked, [this]() { switchCamera("lq"); });
    panelLayout->addWidget(frontCamButton);
    showDebugPlayerOutput("Front LQ Camera button created and added");
  } else {
    showDebugPlayerOutput("Skipping Front Camera button - no video available");
  }

  qDebug() << "[VIDEO DEBUG] Checking hasWideVideo: " << (routeInfo.hasWideVideo ? "true" : "false");
  if (routeInfo.hasWideVideo) {
    qDebug() << "[VIDEO DEBUG] Creating Wide Camera button";
    wideCamButton = new QPushButton("Wide Camera");
    wideCamButton->setFixedHeight(80);  // Adjusted for smaller panel
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
    driverCamButton->setFixedHeight(80);  // Adjusted for smaller panel
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

  // Add spacer to push actions to bottom
  panelLayout->addStretch(1);

  // Actions section - at bottom
  setupActionButtons(panelLayout);
}

void BPRouteVideoDialog::setupActionButtons(QVBoxLayout *parentLayout) {
  // Actions section title
  QLabel *actionsTitle = new QLabel("Actions");
  actionsTitle->setStyleSheet("font-size: 36px; font-weight: 600; color: white; margin-bottom: 15px; margin-top: 20px;");
  parentLayout->addWidget(actionsTitle);

  // Create horizontal flow layout container
  QWidget *actionButtonsContainer = new QWidget;
  QHBoxLayout *buttonLayout = new QHBoxLayout(actionButtonsContainer);
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->setSpacing(15);
  buttonLayout->setAlignment(Qt::AlignCenter);

  // Star button - adjusted for smaller panel
  starButton = new QPushButton;
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
    QPushButton:hover {
      background: #555555;
      border: 3px solid #FFD700;
    }
    QPushButton:pressed {
      background: #333333;
    }
  )");
  connect(starButton, &QPushButton::clicked, this, &BPRouteVideoDialog::toggleStar);

  // Delete button - adjusted for smaller panel
  deleteButton = new QPushButton("🗑");
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
    QPushButton:pressed {
      background: #b71c1c;
    }
    QPushButton:hover {
      background: #c62828;
      border: 3px solid #FFD700;
    }
  )");
  connect(deleteButton, &QPushButton::clicked, this, &BPRouteVideoDialog::deleteRoute);

  // Add buttons to horizontal layout
  buttonLayout->addWidget(starButton);
  buttonLayout->addWidget(deleteButton);

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
        segmentLabel->setStyleSheet("font-size: 28px; color: #ff4444; font-weight: 500; background: transparent;");
      } else {
        segmentLabel->setText(QString("Segment: 1 of %1").arg(currentPlaylist.size()));
        segmentLabel->setStyleSheet("font-size: 28px; color: #888; font-weight: 500; background: transparent;");
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
  frameReader = std::make_shared<FrameReader>();

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

      // Smart preloading logic
      if (currentFrameIndex > totalFrames * 0.8 && currentSegment + 1 < currentPlaylist.size()) {
        // Preload next segment at 80% mark
        int nextSegment = currentSegment + 1;
        if (!segmentCache->hasSegment(nextSegment)) {
          QString nextVideoPath = getVideoPath(currentCameraType, nextSegment);
          CameraType cameraType = (currentCameraType == "driver") ? DriverCam :
                                 (currentCameraType == "wide") ? WideRoadCam : RoadCam;
          segmentCache->preloadSegment(nextSegment, nextVideoPath, cameraType);
          qDebug() << "[VIDEO DEBUG] Preloading next segment:" << nextSegment;
        }
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
      playPauseButton->setIconType(MediaControlButton::Play);
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

    // Restore from paused state if available
    if (pausedState.wasPlaying && pausedState.frameIndex > 0) {
      currentFrameIndex = pausedState.frameIndex;
      currentPosition = pausedState.position;
      currentSegment = pausedState.segment;
      qDebug() << "[VIDEO DEBUG] Restored playback state - frame:" << currentFrameIndex << "position:" << currentPosition;
    }

    updatePlayerState(PlayerState::Playing);

    if (playPauseButton) {
      playPauseButton->setIconType(MediaControlButton::Pause);  // Pause icon
    }
    if (playbackTimer) {
      playbackTimer->start(50); // 20fps
    }
    if (positionTimer) {
      positionTimer->start();
    }

    // Start overlay fade timer to hide controls during playback
    if (overlayFadeTimer) {
      overlayFadeTimer->start();
      qDebug() << "[VIDEO DEBUG] Started overlay fade timer in toggle play - timer interval:" << overlayFadeTimer->interval() << "ms";
    } else {
      qDebug() << "[VIDEO DEBUG] ERROR: overlayFadeTimer is null in toggle play!";
    }

    qDebug() << "[VIDEO DEBUG] Playback and position timers started";
  } else {
    qDebug() << "[VIDEO DEBUG] Stopping playback - saving state";

    // Save current state for precise resume
    pausedState.frameIndex = currentFrameIndex;
    pausedState.segment = currentSegment;
    pausedState.position = currentPosition;
    pausedState.wasPlaying = true;
    pausedState.cameraType = currentCameraType;

    updatePlayerState(PlayerState::Paused);

    if (playPauseButton) {
      playPauseButton->setIconType(MediaControlButton::Play);  // Play icon
    }
    if (playbackTimer) {
      playbackTimer->stop();
    }
    if (positionTimer) {
      positionTimer->stop();
    }

    // Stop overlay fade timer and show controls
    if (overlayFadeTimer) {
      overlayFadeTimer->stop();
    }
    showOverlayControls();

    qDebug() << "[VIDEO DEBUG] Playback state saved - frame:" << pausedState.frameIndex << "position:" << pausedState.position;
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

    // Show loading indicator during camera switch
    updatePlayerState(PlayerState::Loading);

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

    // Clear cache for old camera type
    segmentCache->clearCache();

    // Switch camera type
    currentCameraType = cameraType;

    // Update button states to reflect current selection
    updateCameraButtonStates();

    // Reload video segments for new camera
    loadVideoSegments();

    // Check if new camera actually has video segments
    if (currentPlaylist.isEmpty()) {
      qDebug() << "[VIDEO DEBUG] No segments available for camera:" << cameraType;
      updatePlayerState(PlayerState::Idle);
      showLoadingIndicator("⚠ No video available for " + cameraType + " camera");

      // Try to fallback to another available camera
      handleCameraError(cameraType);
      return;
    }

    // Restore playback state
    currentPosition = savedPosition;
    currentSegment = qMin(savedSegment, currentPlaylist.size() - 1); // Clamp to valid range
    positionSlider->setValue(currentPosition);

    qDebug() << "[VIDEO DEBUG] Restored position: " << currentPosition
              << ", segment: " << currentSegment;

    if (wasPlaying) {
      // Resume playback - wait for segment to load before starting timer
      isPlaying = true;
      playPauseButton->setIconType(MediaControlButton::Pause);  // Pause icon
      positionTimer->start();

      // Ensure frameReader is ready before starting playback timer
      QTimer::singleShot(100, this, [this]() {
        if (frameReader && totalFrames > 0 && frameReader->width > 0 && frameReader->height > 0) {
          if (playbackTimer && isPlaying) {
            playbackTimer->start(50);  // Restart playback timer after segment loads
            updatePlayerState(PlayerState::Playing);
            qDebug() << "[VIDEO DEBUG] Playback timer started after camera switch";
          }
        } else {
          qDebug() << "[VIDEO DEBUG] Cannot start playback timer - frameReader not ready";
          isPlaying = false;
          playPauseButton->setIconType(MediaControlButton::Play);
          updatePlayerState(PlayerState::Idle);
        }
      });
      qDebug() << "[VIDEO DEBUG] Resuming playback after camera switch";
    } else {
      // Show thumbnail for new camera
      loadThumbnail();
      updatePlayerState(PlayerState::Paused);
      qDebug() << "[VIDEO DEBUG] Loaded thumbnail for stopped playback";
    }
  }
}

void BPRouteVideoDialog::handleCameraError(const QString& failedCamera) {
  qDebug() << "[VIDEO DEBUG] Handling camera error for:" << failedCamera;

  // Try alternative cameras in order of preference
  QStringList alternatives;
  if (failedCamera != "front" && routeInfo.hasFrontHQVideo) {
    alternatives << "front";
  }
  if (failedCamera != "wide" && routeInfo.hasWideVideo) {
    alternatives << "wide";
  }
  if (failedCamera != "driver" && routeInfo.hasDriverHQVideo) {
    alternatives << "driver";
  }
  if (failedCamera != "lq" && routeInfo.hasFrontLQVideo) {
    alternatives << "lq";
  }

  for (const QString& altCamera : alternatives) {
    // Test if alternative camera has video
    QString testPath = getVideoPath(altCamera, 0);
    if (QFile::exists(testPath)) {
      qDebug() << "[VIDEO DEBUG] Switching to alternative camera:" << altCamera;
      showLoadingIndicator("⟳ Switching to " + altCamera + " camera...");

      // Delay the switch to show the message
      QTimer::singleShot(1000, this, [this, altCamera]() {
        switchCamera(altCamera);
      });
      return;
    }
  }

  // No alternatives available
  qDebug() << "[VIDEO DEBUG] No alternative cameras available";
  updatePlayerState(PlayerState::Idle);
  showLoadingIndicator("⚠ No video available for this route");
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
    // Hide camera panel AND header widget for true fullscreen
    cameraPanel->hide();
    headerWidget->hide();

    // Update fullscreen toggle button to show exit icon
    if (fullscreenToggleButton) {
      if (!minimizeIcon.isNull()) {
        fullscreenToggleButton->setIcon(QIcon(minimizeIcon));
        fullscreenToggleButton->setText(""); // Clear text if icon loads
        fullscreenToggleButton->setToolTip("Exit fullscreen");
      } else {
        fullscreenToggleButton->setText("⛷");
        fullscreenToggleButton->setIcon(QIcon()); // Clear icon
        fullscreenToggleButton->setToolTip("Exit fullscreen");
      }
    }

    // Make video fill entire container
    videoDisplay->setGeometry(0, 0, videoContainer->width(), videoContainer->height());

    // Update overlay position for fullscreen
    updateOverlayPosition();

    // Force an additional update with slight delay to ensure videoDisplay has the new geometry
    QTimer::singleShot(10, [this]() {
      updateOverlayPosition();
      std::cout << "[VIDEO DEBUG] Fullscreen overlay position update completed" << std::endl;
    });

    std::cout << "[VIDEO DEBUG] Entered fullscreen mode - header and camera panel hidden" << std::endl;
  } else {
    // Exit fullscreen - restore normal layout
    cameraPanel->show();
    headerWidget->show();

    // Update fullscreen toggle button to show fullscreen icon (outward arrows)
    if (fullscreenToggleButton) {
      if (!fullscreenIcon.isNull()) {
        fullscreenToggleButton->setIcon(QIcon(fullscreenIcon));
        fullscreenToggleButton->setText(""); // Clear text if icon loads
        fullscreenToggleButton->setToolTip("Enter fullscreen");
      } else {
        fullscreenToggleButton->setText("⛶");
        fullscreenToggleButton->setIcon(QIcon()); // Clear icon
        fullscreenToggleButton->setToolTip("Enter fullscreen");
      }
    }

    // Restore normal video size (no header offset since header is now separate)
    videoDisplay->setGeometry(0, 0, videoContainer->width(), videoContainer->height());

    // Update overlay position for normal view
    updateOverlayPosition();

    // Force an additional update with slight delay to ensure videoDisplay has the new geometry
    QTimer::singleShot(10, [this]() {
      updateOverlayPosition();
      std::cout << "[VIDEO DEBUG] Normal overlay position update completed" << std::endl;
    });

    std::cout << "[VIDEO DEBUG] Exited fullscreen mode - header and camera panel restored" << std::endl;
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

    // Check if next segment is already cached
    if (auto cachedSegment = segmentCache->getSegment(currentSegment)) {
      qDebug() << "[VIDEO DEBUG] Using cached segment:" << currentSegment;
      frameReader = cachedSegment;
      totalFrames = frameReader->getFrameCount();
      currentFrameIndex = 0;

      // Continue playback immediately
      if (isPlaying && playbackTimer) {
        // No interruption needed - just continue with new segment
        qDebug() << "[VIDEO DEBUG] Seamless transition to cached segment";
      }
    } else {
      // Fall back to normal loading
      qDebug() << "[VIDEO DEBUG] Loading segment normally (not cached):" << currentSegment;
      updatePlayerState(PlayerState::Loading);
      playCurrentSegment();
    }

    // Preload next segment if we're getting close to the end
    if (currentSegment + 1 < currentPlaylist.size()) {
      int nextSegment = currentSegment + 1;
      if (!segmentCache->hasSegment(nextSegment)) {
        QString nextVideoPath = getVideoPath(currentCameraType, nextSegment);
        CameraType cameraType = (currentCameraType == "driver") ? DriverCam :
                               (currentCameraType == "wide") ? WideRoadCam : RoadCam;
        segmentCache->preloadSegment(nextSegment, nextVideoPath, cameraType);
        qDebug() << "[VIDEO DEBUG] Preloading segment after transition:" << nextSegment;
      }
    }

  } else {
    // Playback finished
    isPlaying = false;
    updatePlayerState(PlayerState::Idle);
    playPauseButton->setIconType(MediaControlButton::Play);  // Play icon
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
  if (!frameReader || currentPlaylist.isEmpty() || !videoDisplay || stopAllOperations.load()) {
    qDebug() << "[VIDEO DEBUG] Seek aborted - missing components or stopping";
    return;
  }

  // Cancel any previous seek operation
  if (activeSeekWatcher && activeSeekWatcher->isRunning()) {
    activeSeekWatcher->cancel();
    activeSeekWatcher->waitForFinished();
  }

  // Clamp position to valid range
  positionMs = qBound(0LL, positionMs, totalDuration);
  qDebug() << "[VIDEO DEBUG] Starting async seek to position: " << positionMs << "ms";

  // Update UI immediately
  updatePlayerState(PlayerState::Seeking);
  if (seekProgress) {
    seekProgress->setRange(0, 100);
    seekProgress->setValue(0);
  }

  // Store playback state
  bool wasPlaying = isPlaying;
  if (isPlaying && playbackTimer) {
    playbackTimer->stop();
  }

  // Create seek watcher
  activeSeekWatcher = new QFutureWatcher<void>(this);
  connect(activeSeekWatcher, &QFutureWatcher<void>::finished, this, &BPRouteVideoDialog::onSeekCompleted);

  // Start async seek operation
  auto future = QtConcurrent::run([this, positionMs, wasPlaying]() {
    if (stopAllOperations.load()) return;

    // Calculate which segment and frame to seek to
    qint64 segmentDuration = 60 * 1000; // 60 seconds per segment in ms
    int targetSegment = positionMs / segmentDuration;
    qint64 positionInSegment = positionMs % segmentDuration;

    // Clamp segment to valid range
    targetSegment = qBound(0, targetSegment, currentPlaylist.size() - 1);

    // Report progress
    QMetaObject::invokeMethod(this, [this]() {
      if (seekProgress) seekProgress->setValue(25);
    }, Qt::QueuedConnection);

    // Check if we need to load a different segment
    if (targetSegment != currentSegment) {
      // Check if segment is already cached
      if (auto cachedSegment = segmentCache->getSegment(targetSegment)) {
        QMetaObject::invokeMethod(this, [this, cachedSegment, targetSegment]() {
          frameReader = cachedSegment;
          currentSegment = targetSegment;
          totalFrames = frameReader->getFrameCount();
          if (segmentLabel) {
            segmentLabel->setText(QString("Segment: %1 of %2").arg(currentSegment + 1).arg(currentPlaylist.size()));
          }
        }, Qt::QueuedConnection);
      } else {
        // Load segment synchronously during seek
        QString videoPath = getVideoPath(currentCameraType, targetSegment);

        QMetaObject::invokeMethod(this, [this]() {
          if (seekProgress) seekProgress->setValue(50);
        }, Qt::QueuedConnection);

        // Create new frame reader for target segment
        auto newFrameReader = std::make_unique<FrameReader>();
        CameraType cameraType = (currentCameraType == "driver") ? DriverCam :
                               (currentCameraType == "wide") ? WideRoadCam : RoadCam;

        std::atomic<bool> abort{false};
        if (newFrameReader->load(cameraType, videoPath.toStdString(), false, &abort)) {
          auto sharedReader = std::shared_ptr<FrameReader>(newFrameReader.release());
          QMetaObject::invokeMethod(this, [this, sharedReader, targetSegment]() {
            frameReader = sharedReader;
            currentSegment = targetSegment;
            totalFrames = frameReader->getFrameCount();
            if (segmentLabel) {
              segmentLabel->setText(QString("Segment: %1 of %2").arg(currentSegment + 1).arg(currentPlaylist.size()));
            }
          }, Qt::QueuedConnection);
        }
      }
    }

    if (stopAllOperations.load()) return;

    QMetaObject::invokeMethod(this, [this]() {
      if (seekProgress) seekProgress->setValue(75);
    }, Qt::QueuedConnection);

    // Calculate target frame within segment
    int frameRate = 20;
    int targetFrameInSegment = (positionInSegment * frameRate) / 1000;

    QMetaObject::invokeMethod(this, [this, targetFrameInSegment, positionMs, wasPlaying]() mutable {
      if (stopAllOperations.load()) return;

      // Seek to specific frame on UI thread
      if (frameReader && totalFrames > 0) {
        targetFrameInSegment = qBound(0, targetFrameInSegment, static_cast<int>(totalFrames) - 1);

        // Use buffer pool for seek operation
        size_t frame_size = frameReader->width * frameReader->height * 3 / 2;
        VisionBuf* seekBuf = bufferPool->acquire(frame_size);

        if (seekBuf) {
          seekBuf->init_yuv(frameReader->width, frameReader->height, frameReader->width, frameReader->width * frameReader->height);

          if (frameReader->get(targetFrameInSegment, seekBuf)) {
            videoDisplay->displayFrame(seekBuf, frameReader->width, frameReader->height);
            currentFrameIndex = targetFrameInSegment;
            currentPosition = positionMs;

            if (!isSeeking && positionSlider) {
              positionSlider->setValue(currentPosition);
            }
          }

          bufferPool->release(seekBuf);
        }
      }

      if (seekProgress) seekProgress->setValue(100);

      // Restore playback state
      if (wasPlaying) {
        isPlaying = true;
        updatePlayerState(PlayerState::Playing);
        if (playPauseButton) {
          playPauseButton->setIconType(MediaControlButton::Pause);
        }
        if (!isSeeking && playbackTimer) {
          playbackTimer->start(50);
        }
        if (positionTimer) {
          positionTimer->start();
        }
      } else {
        updatePlayerState(PlayerState::Paused);
      }

    }, Qt::QueuedConnection);
  });

  activeSeekWatcher->setFuture(future);
}

void BPRouteVideoDialog::onSeekCompleted() {
  qDebug() << "[VIDEO DEBUG] Async seek operation completed";

  if (activeSeekWatcher) {
    activeSeekWatcher->deleteLater();
    activeSeekWatcher = nullptr;
  }

  // Hide seek progress after a brief delay
  QTimer::singleShot(200, this, [this]() {
    if (currentPlayerState != PlayerState::Seeking) {
      hideLoadingIndicator();
    }
  });
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

  showDebugPlayerOutput("BPRouteVideoDialog hidden, stopped timers and playback");
}

void BPRouteVideoDialog::hideOverlayControls() {
  showDebugPlayerOutput(QString("hideOverlayControls() called - controlsVisible was: %1").arg(controlsVisible));

  if (controlsWidget) {
    controlsWidget->hide();
    controlsVisible = false;
    showDebugPlayerOutput("Hiding controlsWidget");
  }

  // Hide all overlay widgets except play/pause button
  QList<QPushButton*> buttons = videoDisplay->findChildren<QPushButton*>();
  for (auto btn : buttons) {
    if (btn->parent() == videoDisplay && btn != playPauseButton) {
      btn->hide();
      showDebugPlayerOutput(QString("Hiding overlay button: %1").arg(btn->text()));
    }
  }

  // Hide the bottom slider container
  QWidget *sliderContainer = videoContainer->findChild<QWidget*>("sliderContainer");
  if (sliderContainer) {
    sliderContainer->hide();
    showDebugPlayerOutput("Hiding slider container");
  }

  showDebugPlayerOutput("All overlays should now be hidden");
}

void BPRouteVideoDialog::showOverlayControls() {
  if (controlsWidget) {
    controlsWidget->show();
    controlsVisible = true;
    showDebugPlayerOutput("Showing overlay controls");
  }

  // Show all overlay widgets including fullscreen button
  QList<QPushButton*> buttons = videoDisplay->findChildren<QPushButton*>();
  for (auto btn : buttons) {
    if (btn->parent() == videoDisplay && btn != playPauseButton) {
      btn->show();
      showDebugPlayerOutput(QString("Showing overlay button: %1").arg(btn->text()));
    }
  }

  // Show the bottom slider container
  QWidget *sliderContainer = videoContainer->findChild<QWidget*>("sliderContainer");
  if (sliderContainer) {
    sliderContainer->show();
    showDebugPlayerOutput("Showing slider container");
  }

  // Restart fade timer to hide again after 3 seconds
  if (overlayFadeTimer && isPlaying) {
    overlayFadeTimer->start();
  }
}

void BPRouteVideoDialog::onVideoTap() {
  showDebugPlayerOutput(QString("Video tap detected - controlsVisible: %1").arg(controlsVisible));

  if (!controlsVisible) {
    // Show controls if they're hidden - don't toggle playback yet
    showOverlayControls();
    showDebugPlayerOutput("Showing overlay controls");
  } else {
    // Only toggle play/pause if controls are already visible and we're not in a critical state
    if (frameReader && (totalFrames > 0 || frameReader->width > 0)) {
      togglePlayback();
      showDebugPlayerOutput("Toggling playback");
    } else {
      showDebugPlayerOutput("Video skip playback - invalid frame reader state");
    }
  }
}

void BPRouteVideoDialog::preloadNextSegment() {
  // Placeholder implementation - called when transitioning to next segment
  // This will be implemented when we work on segment preloading
  qDebug() << "[VIDEO DEBUG] preloadNextSegment() called - currently placeholder";
}

void BPRouteVideoDialog::onSegmentTransitionStart() {
  // Placeholder implementation - called when starting segment transition
  // This will be implemented when we work on segment transitions
  qDebug() << "[VIDEO DEBUG] onSegmentTransitionStart() called - currently placeholder";
}

void BPRouteVideoDialog::onSegmentPreloaded() {
  // Called when a segment has been successfully preloaded into cache
  qDebug() << "[VIDEO DEBUG] onSegmentPreloaded() called - segment cached for smooth transition";
}

void BPRouteVideoDialog::updatePlayerState(PlayerState state) {
  if (currentPlayerState == state) return;

  currentPlayerState = state;

  switch(state) {
    case PlayerState::Idle:
      hideLoadingIndicator();
      break;
    case PlayerState::Playing:
      hideLoadingIndicator();
      break;
    case PlayerState::Paused:
      hideLoadingIndicator();
      break;
    case PlayerState::Buffering:
      showLoadingIndicator("⟳ Buffering...");
      break;
    case PlayerState::Seeking:
      showLoadingIndicator("⟲ Seeking...");
      if (seekProgress) {
        seekProgress->show();
      }
      break;
    case PlayerState::Loading:
      showLoadingIndicator("⟳ Loading segment...");
      if (loadingProgress) {
        loadingProgress->show();
      }
      break;
  }
}

void BPRouteVideoDialog::showLoadingIndicator(const QString& message) {
  if (!statusOverlay || !loadingLabel) return;

  loadingLabel->setText(message);
  statusOverlay->show();
  statusOverlay->raise();

  // Animate the spinner
  static QTimer *spinTimer = nullptr;

  if (!spinTimer) {
    spinTimer = new QTimer(this);
    connect(spinTimer, &QTimer::timeout, [this]() {
      static const QStringList spinStates = {"◐", "◓", "◑", "◒"};
      static int state = 0;
      if (statusIndicator) {
        statusIndicator->setText(spinStates[state % spinStates.size()]);
        state++;
      }
    });
  }

  spinTimer->start(200); // Rotate every 200ms
}

void BPRouteVideoDialog::hideLoadingIndicator() {
  if (statusOverlay) {
    statusOverlay->hide();
  }
  if (seekProgress) {
    seekProgress->hide();
  }
  if (loadingProgress) {
    loadingProgress->hide();
  }

  // Stop spinner animation
  if (auto spinTimer = findChild<QTimer*>()) {
    spinTimer->stop();
  }
}

void BPRouteVideoDialog::showDebugPlayerOutput(const QString &message) {
  // Access the parent routes panel's debug functionality
  if (auto routesPanel = qobject_cast<BPRoutesPanel*>(parent())) {
    routesPanel->showDebugPlayerOutput(message);
  }
}
