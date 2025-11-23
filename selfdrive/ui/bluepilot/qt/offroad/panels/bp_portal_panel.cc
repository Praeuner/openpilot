// bp_portal_panel.cc - BluePilot Portal Panel
#include "bp_portal_panel.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QProcess>
#include <QGridLayout>
#include <QSpacerItem>
#include <QFile>
#include <QDateTime>

#include <QrCode.hpp>
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_dialogs.h"

using qrcodegen::QrCode;

BPPortalPanel::BPPortalPanel(QWidget *parent)
    : QWidget(parent),
      serverEnabled(false),
      routeCount(0),
      totalSize("0 GB"),
      recentErrorCount(0),
      lastToggleOnTimestamp(0) {

  networkManager = new QNetworkAccessManager(this);

  // Status update timer - check every 3 seconds
  statusTimer = new QTimer(this);
  statusTimer->setInterval(3000);
  connect(statusTimer, &QTimer::timeout, this, &BPPortalPanel::updateServerStatus);
  connect(statusTimer, &QTimer::timeout, this, &BPPortalPanel::updateWebSocketStatus);

  // Error check timer - check every 10 seconds
  errorTimer = new QTimer(this);
  errorTimer->setInterval(10000);
  connect(errorTimer, &QTimer::timeout, this, &BPPortalPanel::fetchServerErrors);

  setupUI();
  updateServerStatus();
  updateWebSocketStatus();
}

BPPortalPanel::~BPPortalPanel() {
  if (statusTimer) {
    statusTimer->stop();
  }
  if (errorTimer) {
    errorTimer->stop();
  }
}

void BPPortalPanel::setupUI() {
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(30, 30, 30, 30);
  mainLayout->setSpacing(30);

  setStyleSheet("BPPortalPanel { background-color: #1C1C1C; }");

  BPTextSizes sizes = BPTextSizes::getSizes();

  // ========== BLUEPILOT PORTAL GROUP ==========
  QGroupBox *serverGroup = new QGroupBox("BluePilot Portal");
  serverGroup->setStyleSheet(R"(
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
    QGroupBox > QWidget {
      background-color: transparent;
    }
    QGroupBox::indicator {
      width: 0px;
    }
  )");
  serverGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  QVBoxLayout *serverLayout = new QVBoxLayout(serverGroup);
  serverLayout->setContentsMargins(40, 40, 40, 40);
  serverLayout->setSpacing(25);

  // Toggle control
  QHBoxLayout *toggleLayout = new QHBoxLayout();
  QLabel *enableLabel = new QLabel("Enable BluePilot Portal");
  enableLabel->setStyleSheet(QString("font-size: %1px; color: #E4E4E4; font-weight: 500;").arg(sizes.titleSize + 5));
  toggleLayout->addWidget(enableLabel);
  toggleLayout->addStretch();

  serverToggle = new BPToggle();
  serverToggle->setStateColors("#4CAF50", "#808080");
  connect(serverToggle, &BPToggle::toggled, this, &BPPortalPanel::toggleServer);
  toggleLayout->addWidget(serverToggle);
  serverLayout->addLayout(toggleLayout);

  // Status line with websocket badge
  QHBoxLayout *statusLineLayout = new QHBoxLayout();

  serverStatusLabel = new QLabel("Status: Checking...");
  serverStatusLabel->setStyleSheet(QString("font-size: %1px; color: #AAAAAA;").arg(sizes.descSize + 6));
  serverStatusLabel->setWordWrap(true);  // Prevent horizontal scrolling
  statusLineLayout->addWidget(serverStatusLabel);

  // WebSocket status badge
  websocketStatusBadge = new QLabel("");
  websocketStatusBadge->setStyleSheet(QString(R"(
    QLabel {
      font-size: %1px;
      color: #AAAAAA;
      background-color: #2C2C2C;
      border-radius: 8px;
      padding: 4px 12px;
      font-weight: 500;
    }
  )").arg(sizes.descSize));
  websocketStatusBadge->setVisible(false);
  statusLineLayout->addWidget(websocketStatusBadge);
  statusLineLayout->addStretch();

  serverLayout->addLayout(statusLineLayout);

  // QR Code
  qrCodeLabel = new QLabel();
  qrCodeLabel->setAlignment(Qt::AlignCenter);
  qrCodeLabel->setStyleSheet("background-color: transparent; padding: 20px;");
  qrCodeLabel->setVisible(false);
  qrCodeLabel->setScaledContents(false);
  qrCodeLabel->setMinimumHeight(380);
  serverLayout->addWidget(qrCodeLabel);

  // URL display with inset background - centered below QR code
  urlLabel = new QLabel("URL: Not running");
  urlLabel->setAlignment(Qt::AlignCenter);
  urlLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
  urlLabel->setStyleSheet(QString(R"(
    QLabel {
      font-size: %1px;
      color: #2196F3;
      font-family: monospace;
      padding: 20px;
      background-color: #1C1C1C;
      border-radius: 10px;
      font-weight: 500;
    }
  )").arg(sizes.descSize + 6));
  urlLabel->setWordWrap(false);
  urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

  // Center the URL label in the layout
  QHBoxLayout *urlLayout = new QHBoxLayout();
  urlLayout->addStretch();
  urlLayout->addWidget(urlLabel);
  urlLayout->addStretch();
  serverLayout->addLayout(urlLayout);

  mainLayout->addWidget(serverGroup);

  // ========== HELP TEXT - MOVED BELOW SERVER GROUP ==========
  helpLabel = new QLabel(
    "<b>BluePilot Portal Features:</b><br/>"
    "• <b>Dashboard:</b> Device status, system health, and weekly driving statistics<br/>"
    "• <b>Routes:</b> Browse drives with multi-camera video playback, preserve favorites, export data<br/>"
    "• <b>Settings:</b> Manage the same settings found in the Settings panel with favorites, search, and backup/restore<br/>"
    "• <b>Parameters:</b> View and edit all system parameters with live sync<br/>"
    "• <b>Logs:</b> Live system diagnostics with real-time streaming<br/><br/>"
    "Open on any device in your browser. Can be added to your home screen as an app. "
    "Safety: Full interface locked while driving.");
  helpLabel->setStyleSheet(QString("font-size: %1px; color: #808080; line-height: 1.5;").arg(sizes.descSize + 2));
  helpLabel->setWordWrap(true);
  mainLayout->addWidget(helpLabel);

  // ========== WIFI HOTSPOT SHORTCUT ==========
  hotspotGroup = new QGroupBox("WiFi Hotspot");
  hotspotGroup->setStyleSheet(R"(
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
    QGroupBox > QWidget {
      background-color: transparent;
    }
    QGroupBox::indicator {
      width: 0px;
    }
  )");
  hotspotGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  QVBoxLayout *hotspotLayout = new QVBoxLayout(hotspotGroup);
  hotspotLayout->setContentsMargins(40, 40, 40, 40);
  hotspotLayout->setSpacing(25);

  // Info label
  QLabel *hotspotInfoLabel = new QLabel(
    "Quick toggle to enable WiFi hotspot (tethering) on this device. "
    "Other devices can connect to the hotspot and access the BluePilot Portal. "
    "<br/><br/>"
    "<b>Note:</b> You can also enable this in Settings → Network."
  );
  hotspotInfoLabel->setStyleSheet(QString(R"(
    QLabel {
      font-size: %1px;
      color: #AAAAAA;
      padding: 20px;
      background-color: #1C1C1C;
      border-radius: 10px;
      line-height: 1.6;
    }
  )").arg(sizes.descSize));
  hotspotInfoLabel->setWordWrap(true);
  hotspotLayout->addWidget(hotspotInfoLabel);

  // Hotspot toggle - same param as network panel for consistency
  hotspotToggle = new BPToggleControl(
    "EnableTethering",
    "Enable WiFi Hotspot",
    "Share internet connection and allow hotspot access to BluePilot Portal"
  );
  hotspotLayout->addWidget(hotspotToggle);

  mainLayout->addWidget(hotspotGroup);

  // ========== ROUTE STATS GROUP ==========
  statsFrame = new QGroupBox("Routes Overview");
  statsFrame->setStyleSheet(R"(
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
    QGroupBox > QWidget {
      background-color: transparent;
    }
    QGroupBox::indicator {
      width: 0px;
    }
  )");
  statsFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  QVBoxLayout *statsLayout = new QVBoxLayout(statsFrame);
  statsLayout->setContentsMargins(40, 40, 40, 40);
  statsLayout->setSpacing(25);

  // Stats in single row with columns - inset backgrounds
  QHBoxLayout *statsRow = new QHBoxLayout();
  statsRow->setSpacing(20);

  totalRoutesLabel = new QLabel("Routes: ...");
  totalRoutesLabel->setStyleSheet(QString(R"(
    QLabel {
      font-size: %1px;
      color: #E4E4E4;
      font-weight: 500;
      padding: 20px;
      background-color: #1C1C1C;
      border-radius: 10px;
    }
  )").arg(sizes.descSize));
  totalRoutesLabel->setAlignment(Qt::AlignCenter);
  statsRow->addWidget(totalRoutesLabel);

  totalSizeLabel = new QLabel("Size: ...");
  totalSizeLabel->setStyleSheet(QString(R"(
    QLabel {
      font-size: %1px;
      color: #E4E4E4;
      font-weight: 500;
      padding: 20px;
      background-color: #1C1C1C;
      border-radius: 10px;
    }
  )").arg(sizes.descSize));
  totalSizeLabel->setAlignment(Qt::AlignCenter);
  statsRow->addWidget(totalSizeLabel);

  newestRouteLabel = new QLabel("Newest: ...");
  newestRouteLabel->setStyleSheet(QString(R"(
    QLabel {
      font-size: %1px;
      color: #E4E4E4;
      font-weight: 500;
      padding: 20px;
      background-color: #1C1C1C;
      border-radius: 10px;
    }
  )").arg(sizes.descSize));
  newestRouteLabel->setAlignment(Qt::AlignCenter);
  newestRouteLabel->setWordWrap(true);
  statsRow->addWidget(newestRouteLabel);

  statsLayout->addLayout(statsRow);

  // Refresh button with proper BP styling
  refreshStatsButton = new BPButton("Refresh Stats");
  refreshStatsButton->setMinimumHeight(100);
  statsLayout->addWidget(refreshStatsButton);
  connect(refreshStatsButton, &BPButton::clicked, this, &BPPortalPanel::refreshStats);

  mainLayout->addWidget(statsFrame);

  mainLayout->addStretch();
}

void BPPortalPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  statusTimer->start();
  errorTimer->start();
  updateServerStatus();
  fetchServerErrors();
  if (serverEnabled) {
    refreshStats();
  }
}

void BPPortalPanel::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  statusTimer->stop();
  errorTimer->stop();
}

void BPPortalPanel::updateServerStatus() {
  bool running = isServerRunning();
  serverEnabled = running;

  // Check onroad status
  bool onroad = params.getBool("IsOnRoad");

  // Check if we're in startup grace period (30 seconds after toggle on)
  // During this time, allow for pip install and server restart
  qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
  qint64 timeSinceToggleOn = currentTime - lastToggleOnTimestamp;
  bool inStartupGracePeriod = (lastToggleOnTimestamp > 0) && (timeSinceToggleOn < 30000);  // 30 seconds

  // Disable toggle when onroad (can't change server state while driving)
  serverToggle->setEnabled(!onroad);

  if (running) {
    if (onroad) {
      // Server is running AND onroad = disabled for safety
      serverStatusLabel->setText("Status: <span style='color: #FF9800;'>●</span> Disabled (Driving)");

      QString url = getServerUrl();
      urlLabel->setText(url + " (Blocked)");
      urlLabel->setStyleSheet(urlLabel->styleSheet().replace("#808080", "#FF9800").replace("#2196F3", "#FF9800"));

      // Show QR code - though interface will show safety overlay when accessed
      generateQRCode(url);
    } else {
      // Server is running AND offroad = normal mode
      serverStatusLabel->setText("Status: <span style='color: #4CAF50;'>●</span> Running");

      QString url = getServerUrl();
      urlLabel->setText(url);
      urlLabel->setStyleSheet(urlLabel->styleSheet().replace("#FF9800", "#2196F3").replace("#808080", "#2196F3"));

      generateQRCode(url);
    }

    // Update toggle to match running state
    serverToggle->blockSignals(true);
    serverToggle->setChecked(true);
    serverToggle->blockSignals(false);

  } else {
    // Server is not running
    if (inStartupGracePeriod) {
      // Within startup grace period - keep showing "Starting..." and don't change toggle
      serverStatusLabel->setText("Status: <span style='color: #2196F3;'>●</span> Starting (installing dependencies)...");
      urlLabel->setText("Initializing...");
      // Don't change toggle state during grace period - keep it as user set it
    } else {
      // Outside grace period - server is genuinely stopped
      if (onroad) {
        // Not running AND onroad = disabled for safety
        serverStatusLabel->setText("Status: <span style='color: #808080;'>●</span> Stopped (Driving)");
      } else {
        // Not running AND offroad = user disabled or stopped
        serverStatusLabel->setText("Status: <span style='color: #808080;'>●</span> Stopped");
      }

      // Update toggle to match stopped state
      serverToggle->blockSignals(true);
      serverToggle->setChecked(false);
      serverToggle->blockSignals(false);

      urlLabel->setText("URL: Not running");
      urlLabel->setStyleSheet(urlLabel->styleSheet().replace("#2196F3", "#808080").replace("#FF9800", "#808080"));
      qrCodeLabel->setVisible(false);
    }
  }
}

void BPPortalPanel::toggleServer(bool enabled) {
  params.putBool("BPPortalEnabled", enabled);

  if (enabled) {
    // Record timestamp for startup grace period (allows time for pip install + restart)
    lastToggleOnTimestamp = QDateTime::currentMSecsSinceEpoch();

    // Show immediate "Starting..." status
    serverStatusLabel->setText("Status: <span style='color: #2196F3;'>●</span> Starting server...");
    urlLabel->setText("Initializing...");
    qrCodeLabel->setVisible(false);
  }

  // Update status after a brief delay to allow process manager to react
  QTimer::singleShot(1000, this, &BPPortalPanel::updateServerStatus);

  if (enabled) {
    // Check again after 2 seconds in case dependencies are being installed
    QTimer::singleShot(2000, this, &BPPortalPanel::updateServerStatus);
    // Fetch stats once server is running
    QTimer::singleShot(3000, this, &BPPortalPanel::refreshStats);
  }
}

bool BPPortalPanel::isServerRunning() {
  // Check if enabled via param
  bool enabled = params.getBool("BPPortalEnabled");
  if (!enabled) {
    return false;
  }

  // Try to connect to server
  QNetworkRequest request(QUrl(getServerUrl() + "/api/status"));

  QNetworkReply *reply = networkManager->get(request);

  // Synchronous wait with timeout
  QEventLoop loop;
  QTimer timeoutTimer;
  timeoutTimer.setSingleShot(true);

  connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

  timeoutTimer.start(1000);  // 1 second timeout
  loop.exec();

  bool running = (reply->error() == QNetworkReply::NoError && timeoutTimer.isActive());
  timeoutTimer.stop();
  reply->deleteLater();

  return running;
}

QString BPPortalPanel::getServerUrl() {
  QString ip = getWiFiIP();
  int port = QString::fromStdString(params.get("BPPortalPort")).toInt();
  if (port == 0) port = 8088;

  return QString("http://%1:%2").arg(ip).arg(port);
}

QString BPPortalPanel::getWiFiIP() {
  // First try to get WiFi interface IP (wlan0 on Comma devices)
  QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();

  for (const QNetworkInterface &iface : interfaces) {
    // Look for WiFi interface
    if (iface.name().startsWith("wlan") &&
        iface.flags().testFlag(QNetworkInterface::IsUp) &&
        iface.flags().testFlag(QNetworkInterface::IsRunning)) {

      QList<QNetworkAddressEntry> entries = iface.addressEntries();
      for (const QNetworkAddressEntry &entry : entries) {
        QHostAddress addr = entry.ip();
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback()) {
          return addr.toString();
        }
      }
    }
  }

  // Fallback: any non-loopback IPv4 address
  for (const QNetworkInterface &iface : interfaces) {
    if (!iface.flags().testFlag(QNetworkInterface::IsUp)) continue;

    QList<QNetworkAddressEntry> entries = iface.addressEntries();
    for (const QNetworkAddressEntry &entry : entries) {
      QHostAddress addr = entry.ip();
      if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback()) {
        return addr.toString();
      }
    }
  }

  return "127.0.0.1";
}

void BPPortalPanel::fetchRouteStats() {
  if (!isServerRunning()) {
    totalRoutesLabel->setText("Routes: Server not running");
    totalSizeLabel->setText("Size: -");
    newestRouteLabel->setText("Newest: -");
    return;
  }

  QNetworkRequest request(QUrl(getServerUrl() + "/api/routes"));

  QNetworkReply *reply = networkManager->get(request);

  QTimer *timeoutTimer = new QTimer(reply);
  timeoutTimer->setSingleShot(true);
  connect(timeoutTimer, &QTimer::timeout, reply, &QNetworkReply::abort);
  timeoutTimer->start(5000);

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    if (reply->error() == QNetworkReply::NoError) {
      QByteArray data = reply->readAll();
      QJsonDocument doc = QJsonDocument::fromJson(data);
      QJsonObject obj = doc.object();

      if (obj["success"].toBool()) {
        QJsonArray routes = obj["routes"].toArray();
        routeCount = routes.size();

        qint64 totalBytes = 0;
        QString newestRoute = "None";

        if (routes.size() > 0) {
          QJsonObject firstRoute = routes[0].toObject();
          newestRoute = firstRoute["displayTime"].toString() + " - " +
                       firstRoute["displayDate"].toString().split(" - ").first();

          for (const QJsonValue &routeVal : routes) {
            QJsonObject route = routeVal.toObject();
            totalBytes += route["sizeBytes"].toDouble();
          }
        }

        double totalGB = totalBytes / (1024.0 * 1024.0 * 1024.0);
        totalSize = QString::number(totalGB, 'f', 1) + " GB";

        totalRoutesLabel->setText(QString("Routes: %1").arg(routeCount));
        totalSizeLabel->setText(QString("Size: %1").arg(totalSize));
        newestRouteLabel->setText(QString("Newest: %1").arg(newestRoute));
      }
    } else {
      totalRoutesLabel->setText("Routes: Failed to fetch");
      totalSizeLabel->setText("Size: -");
      newestRouteLabel->setText("Newest: -");
    }

    reply->deleteLater();
  });
}

void BPPortalPanel::refreshStats() {
  totalRoutesLabel->setText("Routes: Loading...");
  totalSizeLabel->setText("Size: Loading...");
  newestRouteLabel->setText("Newest: Loading...");

  fetchRouteStats();
}

void BPPortalPanel::generateQRCode(const QString &url) {
  try {
    // Generate QR code from URL
    QrCode qr = QrCode::encodeText(url.toUtf8().data(), QrCode::Ecc::LOW);
    qint32 sz = qr.getSize();

    // Create image from QR modules
    QImage img(sz, sz, QImage::Format_RGB32);
    QRgb black = qRgb(0, 0, 0);
    QRgb white = qRgb(255, 255, 255);

    for (int y = 0; y < sz; y++) {
      for (int x = 0; x < sz; x++) {
        img.setPixel(x, y, qr.getModule(x, y) ? black : white);
      }
    }

    // Scale to larger display size (320x320) with nearest-neighbor to prevent blur
    int scale_factor = 320 / sz;
    int final_sz = scale_factor * sz;
    QPixmap pixmap = QPixmap::fromImage(
      img.scaled(final_sz, final_sz, Qt::KeepAspectRatio, Qt::FastTransformation),
      Qt::MonoOnly
    );

    qrCodeLabel->setPixmap(pixmap);
    qrCodeLabel->setVisible(true);
  } catch (const std::exception &e) {
    qWarning() << "Failed to generate QR code:" << e.what();
    qrCodeLabel->setVisible(false);
  }
}

void BPPortalPanel::fetchServerErrors() {
  // For now, just a stub implementation to fix the linker error
  // This can be enhanced later to show errors in the UI
  // The backend /api/logs endpoint is ready when needed

  if (!serverEnabled) {
    return;
  }

  // Optional: Uncomment below to fetch and log errors
  /*
  QString url = getServerUrl() + "/api/logs?limit=5&level=ERROR";
  QNetworkRequest request(url);
  QNetworkReply *reply = networkManager->get(request);

  connect(reply, &QNetworkReply::finished, [this, reply]() {
    reply->deleteLater();
    if (reply->error() == QNetworkReply::NoError) {
      QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
      if (doc.isObject()) {
        QJsonObject obj = doc.object();
        QJsonObject summary = obj["summary"].toObject();
        int errorCount = summary["ERROR"].toInt(0);
        int criticalCount = summary["CRITICAL"].toInt(0);

        // Store error count for potential UI display
        recentErrorCount = errorCount + criticalCount;

        // Could display errors here if needed
        if (recentErrorCount > 0) {
          qDebug() << "Server has" << recentErrorCount << "errors";
        }
      }
    }
  });
  */
}

void BPPortalPanel::updateWebSocketStatus() {
  if (!isServerRunning()) {
    websocketStatusBadge->setVisible(false);
    return;
  }

  // Fetch websocket status from the server
  QString url = getServerUrl() + "/api/websocket_status";
  QNetworkRequest request(url);
  QNetworkReply *reply = networkManager->get(request);

  // Set timeout for the request
  QTimer *timeoutTimer = new QTimer(reply);
  timeoutTimer->setSingleShot(true);
  timeoutTimer->setInterval(2000);

  connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
    reply->abort();
  });

  connect(reply, &QNetworkReply::finished, this, [this, reply, timeoutTimer]() {
    timeoutTimer->stop();
    timeoutTimer->deleteLater();
    reply->deleteLater();

    if (reply->error() == QNetworkReply::NoError) {
      QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
      if (doc.isObject()) {
        QJsonObject obj = doc.object();
        bool wsAvailable = obj["websockets_available"].toBool(false);
        int wsClients = obj["websocket_clients"].toInt(0);

        if (wsAvailable) {
          QString badgeText = QString("Users: %1 client%2").arg(wsClients).arg(wsClients == 1 ? "" : "s");
          websocketStatusBadge->setText(badgeText);
          websocketStatusBadge->setStyleSheet(websocketStatusBadge->styleSheet().replace("#AAAAAA", "#4CAF50"));
          websocketStatusBadge->setVisible(true);
        } else {
          websocketStatusBadge->setText("Users: Disabled");
          websocketStatusBadge->setStyleSheet(websocketStatusBadge->styleSheet().replace("#4CAF50", "#FF9800"));
          websocketStatusBadge->setVisible(true);
        }
      }
    } else {
      // Hide badge if we can't get status
      websocketStatusBadge->setVisible(false);
    }
  });

  timeoutTimer->start();
}
