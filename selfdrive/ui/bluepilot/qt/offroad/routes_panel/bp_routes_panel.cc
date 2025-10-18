// bp_routes_panel.cc - Web-Based Routes Panel with BP Toggle
#include "bp_routes_panel.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QProcess>
#include <QGridLayout>
#include <QSpacerItem>
#include <QFile>

#include <QrCode.hpp>

using qrcodegen::QrCode;

BPRoutesPanel::BPRoutesPanel(QWidget *parent)
    : QWidget(parent),
      serverEnabled(false),
      routeCount(0),
      totalSize("0 GB") {

  networkManager = new QNetworkAccessManager(this);

  // Status update timer - check every 3 seconds
  statusTimer = new QTimer(this);
  statusTimer->setInterval(3000);
  connect(statusTimer, &QTimer::timeout, this, &BPRoutesPanel::updateServerStatus);

  setupUI();
  updateServerStatus();
}

BPRoutesPanel::~BPRoutesPanel() {
  if (statusTimer) {
    statusTimer->stop();
  }
}

void BPRoutesPanel::setupUI() {
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(30, 30, 30, 30);
  mainLayout->setSpacing(30);

  setStyleSheet("BPRoutesPanel { background-color: #1C1C1C; }");

  BPTextSizes sizes = BPTextSizes::getSizes();

  // ========== WEB SERVER GROUP ==========
  QGroupBox *serverGroup = new QGroupBox("Web Server");
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
  QLabel *enableLabel = new QLabel("Enable Web Server");
  enableLabel->setStyleSheet(QString("font-size: %1px; color: #E4E4E4; font-weight: 500;").arg(sizes.titleSize + 5));
  toggleLayout->addWidget(enableLabel);
  toggleLayout->addStretch();

  serverToggle = new BPToggle();
  serverToggle->setStateColors("#4CAF50", "#808080");
  connect(serverToggle, &BPToggle::toggled, this, &BPRoutesPanel::toggleServer);
  toggleLayout->addWidget(serverToggle);
  serverLayout->addLayout(toggleLayout);

  // Status
  serverStatusLabel = new QLabel("Status: Checking...");
  serverStatusLabel->setStyleSheet(QString("font-size: %1px; color: #AAAAAA;").arg(sizes.descSize + 6));
  serverLayout->addWidget(serverStatusLabel);

  // URL display with inset background
  urlLabel = new QLabel("URL: Not running");
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
  urlLabel->setWordWrap(true);
  urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  serverLayout->addWidget(urlLabel);

  // QR Code
  qrCodeLabel = new QLabel();
  qrCodeLabel->setAlignment(Qt::AlignCenter);
  qrCodeLabel->setStyleSheet("background-color: transparent; padding: 20px;");
  qrCodeLabel->setVisible(false);
  qrCodeLabel->setScaledContents(false);
  qrCodeLabel->setMinimumHeight(380);
  serverLayout->addWidget(qrCodeLabel);

  mainLayout->addWidget(serverGroup);

  // ========== HELP TEXT - MOVED BELOW SERVER GROUP ==========
  helpLabel = new QLabel(
    "Open the URL in Safari or Chrome on your phone or tablet to view routes. "
    "The server stops automatically while driving.");
  helpLabel->setStyleSheet(QString("font-size: %1px; color: #808080;").arg(sizes.descSize + 2));
  helpLabel->setWordWrap(true);
  mainLayout->addWidget(helpLabel);

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
  connect(refreshStatsButton, &BPButton::clicked, this, &BPRoutesPanel::refreshStats);

  mainLayout->addWidget(statsFrame);

  // ========== CELLULAR ACCESS GROUP (ADVANCED) ==========
  cellularGroup = new QGroupBox("Cellular Access (Advanced)");
  cellularGroup->setStyleSheet(R"(
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
      color: #FF9800;
    }
    QGroupBox > QWidget {
      background-color: transparent;
    }
    QGroupBox::indicator {
      width: 0px;
    }
  )");
  cellularGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  QVBoxLayout *cellularLayout = new QVBoxLayout(cellularGroup);
  cellularLayout->setContentsMargins(40, 40, 40, 40);
  cellularLayout->setSpacing(25);

  // Security warning
  cellularWarningLabel = new QLabel(
    "⚠️  <b>Security Warning</b><br/><br/>"
    "Enabling cellular access allows the web server to be accessible over cellular networks. "
    "This may:<br/>"
    "• Use significant cellular data<br/>"
    "• Expose server to wider network access<br/>"
    "• Increase security risks<br/><br/>"
    "Cellular access will <b>automatically disable</b> after the timeout period."
  );
  cellularWarningLabel->setStyleSheet(QString(R"(
    QLabel {
      font-size: %1px;
      color: #FF9800;
      padding: 20px;
      background-color: rgba(255, 152, 0, 0.1);
      border: 2px solid rgba(255, 152, 0, 0.3);
      border-radius: 10px;
      line-height: 1.6;
    }
  )").arg(sizes.descSize));
  cellularWarningLabel->setWordWrap(true);
  cellularLayout->addWidget(cellularWarningLabel);

  // Cellular access toggle
  cellularToggle = new BPToggleControl(
    "BPWebServerAllowCellular",
    "Enable Cellular Access",
    "Allow web server access over cellular networks with automatic timeout"
  );
  cellularLayout->addWidget(cellularToggle);

  // Connect to toggleFlipped signal
  QObject::connect(cellularToggle, &BPToggleControl::toggleFlipped, this, &BPRoutesPanel::toggleCellularAccess);

  // Timeout selection
  cellularTimeoutSelection = new BPSelectionControl(
    "BPWebServerCellularTimeout",
    "Auto-Disable Timeout",
    "Select how long cellular access stays enabled before auto-disabling"
  );

  // Add timeout options as QPair<display, value>
  QVector<QPair<QString, QString>> timeoutOptions;
  timeoutOptions.append(qMakePair(QString("15 minutes"), QString("15")));
  timeoutOptions.append(qMakePair(QString("30 minutes"), QString("30")));
  timeoutOptions.append(qMakePair(QString("1 hour"), QString("60")));
  timeoutOptions.append(qMakePair(QString("2 hours"), QString("120")));
  timeoutOptions.append(qMakePair(QString("4 hours"), QString("240")));
  timeoutOptions.append(qMakePair(QString("8 hours"), QString("480")));

  cellularTimeoutSelection->setOptions(timeoutOptions);
  cellularLayout->addWidget(cellularTimeoutSelection);

  // Status display
  cellularStatusLabel = new QLabel("Status: Disabled");
  cellularStatusLabel->setStyleSheet(QString(R"(
    QLabel {
      font-size: %1px;
      color: #AAAAAA;
      padding: 20px;
      background-color: #1C1C1C;
      border-radius: 10px;
      font-weight: 500;
    }
  )").arg(sizes.descSize + 2));
  cellularLayout->addWidget(cellularStatusLabel);

  mainLayout->addWidget(cellularGroup);

  mainLayout->addStretch();
}

void BPRoutesPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  statusTimer->start();
  updateServerStatus();
  updateCellularStatus();
  if (serverEnabled) {
    refreshStats();
  }
}

void BPRoutesPanel::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  statusTimer->stop();
}

void BPRoutesPanel::updateServerStatus() {
  bool running = isServerRunning();
  serverEnabled = running;

  // Check onroad status first
  bool onroad = params.getBool("IsOnRoad");

  if (onroad) {
    serverStatusLabel->setText("Status: <span style='color: #FF9800;'>●</span> Disabled (Onroad)");
    serverToggle->setEnabled(false);
    urlLabel->setText("URL: Disabled while driving");
    urlLabel->setStyleSheet(urlLabel->styleSheet().replace("#2196F3", "#808080"));
    qrCodeLabel->setVisible(false);
    return;
  }

  serverToggle->setEnabled(true);

  if (running) {
    serverStatusLabel->setText("Status: <span style='color: #4CAF50;'>●</span> Running");

    // Block signals to prevent toggleServer() from being called
    // when we update the checkbox state
    serverToggle->blockSignals(true);
    serverToggle->setChecked(true);
    serverToggle->blockSignals(false);

    // Update cellular status when server is running
    updateCellularStatus();

    QString url = getServerUrl();
    urlLabel->setText(url);

    // Generate and display QR code
    generateQRCode(url);

  } else {
    serverStatusLabel->setText("Status: <span style='color: #808080;'>●</span> Stopped");

    // Block signals to prevent toggleServer() from being called
    // when we update the checkbox state
    serverToggle->blockSignals(true);
    serverToggle->setChecked(false);
    serverToggle->blockSignals(false);

    urlLabel->setText("URL: Not running");
    urlLabel->setStyleSheet(urlLabel->styleSheet().replace("#2196F3", "#808080"));
    qrCodeLabel->setVisible(false);
  }
}

void BPRoutesPanel::toggleServer(bool enabled) {
  params.putBool("BPWebServerEnabled", enabled);

  // Update status after a brief delay to allow process manager to react
  QTimer::singleShot(1000, this, &BPRoutesPanel::updateServerStatus);

  if (enabled) {
    // Fetch stats once server is running
    QTimer::singleShot(2000, this, &BPRoutesPanel::refreshStats);
  }
}

bool BPRoutesPanel::isServerRunning() {
  // Check if enabled via param
  bool enabled = params.getBool("BPWebServerEnabled");
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

QString BPRoutesPanel::getServerUrl() {
  QString ip = getWiFiIP();
  int port = QString::fromStdString(params.get("BPWebServerPort")).toInt();
  if (port == 0) port = 8088;

  return QString("http://%1:%2").arg(ip).arg(port);
}

QString BPRoutesPanel::getWiFiIP() {
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

void BPRoutesPanel::fetchRouteStats() {
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

void BPRoutesPanel::refreshStats() {
  totalRoutesLabel->setText("Routes: Loading...");
  totalSizeLabel->setText("Size: Loading...");
  newestRouteLabel->setText("Newest: Loading...");

  fetchRouteStats();
}

void BPRoutesPanel::generateQRCode(const QString &url) {
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

void BPRoutesPanel::toggleCellularAccess(bool enabled) {
  // Toggle is already handled by BPToggleControl
  // Just trigger status update
  QTimer::singleShot(500, this, &BPRoutesPanel::updateCellularStatus);
}

void BPRoutesPanel::updateCellularStatus() {
  // Fetch detailed status from server
  fetchDetailedStatus();
}

void BPRoutesPanel::fetchDetailedStatus() {
  if (!isServerRunning()) {
    cellularStatusLabel->setText("Status: Server not running");
    cellularStatusLabel->setStyleSheet(cellularStatusLabel->styleSheet().replace("#AAAAAA", "#808080"));
    return;
  }

  QNetworkRequest request(QUrl(getServerUrl() + "/api/status/detailed"));
  QNetworkReply *reply = networkManager->get(request);

  QTimer *timeoutTimer = new QTimer(reply);
  timeoutTimer->setSingleShot(true);

  connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
    reply->abort();
  });

  connect(reply, &QNetworkReply::finished, this, [this, reply, timeoutTimer]() {
    timeoutTimer->stop();

    if (reply->error() == QNetworkReply::NoError) {
      QByteArray response = reply->readAll();
      QJsonDocument doc = QJsonDocument::fromJson(response);

      if (!doc.isNull() && doc.isObject()) {
        QJsonObject obj = doc.object();
        QJsonObject cellularAccess = obj["cellular_access"].toObject();
        QJsonObject network = obj["network"].toObject();

        bool enabled = cellularAccess["enabled"].toBool();
        bool active = cellularAccess["active"].toBool();
        int timeRemaining = cellularAccess["time_remaining_minutes"].toInt();
        QString connectionType = network["connection_type"].toString();

        // Update status label
        QString statusText;
        QString colorStyle = "#AAAAAA";  // Default gray

        if (active) {
          // Cellular access is enabled and active
          statusText = "Status: <span style='color: #FF9800;'>●</span> Active";

          // Add time remaining
          if (timeRemaining > 60) {
            int hours = timeRemaining / 60;
            int mins = timeRemaining % 60;
            statusText += QString("<br/>Time Remaining: %1h %2m").arg(hours).arg(mins);
          } else {
            statusText += QString("<br/>Time Remaining: %1 minutes").arg(timeRemaining);
          }

          // Add connection type
          if (connectionType == "cellular") {
            statusText += "<br/>Connection: <span style='color: #FF9800;'>Cellular</span>";
          } else if (connectionType == "wifi") {
            statusText += "<br/>Connection: WiFi";
          } else {
            statusText += QString("<br/>Connection: %1").arg(connectionType);
          }

          colorStyle = "#FF9800";  // Orange when active
        } else if (enabled && !active) {
          // Enabled but timeout expired
          statusText = "Status: <span style='color: #808080;'>●</span> Disabled (Timeout Expired)";
          colorStyle = "#808080";
        } else {
          // Disabled
          statusText = "Status: <span style='color: #808080;'>●</span> Disabled (WiFi-Only)";
          colorStyle = "#808080";
        }

        cellularStatusLabel->setText(statusText);
      }
    } else {
      cellularStatusLabel->setText("Status: Failed to fetch");
    }

    reply->deleteLater();
  });

  timeoutTimer->start(3000);
}
