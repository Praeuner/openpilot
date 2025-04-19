// bp_data_collect.cc
#include "bp_data_collect.h"
#include "bp_panel_controls.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QDir>
#include <QtMath>
#include <QPainter>
#include <QGraphicsDropShadowEffect>

BPDataCollectPanel::BPDataCollectPanel(QWidget *parent) : BPPanelBase(parent) {
  setObjectName("Data Collection");
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  // Create main layout
  QVBoxLayout *mainLayout = new QVBoxLayout();
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(30);

  // Create header widget
  QWidget *headerWidget = new QWidget();
  QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(0, 0, 0, 0);

  // Create container for title and status with horizontal layout
  QWidget *titleContainer = new QWidget();
  QHBoxLayout *titleContainerLayout = new QHBoxLayout(titleContainer);
  titleContainerLayout->setContentsMargins(0, 0, 0, 0);
  titleContainerLayout->setSpacing(20); // Space between title and status
  titleContainerLayout->setAlignment(Qt::AlignVCenter);

  // Create wrapper for title label
  QWidget *titleWrapper = new QWidget();
  titleWrapper->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  QVBoxLayout *titleWrapperLayout = new QVBoxLayout(titleWrapper);
  titleWrapperLayout->setContentsMargins(0, 0, 0, 0);
  titleWrapperLayout->setSpacing(0);

  // Create wrapper for status label
  QWidget *statusWrapper = new QWidget();
  statusWrapper->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  QVBoxLayout *statusWrapperLayout = new QVBoxLayout(statusWrapper);
  statusWrapperLayout->setContentsMargins(0, 0, 0, 0);
  statusWrapperLayout->setSpacing(0);

  // Add title to its wrapper
  QLabel *titleLabel = new QLabel("Route Data Collection");
  titleLabel->setStyleSheet("font-size: 50px; font-weight: bold; color: #2196F3;");
  titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  titleWrapperLayout->addWidget(titleLabel);

  // Add upload status to its wrapper
  uploadStatusValue = new QLabel("Unknown");
  uploadStatusValue->setStyleSheet("font-size: 38px; font-weight: bold; color: #9E9E9E;");
  uploadStatusValue->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  statusWrapperLayout->addWidget(uploadStatusValue);

  // Add both wrappers to the main container
  titleContainerLayout->addWidget(titleWrapper);
  titleContainerLayout->addWidget(statusWrapper);

  // Add container to header layout
  headerLayout->addWidget(titleContainer);

  // Add stretch to push everything to the left
  headerLayout->addStretch();

  mainLayout->addWidget(headerWidget);

  BPToggleControl *toggleControl = new BPToggleControl("EnableBluepilotDataCollection", "Data Collection", "Toggle to enable or disable route data collection for Bluepilot", this);
  mainLayout->addWidget(toggleControl);

  // Create progress bar
  syncProgressBar = new QProgressBar();
  syncProgressBar->setRange(0, 0); // Indeterminate progress
  syncProgressBar->setTextVisible(false);
  syncProgressBar->setFixedHeight(10);
  syncProgressBar->setStyleSheet(R"(
    QProgressBar {
      background-color: #E0E0E0;
      border-radius: 5px;
      border: none;
    }
    QProgressBar::chunk {
      background-color: #2196F3;
      border-radius: 5px;
    }
  )");
  syncProgressBar->hide();
  mainLayout->addWidget(syncProgressBar);

  // Create stats cards container
  statsContainer = new QWidget();
  QGridLayout *statsGrid = new QGridLayout(statsContainer);
  statsGrid->setContentsMargins(0, 0, 0, 0);
  statsGrid->setSpacing(20);

  // Create stats cards
  QWidget *totalRoutesCard = createStatsCard("Total Routes", "0", "found", "../assets/offroad/icon_data.png");
  QWidget *routesUploadedCard = createStatsCard("Routes Uploaded", "0", "to cloud", "../assets/offroad/icon_upload.png");
  QWidget *lastSyncCard = createStatsCard("Last Sync", "Never", "", "../assets/offroad/icon_cloud_sync.png");
  QWidget *syncStatusCard = createStatsCard("Sync Status", "Idle", "", "../assets/offroad/icon_checkmark.png");

  // Store references to label widgets for updating
  totalRoutesLabel = totalRoutesCard->findChild<QLabel *>("TotalRoutesValue");
  routesUploadedLabel = routesUploadedCard->findChild<QLabel *>("RoutesUploadedValue");
  lastSyncLabel = lastSyncCard->findChild<QLabel *>("LastSyncValue");
  syncStatusLabel = syncStatusCard->findChild<QLabel *>("SyncStatusValue");

  // Add cards to grid
  statsGrid->addWidget(totalRoutesCard, 0, 0);
  statsGrid->addWidget(routesUploadedCard, 0, 1);
  statsGrid->addWidget(lastSyncCard, 1, 0);
  statsGrid->addWidget(syncStatusCard, 1, 1);

  // Add container to main layout
  mainLayout->addWidget(statsContainer);

  // Create routes section
  QWidget *routesGroup = createStyledGroupBox("Recent Routes");
  routesGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); // Make it expand
  QVBoxLayout *routesLayout = new QVBoxLayout(routesGroup);
  routesLayout->setContentsMargins(15, 30, 15, 15);
  routesLayout->setSpacing(15);

  // Create a scroll area for routes
  routesScrollArea = new QScrollArea();
  routesScrollArea->setWidgetResizable(true);
  routesScrollArea->setFrameShape(QFrame::NoFrame);
  routesScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  routesScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  routesScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); // Make scroll area expand

  scrollContent = new QWidget();
  scrollLayout = new QVBoxLayout(scrollContent);
  scrollLayout->setContentsMargins(0, 0, 0, 0);
  scrollLayout->setSpacing(15);
  // Remove the stretch at the end to allow content to fill space
  // scrollLayout->addStretch();

  routesScrollArea->setWidget(scrollContent);
  routesLayout->addWidget(routesScrollArea);

  // Make sure the routes group expands to fill available space
  mainLayout->addWidget(routesGroup, 1); // Add stretch factor of 1 to make it expand

  // Set up the timer to update stats
  statsUpdateTimer = new QTimer(this);
  connect(statsUpdateTimer, &QTimer::timeout, this, &BPDataCollectPanel::updateStats);
  statsUpdateTimer->start(2000); // Update every 2 seconds

  // Set the layout
  addItem(mainLayout);

  // Initial update
  updateStats();
}

BPDataCollectPanel::~BPDataCollectPanel() {
  // Stop the timer first
  if (statsUpdateTimer) {
    statsUpdateTimer->stop();
    disconnect(statsUpdateTimer, &QTimer::timeout, this, &BPDataCollectPanel::updateStats);
  }

  // Clean up widgets
  cleanupWidgets();
}

void BPDataCollectPanel::updateUploadStatus() {
  auto deviceState = (*uiState()->sm)["deviceState"].getDeviceState();
  auto networkTypeEnum = deviceState.getNetworkType();
  int networkType = static_cast<int>(networkTypeEnum); // Cast enum to int
  bool networkMetered = deviceState.getNetworkMetered();

  QString statusText;
  QString statusColor;

  if (networkType == 0) { // None
    statusText = "Blocked (Not connected to WiFi)";
    statusColor = "#F44336";                         // Red
  } else if (networkType == 1 || networkType == 6) { // WiFi or Ethernet
    statusText = "Allowed (Connected to " + QString(networkType == 1 ? "WiFi" : "Ethernet") + ")";
    statusColor = "#4CAF50";                         // Green
  } else if (networkType >= 2 && networkType <= 5) { // Cellular
    if (networkMetered) {
      statusText = "Blocked (Cellular Metered)";
      statusColor = "#F44336"; // Red
    } else {
      statusText = "Blocked (Connected to Cellular)";
      statusColor = "#FF9800"; // Orange/Yellow
    }
  } else {
    statusText = "Unknown";
    statusColor = "#9E9E9E"; // Gray
  }

  if (uploadStatusValue) {
    uploadStatusValue->setText(statusText);
    uploadStatusValue->setStyleSheet("font-size: 38px; font-weight: bold; color: " + statusColor + ";");
  }
}

void BPDataCollectPanel::cleanupWidgets() {
  // This method explicitly cleans up widgets to prevent memory leaks
  if (scrollContent && scrollLayout) {
    QLayoutItem *child;
    while ((child = scrollLayout->takeAt(0)) != nullptr) {
      if (child->widget()) {
        child->widget()->setParent(nullptr);
        delete child->widget();
      }
      delete child;
    }
  }
}

QWidget *BPDataCollectPanel::createStatsCard(QString title, QString value, QString subtitle, QString iconPath) {
  QWidget *card = new QWidget();
  card->setObjectName("statsCard");
  card->setMinimumHeight(180);
  card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  // Create layout
  QHBoxLayout *cardLayout = new QHBoxLayout(card);
  cardLayout->setContentsMargins(20, 20, 20, 20);

  // Add icon if provided
  if (!iconPath.isEmpty()) {
    QLabel *iconLabel = new QLabel();
    QPixmap icon(iconPath);
    if (!icon.isNull()) {
      iconLabel->setPixmap(icon.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    iconLabel->setFixedSize(64, 64);
    cardLayout->addWidget(iconLabel);
    cardLayout->addSpacing(15);
  }

  // Add text content
  QVBoxLayout *textLayout = new QVBoxLayout();
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(8);

  QLabel *titleLabel = new QLabel(title);
  titleLabel->setStyleSheet("font-size: 36px; font-weight: 500; color: #BDBDBD;");
  textLayout->addWidget(titleLabel);

  QString valueObjectName = title;
  valueObjectName.replace(" ", "");
  valueObjectName.append("Value");

  QLabel *valueLabel = new QLabel(value);
  valueLabel->setObjectName(valueObjectName);
  valueLabel->setStyleSheet("font-size: 48px; font-weight: bold; color: #FFFFFF;");
  textLayout->addWidget(valueLabel);

  if (!subtitle.isEmpty()) {
    QLabel *subtitleLabel = new QLabel(subtitle);
    subtitleLabel->setStyleSheet("font-size: 30px; color: #757575;");
    textLayout->addWidget(subtitleLabel);
  }

  cardLayout->addLayout(textLayout);
  cardLayout->addStretch();

  // Style the card to match dark theme
  card->setStyleSheet(R"(
    #statsCard {
      background-color: #242424;
      border-radius: 10px;
    }
  )");

  return card;
}

QWidget *BPDataCollectPanel::createRouteCard(QString routeId, QString timestamp, int segmentCount, bool uploaded, QString uploadTime, QString fingerprint) {
  QWidget *card = new QWidget();
  card->setObjectName("routeCard");
  card->setMinimumHeight(180);
  card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  // Create layout
  QVBoxLayout *cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(20, 15, 20, 15);
  cardLayout->setSpacing(10);

  // Route ID and timestamp (header row)
  QHBoxLayout *headerLayout = new QHBoxLayout();
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(20);

  // Shorten route ID for display
  QString shortRouteId = routeId;
  if (shortRouteId.length() > 20) {
    shortRouteId = shortRouteId.left(10) + "..." + shortRouteId.right(10);
  }

  QLabel *routeIdLabel = new QLabel(shortRouteId);
  routeIdLabel->setStyleSheet("font-size: 36px; font-weight: 500; color: #BDBDBD;");
  headerLayout->addWidget(routeIdLabel);

  headerLayout->addStretch();

  QLabel *timestampLabel = new QLabel(timestamp);
  timestampLabel->setStyleSheet("font-size: 30px; color: #757575;");
  headerLayout->addWidget(timestampLabel);

  cardLayout->addLayout(headerLayout);

  // Segments and upload status
  QHBoxLayout *detailsLayout = new QHBoxLayout();
  detailsLayout->setContentsMargins(0, 0, 0, 0);
  detailsLayout->setSpacing(20);

  QLabel *segmentsLabel = new QLabel(QString("Segments: %1").arg(segmentCount));
  segmentsLabel->setStyleSheet("font-size: 30px; color: #BDBDBD;");
  detailsLayout->addWidget(segmentsLabel);

  detailsLayout->addStretch();

  // Show fingerprint if available
  if (!fingerprint.isEmpty() && fingerprint != "unknown") {
    QLabel *fingerprintLabel = new QLabel(QString("Car: %1").arg(fingerprint));
    fingerprintLabel->setStyleSheet("font-size: 30px; color: #BDBDBD;");
    detailsLayout->addWidget(fingerprintLabel);
    detailsLayout->addStretch();
  }

  // Upload status
  QLabel *statusLabel = new QLabel(uploaded ? "Uploaded" : "Pending");
  statusLabel->setStyleSheet(uploaded ? "font-size: 30px; font-weight: bold; color: #4CAF50;" : "font-size: 30px; font-weight: bold; color: #FF9800;");
  detailsLayout->addWidget(statusLabel);

  cardLayout->addLayout(detailsLayout);

  // Upload time if available
  if (uploaded && !uploadTime.isEmpty()) {
    QHBoxLayout *uploadLayout = new QHBoxLayout();
    uploadLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *uploadedLabel = new QLabel(QString("Uploaded: %1").arg(uploadTime));
    uploadedLabel->setStyleSheet("font-size: 28px; color: #757575;");
    uploadLayout->addWidget(uploadedLabel);
    uploadLayout->addStretch();

    cardLayout->addLayout(uploadLayout);
  }

  // Progress bar for sync status
  if (currentRouteId == routeId && syncStatus != "idle" && syncStatus != "error" && !uploaded) {
    QHBoxLayout *progressLayout = new QHBoxLayout();
    progressLayout->setContentsMargins(0, 5, 0, 0);

    QString progressText = "Processing...";

    if (syncStatus == "processing") {
      progressText = "Processing route data...";
      if (statusDetails.contains("stage")) {
        QString stage = statusDetails["stage"].toString();
        if (stage == "concatenating_segments") {
          if (statusDetails.contains("current_segment") && statusDetails.contains("total_segments")) {
            int current = statusDetails["current_segment"].toInt();
            int total = statusDetails["total_segments"].toInt();
            progressText = QString("Concatenating segments: %1/%2").arg(current).arg(total);
          } else {
            progressText = "Concatenating segments...";
          }
        }
      }
    } else if (syncStatus == "uploading") {
      progressText = "Uploading route data...";
      if (statusDetails.contains("stage")) {
        QString stage = statusDetails["stage"].toString();
        if (stage == "uploading_data" && statusDetails.contains("file_size_mb")) {
          double sizeMb = statusDetails["file_size_mb"].toDouble();
          progressText = QString("Uploading %1 MB...").arg(sizeMb);
        } else if (stage == "compressing_data") {
          progressText = "Compressing route data...";
        } else if (stage == "uploading_metadata") {
          progressText = "Uploading metadata...";
        }
      }
    }

    QLabel *syncLabel = new QLabel(progressText);
    syncLabel->setStyleSheet("font-size: 28px; color: #2196F3;");
    progressLayout->addWidget(syncLabel);
    progressLayout->addStretch();

    cardLayout->addLayout(progressLayout);

    QProgressBar *progressBar = new QProgressBar();
    progressBar->setRange(0, 0); // Indeterminate
    progressBar->setTextVisible(false);
    progressBar->setFixedHeight(12);
    progressBar->setStyleSheet(R"(
      QProgressBar {
        background-color: #3A3A3A;
        border-radius: 6px;
        border: none;
      }
      QProgressBar::chunk {
        background-color: #2196F3;
        border-radius: 6px;
      }
    )");
    cardLayout->addWidget(progressBar);
  }

  // Style the card
  card->setStyleSheet(R"(
    #routeCard {
      background-color: #242424;
      border-radius: 10px;
    }
  )");

  // Highlight currently syncing route
  if (currentRouteId == routeId && syncStatus != "idle" && !uploaded) {
    card->setStyleSheet(R"(
      #routeCard {
        background-color: #263238;
        border-radius: 10px;
        border: 1px solid #2196F3;
      }
    )");
  }

  return card;
}

void BPDataCollectPanel::updateStats() {
  fetchSyncStats();
  fetchSyncStatus();
  updateUploadStatus(); // Update the upload status
  refreshDisplay();
}

void BPDataCollectPanel::fetchSyncStatus() {
  QString statusFile = "/data/bluepilot_data/route_sync_status.json";
  QFile file(statusFile);

  // Reset current values
  syncStatus = "idle";
  currentRouteId = "";
  statusDetails.clear();

  if (file.exists() && file.open(QIODevice::ReadOnly)) {
    QByteArray jsonData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isNull() && doc.isObject()) {
      QJsonObject statusObj = doc.object();

      // Update status info
      syncStatus = statusObj["status"].toString();
      currentRouteId = statusObj["route_id"].toString();

      // Parse details
      if (statusObj.contains("details") && statusObj["details"].isObject()) {
        QJsonObject details = statusObj["details"].toObject();
        for (auto it = details.begin(); it != details.end(); ++it) {
          statusDetails[it.key()] = it.value().toVariant();
        }
      }

      // Update progress bar visibility
      bool shouldShowProgress = (syncStatus == "processing" || syncStatus == "uploading");
      syncProgressBar->setVisible(shouldShowProgress);
    }
  } else {
    // No status file or couldn't open it
    syncProgressBar->setVisible(false);
  }
}

void BPDataCollectPanel::fetchSyncStats() {
  QString statsFile = "/data/bluepilot_data/route_sync_stats.json";
  QFile file(statsFile);

  if (!file.exists()) {
    // Update status message to show no data collection has happened
    totalRoutesFound = 0;
    totalRoutesUploaded = 0;
    lastSyncTimestamp = 0;
    lastSyncRouteId = "";
    successfulSyncs = 0;
    failedSyncs = 0;
    routes.clear();

    if (syncStatusLabel) {
      syncStatusLabel->setText("No data yet");
      syncStatusLabel->setStyleSheet("font-size: 40px; font-weight: bold; color: #9E9E9E;");
    }
    return;
  }

  if (file.open(QIODevice::ReadOnly)) {
    QByteArray jsonData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isNull() && doc.isObject()) {
      QJsonObject statsObj = doc.object();

      // Update basic stats
      totalRoutesFound = statsObj["total_routes_found"].toInt();
      totalRoutesUploaded = statsObj["total_routes_uploaded"].toInt();
      lastSyncTimestamp = statsObj["last_sync_timestamp"].toInt();
      lastSyncRouteId = statsObj["last_sync_route_id"].toString();
      successfulSyncs = statsObj["successful_syncs"].toInt();
      failedSyncs = statsObj["failed_syncs"].toInt();

      // Update routes info
      routes.clear();
      QJsonObject routesObj = statsObj["routes"].toObject();

      for (auto it = routesObj.begin(); it != routesObj.end(); ++it) {
        QString routeId = it.key();
        QJsonObject routeInfo = it.value().toObject();

        RouteInfo info;
        info.routeId = routeId;
        info.timestamp = routeInfo["timestamp"].toInt();
        info.segmentCount = routeInfo["segment_count"].toInt();
        info.uploaded = routeInfo["uploaded"].toBool();
        info.uploadTimestamp = routeInfo["upload_timestamp"].toInt(0);
        info.fingerprint = routeInfo["fingerprint"].toString("unknown");

        routes.append(info);
      }

      // Sort routes by timestamp, newest first
      std::sort(routes.begin(), routes.end(), [](const RouteInfo &a, const RouteInfo &b) { return a.timestamp > b.timestamp; });
    }
  }
}

void BPDataCollectPanel::refreshDisplay() {
  // Update values in the stats cards
  if (totalRoutesLabel)
    totalRoutesLabel->setText(formatNumber(totalRoutesFound));
  if (routesUploadedLabel)
    routesUploadedLabel->setText(formatNumber(totalRoutesUploaded));

  // Update last sync time
  QString syncTimeText = lastSyncTimestamp > 0 ? getElapsedTimeText(lastSyncTimestamp) : "Never";
  if (lastSyncLabel)
    lastSyncLabel->setText(syncTimeText);

  // Update sync status
  if (syncStatusLabel) {
    QString statusText;

    if (syncStatus == "uploading") {
      statusText = "Uploading...";
      syncStatusLabel->setStyleSheet("font-size: 40px; font-weight: bold; color: #2196F3;");
    } else if (syncStatus == "processing") {
      statusText = "Processing...";
      syncStatusLabel->setStyleSheet("font-size: 40px; font-weight: bold; color: #2196F3;");
    } else if (syncStatus == "scanning") {
      statusText = "Scanning...";
      syncStatusLabel->setStyleSheet("font-size: 40px; font-weight: bold; color: #FF9800;");
    } else if (syncStatus == "error") {
      statusText = "Error";
      if (statusDetails.contains("message")) {
        QString msg = statusDetails["message"].toString();
        if (msg.length() > 15) {
          msg = msg.left(15) + "...";
        }
        statusText = "Error: " + msg;
      }
      syncStatusLabel->setStyleSheet("font-size: 40px; font-weight: bold; color: #F44336;");
    } else if (syncStatus == "completed") {
      statusText = "Completed";
      syncStatusLabel->setStyleSheet("font-size: 40px; font-weight: bold; color: #4CAF50;");
    } else if (syncStatus == "idle") {
      if (successfulSyncs > 0) {
        statusText = QString("%1 synced").arg(successfulSyncs);
        syncStatusLabel->setStyleSheet("font-size: 40px; font-weight: bold; color: #4CAF50;");
      } else {
        statusText = "Idle";
        syncStatusLabel->setStyleSheet("font-size: 40px; font-weight: bold; color: #9E9E9E;");
      }
    } else {
      statusText = QString::fromStdString(syncStatus.toStdString());
      statusText[0] = statusText[0].toUpper();
      syncStatusLabel->setStyleSheet("font-size: 40px; font-weight: bold; color: #9E9E9E;");
    }

    syncStatusLabel->setText(statusText);
  }

  // First, clean up existing widgets to prevent memory leaks
  cleanupWidgets();

  // Then update route cards
  if (scrollContent && scrollLayout) {
    // Add empty state if no routes
    if (routes.isEmpty()) {
      QWidget *emptyState = new QWidget();
      QVBoxLayout *emptyLayout = new QVBoxLayout(emptyState);
      emptyLayout->setContentsMargins(20, 40, 20, 40);
      emptyLayout->setSpacing(20);
      emptyLayout->setAlignment(Qt::AlignCenter);

      QLabel *iconLabel = new QLabel();
      QPixmap icon("../assets/offroad/icon_vehicle.png");
      if (!icon.isNull()) {
        iconLabel->setPixmap(icon.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
      }
      iconLabel->setAlignment(Qt::AlignCenter);
      emptyLayout->addWidget(iconLabel);

      QLabel *titleLabel = new QLabel("No routes found");
      titleLabel->setStyleSheet("font-size: 44px; font-weight: bold; color: #9E9E9E;");
      titleLabel->setAlignment(Qt::AlignCenter);
      emptyLayout->addWidget(titleLabel);

      QLabel *subtitleLabel = new QLabel("Drive more to collect route data");
      subtitleLabel->setStyleSheet("font-size: 34px; color: #BDBDBD;");
      subtitleLabel->setAlignment(Qt::AlignCenter);
      emptyLayout->addWidget(subtitleLabel);

      scrollLayout->addWidget(emptyState);
    } else {
      // Add route cards (limit to 10 most recent)
      int count = 0;
      for (const auto &route : routes) {
        if (count++ >= 10)
          break;

        QString timestamp = QDateTime::fromSecsSinceEpoch(route.timestamp).toString("yyyy-MM-dd HH:mm");
        QString uploadTime = route.uploadTimestamp > 0 ? getElapsedTimeText(route.uploadTimestamp) : "";

        scrollLayout->addWidget(createRouteCard(route.routeId, timestamp, route.segmentCount, route.uploaded, uploadTime, route.fingerprint));
      }
    }

    // Only add stretch when there are routes to display
    if (!routes.isEmpty()) {
      scrollLayout->addStretch();
    }
  }
}

QString BPDataCollectPanel::formatNumber(int value) {
  if (value < 1000) {
    return QString::number(value);
  } else if (value < 1000000) {
    double k = value / 1000.0;
    return QString::number(k, 'f', 1) + "k";
  } else {
    double m = value / 1000000.0;
    return QString::number(m, 'f', 1) + "M";
  }
}

QString BPDataCollectPanel::formatFileSize(double sizeInBytes) {
  const QStringList units = {"B", "KB", "MB", "GB"};
  int unitIndex = 0;

  while (sizeInBytes >= 1024.0 && unitIndex < units.size() - 1) {
    sizeInBytes /= 1024.0;
    unitIndex++;
  }

  return QString("%1 %2").arg(QString::number(sizeInBytes, 'f', 1)).arg(units[unitIndex]);
}

QString BPDataCollectPanel::getElapsedTimeText(int timestamp) {
  int now = QDateTime::currentDateTime().toSecsSinceEpoch();
  int elapsed = now - timestamp;

  if (elapsed < 60) {
    return "Just now";
  } else if (elapsed < 3600) {
    int minutes = elapsed / 60;
    return QString("%1 minute%2 ago").arg(minutes).arg(minutes > 1 ? "s" : "");
  } else if (elapsed < 86400) {
    int hours = elapsed / 3600;
    return QString("%1 hour%2 ago").arg(hours).arg(hours > 1 ? "s" : "");
  } else {
    int days = elapsed / 86400;
    return QString("%1 day%2 ago").arg(days).arg(days > 1 ? "s" : "");
  }
}