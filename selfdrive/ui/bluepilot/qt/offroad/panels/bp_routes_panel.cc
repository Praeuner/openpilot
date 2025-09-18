// bp_routes_panel.cc
#include "bp_routes_panel.h"
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
#include <QTimer>
#include <iostream>

#include "common/params.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

// Include the enhanced video modal implementation
#include "bp_enhanced_video_modal.cc"

BPRoutesPanel::BPRoutesPanel(QWidget *parent) : QWidget(parent), isLoading(false), isSyncing(false), syncProgressDialog(nullptr), syncTimer(nullptr) {
  // Register RouteInfo for QVariant
  qRegisterMetaType<RouteInfo>();
  setObjectName("routesPanel");

  // Set size constraints
  setMinimumWidth(1000);
  setMaximumWidth(1920);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

  activityTimer = new QTimer(static_cast<QObject *>(this));
  activityTimer->setInterval(9000); // 9 seconds
  connect(activityTimer, &QTimer::timeout, this, &BPRoutesPanel::simulateActivity);

  setupStyles();
  setupUI();
  setupNetworkSync();
}

BPRoutesPanel::~BPRoutesPanel() {
  if (syncProgressDialog) {
    delete syncProgressDialog;
  }
  if (syncTimer) {
    delete syncTimer;
  }

  // Add this section
  for (auto watcher : thumbnailWatchers) {
    watcher->cancel();
    watcher->deleteLater();
  }
  thumbnailWatchers.clear();
}

QString BPRoutesPanel::findFFmpegExecutable() {
  QStringList possiblePaths;

#ifdef __APPLE__
  // macOS-specific ffmpeg paths
  possiblePaths << "/opt/homebrew/bin/ffmpeg"  // Homebrew on Apple Silicon
               << "/usr/local/bin/ffmpeg"      // Homebrew on Intel
               << "/opt/local/bin/ffmpeg"      // MacPorts
               << "ffmpeg";                    // System PATH
#else
  // Linux/other platforms
  possiblePaths << "/usr/bin/ffmpeg"           // System installation
               << "/usr/local/bin/ffmpeg"      // Local installation
               << "ffmpeg";                    // System PATH
#endif

  for (const QString &path : possiblePaths) {
    QProcess testProcess;
    testProcess.start(path, QStringList() << "-version");
    testProcess.waitForFinished(3000); // 3 second timeout

    if (testProcess.exitCode() == 0) {
      std::cout << "Found ffmpeg at: " << path.toStdString() << std::endl;
      return path;
    }
  }

  std::cout << "ffmpeg not found in any expected locations" << std::endl;
  return QString();
}

void BPRoutesPanel::simulateActivity() {
  // Only run if this widget is visible
  if (!this->isVisible()) {
    return;
  }

  std::cout << "Simulating activity in BPRoutesPanel" << std::endl;

  // Generate random x,y coordinates within the widget's bounds
  int x = this->width() / 2; // Center of the widget
  int y = 10;                // Fixed 10 pixels from the top

  // Convert local coordinates to screen coordinates
  QPoint localPos(x, y);
  QPoint globalPos = this->mapToGlobal(localPos);

  // Create mouse press and release events and simulates a complete click action
  QMouseEvent pressEvent(QEvent::MouseButtonPress, localPos, globalPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QMouseEvent releaseEvent(QEvent::MouseButtonRelease, localPos, globalPos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);

  // Send the events to the widget through the application event queue
  QCoreApplication::sendEvent(this, &pressEvent);
  QCoreApplication::sendEvent(this, &releaseEvent);
}

void BPRoutesPanel::stopActivitySimulation() {
  std::cout << "Stopping BPRoutesPanel activity simulation | max duration timer stopped" << std::endl;
  activityTimer->stop();
}

void BPRoutesPanel::resetMaxDurationTimer() {
  // Reset the max duration timer
  QTimer::singleShot(270000, this, &BPRoutesPanel::stopActivitySimulation); // 4 minutes and 30 seconds
}

void BPRoutesPanel::setupStyles() {
  setStyleSheet(R"(
        BPRoutesPanel {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1a1a1a, stop:1 #0f0f0f);
        }
        QScrollArea {
            background: transparent;
            border: none;
        }
        QScrollBar:vertical {
            width: 24px;
            margin: 0px;
            padding: 2px;
            background: transparent;
        }
        QScrollBar::handle:vertical {
            background: #666666;
            min-height: 100px;
            border-radius: 12px;
            margin: 0 4px;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar::add-page:vertical,
        QScrollBar::sub-page:vertical {
            background: none;
        }
        QPushButton {
            border-radius: 12px;
            font-size: 32px;
            padding: 15px 25px;
            min-width: 120px;
            font-weight: 600;
        }
        QPushButton.routes-panel-action-button {
            background-color: #2196F3;
            color: white;
            border: 2px solid transparent;
        }
        QPushButton.routes-panel-action-button:hover {
            background-color: #1976D2;
            border-color: #64B5F6;
        }
        QPushButton.routes-panel-action-button:pressed {
            background-color: #1565C0;
        }
        QPushButton.routes-panel-action-button:disabled {
            background-color: #404040;
            color: #888888;
        }
        QPushButton.danger-button {
            background-color: #F44336;
            color: white;
            border: 2px solid transparent;
        }
        QPushButton.danger-button:hover {
            background-color: #D32F2F;
            border-color: #FFCDD2;
        }
        QPushButton.danger-button:pressed {
            background-color: #C62828;
        }
        QPushButton.route-card-button {
            background-color: #363636;
            color: white;
            border: 2px solid transparent;
            border-radius: 8px;
            font-size: 28px;
            padding: 12px 20px;
            min-width: 100px;
        }
        QPushButton.route-card-button:hover {
            background-color: #404040;
            border-color: #666666;
        }
        QPushButton.route-card-button:pressed {
            background-color: #2a2a2a;
        }
        QGroupBox {
            background-color: #242424;
            border-radius: 15px;
            padding: 20px;
            border: none;
        }
        QLabel.date-header {
            background-color: #1a1a1a;
            color: #2196F3;
            font-size: 36px;
            font-weight: 700;
            padding: 20px 30px;
            border-radius: 12px;
            border-left: 6px solid #2196F3;
        }
        QWidget.route-card {
            background-color: #242424;
            border-radius: 15px;
            border: 2px solid transparent;
        }
        QWidget.route-card:hover {
            border-color: #404040;
            background-color: #2a2a2a;
        }
        QLabel.route-title {
            color: white;
            font-size: 40px;
            font-weight: 700;
        }
        QLabel.route-subtitle {
            color: #AAAAAA;
            font-size: 32px;
            font-weight: 500;
        }
        QLabel.route-stats {
            color: #888888;
            font-size: 28px;
        }
    )");
}

void BPRoutesPanel::setupUI() {
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(30, 20, 30, 20);
  mainLayout->setSpacing(20);

  // Header
  auto headerWidget = new QWidget(this);
  auto headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(0, 0, 0, 0);

  titleLabel = new QLabel(tr("Route Management"), headerWidget);
  titleLabel->setStyleSheet("font-size: 48px; font-weight: 600; color: white;");

  // Create buttons with proper parent
  refreshButton = new QPushButton(tr("Refresh"), headerWidget);
  cleanupButton = new QPushButton(tr("Cleanup"), headerWidget);
  settingsButton = new QPushButton(tr("Settings"), headerWidget);
  syncAllButton = new QPushButton(tr("Sync All"), headerWidget);
  viewLogButton = new QPushButton(tr("View Sync Log"), headerWidget);

  // Hide all header buttons
  refreshButton->setVisible(false);
  cleanupButton->setVisible(false);
  settingsButton->setVisible(false);
  syncAllButton->setVisible(false);
  viewLogButton->setVisible(false);

  // Set button styles
  QList<QPushButton *> buttons = {refreshButton, cleanupButton, settingsButton, syncAllButton, viewLogButton};
  for (auto button : buttons) {
    button->setProperty("class", "routes-panel-action-button");
  }

  headerLayout->addWidget(titleLabel);
  headerLayout->addStretch();
  for (auto button : buttons) {
    headerLayout->addWidget(button);
  }

  mainLayout->addWidget(headerWidget);

  // Stats
  statsLabel = new QLabel(this);
  statsLabel->setStyleSheet("font-size: 32px; color: #AAAAAA;");
  mainLayout->addWidget(statsLabel);

  // Routes container
  routesContainer = new QWidget();
  routesLayout = new QVBoxLayout(routesContainer);
  routesLayout->setSpacing(10);

  scrollArea = new BPScrollView(routesContainer, this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  // Enable touch scrolling
  QScroller::grabGesture(scrollArea->viewport(), QScroller::TouchGesture);

  // Install event filter for automatic loading on scroll
  scrollArea->viewport()->installEventFilter(this);

  // Add scroll timer for touch-based automatic loading
  scrollCheckTimer = new QTimer(this);
  scrollCheckTimer->setSingleShot(false);
  scrollCheckTimer->setInterval(500); // Check every 500ms
  connect(scrollCheckTimer, &QTimer::timeout, this, &BPRoutesPanel::checkScrollPosition);

  mainLayout->addWidget(scrollArea);

  // Connect signals
  connect(refreshButton, &QPushButton::clicked, this, &BPRoutesPanel::handleRefresh);
  connect(cleanupButton, &QPushButton::clicked, this, &BPRoutesPanel::handleCleanup);
  connect(settingsButton, &QPushButton::clicked, this, &BPRoutesPanel::showSettingsDialog);
  connect(syncAllButton, &QPushButton::clicked, this, &BPRoutesPanel::backupAllRoutes);
  connect(viewLogButton, &QPushButton::clicked, this, &BPRoutesPanel::viewSyncLog);
}

void BPRoutesPanel::loadRoutes() {
  if (isLoading) {
    std::cout << "Already loading routes, skipping..." << std::endl;
    return;
  }
  isLoading = true;

  std::cout << "Starting route loading process..." << std::endl;
  showLoadingOverlay(tr("Loading routes..."));

  // Create absolute paths
  QString absoluteRoutesDir = QDir(getRoutesDir).absolutePath();
  std::cout << "Absolute routes directory: " << absoluteRoutesDir.toStdString() << std::endl;

  // Verify directory exists and is readable
  QDir dir(absoluteRoutesDir);
  if (!dir.exists()) {
    std::cout << "Routes directory does not exist!" << std::endl;
    hideLoadingOverlay();
    isLoading = false;
    return;
  }

  // Use QtConcurrent with proper mutex protection
  QFuture<void> future = QtConcurrent::run([this, absoluteRoutesDir]() {
    QMutexLocker locker(&fileMutex);

    QDir dir(absoluteRoutesDir);
    // Look specifically for route directory pattern (containing --)
    QStringList routeDirs = dir.entryList(QStringList() << "*--*", QDir::Dirs | QDir::NoDotAndDotDot);
    std::cout << "Found " << routeDirs.size() << " potential route directories" << std::endl;

    QVector<RouteInfo> newRoutes;
    QSet<QString> processedRoutes; // Track which base routes we've already processed

    for (const QString &routeDir : routeDirs) {
      // Extract base route name using the first two parts separated by "--"
      QStringList parts = routeDir.split("--");
      QString baseRoute = (parts.size() >= 2) ? parts[0] + "--" + parts[1] : parts.first();

      // Skip if we've already processed this base route
      if (processedRoutes.contains(baseRoute)) {
        continue;
      }
      processedRoutes.insert(baseRoute);

      // std::cout << "Processing route: " << baseRoute.toStdString() << std::endl;

      // Check cache with thread safety
      bool useCache = routeCache.isValid() && routeCache.routeInfoCache.contains(baseRoute);

      if (useCache) {
        newRoutes.append(routeCache.routeInfoCache[baseRoute]);
        std::cout << "Using cached info for route: " << baseRoute.toStdString() << std::endl;
        continue;
      }

      // Cache miss - process route info
      RouteInfo info = getRouteInfo(baseRoute);
      if (info.segments > 0) { // Only add valid routes
        // std::cout << "Adding route: " << baseRoute.toStdString() << " with " << info.segments << " segments" << std::endl;
        routeCache.routeInfoCache[baseRoute] = info;
        newRoutes.append(info);
      }
    }

    // Sort routes by timestamp (newest first)
    std::sort(newRoutes.begin(), newRoutes.end(), [](const RouteInfo &a, const RouteInfo &b) { return a.timestamp > b.timestamp; });

    std::cout << "Total valid routes found: " << newRoutes.size() << std::endl;

    // Update UI in main thread
    QMetaObject::invokeMethod(
        this,
        [this, newRoutes]() mutable {
          routes = std::move(newRoutes);
          updateRouteList();
          updateStats();
          updateButtonStates();
          hideLoadingOverlay();
          isLoading = false;
          std::cout << "Route loading complete" << std::endl;
        },
        Qt::QueuedConnection);
  });
}

void BPRoutesPanel::updateButtonStates() {
  bool onRoad = isOnRoad();
  bool hasValidLocation = !syncConfig.networkLocation.isEmpty() && validateSyncSettings();

  // Disable controls when on road
  refreshButton->setEnabled(!onRoad);
  cleanupButton->setEnabled(!onRoad);
  // settingsButton->setEnabled(!onRoad);
  // viewLogButton->setEnabled(!onRoad);

  // Show/enable sync all button based on conditions
  syncAllButton->setVisible(hasValidLocation);
  syncAllButton->setEnabled(!onRoad && hasValidLocation);

  // Disable route-specific controls when on road
  for (int i = 0; i < routesLayout->count(); ++i) {
    QWidget *widget = routesLayout->itemAt(i)->widget();
    if (widget) {
      // Disable all buttons in the route widget
      QList<QPushButton *> buttons = widget->findChildren<QPushButton *>();
      for (auto btn : buttons) {
        if (btn->property("class") == "routes-panel-action-button" || btn->property("class") == "route-video-play-button" || btn->text() == tr("Concat") ||
            btn->text() == tr("Sync") || btn->text() == tr("Delete") || btn->text().contains("camera", Qt::CaseInsensitive)) {
          btn->setEnabled(!onRoad);
        }
      }
    }
  }
}

void BPRoutesPanel::createRouteWidget(const RouteInfo &route) {
  bool onRoad = isOnRoad();

  auto routeWidget = new QWidget(routesContainer);
  routeWidget->setObjectName("routeItem");
  auto routeLayout = new QVBoxLayout(routeWidget);
  routeLayout->setContentsMargins(15, 15, 15, 15);
  routeLayout->setSpacing(10);

  // Create header container
  auto headerBtn = new QPushButton();
  headerBtn->setMinimumHeight(180);
  headerBtn->setStyleSheet(R"(
    QPushButton {
      background-color: #363636;
      border-radius: 10px;
      padding: 10px;
      text-align: left;
      border: none;
      min-height: 150px;
    }
    QPushButton:hover {
      background-color: #404040;
    }
  )");

  auto headerLayout = new QHBoxLayout(headerBtn);
  headerLayout->setContentsMargins(15, 15, 15, 15);

  // Left side: Thumbnail and basic info
  auto leftWidget = new QWidget();
  auto leftLayout = new QHBoxLayout(leftWidget);
  leftLayout->setSpacing(15);

  // Thumbnail
  auto thumbnailLabel = new QLabel();
  initializeThumbnail(thumbnailLabel, route.baseName);
  leftLayout->addWidget(thumbnailLabel);

  // Basic info (route name and timestamp)
  auto basicInfo = new QWidget();
  basicInfo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  auto basicLayout = new QVBoxLayout(basicInfo);
  basicLayout->setSpacing(5);
  basicLayout->setContentsMargins(0, 0, 0, 0);

  auto nameLabel = new QLabel(route.timestamp);
  nameLabel->setStyleSheet("font-size: 38px; font-weight: 600; color: white;");
  nameLabel->setWordWrap(true);
  nameLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  nameLabel->setMaximumHeight(50);
  basicLayout->addWidget(nameLabel);

  // Trip info line (start time and duration)
  auto tripInfoLabel = new QLabel(tr("%1 • ID: %2").arg(getRouteDuration(route.baseName)).arg(route.baseName));
  tripInfoLabel->setStyleSheet("font-size: 32px; color: #AAAAAA;");
  basicLayout->addWidget(tripInfoLabel);

  // Stats line (segments, size, distance if available)
  QString statsText = tr("%1 segments • %2").arg(route.segments).arg(route.size);
  if (route.tripMiles > 0) {
    statsText += tr(" • %.1f miles").arg(route.tripMiles);
  }
  auto tripStatsLabel = new QLabel(statsText);
  tripStatsLabel->setStyleSheet("font-size: 32px; color: #AAAAAA;");
  basicLayout->addWidget(tripStatsLabel);

  leftLayout->addWidget(basicInfo);

  // Right side: Expand button
  auto expandBtn = new QPushButton();
  expandBtn->setFixedSize(60, 60);              // Make the button bigger
  expandBtn->setCursor(Qt::PointingHandCursor); // Set cursor to show it's clickable
  expandBtn->setStyleSheet(R"(
  QPushButton {
    background-color: transparent;
    border: none;
    padding: 0px;
    margin: 0px;
  }
  QPushButton:hover {
    background-color: rgba(255, 255, 255, 0.1);
    border-radius: 30px; /* Half of width/height for perfect circle */
  }
)");

  // Set the expand icon
  QIcon expandIcon(expandedRoutes[route.baseName] ? "../assets/offroad/icon_expand_up.png" : "../assets/offroad/icon_expand_down.png");
  expandBtn->setIcon(expandIcon);
  expandBtn->setIconSize(QSize(40, 40)); // Slightly smaller icon size for better fit

  // Create a container with fixed dimensions for proper alignment
  auto rightWidget = new QWidget();
  rightWidget->setFixedSize(80, 150); // Fixed width and height to match content area
  auto rightLayout = new QHBoxLayout(rightWidget);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(0);
  rightLayout->addWidget(expandBtn, 0, Qt::AlignCenter);

  // Ensure right widget doesn't resize
  rightWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  headerLayout->addWidget(leftWidget, 1); // Give left widget the stretch
  headerLayout->addWidget(rightWidget);   // Right widget takes minimal space needed

  // Expanded details container with two columns (70% left, 30% right)
  auto detailsContainer = new QWidget();
  detailsContainer->setStyleSheet("background-color: #242424; border-radius: 10px;");
  auto detailsMainLayout = new QHBoxLayout(detailsContainer);
  detailsMainLayout->setContentsMargins(20, 20, 20, 20);
  detailsMainLayout->setSpacing(15);

  // Left Column (70%): Detailed info & action buttons
  auto leftColumn = new QVBoxLayout();
  leftColumn->setSpacing(15);

  QString detailsText = QString("Start Time: %1\n"
                                "End Time: %2\n"
                                "Duration: %3\n"
                                "Elapsed Time: %4\n"
                                // "Trip Distance: %5f miles\n"
                                "Total Size: %6\n"
                                "Video Files: %7")
                            .arg(route.timestamp)
                            .arg(route.endTimestamp)
                            .arg(route.duration)
                            .arg(route.elapsedTime)
                            // .arg(route.tripMiles)
                            .arg(route.size)
                            .arg(route.hasVideo ? tr("Yes") : tr("No"));
  auto detailsLabel = new QLabel(detailsText);
  detailsLabel->setStyleSheet("font-size: 32px; color: #AAAAAA; padding: 10px;");
  detailsLabel->setWordWrap(true);
  leftColumn->addWidget(detailsLabel);

  // Action Buttons Layout (Play, Concat, Sync, Delete)
  auto actionButtonsLayout = new QHBoxLayout();
  actionButtonsLayout->setSpacing(10);

  auto createActionButton = [](const QString &text, const QString &bgColor, const QString &hoverColor, const QString &disabledColor = "#404040") -> QPushButton * {
    auto btn = new QPushButton(text);
    btn->setStyleSheet(QString(R"(
    QPushButton {
      background-color: %1;
      border-radius: 10px;
      color: white;
      font-size: 32px;
      padding: 15px 30px;
      min-width: 150px;
    }
    QPushButton:hover {
      background-color: %2;
    }
    QPushButton:disabled {
      background-color: %3;
      color: #888888;
    }
  )")
                           .arg(bgColor, hoverColor, "#404040"));
    return btn;
  };

  auto concatBtn = createActionButton(tr("Concat"), "#FF9800", "#F57C00");  // Orange
  auto routeSyncBtn = createActionButton(tr("Sync"), "#2196F3", "#1976D2"); // Blue
  auto deleteBtn = createActionButton(tr("Delete"), "#F44336", "#D32F2F");  // Red

  concatBtn->setEnabled(!onRoad);
  concatBtn->setVisible(false);
  routeSyncBtn->setEnabled(!onRoad);
  routeSyncBtn->setVisible(false);
  deleteBtn->setEnabled(!onRoad);
  // deleteBtn->setVisible(false);

  // Add them to the layout
  actionButtonsLayout->addWidget(concatBtn);
  actionButtonsLayout->addWidget(routeSyncBtn);
  actionButtonsLayout->addWidget(deleteBtn);
  actionButtonsLayout->addStretch();
  leftColumn->addLayout(actionButtonsLayout);

  // Right Column (30%): Video buttons (stacked vertically)
  auto rightColumn = new QVBoxLayout();
  rightColumn->addStretch();
  rightColumn->setSpacing(10);
  auto createVideoButton = [](const QString &text, const QString &bgColor, const QString &hoverColor) -> QPushButton * {
    auto btn = new QPushButton(text);
    btn->setProperty("class", "route-video-play-button");
    btn->setFixedWidth(250);
    btn->setStyleSheet(QString(R"(
    QPushButton {
      background-color: %1;
      border-radius: 10px;
      color: white;
      font-size: 32px;
      padding: 15px 30px;
    }
    QPushButton:hover {
      background-color: %2;
    }
    QPushButton:disabled {
      background-color: #404040;
      color: #888888;
    }
  )")
                           .arg(bgColor, hoverColor));
    return btn;
  };

  QStringList videoLabels = {tr("Front"), tr("Front (Wide)"), tr("Driver"), tr("Front (LQ)")};
  QStringList videoFiles = {"fcamera.hevc", "ecamera.hevc", "dcamera.hevc", "qcamera.ts"};

  QVector<QPair<QString, QPair<QString, QString>>> videoColors = {
      {tr("Front"), {"#009688", "#00796B"}},        // teal
      {tr("Front (Wide)"), {"#3F51B5", "#303F9F"}}, // indigo
      {tr("Driver"), {"#9C27B0", "#7B1FA2"}},       // purple
      {tr("Front (LQ)"), {"#606060", "#808080"}}    // gray
  };

  for (int i = 0; i < videoFiles.size(); i++) {
    // Use the custom colors for each video button
    auto videoBtn = createVideoButton(videoLabels[i], videoColors[i].second.first, videoColors[i].second.second);
    videoBtn->setEnabled(!onRoad);
    videoBtn->setVisible(false);
    connect(videoBtn, &QPushButton::clicked, [=]() {
      // Call your concatenated video playback function
      playRouteVideoConcatenated(route.baseName, videoFiles[i]);
    });
    rightColumn->addWidget(videoBtn);
  }

  // Add columns to the main details layout with a 70/30 split
  detailsMainLayout->addLayout(leftColumn, 7);
  detailsMainLayout->addLayout(rightColumn, 3);

  // Add header and expanded container to route widget layout
  routeLayout->addWidget(headerBtn);
  routeLayout->addWidget(detailsContainer);

  // Set initial expanded state
  detailsContainer->setVisible(expandedRoutes[route.baseName]);

  connect(expandBtn, &QPushButton::clicked, [=]() {
    expandedRoutes[route.baseName] = !expandedRoutes[route.baseName];
    detailsContainer->setVisible(expandedRoutes[route.baseName]);

    // Update the icon
    QIcon newIcon(expandedRoutes[route.baseName] ? "../assets/offroad/icon_expand_up.png" : "../assets/offroad/icon_expand_down.png");
    expandBtn->setIcon(newIcon);
  });

  // Connect action buttons
  connect(concatBtn, &QPushButton::clicked, [=]() { handleRouteConcatenation(route.baseName); });
  connect(routeSyncBtn, &QPushButton::clicked, [=]() { handleRouteSync(); });
  connect(deleteBtn, &QPushButton::clicked, [=]() { handleRouteRemoval(route.baseName); });

  // Set button states based on route properties
  concatBtn->setEnabled(route.segments > 1);

  routesLayout->addWidget(routeWidget);
}

void BPRoutesPanel::updateRouteList() {
  // Clear existing routes
  QLayoutItem *item;
  while ((item = routesLayout->takeAt(0)) != nullptr) {
    delete item->widget();
    delete item;
  }

  // If no routes, add stretch and return
  if (routes.isEmpty()) {
    routesLayout->addStretch();
    return;
  }

  // Group routes by date
  groupRoutesByDate();

  // Reset pagination
  currentPage = 0;
  totalPages = (routes.size() + routesPerPage - 1) / routesPerPage;

  // Load first page
  loadMoreRoutes();

  // Start scroll checking timer for automatic loading
  if (scrollCheckTimer) {
    scrollCheckTimer->start();
  }

  // No manual pagination controls - routes load automatically on scroll
  routesLayout->addStretch();
}

void BPRoutesPanel::continueRouteProcessing() {
  int count = 0;
  while (routeIndex < routes.size() && count < 5) {
    createRouteWidget(routes[routeIndex]);
    routeIndex++;
    count++;
  }

  // Update UI after each batch
  QApplication::processEvents();

  // If more routes to process, schedule the next batch
  if (routeIndex < routes.size()) {
    QTimer::singleShot(10, this, &BPRoutesPanel::continueRouteProcessing);
  } else {
    // Don't reset routeIndex here! Just add stretch
    routesLayout->addStretch();
  }
}

void BPRoutesPanel::showLoadingOverlay(const QString &message) {
  if (!loadingOverlay) {
    loadingOverlay = new QWidget(this);
    loadingOverlay->setObjectName("loadingOverlay");
    auto layout = new QVBoxLayout(loadingOverlay);

    loadingLabel = new QLabel(loadingOverlay);
    loadingLabel->setAlignment(Qt::AlignCenter);
    loadingLabel->setStyleSheet(R"(
            color: white;
            font-size: 40px;
            font-weight: 500;
            background-color: transparent;
        )");

    layout->addWidget(loadingLabel, 0, Qt::AlignCenter);

    loadingOverlay->setStyleSheet(R"(
            QWidget#loadingOverlay {
                background-color: rgba(0, 0, 0, 0.8);
                border-radius: 15px;
            }
        )");
  }

  loadingLabel->setText(message);
  loadingOverlay->setGeometry(rect());
  loadingOverlay->show();
  loadingOverlay->raise();
}

void BPRoutesPanel::hideLoadingOverlay() {
  if (loadingOverlay) {
    loadingOverlay->hide();
  }
}

void BPRoutesPanel::showStatusOverlay(const QString &message) {
  if (!statusOverlay) {
    statusOverlay = new QWidget(this);
    statusOverlay->setObjectName("statusOverlay");
    auto layout = new QVBoxLayout(statusOverlay);

    statusLabel = new QLabel(statusOverlay);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet(R"(
            color: white;
            font-size: 36px;
            font-weight: 500;
            background-color: transparent;
        )");

    layout->addWidget(statusLabel, 0, Qt::AlignCenter);

    statusOverlay->setStyleSheet(R"(
            QWidget#statusOverlay {
                background-color: rgba(0, 0, 0, 0.7);
                border-radius: 15px;
            }
        )");
  }

  statusLabel->setText(message);
  statusOverlay->setGeometry(rect());
  statusOverlay->show();
  statusOverlay->raise();
}

void BPRoutesPanel::hideStatusOverlay() {
  if (statusOverlay) {
    statusOverlay->hide();
  }
}

void BPRoutesPanel::updateStats() {
  qint64 totalSize = 0;
  int totalSegments = 0;

  for (const auto &route : routes) {
    totalSegments += route.segments;
    totalSize += QStringToSize(route.size);
  }

  statsLabel->setText(tr("Total Routes: %1 | Total Segments: %2 | Total Size: %3").arg(routes.size()).arg(totalSegments).arg(formatSize(totalSize)));

  if (syncConfig.enabled) {
    QString syncInfo = tr(" | Sync: Enabled");
    if (!syncConfig.networkLocation.isEmpty()) {
      syncInfo += tr(" | Location: %1").arg(syncConfig.networkLocation);
    }
    statsLabel->setText(statsLabel->text() + syncInfo);
  }
}

void BPRoutesPanel::handleRouteDetails(const QString &route) {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = tr("Route Details");
  config.prompt = tr("View route %1?").arg(route);
  config.confirmText = tr("View");
  config.cancelText = tr("Cancel");

  auto dialog = BPConfirmationDialog::showConfirmation(config, this);
  connect(dialog, &BPConfirmationDialog::confirmed, this, [this, route](bool accepted) {
    if (accepted) {
      QString routePath = getRouteSegmentPath(route, 0);
      auto routeInfo = getRouteInfo(routePath);

      QString details = tr("Route: %1\n"
                           "Recorded: %2\n"
                           "Duration: %3\n"
                           "Segments: %4\n"
                           "Size: %5\n"
                           "Has Video: %6\n"
                           "Has RLog: %7\n"
                           "Has QLog: %8")
                            .arg(route)
                            .arg(routeInfo.timestamp)
                            .arg(routeInfo.duration)
                            .arg(routeInfo.segments)
                            .arg(routeInfo.size)
                            .arg(routeInfo.hasVideo ? "Yes" : "No")
                            .arg(routeInfo.hasRLog ? "Yes" : "No")
                            .arg(routeInfo.hasQLog ? "Yes" : "No");

      BPConfirmationDialog::ConfirmConfig detailsConfig;
      detailsConfig.title = tr("Route Details");
      detailsConfig.prompt = details;
      detailsConfig.confirmText = tr("Close");
      BPConfirmationDialog::showMessage(detailsConfig, this);
    }
  });
}

void BPRoutesPanel::handleRouteConcatenation(const QString &route) { concatRouteMenu(route); }

void BPRoutesPanel::concatRouteMenu(const QString &routeBase) {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = tr("Concatenate Route Files");
  config.prompt = tr("Select files to concatenate for route: %1").arg(routeBase);

  auto dialog = new BPConfirmationDialog(config, this);
  dialog->setModal(true);

  // Create selection buttons
  QVBoxLayout *buttonLayout = new QVBoxLayout();

  // Add concatenation options
  QStringList options = {tr("RLog files"), tr("QLog files"), tr("Video files"), tr("All files")};

  QString outputDir = getRoutesDir + "/concatenated/" + routeBase;
  QDir().mkpath(outputDir);

  for (const QString &option : options) {
    auto button = new QPushButton(option);
    button->setStyleSheet(R"(
            QPushButton {
                background-color: #2196F3;
                border-radius: 10px;
                color: white;
                font-size: 32px;
                padding: 15px;
                margin: 5px;
            }
            QPushButton:hover {
                background-color: #1976D2;
            }
            QPushButton:pressed {
                background-color: #1565C0;
            }
        )");

    connect(button, &QPushButton::clicked, [=]() {
      dialog->accept();
      if (option == tr("RLog files")) {
        concatRouteSegments(routeBase, "rlog", outputDir);
      } else if (option == tr("QLog files")) {
        concatRouteSegments(routeBase, "qlog", outputDir);
      } else if (option == tr("Video files")) {
        concatRouteSegments(routeBase, "video", outputDir);
      } else {
        concatRouteSegments(routeBase, "rlog", outputDir);
        concatRouteSegments(routeBase, "qlog", outputDir);
        concatRouteSegments(routeBase, "video", outputDir);
      }
    });

    buttonLayout->addWidget(button);
  }

  // Add the button layout to the dialog
  QWidget *dialogContent = dialog->findChild<QWidget *>();
  if (dialogContent) {
    QVBoxLayout *dialogMainLayout = qobject_cast<QVBoxLayout *>(dialogContent->layout());
    if (dialogMainLayout) {
      dialogMainLayout->addLayout(buttonLayout);
    }
  }

  dialog->setupFullscreen();
}

bool BPRoutesPanel::concatRouteSegments(const QString &routeBase, const QString &concatType, const QString &outputDir, bool keepOriginals) {
  QDir().mkpath(getConcatDir);

  if (concatType == "rlog") {
    return concatRLog(routeBase, outputDir);
  } else if (concatType == "qlog") {
    return concatQLog(routeBase, outputDir);
  } else if (concatType == "video") {
    return concatVideos(routeBase, outputDir);
  }

  return false;
}

bool BPRoutesPanel::concatRLog(const QString &routeBase, const QString &outputDir) {
  QString outputFile = outputDir + "/rlog";
  QFile output(outputFile);

  if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = tr("Error");
    config.prompt = tr("Failed to create output file: %1").arg(outputFile);
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    return false;
  }

  QTextStream out(&output);
  int totalSegments = getTotalSegments(routeBase);

  // Setup progress dialog
  QProgressDialog progress(tr("Concatenating RLog files..."), tr("Cancel"), 0, totalSegments, this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);

  for (int i = 0; i < totalSegments; i++) {
    if (progress.wasCanceled()) {
      output.close();
      QFile::remove(outputFile);
      return false;
    }

    QString segmentPath = getRouteSegmentPath(routeBase, i);
    QFile rlog(segmentPath + "/rlog");

    if (rlog.exists() && rlog.open(QIODevice::ReadOnly | QIODevice::Text)) {
      out << "\n=== Segment " << i << " ===\n";
      out << rlog.readAll();
      rlog.close();
    }

    progress.setValue(i + 1);
  }

  output.close();
  return true;
}

bool BPRoutesPanel::concatQLog(const QString &routeBase, const QString &outputDir) {
  QString outputFile = outputDir + "/qlog";
  QFile output(outputFile);

  if (!output.open(QIODevice::WriteOnly)) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = tr("Error");
    config.prompt = tr("Failed to create output file: %1").arg(outputFile);
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    return false;
  }

  int totalSegments = getTotalSegments(routeBase);

  QProgressDialog progress(tr("Concatenating QLog files..."), tr("Cancel"), 0, totalSegments, this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);

  for (int i = 0; i < totalSegments; i++) {
    if (progress.wasCanceled()) {
      output.close();
      QFile::remove(outputFile);
      return false;
    }

    QString segmentPath = getRouteSegmentPath(routeBase, i);
    QFile qlog(segmentPath + "/qlog");

    if (qlog.exists() && qlog.open(QIODevice::ReadOnly)) {
      output.write(qlog.readAll());
      qlog.close();
    }

    progress.setValue(i + 1);
  }

  output.close();
  return true;
}

bool BPRoutesPanel::concatVideos(const QString &routeBase, const QString &outputDir) {
  // Check for ffmpeg using platform-specific detection
  QString ffmpegPath = findFFmpegExecutable();
  if (ffmpegPath.isEmpty()) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = tr("Error");
    config.prompt = tr("ffmpeg not found. Cannot concatenate video files.");
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    return false;
  }

  QStringList cameras = {"dcamera", "ecamera", "fcamera", "qcamera"};
  QStringList extensions = {"hevc", "hevc", "hevc", "ts"};

  for (int i = 0; i < cameras.size(); i++) {
    QString camera = cameras[i];
    QString ext = extensions[i];
    QString outputFile = QString("%1/%2.%3").arg(outputDir, camera, ext);

    // Create concat list
    QString concatList = QString("%1/%2_concat_list.txt").arg(getConcatDir, camera);
    QFile list(concatList);

    if (!list.open(QIODevice::WriteOnly | QIODevice::Text)) {
      continue;
    }

    QTextStream out(&list);
    int totalSegments = getTotalSegments(routeBase);
    int validSegments = 0;

    // Count valid segments
    for (int j = 0; j < totalSegments; j++) {
      QString segmentPath = getRouteSegmentPath(routeBase, j);
      QString videoFile = QString("%1/%2.%3").arg(segmentPath, camera, ext);
      if (QFile::exists(videoFile)) {
        out << "file '" << videoFile << "'\n";
        validSegments++;
      }
    }

    list.close();

    if (validSegments == 0) {
      QFile::remove(concatList);
      continue;
    }

    // Create progress dialog
    QProgressDialog progress(tr("Concatenating %1 videos... This may take a while.").arg(camera), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    // Setup ffmpeg process
    QProcess ffmpegProcess;
    ffmpegProcess.setProcessChannelMode(QProcess::MergedChannels);

    QString cmd = QString("%1 -y -f concat -safe 0 -i %2 -c copy -fflags +genpts %3").arg(ffmpegPath, concatList, outputFile);

    ffmpegProcess.start(cmd);

    // Monitor progress
    while (ffmpegProcess.state() != QProcess::NotRunning) {
      ffmpegProcess.waitForReadyRead();
      QString output = ffmpegProcess.readAll();

      // Parse ffmpeg progress output
      // Looking for time=HH:MM:SS.ss
      QRegExp rx("time=(\\d+):(\\d+):(\\d+\\.\\d+)");
      if (rx.indexIn(output) != -1) {
        int hours = rx.cap(1).toInt();
        int minutes = rx.cap(2).toInt();
        float seconds = rx.cap(3).toFloat();

        // Calculate progress percentage based on total duration
        float currentTime = hours * 3600 + minutes * 60 + seconds;
        float totalTime = validSegments * 60; // 60 seconds per segment
        int progressValue = (currentTime / totalTime) * 100;

        progress.setValue(qMin(progressValue, 100));
      }

      if (progress.wasCanceled()) {
        ffmpegProcess.kill();
        QFile::remove(outputFile);
        QFile::remove(concatList);
        return false;
      }

      QApplication::processEvents();
    }

    QFile::remove(concatList);

    if (ffmpegProcess.exitCode() != 0) {
      BPConfirmationDialog::ConfirmConfig config;
      config.title = tr("Error");
      config.prompt = tr("Failed to concatenate %1 videos").arg(camera);
      config.confirmText = tr("OK");
      config.confirmColor = "#FF0000";
      BPConfirmationDialog::showMessage(config, this);
      QFile::remove(outputFile);
      return false;
    }

    progress.setValue(100);
  }

  return true;
}

void BPRoutesPanel::setupNetworkSync() {
  loadSyncConfig();

  syncTimer = new QTimer(this);
  connect(syncTimer, &QTimer::timeout, this, [this]() {
    // Run sync in background thread to avoid blocking UI
    QtConcurrent::run([this]() {
      syncRoutes();
    });
  });

  if (syncConfig.enabled) {
    syncTimer->start(syncConfig.startupDelay * 1000);
  }
}

void BPRoutesPanel::loadSyncConfig() {
  syncConfig.enabled = params.getBool("RouteSync");
  syncConfig.startupDelay = params.getInt("RouteSyncDelay");
  syncConfig.retentionDays = params.getInt("RouteRetentionDays");
  syncConfig.autoConcat = params.getBool("RouteAutoConcat");
  syncConfig.networkLocation = QString::fromStdString(params.get("RouteSyncLocation"));
  syncConfig.protocol = QString::fromStdString(params.get("RouteSyncProtocol"));

  // Set defaults if not configured
  if (syncConfig.startupDelay < 0)
    syncConfig.startupDelay = 30;
  if (syncConfig.retentionDays < 0)
    syncConfig.retentionDays = 30;

  updateButtonStates();
}

void BPRoutesPanel::saveSyncConfig() {
  params.putBool("RouteSync", syncConfig.enabled);
  params.putInt("RouteSyncDelay", syncConfig.startupDelay);
  params.putInt("RouteRetentionDays", syncConfig.retentionDays);
  params.putBool("RouteAutoConcat", syncConfig.autoConcat);
  params.put("RouteSyncLocation", syncConfig.networkLocation.toStdString());
  params.put("RouteSyncProtocol", syncConfig.protocol.toStdString());
}

bool BPRoutesPanel::syncRoutes() {
  if (isSyncing)
    return false;
  isSyncing = true;

  // Update UI
  syncAllButton->setText(tr("Syncing..."));
  syncAllButton->setEnabled(false);

  QFile logFile(getSyncLogPath());
  logFile.open(QIODevice::WriteOnly | QIODevice::Append);
  QTextStream log(&logFile);

  log << QDateTime::currentDateTime().toString() << " Starting route sync\n";

  // Validate settings
  if (!validateSyncSettings()) {
    log << "Invalid sync settings. Aborting.\n";
    isSyncing = false;
    updateSyncStatus();
    return false;
  }

  // Get list of routes to sync
  QDir routeDir(getRoutesDir);
  QStringList routesList = routeDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

  syncStatus = SyncStatus();
  syncStatus.totalRoutes = routesList.size();

  // Calculate total size
  for (const QString &route : routesList) {
    QDir routePath(getRoutesDir + "/" + route);
    syncStatus.totalBytes += calculateDirSize(routePath.absolutePath());
  }

  showSyncProgressDialog();

  for (const QString &route : routesList) {
    syncStatus.currentRoute = route;
    updateSyncProgress(tr("Syncing route: %1").arg(route));

    // Auto concatenate if enabled
    if (syncConfig.autoConcat) {
      int segments = getTotalSegments(route);
      if (segments > 1) {
        log << "Auto-concatenating route: " << route << "\n";
        QString outputDir = getRoutesDir + "/concatenated/" + route;
        QDir().mkpath(outputDir);

        bool concatSuccess = concatRouteSegments(route, "rlog", outputDir) && concatRouteSegments(route, "qlog", outputDir) && concatRouteSegments(route, "video", outputDir);

        if (!concatSuccess) {
          log << "Concatenation failed for route: " << route << "\n";
          syncStatus.failedSyncs++;
          continue;
        }
      }
    }

    // Sync route
    if (backupRoute(route)) {
      syncStatus.successfulSyncs++;
      log << "Successfully synced route: " << route << "\n";

      // Clean up if retention period exceeded
      QFileInfo routeInfo(routeDir.filePath(route));
      if (routeInfo.lastModified().daysTo(QDateTime::currentDateTime()) > syncConfig.retentionDays) {
        log << "Removing old route: " << route << "\n";
        QDir(routeDir.filePath(route)).removeRecursively();
      }
    } else {
      syncStatus.failedSyncs++;
      log << "Failed to sync route: " << route << "\n";
    }

    syncStatus.processedRoutes++;
    updateSyncProgress(tr("Processed %1 of %2 routes").arg(syncStatus.processedRoutes).arg(syncStatus.totalRoutes));

    if (syncProgressDialog && syncProgressDialog->wasCanceled()) {
      log << "Sync canceled by user\n";
      break;
    }
  }

  if (syncProgressDialog) {
    syncProgressDialog->close();
    delete syncProgressDialog;
    syncProgressDialog = nullptr;
  }

  log << QString("Sync completed. Successful: %1 Failed: %2\n\n").arg(syncStatus.successfulSyncs).arg(syncStatus.failedSyncs);

  isSyncing = false;
  updateSyncStatus();
  return (syncStatus.failedSyncs == 0);
}

void BPRoutesPanel::updateSyncProgress(const QString &status) {
  if (!syncProgressDialog)
    return;

  QString progressText = status;
  if (syncStatus.totalBytes > 0) {
    double progress = (double)syncStatus.transferredBytes / syncStatus.totalBytes * 100;
    progressText += tr("\n%1 of %2 (%3%)").arg(formatSize(syncStatus.transferredBytes)).arg(formatSize(syncStatus.totalBytes)).arg(QString::number(progress, 'f', 1));
  }

  syncProgressDialog->setLabelText(progressText);
  syncProgressDialog->setValue(syncStatus.processedRoutes);
}

qint64 BPRoutesPanel::calculateDirSize(const QString &path) {
  QDir dir(path);
  qint64 size = 0;

  for (const QFileInfo &info : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (info.isDir()) {
      size += calculateDirSize(info.absoluteFilePath());
    } else {
      size += info.size();
    }
  }

  return size;
}

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

qint64 BPRoutesPanel::QStringToSize(const QString &sizeStr) {
  QRegExp rx("(\\d+(\\.\\d+)?)(\\s*)(B|KB|MB|GB|TB)");
  if (rx.indexIn(sizeStr) != -1) {
    double number = rx.cap(1).toDouble();
    QString unit = rx.cap(4);

    if (unit == "B")
      return number;
    if (unit == "KB")
      return number * 1024;
    if (unit == "MB")
      return number * 1024 * 1024;
    if (unit == "GB")
      return number * 1024 * 1024 * 1024;
    if (unit == "TB")
      return number * 1024 * 1024 * 1024 * 1024;
  }
  return 0;
}

void BPRoutesPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  updateButtonStates();

  // Start activity simulation
  activityTimer->start();
  resetMaxDurationTimer();

  // Verify routes directory
  QDir routeDir(getRoutesDir);
  std::cout << "Checking routes directory: " << getRoutesDir.toStdString() << std::endl;

  if (!routeDir.exists()) {
    std::cout << "Routes directory does not exist, creating..." << std::endl;
    if (!QDir().mkpath(getRoutesDir)) {
      std::cout << "Failed to create routes directory!" << std::endl;
      BPConfirmationDialog::ConfirmConfig config;
      config.title = tr("Error");
      config.prompt = tr("Could not create routes directory: %1").arg(getRoutesDir);
      config.confirmText = tr("OK");
      config.confirmColor = "#FF0000";
      BPConfirmationDialog::showMessage(config, this);
      return;
    }
  }

  // Clear existing data safely
  {
    QMutexLocker locker(&fileMutex);
    routes.clear();
    expandedRoutes.clear();
    while (QLayoutItem *item = routesLayout->takeAt(0)) {
      delete item->widget();
      delete item;
    }
  }

  showLoadingOverlay(tr("Loading routes..."));

  // Reset loading state to ensure routes can be loaded
  isLoading = false;

  // Load routes in background with delay
  QTimer::singleShot(100, this, [this]() {
    std::cout << "Starting route loading from showEvent..." << std::endl;
    loadRoutes();
  });
}

void BPRoutesPanel::hideEvent(QHideEvent *event) {
  if (syncProgressDialog) {
    syncProgressDialog->cancel();
  }

  // Add this section
  for (auto watcher : thumbnailWatchers) {
    watcher->cancel();
    watcher->deleteLater();
  }
  thumbnailWatchers.clear();

  routes.clear();
  while (QLayoutItem *item = routesLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  // Stop activity timer
  if (activityTimer) {
    activityTimer->stop();
    std::cout << "Activity timer stopped" << std::endl;
  }

  if (scrollCheckTimer) {
    scrollCheckTimer->stop();
  }

  QWidget::hideEvent(event);
}

void BPRoutesPanel::handleCleanup() {
  QVector<QPair<QString, int>> options = {{tr("Last 7 days"), 7},   {tr("Last 14 days"), 14},  {tr("Last 21 days"), 21},
                                          {tr("Last 30 days"), 30}, {tr("Last 3 months"), 90}, {tr("Last 6 months"), 180}};

  BPConfirmationDialog::ConfirmConfig config;
  config.title = tr("Route Cleanup");
  config.prompt = tr("Keep routes from:");

  auto dialog = new BPConfirmationDialog(config, this);

  // Get the dialog's main content widget
  QWidget *content = dialog->findChild<QWidget *>();
  if (content) {
    auto dialogLayout = qobject_cast<QVBoxLayout *>(content->layout());
    if (dialogLayout) {
      // Add combobox for selection
      auto combo = new QComboBox();
      combo->setStyleSheet(R"(
                QComboBox {
                    background-color: #222222;
                    border: none;
                    border-radius: 8px;
                    padding: 12px;
                    color: white;
                    font-size: 32px;
                    min-height: 54px;
                }
            )");

      for (const auto &option : options) {
        combo->addItem(option.first, option.second);
      }

      dialogLayout->addWidget(combo);

      // Add cleanup button
      auto cleanupBtn = new QPushButton(tr("Clean Up"));
      cleanupBtn->setStyleSheet(R"(
                QPushButton {
                    background-color: #F44336;
                    border: none;
                    border-radius: 10px;
                    padding: 16px;
                    color: white;
                    font-size: 32px;
                    min-height: 60px;
                }
            )");

      connect(cleanupBtn, &QPushButton::clicked, [=]() {
        int days = combo->currentData().toInt();
        cleanupRoutes(days);
        routeCache.clear();
        loadRoutes();
        dialog->accept();
      });

      dialogLayout->addWidget(cleanupBtn);
    }
  }

  dialog->setupFullscreen();
}

void BPRoutesPanel::cleanupRoutes(int daysOld) {
  QDir dir(getRoutesDir);
  QDateTime cutoff = QDateTime::currentDateTime().addDays(-daysOld);

  for (const QFileInfo &info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (info.lastModified() < cutoff) {
      QDir(info.absoluteFilePath()).removeRecursively();
      logSyncEvent(tr("Removed old route: %1").arg(info.fileName()));
    }
  }
}

void BPRoutesPanel::handleRefresh() {
  routeCache.clear();
  loadRoutes();
}

void BPRoutesPanel::viewSyncLog() {
  auto dialog = new BPFileViewerDialog(this);
  dialog->loadFileAndShow(getSyncLogPath(), tr("Route Sync Log"), tr("Sync Log"));
}

void BPRoutesPanel::handleRouteRemoval(const QString &route) {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = tr("Remove Route");
  config.prompt = tr("Are you sure you want to remove route %1?\nThis action cannot be undone.").arg(route);
  config.confirmText = tr("Remove");
  config.cancelText = tr("Cancel");
  config.confirmColor = "#F44336";

  auto dialog = BPConfirmationDialog::showConfirmation(config, this);
  connect(dialog, &BPConfirmationDialog::confirmed, this, [this, route](bool accepted) {
    if (accepted) {
      QString routePath = getRoutesDir + "/" + route;
      if (QDir(routePath).removeRecursively()) {
        cleanupThumbnail(route); // Add this line
        routeCache.clear();
        logSyncEvent(tr("Manually removed route: %1").arg(route));
        loadRoutes();
      }
    }
  });
}

BPRoutesPanel::RouteInfo BPRoutesPanel::getRouteInfo(const QString &routeBase) {
  RouteInfo info;
  info.baseName = routeBase;
  info.tripMiles = 0.0; // Initialize trip miles

  // Find all segments for this route
  QDir dir(getRoutesDir);
  QStringList segments = dir.entryList(QStringList() << routeBase + "--*", QDir::Dirs | QDir::NoDotAndDotDot);
  info.segments = segments.size();

  if (info.segments == 0) {
    return info;
  }

  // Get start time from first segment
  QString firstSegment = segments.first();
  QDateTime startTime = QFileInfo(dir.absoluteFilePath(firstSegment)).created();
  info.timestamp = startTime.toString("yyyy-MM-dd h:mm:ss a");
  info.date = startTime.date(); // Set the date for grouping

  // Get end time from last segment
  QString lastSegment = segments.last();
  QDateTime endTime = QFileInfo(dir.absoluteFilePath(lastSegment)).created();
  info.endTimestamp = endTime.toString("yyyy-MM-dd h:mm:ss a");

  // Calculate elapsed time
  qint64 elapsedSecs = startTime.secsTo(endTime);
  info.elapsedTime = QString("%1:%2:%3").arg(elapsedSecs / 3600, 2, 10, QChar('0')).arg((elapsedSecs % 3600) / 60, 2, 10, QChar('0')).arg(elapsedSecs % 60, 2, 10, QChar('0'));

  // Calculate total size
  qint64 totalSize = 0;
  for (const QString &segment : segments) {
    QString segmentPath = dir.absoluteFilePath(segment);
    totalSize += calculateDirSize(segmentPath);
  }
  info.size = formatSize(totalSize);

  // Check for file types in first segment
  QString firstSegmentPath = dir.absoluteFilePath(segments.first());
  info.hasVideo = countFilesOfType(firstSegmentPath, "hevc") > 0;
  info.hasRLog = countFilesOfType(firstSegmentPath, "rlog") > 0;
  info.hasQLog = countFilesOfType(firstSegmentPath, "qlog") > 0;

  // Calculate duration (1 minute per segment)
  info.duration = getRouteDuration(routeBase);

  // Set thumbnail path
  info.thumbnailPath = getThumbnailPath(routeBase);

  return info;
}

int BPRoutesPanel::countFilesOfType(const QString &path, const QString &extension) {
  QDir dir(path);
  return dir.entryList(QStringList() << "*." + extension, QDir::Files).count();
}

QString BPRoutesPanel::getRouteSegmentPath(const QString &routeBase, int segment) {
  QDir dir(getRoutesDir);
  // Now expect folders like "routeBase--0", "routeBase--1", etc.
  QStringList segments = dir.entryList(QStringList() << QString("%1--%2").arg(routeBase).arg(segment), QDir::Dirs | QDir::NoDotAndDotDot);
  if (segments.isEmpty()) {
    return QString();
  }
  return getRoutesDir + "/" + segments.first();
}

int BPRoutesPanel::getTotalSegments(const QString &routeBase) {
  QDir dir(getRoutesDir);
  return dir.entryList(QStringList() << routeBase + "--*", QDir::Dirs | QDir::NoDotAndDotDot).count();
}

void BPRoutesPanel::logSyncEvent(const QString &message) {
  QFile logFile(getSyncLogPath());
  if (logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
    QTextStream stream(&logFile);
    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd h:mm:ss a") << " - " << message << "\n";
  }
}

void BPRoutesPanel::showSyncProgressDialog() {
  if (syncProgressDialog) {
    delete syncProgressDialog;
  }

  syncProgressDialog = new QProgressDialog(tr("Preparing..."), tr("Cancel"), 0, syncStatus.totalRoutes, this);
  syncProgressDialog->setWindowModality(Qt::WindowModal);
  syncProgressDialog->setMinimumDuration(0);
  syncProgressDialog->setAutoClose(false);
  syncProgressDialog->setStyleSheet(R"(
        QProgressDialog {
            background-color: #242424;
            border-radius: 15px;
        }
        QLabel {
            color: white;
            font-size: 32px;
            padding: 20px;
        }
        QProgressBar {
            border: none;
            border-radius: 10px;
            background-color: #363636;
            text-align: center;
            color: white;
            font-size: 24px;
        }
        QProgressBar::chunk {
            background-color: #2196F3;
            border-radius: 10px;
        }
    )");
}

bool BPRoutesPanel::validateSyncSettings() {
  if (!syncConfig.enabled)
    return false;
  if (syncConfig.networkLocation.isEmpty())
    return false;
  if (syncConfig.protocol.isEmpty())
    return false;

  // Test network location accessibility
  QDir dir(syncConfig.networkLocation);
  if (!dir.exists()) {
    if (!dir.mkpath(".")) {
      return false;
    }
  }

  // Test write permissions
  QString testFile = syncConfig.networkLocation + "/.test";
  QFile file(testFile);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.close();
  file.remove();

  return true;
}

void BPRoutesPanel::showSettingsDialog() {
  if (isSyncing) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = tr("Sync in Progress");
    config.prompt = tr("Route sync is already in progress.");
    config.confirmText = tr("OK");
    config.confirmColor = "#00FF00";
    BPConfirmationDialog::showMessage(config, this);
    return;
  }

  auto dialog = new BPRouteSyncSettingsDialog(syncConfig, this);
  connect(dialog, &BPRouteSyncSettingsDialog::configurationUpdated, this, [this](const SyncConfig &newConfig) {
    syncConfig = newConfig;
    saveSyncConfig();

    if (syncConfig.enabled) {
      syncTimer->start(syncConfig.startupDelay * 1000);
      // Run sync in background thread to avoid blocking UI
      QtConcurrent::run([this]() {
        syncRoutes();
      });
    } else {
      syncTimer->stop();
    }

    updateSyncStatus();
  });

  dialog->setupFullscreen();
}

void BPRoutesPanel::handleRouteSync() {
  if (isSyncing) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = tr("Sync in Progress");
    config.prompt = tr("Route sync is already in progress.");
    config.confirmText = tr("OK");
    config.confirmColor = "#00FF00";
    BPConfirmationDialog::showMessage(config, this);
    return;
  }

  if (syncConfig.enabled) {
    syncTimer->start(syncConfig.startupDelay * 1000);
    // Run sync in background thread to avoid blocking UI
    QtConcurrent::run([this]() {
      syncRoutes();
    });
  } else {
    syncTimer->stop();
  }

  updateSyncStatus();
}

void BPRoutesPanel::backupAllRoutes() {
  BPConfirmationDialog::ConfirmConfig config;

  if (!createBackupLocation()) {
    config.title = tr("Backup Error");
    config.prompt = tr("Failed to create backup location");
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    return;
  }

  // Show loading overlay
  showStatusOverlay(tr("Backing up routes..."));

  // Run backup in background thread to avoid blocking UI
  QtConcurrent::run([this]() {
    QDir routeDir(getRoutesDir);
    QStringList routesList = routeDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    bool allSuccessful = true;
    QString failedRoute;

    for (const QString &route : routesList) {
      if (!backupRoute(route)) {
        allSuccessful = false;
        failedRoute = route;
        break;
      }
    }

    // Update UI in main thread
    QMetaObject::invokeMethod(this, [this, allSuccessful, failedRoute, routesList]() {
      hideStatusOverlay();

      BPConfirmationDialog::ConfirmConfig config;
      if (allSuccessful) {
        config.title = tr("Backup Complete");
        config.prompt = tr("Successfully backed up %1 routes").arg(routesList.size());
        config.confirmText = tr("OK");
        config.confirmColor = "#00FF00";
      } else {
        config.title = tr("Backup Error");
        config.prompt = tr("Failed to backup route: %1").arg(failedRoute);
        config.confirmText = tr("OK");
        config.confirmColor = "#FF0000";
      }
      BPConfirmationDialog::showMessage(config, this);
    }, Qt::QueuedConnection);
  });
}

bool BPRoutesPanel::backupRoute(const QString &routeBase) {
  QString sourcePath = getRoutesDir + "/" + routeBase;
  QString destPath = getRoutesDirBackup + "/" + routeBase;

  QDir().mkpath(QFileInfo(destPath).path());

  QProcess rsync;
  QStringList args;
  args << "-av" << "--delete" << sourcePath << destPath;

  rsync.start("rsync", args);
  // Use a reasonable timeout instead of infinite wait
  return rsync.waitForFinished(30000) && rsync.exitCode() == 0; // 30 second timeout
}

bool BPRoutesPanel::createBackupLocation() {
  QDir dir(getRoutesDirBackup);
  if (!dir.exists()) {
    return dir.mkpath(".");
  }
  return true;
}

void BPRoutesPanel::updateSyncStatus() {
  syncAllButton->setText(isSyncing ? tr("Syncing...") : tr("Sync"));
  syncAllButton->setEnabled(!isSyncing);
  updateStats();
}

void BPRoutesPanel::showConfirmDialog(const QString &title, const QString &message, const std::function<void()> &onConfirm) {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = title;
  config.prompt = message;
  config.confirmText = tr("Yes");
  config.cancelText = tr("No");

  auto dialog = BPConfirmationDialog::showConfirmation(config, this);
  connect(dialog, &BPConfirmationDialog::confirmed, this, [onConfirm](bool accepted) {
    if (accepted) {
      onConfirm();
    }
  });
}

QString BPRoutesPanel::getSyncErrorMessage(const QString &route, const QString &error) { return tr("Failed to sync route %1: %2").arg(route).arg(error); }

QString BPRoutesPanel::getDirectorySize(const QString &path) { return formatSize(calculateDirSize(path)); }

QString BPRoutesPanel::formatRouteTimestamp(const QString &routeDir) {
  QFileInfo firstSegment(getRoutesDir + "/" + routeDir);
  return firstSegment.created().toString("yyyy-MM-dd h:mm:ss a");
}

QString BPRoutesPanel::getRouteDuration(const QString &routeBase) {
  int segments = getTotalSegments(routeBase);
  int totalSeconds = segments * 60; // 60 seconds per segment

  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;

  QStringList parts;

  if (hours > 0) {
    parts << tr("%1hrs").arg(hours);
  }
  if (minutes > 0) {
    parts << tr("%1mins").arg(minutes);
  }
  if (seconds > 0 || parts.isEmpty()) {
    parts << tr("%1secs").arg(seconds);
  }

  return parts.join(" ");
}

QString BPRoutesPanel::getThumbnailPath(const QString &routeBase) { return QString("%1/%2.jpg").arg(getThumbnailCacheDir, routeBase); }

QString BPRoutesPanel::generateThumbnailAsync(const QString &routeBase) {
  if (routeBase.isEmpty()) {
    return QString();
  }

  // Check ffmpeg availability using platform-specific detection
  QString ffmpegPath = findFFmpegExecutable();
  if (ffmpegPath.isEmpty()) {
    return QString();
  }

  QDir().mkpath(getThumbnailCacheDir);

  // Find the correct segment directory
  QDir dir(getRoutesDir);
  QStringList segments = dir.entryList(QStringList() << QString("%1--0").arg(routeBase), QDir::Dirs | QDir::NoDotAndDotDot);
  if (segments.isEmpty()) {
    return QString();
  }

  QString videoPath = getRoutesDir + "/" + segments.first() + "/fcamera.hevc";
  if (!QFile::exists(videoPath)) {
    return QString();
  }

  QString thumbnailPath = getThumbnailPath(routeBase);

  QProcess ffmpeg;
  ffmpeg.setProcessChannelMode(QProcess::MergedChannels);

  QStringList args;
  args << "-y"                                                                       // Overwrite output file
       << "-nostdin"                                                                 // Disable interaction
       << "-i" << videoPath                                                          // Input file
       << "-vframes" << "1"                                                          // Extract one frame
       << "-an"                                                                      // Disable audio
       << "-vf" << QString("scale=%1:%2").arg(THUMBNAIL_WIDTH).arg(THUMBNAIL_HEIGHT) // Scale
       << "-strict" << "unofficial"                                                  // Allow non-standard YUV
       << "-pix_fmt" << "yuvj420p"                                                   // Use full-range YUV
       << thumbnailPath;                                                             // Output file

  ffmpeg.start(ffmpegPath, args);

  if (!ffmpeg.waitForStarted(5000)) {
    return QString();
  }

  if (!ffmpeg.waitForFinished(10000)) {
    ffmpeg.kill();
    return QString();
  }

  return ffmpeg.exitCode() == 0 ? thumbnailPath : QString();
}

void BPRoutesPanel::initializeThumbnail(QLabel *thumbnailLabel, const QString &routeBase) {
  thumbnailLabel->setFixedSize(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
  thumbnailLabel->setStyleSheet("background-color: #242424; border-radius: 5px;");
  thumbnailLabel->setAlignment(Qt::AlignCenter);

  QString thumbnailPath = getThumbnailPath(routeBase);
  if (QFile::exists(thumbnailPath)) {
    thumbnailLabel->setPixmap(QPixmap(thumbnailPath).scaled(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  } else {
    thumbnailLabel->setText(tr("Loading..."));
    thumbnailLabel->setStyleSheet("background-color: #242424; color: #666666; border-radius: 5px;");

    if (!thumbnailWatchers.contains(routeBase)) {
      auto watcher = new QFutureWatcher<QString>(this);
      thumbnailWatchers[routeBase] = watcher;

      connect(watcher, &QFutureWatcher<QString>::finished, [=]() {
        QString path = watcher->result();
        if (!path.isEmpty() && QFile::exists(path)) {
          thumbnailLabel->setPixmap(QPixmap(path).scaled(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
          thumbnailLabel->setText(tr("No Preview"));
        }

        thumbnailWatchers.remove(routeBase);
        watcher->deleteLater();
      });

      QFuture<QString> future = QtConcurrent::run(this, &BPRoutesPanel::generateThumbnailAsync, routeBase);
      watcher->setFuture(future);
    }
  }
}

void BPRoutesPanel::cleanupThumbnail(const QString &routeBase) { QFile::remove(getThumbnailPath(routeBase)); }

void BPRoutesPanel::cleanupThumbnailCache() {
  QDir routesDirectory(getRoutesDir);
  QStringList routeDirs = routesDirectory.entryList(QStringList() << "*--*", QDir::Dirs | QDir::NoDotAndDotDot);
  QSet<QString> validRoutes;

  for (const QString &routePath : routeDirs) {
    validRoutes.insert(routePath.split("--").first());
  }

  QDir cacheDir(getThumbnailCacheDir);
  QStringList thumbnails = cacheDir.entryList(QStringList() << "*.jpg", QDir::Files);

  for (const QString &thumbnail : thumbnails) {
    QString routeBase = thumbnail.left(thumbnail.length() - 4);
    if (!validRoutes.contains(routeBase)) {
      QFile::remove(cacheDir.filePath(thumbnail));
    }
  }
}

void BPRoutesPanel::playRouteVideoConcatenated(const QString &routeBase, const QString &videoFileName) {
  // Find the route info for this routeBase
  BPRoutesPanel::RouteInfo routeInfo;
  bool foundRoute = false;
  for (const BPRoutesPanel::RouteInfo &route : routes) {
    if (route.baseName == routeBase) {
      routeInfo = route;
      foundRoute = true;
      break;
    }
  }

  if (!foundRoute) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = tr("Error");
    config.prompt = tr("Route information not found.");
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    return;
  }

  // Create and show the enhanced video modal with hardware acceleration
  auto videoModal = new BPEnhancedVideoModal(routeBase, routeInfo, this);
  videoModal->exec();
  delete videoModal;

}

BPRouteSyncSettingsDialog::BPRouteSyncSettingsDialog(const BPRoutesPanel::SyncConfig &config, QWidget *parent) : BPDialogBase(parent), currentConfig(config) {
  setupUI();
  loadSavedSettings();
}

void BPRouteSyncSettingsDialog::setupUI() {
  setStyleSheet(R"(
    BPRouteSyncSettingsDialog {
      background-color: #111111;  /* Dark background matching BP theme */
    }
  )");

  auto mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(30, 20, 30, 20);
  mainLayout->setSpacing(20);

  // Header with back button and title
  auto headerWidget = new QWidget(this);
  headerWidget->setStyleSheet("background: transparent;");
  auto headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(0, 0, 0, 0);

  auto backBtn = new BPBackButton(this);
  connect(backBtn, &BPButton::clicked, this, &QDialog::accept);

  auto titleLabel = new QLabel(tr("Route Sync Configuration"), this);
  titleLabel->setStyleSheet("font-size: 48px; font-weight: 600; color: white;");
  titleLabel->setAlignment(Qt::AlignCenter);

  headerLayout->addWidget(backBtn);
  headerLayout->addWidget(titleLabel, 1);
  headerLayout->addSpacing(backBtn->width());

  mainLayout->addWidget(headerWidget);

  // Two-column layout for the content
  auto contentWidget = new QWidget(this);
  contentWidget->setStyleSheet("background: transparent;");
  auto contentLayout = new QHBoxLayout(contentWidget);
  contentLayout->setSpacing(20);

  // Left Column
  auto leftColumn = new QVBoxLayout();
  leftColumn->setSpacing(20);

  // Network Settings Group
  auto networkGroup = new QGroupBox(tr("Network Settings"));
  networkGroup->setStyleSheet(R"(
    QGroupBox {
      background-color: #242424;
      border: none;
      border-radius: 15px;
      margin-top: 50px;
      padding: 5px;
      font-size: 40px;
      font-weight: 500;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      padding: 5px 15px;
      border-top-left-radius: 15px;
      border-top-right-radius: 15px;
      border-bottom: none;
      margin-left: 35px;
      margin-top: 0px;
      background-color: #242424;
      color: #2196F3;
    }
  )");

  auto networkLayout = new QVBoxLayout(networkGroup);
  networkLayout->setContentsMargins(25, 25, 25, 25);
  networkLayout->setSpacing(20);

  // Location input with test button
  auto locationContainer = new QWidget();
  auto locationLayout = new QHBoxLayout(locationContainer);
  locationLayout->setContentsMargins(0, 0, 0, 0);
  locationLayout->setSpacing(15);

  locationButton = new QPushButton(this);
  locationButton->setStyleSheet(R"(
    QPushButton {
      background-color: #363636;
      border: none;
      border-radius: 8px;
      padding: 12px;
      color: white;
      font-size: 32px;
      text-align: left;
      min-height: 54px;
    }
    QPushButton:pressed {
      background-color: #404040;
    }
  )");
  locationButton->setText(currentConfig.networkLocation.isEmpty() ? tr("Enter network path") : currentConfig.networkLocation);
  locationLayout->addWidget(locationButton, 1);

  auto testButton = new BPButton(tr("Test"), this);
  testButton->setFixedWidth(250);
  connect(testButton, &BPButton::clicked, this, &BPRouteSyncSettingsDialog::testConnection);
  locationLayout->addWidget(testButton);

  networkLayout->addWidget(new QLabel(tr("Network Location:")));
  networkLayout->addWidget(locationContainer);

  // Credentials Group
  auto credentialsGroup = new QGroupBox(tr("Credentials"));
  credentialsGroup->setStyleSheet(networkGroup->styleSheet());
  auto credentialsLayout = new QVBoxLayout(credentialsGroup);
  credentialsLayout->setContentsMargins(25, 25, 25, 25);
  credentialsLayout->setSpacing(20);

  credentialsLayout->addWidget(new QLabel(tr("Username:")));
  usernameButton = new QPushButton(this);
  usernameButton->setStyleSheet(locationButton->styleSheet());
  usernameButton->setText(currentConfig.username.isEmpty() ? tr("Enter username") : currentConfig.username);
  credentialsLayout->addWidget(usernameButton);

  credentialsLayout->addWidget(new QLabel(tr("Password:")));
  passwordButton = new QPushButton(this);
  passwordButton->setStyleSheet(locationButton->styleSheet());
  passwordButton->setText(currentConfig.password.isEmpty() ? tr("Enter password") : QString("*").repeated(currentConfig.password.length()));
  credentialsLayout->addWidget(passwordButton);

  leftColumn->addWidget(networkGroup);
  leftColumn->addWidget(credentialsGroup);
  leftColumn->addStretch();

  // Right Column
  auto rightColumn = new QVBoxLayout();
  rightColumn->setSpacing(20);

  // Sync Options Group
  auto optionsGroup = new QGroupBox(tr("Sync Options"));
  optionsGroup->setStyleSheet(networkGroup->styleSheet());
  auto optionsLayout = new QVBoxLayout(optionsGroup);
  optionsLayout->setContentsMargins(25, 25, 25, 25);
  optionsLayout->setSpacing(20);

  // Create toggles
  auto enabledContainer = new QWidget();
  auto enabledLayout = new QHBoxLayout(enabledContainer);
  enabledLayout->setContentsMargins(0, 0, 0, 0);
  enabledLayout->setSpacing(20);

  enabledToggle = new BPToggle();
  enabledToggle->setChecked(currentConfig.enabled);
  enabledLayout->addWidget(enabledToggle);

  auto enabledLabel = new QLabel(tr("Enable Route Sync"));
  enabledLabel->setStyleSheet("font-size: 32px; color: white;");
  enabledLayout->addWidget(enabledLabel, 1);
  optionsLayout->addWidget(enabledContainer);

  auto autoConcatContainer = new QWidget();
  auto autoConcatLayout = new QHBoxLayout(autoConcatContainer);
  autoConcatLayout->setContentsMargins(0, 0, 0, 0);
  autoConcatLayout->setSpacing(20);

  autoConcatToggle = new BPToggle();
  autoConcatToggle->setChecked(currentConfig.autoConcat);
  autoConcatLayout->addWidget(autoConcatToggle);

  auto autoConcatLabel = new QLabel(tr("Combine segments into a single route before sync"));
  autoConcatLabel->setStyleSheet("font-size: 32px; color: white;");
  autoConcatLayout->addWidget(autoConcatLabel, 1);
  optionsLayout->addWidget(autoConcatContainer);

  rightColumn->addWidget(optionsGroup);
  rightColumn->addStretch();

  // Add columns to content layout
  contentLayout->addLayout(leftColumn);
  contentLayout->addLayout(rightColumn);

  mainLayout->addWidget(contentWidget);

  // Save button at bottom
  auto saveButton = new BPButton(tr("Save Settings"), this);
  saveButton->setFixedHeight(70);
  connect(saveButton, &BPButton::clicked, this, &BPRouteSyncSettingsDialog::validateAndSave);
  mainLayout->addWidget(saveButton);

  // Connect buttons to keyboard dialogs
  connect(locationButton, &QPushButton::clicked, this, &BPRouteSyncSettingsDialog::showLocationKeyboard);
  connect(usernameButton, &QPushButton::clicked, this, &BPRouteSyncSettingsDialog::showUsernameKeyboard);
  connect(passwordButton, &QPushButton::clicked, this, &BPRouteSyncSettingsDialog::showPasswordKeyboard);
}

void BPRouteSyncSettingsDialog::validateAndSave() {
  // Get values from controls
  currentConfig.enabled = enabledToggle->isChecked();
  currentConfig.autoConcat = autoConcatToggle->isChecked();
  currentConfig.networkLocation = locationButton->text();
  currentConfig.protocol = "smb";

  // Only update username if it's not the placeholder text
  if (usernameButton->text() != tr("Enter username")) {
    currentConfig.username = usernameButton->text();
  }

  // Only update password if it was changed (not asterisks)
  if (!passwordButton->text().startsWith("*")) {
    currentConfig.password = passwordButton->text();
  }

  // Save all settings to Params
  params.putBool("RouteSync", currentConfig.enabled);
  params.putBool("RouteAutoConcat", currentConfig.autoConcat);
  params.put("RouteSyncLocation", currentConfig.networkLocation.toStdString());
  params.put("RouteSyncUsername", currentConfig.username.toStdString());
  params.put("RouteSyncPassword", currentConfig.password.toStdString());
  params.put("RouteSyncProtocol", currentConfig.protocol.toStdString());
  params.putInt("RouteSyncDelay", currentConfig.startupDelay);
  params.putInt("RouteRetentionDays", currentConfig.retentionDays);

  emit configurationUpdated(currentConfig);
  accept();
}

void BPRouteSyncSettingsDialog::loadSavedSettings() {
  // Load enabled state
  currentConfig.enabled = params.getBool("RouteSync");
  enabledToggle->setChecked(currentConfig.enabled);

  // Load auto concat state
  currentConfig.autoConcat = params.getBool("RouteAutoConcat");
  autoConcatToggle->setChecked(currentConfig.autoConcat);

  // Load network location
  QString location = QString::fromStdString(params.get("RouteSyncLocation"));
  if (!location.isEmpty()) {
    locationButton->setText(location);
    currentConfig.networkLocation = location;
  }

  // Load username
  QString username = QString::fromStdString(params.get("RouteSyncUsername"));
  if (!username.isEmpty()) {
    usernameButton->setText(username);
    currentConfig.username = username;
  }

  // Load password - show asterisks if password exists
  QString password = QString::fromStdString(params.get("RouteSyncPassword"));
  if (!password.isEmpty()) {
    passwordButton->setText(QString("*").repeated(password.length()));
    currentConfig.password = password;
  }

  // Load delay and retention settings
  currentConfig.startupDelay = params.getInt("RouteSyncDelay");
  if (currentConfig.startupDelay < 0)
    currentConfig.startupDelay = 30;

  currentConfig.retentionDays = params.getInt("RouteRetentionDays");
  if (currentConfig.retentionDays < 0)
    currentConfig.retentionDays = 30;
}

void BPRouteSyncSettingsDialog::showLocationKeyboard() {
  QString text =
      InputDialog::getText(tr("Enter Network Location"), this, tr("Network path:"), false, -1, locationButton->text() == tr("Enter network path") ? "" : locationButton->text());

  if (!text.isEmpty()) {
    locationButton->setText(text);
    currentConfig.networkLocation = text;
  }
}

void BPRouteSyncSettingsDialog::showUsernameKeyboard() {
  QString text = InputDialog::getText(tr("Enter Username"), this, tr("Username:"), false, -1, usernameButton->text() == tr("Enter username") ? "" : usernameButton->text());

  if (!text.isEmpty()) {
    usernameButton->setText(text);
    currentConfig.username = text;
  }
}

void BPRouteSyncSettingsDialog::showPasswordKeyboard() {
  QString text = InputDialog::getText(tr("Enter Password"), this, tr("Password:"), true, -1, passwordButton->text() == tr("Enter password") ? "" : passwordButton->text());

  if (!text.isEmpty()) {
    passwordButton->setText(QString("*").repeated(text.length()));
    currentConfig.password = text;
  }
}

void BPRouteSyncSettingsDialog::testConnection() {
  BPConfirmationDialog::ConfirmConfig config;

  QString location = locationButton->text();
  QString user = usernameButton->text();
  QString pass = passwordButton->text();

  if (location.isEmpty() || location == tr("Enter network path")) {
    config.title = tr("Validation Error");
    config.prompt = tr("Please enter a network path");
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    return;
  }

#ifndef __APPLE__
  // Non-Apple devices: use smbclient.
  QProgressDialog progress(tr("Testing smbclient connection..."), tr("Cancel"), 0, 0, this);
  progress.setWindowModality(Qt::WindowModal);
  progress.show();

  progress.setLabelText(tr("Checking for smbclient..."));
  QProcess whichProc;
  whichProc.start("which", QStringList() << "smbclient");
  whichProc.waitForFinished();
  if (whichProc.exitCode() != 0) {
    progress.setLabelText(tr("smbclient not found. Installing..."));
    QProcess installProc;
    installProc.start("sudo", QStringList() << "apt" << "update");
    installProc.waitForFinished();
    installProc.start("sudo", QStringList() << "apt" << "install" << "smbclient" << "-y");
    installProc.waitForFinished();
    if (installProc.exitCode() != 0) {
      progress.close();
      config.title = tr("Installation Error");
      config.prompt = tr("Failed to install smbclient. Please install it manually.");
      config.confirmText = tr("OK");
      config.confirmColor = "#FF0000";
      BPConfirmationDialog::showMessage(config, this);
      return;
    }
  }

  progress.setLabelText(tr("Testing connection using smbclient..."));
  QString auth;
  if (!user.isEmpty() && user != tr("Enter username")) {
    auth = user;
    if (!pass.isEmpty() && pass != tr("Enter password")) {
      auth += "%" + pass;
    }
  }
  QProcess smbProc;
  QStringList smbArgs;
  smbArgs << location;
  if (!auth.isEmpty()) {
    smbArgs << "-U" << auth;
  }
  smbArgs << "-c" << "ls";
  smbProc.start("smbclient", smbArgs);
  if (!smbProc.waitForStarted(5000)) {
    progress.close();
    config.title = tr("Connection Error");
    config.prompt = tr("Failed to start smbclient process.");
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    return;
  }
  smbProc.waitForFinished(10000);
  progress.close();
  if (smbProc.exitCode() == 0) {
    config.title = tr("Connection Success");
    config.prompt = tr("Successfully connected to network share.");
    config.confirmText = tr("OK");
    config.confirmColor = "#00FF00";
    BPConfirmationDialog::showMessage(config, this);
  } else {
    QString errorMsg = QString::fromUtf8(smbProc.readAllStandardError());
    config.title = tr("Connection Error");
    config.prompt = tr("Failed to connect to network share:\n%1").arg(errorMsg);
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
  }
#else
  // Apple devices: use mount-based testing with smbfs.
  QProgressDialog progress(tr("Testing connection..."), tr("Cancel"), 0, 0, this);
  progress.setWindowModality(Qt::WindowModal);
  progress.show();
  progress.setLabelText(tr("Mounting share using smbfs..."));

  // Create a temporary mount point.
  QString routesDir = getAbsolutePath(isCommaDevice() ? "/data/media/0/realdata" : "~/comma_data/media/0/realdata");
  QString tempMountPath = routesDir + "/tmp_mount_test";
  QDir().mkpath(tempMountPath);

  QStringList mountArgs;
  mountArgs << "-t" << "smbfs";
  if (!user.isEmpty() && user != tr("Enter username")) {
    QString mountUser = user;
    if (!pass.isEmpty() && pass != tr("Enter password")) {
      mountUser += ":" + pass;
    }
    mountUser += "@";
    QString serverPath = location;
    if (serverPath.startsWith("//")) {
      serverPath = serverPath.mid(2);
    }
    location = "//" + mountUser + serverPath;
  }
  mountArgs << location << tempMountPath;
  QProcess mountProc;
  mountProc.start("mount", mountArgs);
  if (!mountProc.waitForStarted(5000)) {
    progress.close();
    config.title = tr("Connection Error");
    config.prompt = tr("Failed to start mount process on Apple.");
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    return;
  }
  if (!mountProc.waitForFinished(10000)) {
    progress.close();
    config.title = tr("Connection Error");
    config.prompt = tr("Mount process timed out on Apple.");
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    QDir(tempMountPath).removeRecursively();
    return;
  }
  if (mountProc.exitCode() != 0) {
    QString errorMsg = QString::fromUtf8(mountProc.readAllStandardError());
    progress.close();
    config.title = tr("Connection Error");
    config.prompt = tr("Failed to mount network share on Apple:\n%1").arg(errorMsg);
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    QDir(tempMountPath).removeRecursively();
    return;
  }

  progress.setLabelText(tr("Verifying write permissions..."));
  QString testFile = tempMountPath + "/.test";
  QFile file(testFile);
  bool writeSuccess = file.open(QIODevice::WriteOnly);
  // Always unmount and clean up.
  QProcess::execute("umount", QStringList() << tempMountPath);
  QDir(tempMountPath).removeRecursively();

  if (!writeSuccess) {
    progress.close();
    config.title = tr("Connection Error");
    config.prompt = tr("Connected to share but failed to write test file on Apple.\nPlease check write permissions.");
    config.confirmText = tr("OK");
    config.confirmColor = "#FF0000";
    BPConfirmationDialog::showMessage(config, this);
    return;
  }
  file.close();
  file.remove();
  progress.close();
  config.title = tr("Connection Success");
  config.prompt = tr("Successfully connected to network share and verified write permissions on Apple.");
  config.confirmText = tr("OK");
  config.confirmColor = "#00FF00";
  BPConfirmationDialog::showMessage(config, this);
#endif
}

// Modern UI Methods Implementation
void BPRoutesPanel::groupRoutesByDate() {
  routesByDate.clear();

  for (const RouteInfo &route : routes) {
    QDate routeDate = route.date;
    if (!routeDate.isValid()) {
      // Extract date from timestamp if not set
      QDateTime routeDateTime = QDateTime::fromString(route.timestamp, "yyyy-MM-dd h:mm:ss a");
      routeDate = routeDateTime.date();
    }

    routesByDate[routeDate].append(route);
  }

  // Sort routes within each date group by timestamp (newest first)
  for (auto it = routesByDate.begin(); it != routesByDate.end(); ++it) {
    std::sort(it.value().begin(), it.value().end(),
              [](const RouteInfo &a, const RouteInfo &b) {
                return a.timestamp > b.timestamp;
              });
  }
}

void BPRoutesPanel::createDateGroupHeader(const QDate &date, int routeCount) {
  auto headerWidget = new QWidget(routesContainer);
  headerWidget->setObjectName("dateHeader");

  auto headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(0, 20, 0, 10);

  auto dateLabel = new QLabel();
  dateLabel->setProperty("class", "date-header");

  // Fixed date formatting - use QLocale for consistent results
  QLocale locale(QLocale::English, QLocale::UnitedStates);
  QString dayName = locale.dayName(date.dayOfWeek(), QLocale::LongFormat);
  QString monthName = locale.monthName(date.month(), QLocale::LongFormat);
  QString dayNumber = QString::number(date.day());
  QString year = QString::number(date.year());

  // Format: "Monday, January 15, 2025"
  QString dateText = QString("%1, %2 %3, %4").arg(dayName, monthName, dayNumber, year);
  QString countText = tr("%1 route%2").arg(routeCount).arg(routeCount == 1 ? "" : "s");

  dateLabel->setText(QString("%1 • %2").arg(dateText, countText));

  headerLayout->addWidget(dateLabel);
  headerLayout->addStretch();

  routesLayout->addWidget(headerWidget);
}

void BPRoutesPanel::createModernRouteWidget(const RouteInfo &route) {
  auto routeCard = new QWidget(routesContainer);
  routeCard->setObjectName("routeCard");
  routeCard->setProperty("class", "route-card");
  routeCard->setMinimumHeight(220);
  routeCard->setStyleSheet(R"(
    QWidget#routeCard {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #242424, stop:1 #2a2a2a);
      border-radius: 16px;
      border: 2px solid transparent;
    }
    QWidget#routeCard:hover {
      border-color: #2196F3;
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2a2a2a, stop:1 #303030);
    }
  )");

  auto cardLayout = new QHBoxLayout(routeCard);
  cardLayout->setContentsMargins(30, 25, 30, 25);
  cardLayout->setSpacing(25);

  // Left side: Thumbnail (larger size with shadow effect)
  auto thumbnailContainer = new QWidget();
  thumbnailContainer->setStyleSheet("background: transparent;");
  auto thumbnailLayout = new QVBoxLayout(thumbnailContainer);
  thumbnailLayout->setContentsMargins(0, 0, 0, 0);

  auto thumbnailLabel = new QLabel();
  thumbnailLabel->setFixedSize(320, 180); // Larger thumbnail size
  thumbnailLabel->setStyleSheet(R"(
    background-color: #1a1a1a;
    border-radius: 12px;
    border: 1px solid #333333;
  )");
  thumbnailLabel->setAlignment(Qt::AlignCenter);
  thumbnailLabel->setScaledContents(false);

  // Load thumbnail
  if (QFile::exists(route.thumbnailPath)) {
    QPixmap thumbnail(route.thumbnailPath);
    thumbnailLabel->setPixmap(thumbnail.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  } else {
    thumbnailLabel->setText(tr("Loading..."));
    thumbnailLabel->setStyleSheet("background-color: #1a1a1a; color: #666666; border-radius: 12px;");
    initializeThumbnail(thumbnailLabel, route.baseName);
  }

  thumbnailLayout->addWidget(thumbnailLabel);

  // Center: Route information
  auto infoContainer = new QWidget();
  auto infoLayout = new QVBoxLayout(infoContainer);
  infoLayout->setContentsMargins(0, 0, 0, 0);
  infoLayout->setSpacing(8);

  // Route title (timestamp)
  auto routeTitleLabel = new QLabel(route.timestamp);
  routeTitleLabel->setProperty("class", "route-title");
  routeTitleLabel->setWordWrap(true);
  infoLayout->addWidget(routeTitleLabel);

  // Route ID and duration
  auto subtitleLabel = new QLabel(tr("ID: %1 • %2").arg(route.baseName).arg(route.duration));
  subtitleLabel->setProperty("class", "route-subtitle");
  infoLayout->addWidget(subtitleLabel);

  // Stats
  QString statsText = tr("%1 segments • %2").arg(route.segments).arg(route.size);
  if (route.tripMiles > 0) {
    statsText += tr(" • %.1f miles").arg(route.tripMiles);
  }
  auto routeStatsLabel = new QLabel(statsText);
  routeStatsLabel->setProperty("class", "route-stats");
  infoLayout->addWidget(routeStatsLabel);

  // File types indicator
  QStringList fileTypes;
  if (route.hasVideo) fileTypes << tr("Video");
  if (route.hasRLog) fileTypes << tr("RLog");
  if (route.hasQLog) fileTypes << tr("QLog");

  if (!fileTypes.isEmpty()) {
    auto fileTypesLabel = new QLabel(tr("Files: %1").arg(fileTypes.join(", ")));
    fileTypesLabel->setProperty("class", "route-stats");
    infoLayout->addWidget(fileTypesLabel);
  }

  infoLayout->addStretch();

  // Add to card layout (no action buttons - entire card is clickable)
  cardLayout->addWidget(thumbnailContainer);
  cardLayout->addWidget(infoContainer, 1);

  // Make entire route card clickable to open modal with fcamera by default
  routeCard->setCursor(Qt::PointingHandCursor);
  routeCard->installEventFilter(this);

  // Store route info in the card for event filtering
  routeCard->setProperty("routeBaseName", route.baseName);
  routeCard->setProperty("routeInfo", QVariant::fromValue(route));

  routesLayout->addWidget(routeCard);
}

void BPRoutesPanel::showVideoSelectionMenu(const RouteInfo &route) {
  auto menu = new QMenu(this);
  menu->setStyleSheet(R"(
    QMenu {
      background-color: #242424;
      border: 2px solid #404040;
      border-radius: 8px;
      padding: 5px;
    }
    QMenu::item {
      background-color: transparent;
      color: white;
      padding: 15px 20px;
      border-radius: 5px;
      font-size: 28px;
    }
    QMenu::item:selected {
      background-color: #404040;
    }
  )");

  QStringList videoLabels = {tr("Front Camera"), tr("Front Wide"), tr("Driver Camera"), tr("Front Low Quality")};
  QStringList videoFiles = {"fcamera.hevc", "ecamera.hevc", "dcamera.hevc", "qcamera.ts"};

  for (int i = 0; i < videoFiles.size(); i++) {
    auto action = menu->addAction(videoLabels[i]);
    connect(action, &QAction::triggered, [this, route, videoFiles, i]() {
      playRouteVideoConcatenated(route.baseName, videoFiles[i]);
    });
  }

  // Show menu at cursor position
  menu->exec(QCursor::pos());
}

void BPRoutesPanel::loadMoreRoutes() {
  if (isLoading) return;

  // Only increment page if we're loading additional routes (not initial load)
  if (currentPage > 0) {
    currentPage++;
  }
  // Note: Using routesLoaded/targetRoutes approach instead of startIndex/endIndex

  // Load routes for current page
  QList<QDate> sortedDates = routesByDate.keys();
  std::sort(sortedDates.begin(), sortedDates.end(), std::greater<QDate>());

  int routesLoaded = 0;
  int targetRoutes = routesPerPage;

  for (const QDate &date : sortedDates) {
    if (routesLoaded >= targetRoutes) break;

    const QVector<RouteInfo> &dateRoutes = routesByDate[date];
    int routesToLoad = qMin(targetRoutes - routesLoaded, dateRoutes.size());

    // Create date header if this is the first route for this date
    if (routesLoaded == 0 || routesByDate[date].size() > routesPerPage) {
      createDateGroupHeader(date, dateRoutes.size());
    }

    // Create route widgets
    for (int i = 0; i < routesToLoad; i++) {
      createModernRouteWidget(dateRoutes[i]);
      routesLoaded++;
    }
  }

}

bool BPRoutesPanel::eventFilter(QObject *obj, QEvent *event) {
  // Handle both mouse and touch events for route cards
  if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin) {
    QPoint pressPos;
    if (event->type() == QEvent::MouseButtonPress) {
      QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->button() != Qt::LeftButton) return false;
      pressPos = mouseEvent->pos();
    } else {
      QTouchEvent *touchEvent = static_cast<QTouchEvent*>(event);
      if (!touchEvent->touchPoints().isEmpty()) {
        pressPos = touchEvent->touchPoints().first().pos().toPoint();
      }
    }

    QWidget *widget = qobject_cast<QWidget*>(obj);
    if (widget && widget->property("routeBaseName").isValid()) {
      // Store the press position and time for click detection
      widget->setProperty("pressPos", pressPos);
      widget->setProperty("pressTime", QDateTime::currentMSecsSinceEpoch());
      widget->setProperty("isPressed", true);

      // Visual feedback - slight color change
      widget->setStyleSheet(widget->styleSheet() + "\nbackground-color: #353535;");

      return false; // Don't consume the event, let scrolling work
    }
  }

  // Handle release events for both mouse and touch
  if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::TouchEnd) {
    QPoint releasePos;
    if (event->type() == QEvent::MouseButtonRelease) {
      QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->button() != Qt::LeftButton) return false;
      releasePos = mouseEvent->pos();
    } else {
      QTouchEvent *touchEvent = static_cast<QTouchEvent*>(event);
      if (!touchEvent->touchPoints().isEmpty()) {
        releasePos = touchEvent->touchPoints().first().pos().toPoint();
      }
    }

    QWidget *widget = qobject_cast<QWidget*>(obj);
    if (widget && widget->property("isPressed").toBool()) {
      // Reset visual feedback
      widget->setProperty("isPressed", false);

      if (widget->property("routeBaseName").isValid()) {
        QPoint pressPos = widget->property("pressPos").toPoint();
        qint64 pressTime = widget->property("pressTime").toLongLong();
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

        // Check if it was a tap/click (not a scroll gesture)
        // More forgiving for touch: 20 pixels movement, 500ms time
        int distance = QLineF(pressPos, releasePos).length();
        qint64 timeElapsed = currentTime - pressTime;
        int maxDistance = (event->type() == QEvent::TouchEnd) ? 20 : 10;
        int maxTime = (event->type() == QEvent::TouchEnd) ? 500 : 200;

        if (distance < maxDistance && timeElapsed < maxTime) {
          QString routeBaseName = widget->property("routeBaseName").toString();
          RouteInfo route = widget->property("routeInfo").value<RouteInfo>();

          // Open modal with fcamera by default
          playRouteVideoConcatenated(routeBaseName, "fcamera.hevc");
          return true;
        }
      }
    }
  }

  // Handle touch cancel (user scrolled instead of tapped)
  if (event->type() == QEvent::TouchCancel) {
    QWidget *widget = qobject_cast<QWidget*>(obj);
    if (widget && widget->property("isPressed").toBool()) {
      widget->setProperty("isPressed", false);
    }
  }

  // Touch screen devices don't use wheel events - scroll detection handled by timer

  return QWidget::eventFilter(obj, event);
}

void BPRoutesPanel::checkScrollPosition() {
  if (!scrollArea || isLoading) return;

  // Check if scrolled near bottom (within 300px for touch)
  int scrollBarValue = scrollArea->verticalScrollBar()->value();
  int scrollBarMaximum = scrollArea->verticalScrollBar()->maximum();
  int scrollBarPageStep = scrollArea->verticalScrollBar()->pageStep();

  // If near bottom, load more routes
  if (scrollBarValue >= scrollBarMaximum - scrollBarPageStep - 300) {
    loadMoreRoutes();
  }
}
