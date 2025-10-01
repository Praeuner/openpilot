// bp_routes_panel.cc
#include "bp_routes_panel.h"
#include "bp_video_dialog.h"
#include "selfdrive/ui/bluepilot/bp_logging.h"
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QMouseEvent>
#include <QFormLayout>
#include <QStandardPaths>
#include <QApplication>
#include <QGuiApplication>
#include <QtConcurrent>
#include <QFuture>
#include <algorithm>
#include <QVideoWidget>
#include <QMediaPlayer>
#include <QUrl>
#include <QScroller>
#include <QDialog>
#include <QVBoxLayout>

#include <QHBoxLayout>
#include <QTimer>
#include <QScrollBar>
#include <QButtonGroup>
#include <QSpacerItem>
#include <QSet>
#include <iostream>
#include <QJsonDocument>
#include <QJsonObject>
#include "bp_frame_reader.h"
#include "msgq/visionipc/visionbuf.h"
#include "third_party/libyuv/include/libyuv.h"
#include "third_party/libyuv/include/libyuv/scale_argb.h"
#include <QImage>
#include <QBuffer>
#include <QJsonArray>

#include "common/params.h"
#ifdef QCOM2
// #include "system/loggerd/decoder/thumbnail_decoder.h"  // Disabled - using FFmpeg for now
#endif

QString BPRoutesPanel::getRoutesDir() const {
  return getAbsolutePath(isCommaDevice() ? "/data/media/0/realdata" : "~/comma_data/media/0/realdata");
}

BPRoutesPanel::BPRoutesPanel(QWidget *parent) : QWidget(parent), isLoading(false) {
  setObjectName("routesPanel");

  // Set size constraints
  setMinimumWidth(1000);
  setMaximumWidth(1920);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

  activityTimer = new QTimer(static_cast<QObject *>(this));
  activityTimer->setInterval(9000); // 9 seconds
  connect(activityTimer, &QTimer::timeout, this, &BPRoutesPanel::simulateActivity);

  // Monitor onroad status transitions
  QObject::connect(uiState(), &UIState::offroadTransition, this, &BPRoutesPanel::onOffroadTransition);

  setupStyles();
  setupUI();
}

BPRoutesPanel::~BPRoutesPanel() {
  // Cleanup thumbnail watchers
  for (auto watcher : thumbnailWatchers) {
    watcher->cancel();
    watcher->deleteLater();
  }
  thumbnailWatchers.clear();
}

void BPRoutesPanel::setupStyles() {
  setStyleSheet(R"(
    BPRoutesPanel {
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                  stop:0 #1a1a1a, stop:1 #0f0f0f);
    }

    QScrollArea {
      background: transparent;
      border: none;
    }

    QScrollBar:vertical {
      background: #2a2a2a;
      width: 15px;
      border-radius: 7px;
      margin: 0;
    }

    QScrollBar::handle:vertical {
      background: #505050;
      border-radius: 7px;
      min-height: 20px;
    }

    QScrollBar::handle:vertical:hover {
      background: #606060;
    }

    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
      border: none;
      background: none;
    }
  )");
}

void BPRoutesPanel::setupUI() {
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(30, 30, 30, 30);
  mainLayout->setSpacing(20);

  // Header (120px for better visibility on 6" display)
  QWidget *headerWidget = new QWidget;
  headerWidget->setFixedHeight(120);
  headerWidget->setStyleSheet("background: transparent;");

  QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(20);

  // Title and stats
  QVBoxLayout *titleStatsLayout = new QVBoxLayout;
  titleStatsLayout->setSpacing(5);

  titleLabel = new QLabel("Driving Routes");
  titleLabel->setStyleSheet("font-size: 48px; font-weight: 600; color: white;");

  statsLabel = new QLabel;
  statsLabel->setStyleSheet("font-size: 32px; color: #cccccc;");

  titleStatsLayout->addWidget(titleLabel);
  titleStatsLayout->addWidget(statsLabel);

  headerLayout->addLayout(titleStatsLayout, 1);

  // Action buttons
  refreshButton = new QPushButton("Refresh");
  refreshButton->setFixedSize(180, 80);
  refreshButton->setStyleSheet(R"(
    QPushButton {
      background-color: #2196F3;
      color: white;
      font-size: 28px;
      font-weight: 500;
      border: none;
      border-radius: 10px;
    }
    QPushButton:pressed {
      background-color: #1976D2;
    }
  )");

  QPushButton *clearCacheButton = new QPushButton("Clear Cache");
  clearCacheButton->setFixedSize(180, 80);
  clearCacheButton->setStyleSheet(refreshButton->styleSheet());

  headerLayout->addWidget(refreshButton);
  headerLayout->addWidget(clearCacheButton);

  // Connect buttons
  connect(refreshButton, &QPushButton::clicked, this, &BPRoutesPanel::handleRefresh);
  connect(clearCacheButton, &QPushButton::clicked, this, &BPRoutesPanel::cleanupThumbnailCache);

  mainLayout->addWidget(headerWidget);

  // Routes scroll area
  scrollArea = new BPScrollView;
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scrollArea->setWidgetResizable(true);

  // Routes container
  routesContainer = new QWidget;
  routesLayout = new QVBoxLayout(routesContainer);
  routesLayout->setContentsMargins(0, 0, 0, 0);
  routesLayout->setSpacing(15);

  scrollArea->setWidget(routesContainer);
  mainLayout->addWidget(scrollArea, 1);

  // Connect scroll area for lazy loading
  connect(scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
    QScrollBar *scrollBar = scrollArea->verticalScrollBar();
    if (scrollBar->maximum() > 0 && value >= scrollBar->maximum() - 100) {
      loadMoreRoutes();
    }
  });
}


void BPRoutesPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  resetMaxDurationTimer();
  activityTimer->start();

  // Check if device is onroad - if so, show message and don't load routes
  if (uiState()->scene.started) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] showEvent | Device is onroad - showing safety message" << std::endl;
    showOnroadMessage();
    return;
  }

  if (routes.isEmpty()) {
    loadRoutes();
  }
}

void BPRoutesPanel::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  stopActivitySimulation();
}

void BPRoutesPanel::loadRoutes() {
  BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRoutes | loadRoutes() called" << std::endl;

  if (isLoading) return;

  // Safety check: Don't load routes while onroad
  if (uiState()->scene.started) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRoutes | Aborting route loading - device is onroad" << std::endl;
    showOnroadMessage();
    return;
  }

  isLoading = true;
  showLoadingOverlay("Loading routes...");

  // Clear existing data
  routes.clear();
  displayedRoutes.clear();
  loadedCount = 0;
  dateGroupWidgets.clear();

  // Clear layout
  QLayoutItem *child;
  while ((child = routesLayout->takeAt(0)) != nullptr) {
    delete child->widget();
    delete child;
  }

  // Try to load from cache first
  bool cacheLoaded = loadRouteCacheFromDisk();

  // If cache is loaded and valid, and no routes need refreshing, use cache
  if (cacheLoaded && routeCache.isValid() && !shouldRefreshRoutes() && !routeCache.routeInfoCache.isEmpty()) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRoutes | Loading routes from valid cache" << std::endl;

    // Convert cached data to routes vector
    QVector<RouteInfo> cachedRoutes;
    for (const RouteInfo &info : routeCache.routeInfoCache.values()) {
      cachedRoutes.append(info);
    }

    // Sort by date/time (newest first)
    std::sort(cachedRoutes.begin(), cachedRoutes.end(), [](const RouteInfo &a, const RouteInfo &b) {
      return a.dateTime > b.dateTime;
    });

    QMetaObject::invokeMethod(this, [this, cachedRoutes]() {
      routes = cachedRoutes;
      isLoading = false;
      hideLoadingOverlay();
      updateStats();
      loadMoreRoutes(); // Load first batch
      generateAllMissingThumbnails(); // Generate all missing thumbnails in background
    }, Qt::QueuedConnection);

    return;
  }

  // Cache not available or needs updating - load incrementally
  // BPLog::bpInfo() << "[bp.routes.panel] loadRoutes | Loading routes incrementally (cache loaded: " << (cacheLoaded ? "yes" : "no") << ")" << std::endl;

  QtConcurrent::run([this]() {
    QDir routesDir(getRoutesDir());
    QStringList routeDirectories = routesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);

    // BPLog::bpInfo() << "[bp.routes.panel] loadRoutes | Found " << routeDirectories.size() << " total directories in " << getRoutesDir().toStdString() << std::endl;

    QVector<RouteInfo> newRoutes;
    QHash<QString, RouteInfo> baseRouteMap; // Map to store the latest segment for each base route
    QSet<QString> processedRoutes; // Track which base routes we've already processed

    int skippedCount = 0;
    for (const QString &routeDir : routeDirectories) {
      // Skip non-route directories
      if (routeDir == "boot" || routeDir == "crash" || !routeDir.contains("--")) {
        skippedCount++;
        continue;
      }

      QString routePath = routesDir.absoluteFilePath(routeDir);

      // Extract base route name (remove segment suffix)
      QString baseRouteName = routeDir;
      QRegExp segmentRegex("--\\d+$");
      if (segmentRegex.indexIn(baseRouteName) != -1) {
        baseRouteName = baseRouteName.left(segmentRegex.pos());
      }

      // Skip if we've already processed this base route
      if (processedRoutes.contains(baseRouteName)) {
        continue;
      }
      processedRoutes.insert(baseRouteName);

      // BPLog::bpInfo() << "[bp.routes.panel] loadRoutes | Processing directory: " << routeDir.toStdString() << " Base: " << baseRouteName.toStdString() << std::endl;

      // Check if we have valid cached data for this route
      bool useCache = routeCache.isValid() && routeCache.routeInfoCache.contains(baseRouteName);
      if (useCache) {
        // Check if the route on disk is newer than cache
        QFileInfo routeInfo(routePath);
        if (routeInfo.lastModified() <= routeCache.lastUpdated) {
          // BPLog::bpInfo() << "[bp.routes.panel] loadRoutes | Using cached info for route: " << baseRouteName.toStdString() << std::endl;
          baseRouteMap[baseRouteName] = routeCache.routeInfoCache[baseRouteName];
          continue;
        } else {
          // BPLog::bpInfo() << "[bp.routes.panel] loadRoutes | Route modified, reprocessing: " << baseRouteName.toStdString() << std::endl;
        }
      }

      // Cache miss or route modified - process route info
      RouteInfo info = getRouteInfo(routePath);
      if (info.baseName.isEmpty()) {
        continue;
      }

      // Calculate total size for all segments of this base route
      QDir routesDirInner(getRoutesDir());
      QStringList allSegments = routesDirInner.entryList(QStringList() << baseRouteName + "--*", QDir::Dirs | QDir::NoDotAndDotDot);
      qint64 totalSize = 0;
      for (const QString &segment : allSegments) {
        QString segmentPath = routesDirInner.absoluteFilePath(segment);
        totalSize += calculateDirSize(segmentPath);
      }
      info.size = formatSize(totalSize);
      info.segments = allSegments.size();
      info.duration = getRouteDuration(baseRouteName);

      baseRouteMap[baseRouteName] = info;
    }

    BPLog::bpInfo() << "[bp.routes.panel] loadRoutes | Skipped " << skippedCount << " directories, processed " << processedRoutes.size() << " unique base routes" << std::endl;

    // Convert map to vector
    for (const RouteInfo &info : baseRouteMap.values()) {
      newRoutes.append(info);
    }

    // BPLog::bpInfo() << "[bp.routes.panel] loadRoutes | Final route count: " << newRoutes.size() << std::endl;

    // Sort by date/time (newest first)
    std::sort(newRoutes.begin(), newRoutes.end(), [](const RouteInfo &a, const RouteInfo &b) {
      return a.dateTime > b.dateTime;
    });

    QMetaObject::invokeMethod(this, [this, newRoutes]() {
      routes = newRoutes;

      // Update cache with new data
      routeCache.routeInfoCache.clear();
      for (const RouteInfo &info : newRoutes) {
        routeCache.routeInfoCache[info.baseName] = info;
      }
      routeCache.update();
      saveRouteCacheToDisk();

      isLoading = false;
      hideLoadingOverlay();
      updateStats();
      loadMoreRoutes(); // Load first batch
      generateAllMissingThumbnails(); // Generate all missing thumbnails in background
    }, Qt::QueuedConnection);
  });
}

void BPRoutesPanel::loadMoreRoutes() {
  if (isLoading || loadedCount >= routes.size()) return;

  int endIndex = qMin(loadedCount + ROUTES_PER_LOAD, routes.size());

  for (int i = loadedCount; i < endIndex; i++) {
    const RouteInfo &route = routes[i];

    // Check if we need a new date group
    if (!dateGroupWidgets.contains(route.displayDate)) {
      QWidget *dateGroup = createDateGroup(route.displayDate);
      routesLayout->addWidget(dateGroup);
      dateGroupWidgets[route.displayDate] = dateGroup;
    }

    // Create route card
    // BPLog::bpInfo() << "[bp.routes.panel] loadRoutes | About to create route card for: " << route.baseName.toStdString() << std::endl;
    QWidget *routeCard = createRouteCard(route);
    routesLayout->addWidget(routeCard);

    // Check if this is the last route in this date group
    bool isLastInGroup = (i == endIndex - 1) ||
                         (i + 1 < routes.size() && routes[i + 1].displayDate != route.displayDate);
    if (isLastInGroup) {
      // Add extra spacing after the last route in a group
      QWidget *spacer = new QWidget;
      spacer->setFixedHeight(30);
      spacer->setStyleSheet("background: transparent;");
      routesLayout->addWidget(spacer);
    }

    displayedRoutes.append(route);
  }

  // Add spacer at the end
  routesLayout->addStretch();

  loadedCount = endIndex;
}

QWidget* BPRoutesPanel::createDateGroup(const QString &dateText) {
  // BPLog::bpInfo() << "[bp.routes.panel] loadRoutes | createDateGroup called with text: " << dateText.toStdString() << std::endl;

  QWidget *dateGroup = new QWidget;
  dateGroup->setFixedHeight(90);
  dateGroup->setStyleSheet("background: transparent;");

  QHBoxLayout *dateLayout = new QHBoxLayout(dateGroup);
  dateLayout->setContentsMargins(10, 20, 10, 20);

  QLabel *dateLabel = new QLabel(dateText);
  dateLabel->setStyleSheet("font-size: 56px; font-weight: 600; color: white;");

  // BPLog::bpInfo() << "[bp.routes.panel] loadRoutes | Created date label with text: " << dateLabel->text().toStdString() << std::endl;

  dateLayout->addWidget(dateLabel);
  dateLayout->addStretch();

  return dateGroup;
}

QWidget* BPRoutesPanel::createRouteCard(const RouteInfo &route) {
  // BPLog::bpInfo() << "[bp.routes.panel] createRouteCard | createRouteCard called for: " << route.baseName.toStdString() << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] createRouteCard | Card data - Timestamp: " << route.timestamp.toStdString() << " ElapsedTime: " << route.elapsedTime.toStdString() << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] createRouteCard | Card data - Duration: " << route.duration.toStdString() << " Segments: " << route.segments << " Size: " << route.size.toStdString() << std::endl;

  QWidget *card = new QWidget;
  card->setFixedHeight(400);
  card->setStyleSheet(R"(
    QWidget {
      background: #2a2a2a;
      border: 1px solid #333;
      border-radius: 12px;
    }
    QWidget:hover {
      background: #333;
      border: 1px solid #2196F3;
    }
  )");

  QHBoxLayout *cardLayout = new QHBoxLayout(card);
  cardLayout->setContentsMargins(25, 25, 25, 25);
  cardLayout->setSpacing(20);

  // Video thumbnail (scaled for 6" display visibility)
  QLabel *thumbnail = new QLabel;
  thumbnail->setFixedSize(480, 270);
  thumbnail->setStyleSheet(R"(
    border: 2px solid #1a1a1a;
    border-radius: 10px;
    background: #000;
  )");
  thumbnail->setAlignment(Qt::AlignCenter);
  thumbnail->setScaledContents(true);

  // Set placeholder text with better styling
  thumbnail->setText("🎬");
  thumbnail->setStyleSheet(thumbnail->styleSheet() + "color: #666; font-size: 80px;");

  // Initialize thumbnail loading - extract base route name for thumbnail generation
  QString baseRouteName = route.baseName;
  QRegExp segmentRegex("--\\d+$");
  if (segmentRegex.indexIn(baseRouteName) != -1) {
    baseRouteName = baseRouteName.left(segmentRegex.pos());
  }
  initializeThumbnail(thumbnail, baseRouteName);

  cardLayout->addWidget(thumbnail);

  // Route info section - main content area
  QVBoxLayout *infoLayout = new QVBoxLayout;
  infoLayout->setSpacing(12);

  // Top row - Route date/time display
  QHBoxLayout *topRow = new QHBoxLayout;
  topRow->setSpacing(15);

  // Format route name as time only (e.g., "9:10am")
  QString displayName = route.dateTime.toString("h:mmap");
  QLabel *routeLabel = new QLabel(displayName);
  routeLabel->setStyleSheet("font-size: 52px; font-weight: 600; color: white;");
  topRow->addWidget(routeLabel);

  topRow->addStretch();

  // Blue timestamp removed per request

  infoLayout->addLayout(topRow);

  // Middle row - Duration, segments, size
  QHBoxLayout *middleRow = new QHBoxLayout;
  middleRow->setSpacing(25);

  QLabel *durationLabel = new QLabel(QString("⏱ %1").arg(route.duration));
  durationLabel->setStyleSheet("font-size: 36px; color: #2196F3; font-weight: 500;");
  middleRow->addWidget(durationLabel);

  QLabel *segmentsLabel = new QLabel(QString("📦 %1 segments").arg(route.segments));
  segmentsLabel->setStyleSheet("font-size: 36px; color: #bbb;");
  middleRow->addWidget(segmentsLabel);

  QLabel *sizeLabel = new QLabel(QString("💾 %1").arg(route.size));
  sizeLabel->setStyleSheet("font-size: 36px; color: #bbb;");
  middleRow->addWidget(sizeLabel);

  middleRow->addStretch();
  infoLayout->addLayout(middleRow);

  // Camera badges in 2 rows (max 3 per row)
  QVBoxLayout *badgesContainer = new QVBoxLayout;
  badgesContainer->setSpacing(8);

  QHBoxLayout *badgesRow1 = new QHBoxLayout;
  badgesRow1->setSpacing(8);
  QHBoxLayout *badgesRow2 = new QHBoxLayout;
  badgesRow2->setSpacing(8);

  auto createBadge = [](const QString &text, const QString &color) {
    QLabel *badge = new QLabel(text);
    badge->setStyleSheet(QString(R"(
      background: %1;
      color: white;
      padding: 10px 18px;
      border-radius: 10px;
      font-size: 28px;
      font-weight: 500;
    )").arg(color));
    return badge;
  };

  int badgeCount = 0;
  if (route.hasFrontHQVideo) {
    if (badgeCount < 3) {
      badgesRow1->addWidget(createBadge("Front-HQ", "#2196F3"));
    } else {
      badgesRow2->addWidget(createBadge("Front-HQ", "#2196F3"));
    }
    badgeCount++;
  }
  if (route.hasFrontLQVideo) {
    if (badgeCount < 3) {
      badgesRow1->addWidget(createBadge("Front-LQ", "#9C27B0"));
    } else {
      badgesRow2->addWidget(createBadge("Front-LQ", "#9C27B0"));
    }
    badgeCount++;
  }
  if (route.hasWideVideo) {
    if (badgeCount < 3) {
      badgesRow1->addWidget(createBadge("Wide", "#4CAF50"));
    } else {
      badgesRow2->addWidget(createBadge("Wide", "#4CAF50"));
    }
    badgeCount++;
  }
  if (route.hasDriverHQVideo) {
    if (badgeCount < 3) {
      badgesRow1->addWidget(createBadge("Driver", "#FF9800"));
    } else {
      badgesRow2->addWidget(createBadge("Driver", "#FF9800"));
    }
    badgeCount++;
  }
  if (route.hasRLog || route.hasQLog) {
    if (badgeCount < 3) {
      badgesRow1->addWidget(createBadge("Logs", "#607D8B"));
    } else {
      badgesRow2->addWidget(createBadge("Logs", "#607D8B"));
    }
    badgeCount++;
  }

  badgesRow1->addStretch();
  badgesRow2->addStretch();

  if (badgeCount > 0) {
    badgesContainer->addLayout(badgesRow1);
  }
  if (badgeCount > 3) {
    badgesContainer->addLayout(badgesRow2);
  }

  infoLayout->addLayout(badgesContainer);
  infoLayout->addStretch();

  cardLayout->addLayout(infoLayout, 1);

  // Right side - Star button and elapsed time
  QVBoxLayout *rightLayout = new QVBoxLayout;
  rightLayout->setSpacing(0);
  rightLayout->setAlignment(Qt::AlignTop);

  // Star button
  QPushButton *starButton = new QPushButton;
  starButton->setFixedSize(70, 70);
  starButton->setText(route.isStarred ? "★" : "☆");
  starButton->setObjectName("starButton");
  starButton->setStyleSheet(R"(
    QPushButton {
      background: transparent;
      border: none;
      font-size: 44px;
      color: #FFD700;
      padding: 0;
    }
    QPushButton:hover {
      background: rgba(255, 215, 0, 0.2);
      border-radius: 35px;
    }
    QPushButton:pressed {
      background: rgba(255, 215, 0, 0.3);
    }
  )");

  QString routeBase = route.baseName;
  connect(starButton, &QPushButton::clicked, [this, routeBase]() {
    handleRouteStarToggle(routeBase);
  });

  rightLayout->addWidget(starButton, 0, Qt::AlignRight | Qt::AlignTop);

  // Add spacer
  rightLayout->addSpacing(40);

  // Elapsed time at bottom
  QLabel *elapsedLabel = new QLabel(route.elapsedTime);
  elapsedLabel->setStyleSheet("font-size: 32px; color: #888;");
  elapsedLabel->setAlignment(Qt::AlignRight);
  rightLayout->addWidget(elapsedLabel, 0, Qt::AlignRight);

  rightLayout->addStretch();
  cardLayout->addLayout(rightLayout);

  // Make card clickable for video playback
  card->setProperty("routeBase", route.baseName);
  card->installEventFilter(this);

  return card;
}

// Event filter for route card clicks
bool BPRoutesPanel::eventFilter(QObject *obj, QEvent *event) {
  static QHash<QWidget*, QPoint> pressPosMap;
  static const int CLICK_THRESHOLD = 15; // pixels of movement allowed for a click

  if (event->type() == QEvent::MouseButtonPress) {
    QWidget *widget = qobject_cast<QWidget*>(obj);
    if (widget && widget->property("routeBase").isValid()) {
      QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
      pressPosMap[widget] = mouseEvent->pos();
      return false; // Don't consume the event, let scrolling work
    }
  } else if (event->type() == QEvent::MouseButtonRelease) {
    QWidget *widget = qobject_cast<QWidget*>(obj);
    if (widget && widget->property("routeBase").isValid() && pressPosMap.contains(widget)) {
      QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
      QPoint pressPos = pressPosMap[widget];
      pressPosMap.remove(widget);

      // Check if the release is close to the press position (actual click, not scroll)
      if ((mouseEvent->pos() - pressPos).manhattanLength() <= CLICK_THRESHOLD) {
        // Check if the click is on a child widget (like the star button)
        QWidget *childAt = widget->childAt(mouseEvent->pos());
        if (childAt) {
          // If it's a button, don't handle it here
          QPushButton *button = qobject_cast<QPushButton*>(childAt);
          if (button) {
            return false; // Let the button handle its own click
          }
        }

        QString routeBase = widget->property("routeBase").toString();
        handleRouteVideoPlayback(routeBase);
        return true;
      }
    }
  }
  return QWidget::eventFilter(obj, event);
}

void BPRoutesPanel::handleRouteVideoPlayback(const QString &route, const QString &cameraType) {
  currentSelectedRoute = route;

  // Debug info
  BPLog::bpDebugRoutes() << "[bp.routes.panel] handleRouteVideoPlayback | Opening video dialog for route: " << route.toStdString() << std::endl;
  BPLog::bpDebugRoutes() << "[bp.routes.panel] handleRouteVideoPlayback | Route path: " << (getRoutesDir() + "/" + route).toStdString() << std::endl;

  BPRouteVideoDialog *videoDialog = new BPRouteVideoDialog(route, this);
  if (videoDialog) {
    videoDialog->setupFullscreen();  // Must call setupFullscreen BEFORE exec() for proper QCOM2 rotation
    videoDialog->exec();
    videoDialog->deleteLater();
  } else {
    BPLog::bpWarn() << "[bp.routes.panel] handleRouteVideoPlayback | Failed to create video dialog" << std::endl;
  }
}

void BPRoutesPanel::handleRouteStarToggle(const QString &route) {
  bool currentlyStarred = isRouteStarred(route);
  setRouteStarred(route, !currentlyStarred);

  // Update the star state in the cache
  if (routeCache.routeInfoCache.contains(route)) {
    routeCache.routeInfoCache[route].isStarred = !currentlyStarred;
  }

  // Update the star state in the routes vector
  for (RouteInfo &routeInfo : routes) {
    if (routeInfo.baseName == route) {
      routeInfo.isStarred = !currentlyStarred;
      break;
    }
  }

  // Find and update the star button directly instead of reloading
  for (QWidget *widget : routesContainer->findChildren<QWidget*>()) {
    if (widget->property("routeBase").toString() == route) {
      QPushButton *starButton = widget->findChild<QPushButton*>("starButton");
      if (starButton) {
        starButton->setText(!currentlyStarred ? "★" : "☆");
      }
      break;
    }
  }
}

void BPRoutesPanel::updateStats() {
  qint64 totalSize = 0;
  for (const auto &route : routes) {
    totalSize += QStringToSize(route.size);
  }

  QString statsText = QString("%1 routes • %2").arg(routes.size()).arg(formatSize(totalSize));
  statsLabel->setText(statsText);
}

// Star persistence methods
QString BPRoutesPanel::getStarFilePath(const QString &routeBase) {
  return getRoutesDir() + "/" + routeBase + "/.star";
}

bool BPRoutesPanel::isRouteStarred(const QString &routeBase) {
  return QFile::exists(getStarFilePath(routeBase));
}

void BPRoutesPanel::setRouteStarred(const QString &routeBase, bool starred) {
  QString starPath = getStarFilePath(routeBase);
  if (starred) {
    QFile starFile(starPath);
    starFile.open(QIODevice::WriteOnly);
    starFile.close();
  } else {
    QFile::remove(starPath);
  }

  // Update cache if route exists
  if (routeCache.routeInfoCache.contains(routeBase)) {
    routeCache.routeInfoCache[routeBase].isStarred = starred;
    saveRouteCacheToDisk();
  }
}

QString BPRoutesPanel::formatDisplayDate(const QDateTime &dateTime) {
  // Format: "Thursday - September 17th, 2025"
  QString dayName = dateTime.toString("dddd");
  QString monthName = dateTime.toString("MMMM");
  int day = dateTime.date().day();
  int year = dateTime.date().year();

  // Add ordinal suffix to day
  QString suffix;
  if (day % 10 == 1 && day != 11) suffix = "st";
  else if (day % 10 == 2 && day != 12) suffix = "nd";
  else if (day % 10 == 3 && day != 13) suffix = "rd";
  else suffix = "th";

  return QString("%1 - %2 %3%4, %5").arg(dayName).arg(monthName).arg(day).arg(suffix).arg(year);
}

// Utility methods (note: RouteInfo derivations documented in header)
BPRoutesPanel::RouteInfo BPRoutesPanel::getRouteInfo(const QString &routePath) {
  RouteInfo info;
  QFileInfo routeFileInfo(routePath);
  QString originalName = routeFileInfo.fileName();

  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | Processing route: " << originalName.toStdString() << std::endl;

  // Extract base route name (remove segment suffix)
  QString baseRouteName = originalName;
  QRegExp segmentRegex("--\\d+$");
  if (segmentRegex.indexIn(baseRouteName) != -1) {
    baseRouteName = baseRouteName.left(segmentRegex.pos());
  }

  info.baseName = baseRouteName;

  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | Original name: " << originalName.toStdString() << " Base name: " << baseRouteName.toStdString() << std::endl;

  // Use directory modification time as the route timestamp
  QDateTime routeDateTime = routeFileInfo.lastModified();
  info.dateTime = routeDateTime;
  info.timestamp = routeDateTime.toString("h:mm AP");
  info.displayDate = formatDisplayDate(routeDateTime);
  info.elapsedTime = formatElapsedTime(routeDateTime);
  info.humanTime = routeDateTime.toString("h:mm AP");  // Preformatted human-readable time

  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | DateTime: " << routeDateTime.toString().toStdString() << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | Timestamp: " << info.timestamp.toStdString() << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | DisplayDate: " << info.displayDate.toStdString() << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | ElapsedTime: " << info.elapsedTime.toStdString() << std::endl;

  // Check for video files in segment directories
  QDir routeDir(routePath);

  // Initialize video flags
  info.hasFrontHQVideo = false;
  info.hasFrontLQVideo = false;
  info.hasDriverHQVideo = false;
  info.hasWideVideo = false;

  // Get all segment directories for this base route
  QStringList segmentFilter;
  segmentFilter << baseRouteName + "--*";
  QDir parentDir = routeDir;
  parentDir.cdUp();
  QStringList segmentDirs = parentDir.entryList(segmentFilter, QDir::Dirs);

  BPLog::bpDebugRoutes() << "[bp.routes.panel] getRouteInfo | Found " << segmentDirs.size() << " segment directories for " << baseRouteName.toStdString() << std::endl;

  // Check each segment directory for video files
  for (const QString &segmentDir : segmentDirs) {
    QString segmentPath = parentDir.absoluteFilePath(segmentDir);
    QDir segment(segmentPath);

    if (QFile::exists(segment.absoluteFilePath("fcamera.hevc"))) {
      info.hasFrontHQVideo = true;
      // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | Found fcamera.hevc in " << segmentDir.toStdString() << std::endl;
    }
    if (QFile::exists(segment.absoluteFilePath("qcamera.ts"))) {
      info.hasFrontLQVideo = true;
      // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | Found qcamera.ts in " << segmentDir.toStdString() << std::endl;
    }
    if (QFile::exists(segment.absoluteFilePath("dcamera.hevc"))) {
      info.hasDriverHQVideo = true;
      // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | Found dcamera.hevc in " << segmentDir.toStdString() << std::endl;
    }
    if (QFile::exists(segment.absoluteFilePath("ecamera.hevc"))) {
      info.hasWideVideo = true;
      // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | Found ecamera.hevc in " << segmentDir.toStdString() << std::endl;
    }
  }

  // Legacy flags for compatibility
  info.hasFrontVideo = info.hasFrontHQVideo || info.hasFrontLQVideo;
  info.hasDriverVideo = info.hasDriverHQVideo;
  info.hasLQVideo = info.hasFrontLQVideo;
  info.hasVideo = info.hasFrontVideo || info.hasWideVideo || info.hasDriverVideo;

  // Check for logs
  info.hasRLog = QFile::exists(routeDir.absoluteFilePath("rlog.zst"));
  info.hasQLog = QFile::exists(routeDir.absoluteFilePath("qlog.zst"));

  // Check if starred
  info.isStarred = isRouteStarred(baseRouteName);

  // Get segment count and duration for the entire route
  info.segments = getTotalSegments(baseRouteName);
  info.duration = getRouteDuration(baseRouteName);

  // Calculate total size for all segments of this base route
  qint64 totalSize = 0;
  QDir routesDir(getRoutesDir());
  QStringList allSegments = routesDir.entryList(QStringList() << baseRouteName + "--*", QDir::Dirs | QDir::NoDotAndDotDot);
  BPLog::bpDebugRoutes() << "[bp.routes.panel] getRouteInfo | Found " << allSegments.size() << " segments for base route: " << baseRouteName.toStdString() << std::endl;
  for (const QString &segment : allSegments) {
    QString segmentPath = routesDir.absoluteFilePath(segment);
    totalSize += calculateDirSize(segmentPath);
  }
  info.size = formatSize(totalSize);

  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | Total size: " << totalSize << " bytes" << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | Segments: " << info.segments << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | Duration: " << info.duration.toStdString() << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | HasFrontHQVideo: " << (info.hasFrontHQVideo ? "true" : "false") << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | HasFrontLQVideo: " << (info.hasFrontLQVideo ? "true" : "false") << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | HasDriverHQVideo: " << (info.hasDriverHQVideo ? "true" : "false") << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | HasWideVideo: " << (info.hasWideVideo ? "true" : "false") << std::endl;
  // BPLog::bpInfo() << "[bp.routes.panel] getRouteInfo | ==================" << std::endl;

  return info;
}

QString BPRoutesPanel::formatElapsedTime(const QDateTime &routeTime) {
  qint64 elapsed = routeTime.secsTo(QDateTime::currentDateTime());

  if (elapsed < 60) { // Less than 1 minute
    return "Just now";
  } else if (elapsed < 3600) { // Less than 1 hour
    int minutes = elapsed / 60;
    return QString("%1 min ago").arg(minutes);
  } else if (elapsed < 86400) { // Less than 1 day
    int hours = elapsed / 3600;
    if (hours == 1) {
      return "1 hour ago";
    }
    return QString("%1 hours ago").arg(hours);
  } else if (elapsed < 604800) { // Less than 1 week
    int days = elapsed / 86400;
    if (days == 1) {
      return "Yesterday";
    }
    return QString("%1 days ago").arg(days);
  } else if (elapsed < 2592000) { // Less than 1 month (30 days)
    int weeks = elapsed / 604800;
    if (weeks == 1) {
      return "1 week ago";
    }
    return QString("%1 weeks ago").arg(weeks);
  } else if (elapsed < 31536000) { // Less than 1 year
    int months = elapsed / 2592000;
    if (months == 1) {
      return "1 month ago";
    }
    return QString("%1 months ago").arg(months);
  } else { // 1 year or more
    int years = elapsed / 31536000;
    if (years == 1) {
      return "1 year ago";
    }
    return QString("%1 years ago").arg(years);
  }
}

// Keep existing utility methods but ensure they work with new structure
void BPRoutesPanel::simulateActivity() {
  if (!this->isVisible()) {
    return;
  }

  int x = this->width() / 2;
  int y = 10;

  QPoint localPos(x, y);
  QPoint globalPos = this->mapToGlobal(localPos);

  QMouseEvent pressEvent(QEvent::MouseButtonPress, localPos, globalPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QMouseEvent releaseEvent(QEvent::MouseButtonRelease, localPos, globalPos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);

  QCoreApplication::sendEvent(this, &pressEvent);
  QCoreApplication::sendEvent(this, &releaseEvent);
}

void BPRoutesPanel::stopActivitySimulation() {
  activityTimer->stop();
}

void BPRoutesPanel::resetMaxDurationTimer() {
  QTimer::singleShot(270000, this, &BPRoutesPanel::stopActivitySimulation);
}

void BPRoutesPanel::handleRefresh() {
  loadRoutes();
}

void BPRoutesPanel::showLoadingOverlay(const QString &message) {
  if (!loadingOverlay) {
    loadingOverlay = new QWidget(this);
    loadingOverlay->setStyleSheet("background: rgba(0, 0, 0, 0.7);");

    QVBoxLayout *overlayLayout = new QVBoxLayout(loadingOverlay);
    overlayLayout->setAlignment(Qt::AlignCenter);

    loadingLabel = new QLabel;
    loadingLabel->setStyleSheet("color: white; font-size: 48px; font-weight: 500;");
    loadingLabel->setAlignment(Qt::AlignCenter);

    overlayLayout->addWidget(loadingLabel);
  }

  loadingLabel->setText(message);
  loadingOverlay->resize(size());
  loadingOverlay->show();
  loadingOverlay->raise();
}

void BPRoutesPanel::hideLoadingOverlay() {
  if (loadingOverlay) {
    loadingOverlay->hide();
  }
}

// Existing utility methods - keeping implementation from original file

QString BPRoutesPanel::formatSize(qint64 bytes) {
  static const QStringList units = {"B", "KB", "MB", "GB", "TB"};
  int unitIndex = 0;
  double size = bytes;
  while (size >= 1024 && unitIndex < units.size() - 1) {
    size /= 1024;
    unitIndex++;
  }
  return QString("%1 %2").arg(size, 0, 'f', 1).arg(units[unitIndex]);
}

QString BPRoutesPanel::getDirectorySize(const QString &path) {
  return formatSize(calculateDirSize(path));
}

qint64 BPRoutesPanel::calculateDirSize(const QString &path) {
  qint64 size = 0;
  QDir dir(path);
  if (dir.exists()) {
    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &entry : entries) {
      if (entry.isFile()) {
        size += entry.size();
      } else if (entry.isDir()) {
        size += calculateDirSize(entry.absoluteFilePath());
      }
    }
  }
  return size;
}

qint64 BPRoutesPanel::QStringToSize(const QString &sizeStr) {
  QString cleanStr = sizeStr.trimmed().toUpper();
  QRegExp rx("(\\d+\\.?\\d*)\\s*(B|KB|MB|GB|TB)");
  if (rx.indexIn(cleanStr) != -1) {
    double value = rx.cap(1).toDouble();
    QString unit = rx.cap(2);

    if (unit == "KB") value *= 1024;
    else if (unit == "MB") value *= 1024 * 1024;
    else if (unit == "GB") value *= 1024 * 1024 * 1024;
    else if (unit == "TB") value *= 1024LL * 1024 * 1024 * 1024;

    return (qint64)value;
  }
  return 0;
}

int BPRoutesPanel::getTotalSegments(const QString &routeBase) {
  QDir dir(getRoutesDir());
  // Count directories that match the base route pattern (e.g., 2024-09-18--14-30-00--*)
  return dir.entryList(QStringList() << routeBase + "--*", QDir::Dirs | QDir::NoDotAndDotDot).count();
}

QString BPRoutesPanel::getRouteDuration(const QString &routeBase) {
  int segments = getTotalSegments(routeBase);
  int totalSeconds = segments * 60; // 60 seconds per segment
  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;

  QStringList parts;
  if (hours > 0) {
    parts << QString("%1h").arg(hours);
  }
  if (minutes > 0 || hours == 0) {
    parts << QString("%1m").arg(minutes);
  }

  return parts.join(" ");
}

QString BPRoutesPanel::getRouteSegmentPath(const QString &routeBase, int segment) {
  return QString("%1/%2--%3").arg(getRoutesDir(), routeBase, QString::number(segment));
}

int BPRoutesPanel::countFilesOfType(const QString &path, const QString &extension) {
  QDir dir(path);
  QStringList filters;
  filters << "*." + extension;
  return dir.entryList(filters, QDir::Files).count();
}

// Thumbnail methods
void BPRoutesPanel::initializeThumbnail(QLabel *thumbnailLabel, const QString &routeBase) {
  QString thumbnailPath = getThumbnailPath(routeBase);

  if (QFile::exists(thumbnailPath)) {
    QPixmap pixmap(thumbnailPath);
    if (!pixmap.isNull()) {
      thumbnailLabel->setPixmap(pixmap.scaled(thumbnailLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
      return;
    }
  }

  // Generate thumbnail in background with concurrency limit
  if (!thumbnailWatchers.contains(routeBase) && thumbnailWatchers.size() < MAX_CONCURRENT_THUMBNAILS) {
    QFutureWatcher<QString> *watcher = new QFutureWatcher<QString>(this);
    thumbnailWatchers[routeBase] = watcher;

    connect(watcher, &QFutureWatcher<QString>::finished, [this, thumbnailLabel, routeBase, watcher]() {
      QString result = watcher->result();
      if (!result.isEmpty() && QFile::exists(result)) {
        QPixmap pixmap(result);
        if (!pixmap.isNull()) {
          thumbnailLabel->setPixmap(pixmap.scaled(thumbnailLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
      } else {
        thumbnailLabel->setText("No Video");
        thumbnailLabel->setStyleSheet(thumbnailLabel->styleSheet() + "color: #666666; font-size: 16px;");
      }

      thumbnailWatchers.remove(routeBase);
      watcher->deleteLater();
    });

    // Use auto thumbnail generation (hardware on QCOM, software fallback)
    QFuture<QString> future = QtConcurrent::run(this, &BPRoutesPanel::generateThumbnailAuto, routeBase);
    watcher->setFuture(future);
  }
}

QString BPRoutesPanel::generateThumbnail(const QString &routeBase) {
  QString thumbnailPath = getThumbnailPath(routeBase);

  // Check if thumbnail already exists
  if (QFile::exists(thumbnailPath)) {
    return thumbnailPath;
  }

  // Find the correct segment directory
  // routeBase might be like "0000009b--d7712fe77a--0" or "0000009b--d7712fe77a"
  QString baseRouteSearch = routeBase;
  if (routeBase.contains("--") && routeBase.count("--") >= 2) {
    // Extract the base route name (remove the last --N part)
    QStringList parts = routeBase.split("--");
    if (parts.size() >= 3) {
      baseRouteSearch = parts[0] + "--" + parts[1];
    }
  }

  QDir dir(getRoutesDir());
  QStringList segments = dir.entryList(QStringList() << QString("%1--0").arg(baseRouteSearch), QDir::Dirs | QDir::NoDotAndDotDot);
  if (segments.isEmpty()) {
    BPLog::bpWarn() << "[bp.routes.panel] generateThumbnail | No segment found for route: " << routeBase.toStdString() << std::endl;
    return QString();
  }

  QString inputVideo = getRoutesDir() + "/" + segments.first() + "/fcamera.hevc";
  if (!QFile::exists(inputVideo)) {
    BPLog::bpWarn() << "[bp.routes.panel] generateThumbnail | Video file not found: " << inputVideo.toStdString() << std::endl;
    return QString();
  }

  BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnail | Found video: " << inputVideo.toStdString() << std::endl;

  // Create thumbnail directory
  QDir().mkpath(QFileInfo(thumbnailPath).absolutePath());

  // Generate thumbnail using FFmpeg with better resource management
  QProcess ffmpeg;
  ffmpeg.setProcessChannelMode(QProcess::MergedChannels);
  QStringList args;
  args << "-y"                                                                       // Overwrite output file
       << "-nostdin"                                                                 // Disable interaction
       << "-i" << inputVideo                                                          // Input file
       << "-vframes" << "1"                                                          // Extract one frame
       << "-an"                                                                      // Disable audio
       << "-vf" << QString("scale=%1:%2").arg(THUMBNAIL_WIDTH).arg(THUMBNAIL_HEIGHT) // Scale
       << "-strict" << "unofficial"                                                  // Allow non-standard YUV
       << "-pix_fmt" << "yuvj420p"                                                   // Use full-range YUV
       << thumbnailPath;

  ffmpeg.start("ffmpeg", args);

  // Wait with reasonable timeout for single frame extraction
  bool success = ffmpeg.waitForFinished(15000); // 15 second timeout

  if (!success || ffmpeg.state() != QProcess::NotRunning) {
    BPLog::bpWarn() << "[bp.routes.panel] generateThumbnail | FFmpeg timeout or still running, terminating..." << std::endl;
    ffmpeg.terminate();
    if (!ffmpeg.waitForFinished(2000)) {
      ffmpeg.kill();
    }
  }

  if (success && ffmpeg.exitCode() == 0 && QFile::exists(thumbnailPath)) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnail | Successfully generated thumbnail: " << thumbnailPath.toStdString() << std::endl;
    return thumbnailPath;
  } else {
    BPLog::bpWarn() << "[bp.routes.panel] generateThumbnail | Failed to generate thumbnail. Exit code: " << ffmpeg.exitCode() << std::endl;
    if (ffmpeg.exitCode() != 0) {
      QByteArray errorOutput = ffmpeg.readAllStandardError();
      if (!errorOutput.isEmpty()) {
        BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnail | FFmpeg error: " << errorOutput.toStdString() << std::endl;
      }
    }
    return QString(); // Return empty for placeholder
  }

  return QString();
}

QString BPRoutesPanel::generateThumbnailHardware(const QString &routeBase) {
  QString thumbnailPath = getThumbnailPath(routeBase);

  // Check if thumbnail already exists
  if (QFile::exists(thumbnailPath)) {
      BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailHardware | Hardware thumbnail already exists: " << thumbnailPath.toStdString() << std::endl;
    return thumbnailPath;
  }

  // Find the correct segment directory
  // routeBase might be like "0000009b--d7712fe77a--0" or "0000009b--d7712fe77a"
  QString baseRouteSearch = routeBase;
  if (routeBase.contains("--") && routeBase.count("--") >= 2) {
    // Extract the base route name (remove the last --N part)
    QStringList parts = routeBase.split("--");
    if (parts.size() >= 3) {
      baseRouteSearch = parts[0] + "--" + parts[1];
    }
  }

  QDir dir(getRoutesDir());
  QStringList segments = dir.entryList(QStringList() << QString("%1--0").arg(baseRouteSearch), QDir::Dirs | QDir::NoDotAndDotDot);
  if (segments.isEmpty()) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailHardware | No segment found for route: " << routeBase.toStdString() << std::endl;
    return QString();
  }

  QString inputVideo = getRoutesDir() + "/" + segments.first() + "/fcamera.hevc";
  if (!QFile::exists(inputVideo)) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailHardware | Video file not found: " << inputVideo.toStdString() << std::endl;
    return QString();
  }

  BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailHardware | Using hardware decoder for: " << inputVideo.toStdString() << std::endl;

  // Create thumbnail directory
  QDir().mkpath(QFileInfo(thumbnailPath).absolutePath());

  try {
    // Use the new QCOM hardware decoder
    FrameReader frameReader;
    std::atomic<bool> abort{false};

    if (!frameReader.load(RoadCam, inputVideo.toStdString(), false, &abort)) {
      BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailHardware | Failed to load video with hardware decoder, falling back to FFmpeg" << std::endl;
      return generateThumbnail(routeBase); // Fallback to software FFmpeg
    }

    if (frameReader.getFrameCount() == 0) {
      BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailHardware | No frames found in video" << std::endl;
      return QString();
    }

    // Create VisionBuf for decoded frame
    VisionBuf thumbnail_buf = {};
    size_t frame_size = frameReader.width * frameReader.height * 3 / 2; // YUV420 format
    thumbnail_buf.allocate(frame_size);
    thumbnail_buf.init_yuv(frameReader.width, frameReader.height, frameReader.width, frameReader.width * frameReader.height);

    // Decode the first frame (index 0) for thumbnail
    if (!frameReader.get(0, &thumbnail_buf)) {
      BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailHardware | Failed to decode first frame" << std::endl;
      thumbnail_buf.free();
      return QString();
    }

    // Convert YUV420/NV12 to RGB and save as JPEG
    if (saveYUVasJPEG(&thumbnail_buf, thumbnailPath, frameReader.width, frameReader.height)) {
      BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailHardware | Successfully generated hardware thumbnail: " << thumbnailPath.toStdString() << std::endl;
      thumbnail_buf.free();
      return thumbnailPath;
    } else {
      BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailHardware | Failed to save thumbnail image" << std::endl;
      thumbnail_buf.free();
      return QString();
    }

  } catch (const std::exception& e) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailHardware | Hardware thumbnail generation failed: " << e.what() << std::endl;
    return generateThumbnail(routeBase); // Fallback to software FFmpeg
  } catch (...) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailHardware | Hardware thumbnail generation failed with unknown error" << std::endl;
    return generateThumbnail(routeBase); // Fallback to software FFmpeg
  }
}

QString BPRoutesPanel::generateThumbnailAuto(const QString &routeBase) {
  // Check if thumbnail already exists
  QString thumbnailPath = getThumbnailPath(routeBase);
  if (QFile::exists(thumbnailPath)) {
    return thumbnailPath;
  }

  // For now, use FFmpeg directly as it's more reliable
  // Hardware decoding can be re-enabled once V4L2 issues are resolved
  /*
#ifndef __APPLE__
  // On QCOM devices, try hardware decoding first
  if (isCommaDevice()) {
    BPLog::bpInfo() << "[bp.routes.panel] generateThumbnailAuto | Attempting hardware thumbnail generation for: " << routeBase.toStdString() << std::endl;
    QString hardwareResult = generateThumbnailHardware(routeBase);
    if (!hardwareResult.isEmpty()) {
      return hardwareResult;
    }
    BPLog::bpInfo() << "[THUMBNAIL DEBUG] Hardware generation failed, falling back to software" << std::endl;
  }
#endif
  */

  // Use software FFmpeg generation (more reliable)
  BPLog::bpDebugRoutes() << "[bp.routes.panel] generateThumbnailAuto | Using software FFmpeg thumbnail generation for: " << routeBase.toStdString() << std::endl;
  return generateThumbnail(routeBase);
}

bool BPRoutesPanel::saveYUVasJPEG(VisionBuf *buf, const QString &outputPath, int width, int height) {
  if (!buf || !buf->y || !buf->uv) {
    BPLog::bpWarn() << "[bp.routes.panel] saveYUVasJPEG | Invalid VisionBuf for JPEG conversion" << std::endl;
    return false;
  }

  // Scale down to thumbnail size
  int thumb_width = THUMBNAIL_WIDTH;
  int thumb_height = THUMBNAIL_HEIGHT;

  // Create RGB buffer for libyuv conversion
  std::vector<uint8_t> rgb_buffer(thumb_width * thumb_height * 3);

  // Convert NV12 to ARGB with scaling using libyuv
  std::vector<uint8_t> argb_buffer(thumb_width * thumb_height * 4);

  // First convert NV12 to ARGB
  int result1 = libyuv::NV12ToARGB(
    buf->y, buf->stride,           // Y plane
    buf->uv, buf->stride,          // UV plane (interleaved)
    argb_buffer.data(), thumb_width * 4,  // ARGB output
    width, height                  // Source dimensions
  );

  if (result1 != 0) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] saveYUVasJPEG | Failed to convert NV12 to ARGB, result: " << result1 << std::endl;
    return false;
  }

  // Scale if needed
  if (width != thumb_width || height != thumb_height) {
    std::vector<uint8_t> scaled_argb(thumb_width * thumb_height * 4);
    int result2 = libyuv::ARGBScale(
      argb_buffer.data(), width * 4,
      width, height,
      scaled_argb.data(), thumb_width * 4,
      thumb_width, thumb_height,
      libyuv::kFilterBilinear
    );

    if (result2 != 0) {
      BPLog::bpDebugRoutes() << "[bp.routes.panel] saveYUVasJPEG | Failed to scale ARGB, result: " << result2 << std::endl;
      return false;
    }
    argb_buffer = std::move(scaled_argb);
  }

  // Convert ARGB to RGB for QImage
  rgb_buffer.resize(thumb_width * thumb_height * 3);
  for (int i = 0; i < thumb_width * thumb_height; i++) {
    rgb_buffer[i * 3 + 0] = argb_buffer[i * 4 + 2]; // R
    rgb_buffer[i * 3 + 1] = argb_buffer[i * 4 + 1]; // G
    rgb_buffer[i * 3 + 2] = argb_buffer[i * 4 + 0]; // B
  }


  // Create QImage from RGB data
  QImage image(rgb_buffer.data(), thumb_width, thumb_height, thumb_width * 3, QImage::Format_RGB888);

  if (image.isNull()) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] saveYUVasJPEG | Failed to create QImage from RGB data" << std::endl;
    return false;
  }

  // Save as JPEG with quality 85
  bool saved = image.save(outputPath, "JPEG", 85);
  if (!saved) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] saveYUVasJPEG | Failed to save JPEG image to: " << outputPath.toStdString() << std::endl;
    return false;
  }

  BPLog::bpDebugRoutes() << "[bp.routes.panel] saveYUVasJPEG | Saved JPEG thumbnail: " << outputPath.toStdString() << " (" << thumb_width << "x" << thumb_height << ")" << std::endl;
  return true;
}

QString BPRoutesPanel::getThumbnailPath(const QString &routeBase) {
  return getThumbnailCacheDir + "/" + routeBase + ".jpg";
}

void BPRoutesPanel::generateAllMissingThumbnails() {
  BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | Starting batch thumbnail generation for all routes..." << std::endl;

  // Count missing thumbnails and currently generating ones
  int missingCount = 0;
  int generatingCount = 0;
  int skippedCount = 0;

  for (const RouteInfo &route : routes) {
    QString thumbnailPath = getThumbnailPath(route.baseName);

    // Skip if thumbnail already exists
    if (QFile::exists(thumbnailPath)) {
      continue;
    }

    // Skip if permanently failed (no video file)
    if (permanentlyFailedRoutes.contains(route.baseName)) {
      skippedCount++;
      continue;
    }

    // Count if already being generated
    if (thumbnailWatchers.contains(route.baseName)) {
      generatingCount++;
      continue;
    }

    missingCount++;
  }

  // If no missing thumbnails and no generation in progress, we're done
  if (missingCount == 0 && generatingCount == 0) {
    if (skippedCount > 0) {
      BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | Thumbnail generation complete! (" << skippedCount << " routes skipped due to missing video files)" << std::endl;
    } else {
      BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | All thumbnails have been generated successfully!" << std::endl;
    }
    return;
  }

  BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | Found " << missingCount << " missing thumbnails, " << generatingCount << " currently generating, " << skippedCount << " permanently skipped..." << std::endl;

  // Start generation for missing thumbnails (respecting concurrent limit)
  int started = 0;
  for (const RouteInfo &route : routes) {
    QString thumbnailPath = getThumbnailPath(route.baseName);

    // Skip if thumbnail already exists
    if (QFile::exists(thumbnailPath)) {
      continue;
    }

    // Skip if permanently failed
    if (permanentlyFailedRoutes.contains(route.baseName)) {
      continue;
    }

    // Skip if already being generated
    if (thumbnailWatchers.contains(route.baseName)) {
      continue;
    }

    // Respect concurrent limit
    if (thumbnailWatchers.size() >= MAX_CONCURRENT_THUMBNAILS) {
      BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | Reached concurrent limit (" << MAX_CONCURRENT_THUMBNAILS << "), waiting for some to complete..." << std::endl;
      break;
    }

    // Start generation for this route
    QFutureWatcher<QString> *watcher = new QFutureWatcher<QString>(this);
    thumbnailWatchers[route.baseName] = watcher;
    started++;

    connect(watcher, &QFutureWatcher<QString>::finished, [this, route, watcher]() {
      QString result = watcher->result();
      if (!result.isEmpty()) {
        BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | Generated thumbnail for: " << route.baseName.toStdString() << std::endl;
      } else {
        BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | Failed to generate thumbnail for: " << route.baseName.toStdString() << " - marking as permanently failed" << std::endl;
        // Mark this route as permanently failed so we don't retry it
        permanentlyFailedRoutes.insert(route.baseName);
      }

      thumbnailWatchers.remove(route.baseName);
      watcher->deleteLater();

      // Check if we need to continue generating more thumbnails
      // Only schedule another check after a delay to prevent excessive calls
      QTimer::singleShot(1000, this, [this]() {
        // Only continue if there are still missing thumbnails (excluding permanently failed ones)
        bool hasMissing = false;
        for (const RouteInfo &route : routes) {
          QString thumbnailPath = getThumbnailPath(route.baseName);
          if (!QFile::exists(thumbnailPath) &&
              !thumbnailWatchers.contains(route.baseName) &&
              !permanentlyFailedRoutes.contains(route.baseName)) {
            hasMissing = true;
            break;
          }
        }

        if (hasMissing) {
          BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | Continuing thumbnail generation for remaining routes..." << std::endl;
          generateAllMissingThumbnails();
        } else {
          int failedCount = permanentlyFailedRoutes.size();
          if (failedCount > 0) {
            BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | Thumbnail generation complete! (" << failedCount << " routes had no video files)" << std::endl;
          } else {
            BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | Thumbnail generation complete!" << std::endl;
          }
        }
      });
    });

    // Use auto thumbnail generation (hardware on QCOM, software fallback)
    QFuture<QString> future = QtConcurrent::run(this, &BPRoutesPanel::generateThumbnailAuto, route.baseName);
    watcher->setFuture(future);

    BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | Started generation for: " << route.baseName.toStdString() << std::endl;
  }

  if (started > 0) {
    BPLog::bpDebugRoutes() << "[bp.routes.panel] generateAllMissingThumbnails | Started " << started << " new thumbnail generations" << std::endl;
  }
}

void BPRoutesPanel::cleanupThumbnailCache() {
  QDir cacheDir(getThumbnailCacheDir);
  if (cacheDir.exists()) {
    QFileInfoList files = cacheDir.entryInfoList(QStringList() << "*.jpg", QDir::Files);
    for (const QFileInfo &file : files) {
      // Remove thumbnails older than 30 days
      if (file.lastModified().daysTo(QDateTime::currentDateTime()) > 30) {
        QFile::remove(file.absoluteFilePath());
      }
    }
  }
}


void BPRoutesPanel::showConfirmDialog(const QString &title, const QString &message, const std::function<void()> &onConfirm) {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = title;
  config.prompt = message;
  config.confirmText = "Yes";
  config.cancelText = "No";

  BPConfirmationDialog *dialog = BPConfirmationDialog::showConfirmation(config, this);
  connect(dialog, &BPConfirmationDialog::confirmed, [onConfirm](bool accepted) {
    if (accepted) {
      onConfirm();
    }
  });
}

// Route operations
void BPRoutesPanel::handleRouteDetails(const QString &route) {
  // Show route details dialog (placeholder)
}

void BPRoutesPanel::handleRouteConcatenation(const QString &route) {
  // Handle route concatenation (placeholder)
}

void BPRoutesPanel::handleRouteRemoval(const QString &route) {
  showConfirmDialog("Delete Route",
    QString("Are you sure you want to delete route %1?").arg(route),
    [this, route]() {
      // Perform deletion
      QString routePath = getRoutesDir() + "/" + route;
      QDir routeDir(routePath);
      if (routeDir.removeRecursively()) {
        // Remove from routes list and cache
        routes.erase(std::remove_if(routes.begin(), routes.end(),
          [route](const RouteInfo &info) {
            return info.baseName == route;
          }), routes.end());

        // Remove from cache
        routeCache.routeInfoCache.remove(route);
        saveRouteCacheToDisk();

        loadRoutes(); // Refresh display
      }
    });
}

// Disk caching implementation
QString BPRoutesPanel::getRouteCacheFile() const {
  // Create BluePilot routes directory if it doesn't exist
  QDir dir("/data/bluepilot/routes");
  if (!dir.exists()) {
    dir.mkpath(".");
  }
  return "/data/bluepilot/routes/routes_cache.json";
}

void BPRoutesPanel::saveRouteCacheToDisk() {
  QString cacheFile = getRouteCacheFile();

  QJsonObject cacheObj;
  cacheObj["lastUpdated"] = routeCache.lastUpdated.toString(Qt::ISODate);

  QJsonArray routesArray;
  for (auto it = routeCache.routeInfoCache.begin(); it != routeCache.routeInfoCache.end(); ++it) {
    const RouteInfo &info = it.value();
    QJsonObject routeObj;
    routeObj["baseName"] = info.baseName;
    routeObj["timestamp"] = info.timestamp;
    routeObj["endTimestamp"] = info.endTimestamp;
    routeObj["duration"] = info.duration;
    routeObj["elapsedTime"] = info.elapsedTime;
    routeObj["displayDate"] = info.displayDate;
    routeObj["segments"] = info.segments;
    routeObj["size"] = info.size;
    routeObj["tripMiles"] = info.tripMiles;
    routeObj["hasVideo"] = info.hasVideo;
    routeObj["hasRLog"] = info.hasRLog;
    routeObj["hasQLog"] = info.hasQLog;
    routeObj["hasFrontVideo"] = info.hasFrontVideo;
    routeObj["hasWideVideo"] = info.hasWideVideo;
    routeObj["hasDriverVideo"] = info.hasDriverVideo;
    routeObj["hasLQVideo"] = info.hasLQVideo;
    routeObj["hasFrontHQVideo"] = info.hasFrontHQVideo;
    routeObj["hasFrontLQVideo"] = info.hasFrontLQVideo;
    routeObj["hasDriverHQVideo"] = info.hasDriverHQVideo;
    routeObj["isStarred"] = info.isStarred;
    routeObj["dateTime"] = info.dateTime.toString(Qt::ISODate);
    routesArray.append(routeObj);
  }

  cacheObj["routes"] = routesArray;

  QJsonDocument doc(cacheObj);
  QFile file(cacheFile);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(doc.toJson());
    file.close();
  }
}

bool BPRoutesPanel::loadRouteCacheFromDisk() {
  QString cacheFile = getRouteCacheFile();

  if (!QFile::exists(cacheFile)) {
    return false;
  }

  QFile file(cacheFile);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (doc.isNull()) {
    return false;
  }

  QJsonObject cacheObj = doc.object();

  // Check if cache is valid (not too old)
  QString lastUpdatedStr = cacheObj["lastUpdated"].toString();
  routeCache.lastUpdated = QDateTime::fromString(lastUpdatedStr, Qt::ISODate);

  if (!routeCache.isValid()) {
    return false;
  }

  // Load routes
  QJsonArray routesArray = cacheObj["routes"].toArray();
  routeCache.routeInfoCache.clear();

  for (const QJsonValue &value : routesArray) {
    QJsonObject routeObj = value.toObject();
    RouteInfo info;
    info.baseName = routeObj["baseName"].toString();
    info.timestamp = routeObj["timestamp"].toString();
    info.endTimestamp = routeObj["endTimestamp"].toString();
    info.duration = routeObj["duration"].toString();
    info.elapsedTime = routeObj["elapsedTime"].toString();
    info.displayDate = routeObj["displayDate"].toString();
    info.segments = routeObj["segments"].toInt();
    info.size = routeObj["size"].toString();
    info.tripMiles = routeObj["tripMiles"].toDouble();
    info.hasVideo = routeObj["hasVideo"].toBool();
    info.hasRLog = routeObj["hasRLog"].toBool();
    info.hasQLog = routeObj["hasQLog"].toBool();
    info.hasFrontVideo = routeObj["hasFrontVideo"].toBool();
    info.hasWideVideo = routeObj["hasWideVideo"].toBool();
    info.hasDriverVideo = routeObj["hasDriverVideo"].toBool();
    info.hasLQVideo = routeObj["hasLQVideo"].toBool();
    info.hasFrontHQVideo = routeObj["hasFrontHQVideo"].toBool();
    info.hasFrontLQVideo = routeObj["hasFrontLQVideo"].toBool();
    info.hasDriverHQVideo = routeObj["hasDriverHQVideo"].toBool();
    info.isStarred = routeObj["isStarred"].toBool();
    info.dateTime = QDateTime::fromString(routeObj["dateTime"].toString(), Qt::ISODate);

    routeCache.routeInfoCache[info.baseName] = info;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Route Loaded: " << info.baseName.toStdString() << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Timestamp: " << info.timestamp.toStdString() << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | End Timestamp: " << info.endTimestamp.toStdString() << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Duration: " << info.duration.toStdString() << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Elapsed Time: " << info.elapsedTime.toStdString() << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Display Date: " << info.displayDate.toStdString() << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Segments: " << info.segments << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Size: " << info.size.toStdString() << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Trip Miles: " << info.tripMiles << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Has Video: " << info.hasVideo << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Has RLog: " << info.hasRLog << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Has QLog: " << info.hasQLog << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Has Front Video: " << info.hasFrontVideo << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Has Wide Video: " << info.hasWideVideo << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Has Driver Video: " << info.hasDriverVideo << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Has LQ Video: " << info.hasLQVideo << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Has Front HQ Video: " << info.hasFrontHQVideo << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Has Front LQ Video: " << info.hasFrontLQVideo << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Has Driver HQ Video: " << info.hasDriverHQVideo << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Is Starred: " << info.isStarred << std::endl;
    BPLog::bpDebugRoutes() << "[bp.routes.panel] loadRouteCacheFromDisk | Date Time: " << info.dateTime.toString().toStdString() << std::endl;
  }

  return true;
}

bool BPRoutesPanel::shouldRefreshRoutes() const {
  // Check if routes directory has been modified since last cache
  QString routesDir = getRoutesDir();
  QDir dir(routesDir);

  if (!dir.exists()) {
    return true;
  }

  // Check if any route directories are newer than cache
  QStringList routeDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString &routeDir : routeDirs) {
    if (routeDir == "boot" || routeDir == "crash" || !routeDir.contains("--")) {
      continue;
    }

    QString routePath = dir.absoluteFilePath(routeDir);
    QFileInfo routeInfo(routePath);
    if (routeInfo.lastModified() > routeCache.lastUpdated) {
      return true;
    }
  }

  return false;
}

// Onroad Safety Methods
void BPRoutesPanel::showOnroadMessage() {
  // Clear existing content
  if (scrollArea) {
    scrollArea->setVisible(false);
  }
  if (loadingOverlay) {
    loadingOverlay->setVisible(false);
  }

  // Create or show onroad message
  if (!onroadMessageWidget) {
    onroadMessageWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(onroadMessageWidget);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(40);

    // Icon
    QLabel *iconLabel = new QLabel("🚗");
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet("font-size: 120px;");
    layout->addWidget(iconLabel);

    // Main message
    QLabel *messageLabel = new QLabel("Routes Panel Disabled");
    messageLabel->setAlignment(Qt::AlignCenter);
    messageLabel->setStyleSheet(R"(
      font-size: 72px;
      font-weight: bold;
      color: #FFD700;
      margin: 20px;
    )");
    layout->addWidget(messageLabel);

    // Safety message
    QLabel *safetyLabel = new QLabel("For your safety, route management is disabled while driving.\nThis panel will be available when you park.");
    safetyLabel->setAlignment(Qt::AlignCenter);
    safetyLabel->setWordWrap(true);
    safetyLabel->setStyleSheet(R"(
      font-size: 48px;
      color: #CCCCCC;
      line-height: 1.4;
      margin: 20px 40px;
    )");
    layout->addWidget(safetyLabel);

    onroadMessageWidget->setStyleSheet("background: #000000;");
    onroadMessageWidget->setGeometry(0, 0, width(), height());
  }

  onroadMessageWidget->setVisible(true);
  onroadMessageWidget->raise();
}

void BPRoutesPanel::clearOnroadMessage() {
  if (onroadMessageWidget) {
    onroadMessageWidget->setVisible(false);
  }
  if (scrollArea) {
    scrollArea->setVisible(true);
  }
}

void BPRoutesPanel::onOffroadTransition() {
  // BPLog::bpInfo() << "[ROUTE DEBUG] Offroad transition detected" << std::endl;

  if (uiState()->scene.started) {
    // Device went onroad - show safety message and clear routes
    // BPLog::bpInfo() << "[ROUTE DEBUG] Device went onroad - disabling routes panel" << std::endl;
    showOnroadMessage();

    // Close any open video dialogs for safety
    if (QWidget *topLevel = window()) {
      QList<QDialog*> dialogs = topLevel->findChildren<QDialog*>();
      for (QDialog* dialog : dialogs) {
        if (dialog->isVisible()) {
          BPLog::bpDebugRoutes() << "[bp.routes.panel] onOffroadTransition | Closing video dialog due to onroad transition" << std::endl;

          dialog->close();
        }
      }
    }
  } else {
    // Device went offroad - clear message and load routes only if panel is visible
    // BPLog::bpInfo() << "[ROUTE DEBUG] Device went offroad - enabling routes panel" << std::endl;
    clearOnroadMessage();
    if (routes.isEmpty() && isVisible()) {
      // BPLog::bpInfo() << "[ROUTE DEBUG] Panel is visible - loading routes" << std::endl;
      loadRoutes();
    } else if (routes.isEmpty()) {
      // BPLog::bpInfo() << "[ROUTE DEBUG] Panel not visible - deferring route loading to showEvent" << std::endl;
    }
  }
}


