// selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_controls.cc

#include "bp_panel_controls.h"
#include "selfdrive/ui/qt/widgets/input.h"
#include "selfdrive/ui/qt/util.h"
#include <QTimer>

// ========== BPWifiItem Implementation ==========

BPWifiItem::BPWifiItem(QWidget *parent) : QFrame(parent) {
  setObjectName("bp_wifi_item");
  setFrameShape(QFrame::StyledPanel);

  QHBoxLayout *mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(25, 22, 25, 22);
  mainLayout->setSpacing(20);

  // Status icon (lock/checkmark/slash) - larger
  statusIconLabel = new QLabel(this);
  statusIconLabel->setFixedSize(55, 55);
  statusIconLabel->setScaledContents(true);
  statusIconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
  mainLayout->addWidget(statusIconLabel);

  // SSID label - larger text
  ssidLabel = new QLabel(this);
  BPTextSizes sizes = BPTextSizes::getSizes();
  ssidLabel->setStyleSheet(QString("font-size: %1px; font-weight: 500; color: #E4E4E4;").arg(sizes.titleSize));
  ssidLabel->setWordWrap(false);
  ssidLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
  mainLayout->addWidget(ssidLabel, 1);

  // Connecting label (shown during connection) - larger
  connectingLabel = new QPushButton(tr("CONNECTING..."), this);
  connectingLabel->setObjectName("bp_connecting_label");
  connectingLabel->setFixedSize(240, 70);
  connectingLabel->setEnabled(false);
  connectingLabel->setVisible(false);
  mainLayout->addWidget(connectingLabel);

  // Forget button - larger
  forgetBtn = new QPushButton(tr("FORGET"), this);
  forgetBtn->setObjectName("bp_forget_btn");
  forgetBtn->setFixedSize(200, 70);
  forgetBtn->setVisible(false);
  mainLayout->addWidget(forgetBtn);

  // Signal strength icon - larger
  strengthLabel = new QLabel(this);
  strengthLabel->setFixedSize(80, 80);
  strengthLabel->setScaledContents(true);
  strengthLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
  mainLayout->addWidget(strengthLabel);

  // Connect forget button signal
  connect(forgetBtn, &QPushButton::clicked, this, [this]() {
    emit forgetNetwork(network);
  });

  setMinimumHeight(110);
  setCursor(Qt::PointingHandCursor);
}

void BPWifiItem::setNetwork(const Network &n, const QPixmap &statusIcon, const QPixmap &strengthIcon, bool showForget) {
  network = n;

  ssidLabel->setText(QString::fromUtf8(network.ssid));
  statusIconLabel->setPixmap(statusIcon);
  strengthLabel->setPixmap(strengthIcon);

  bool isConnecting = (network.connected == ConnectedType::CONNECTING);
  bool isConnected = (network.connected == ConnectedType::CONNECTED);

  connectingLabel->setVisible(isConnecting);
  forgetBtn->setVisible(showForget && isConnected && !isConnecting);

  // Update styling based on connection state
  BPTextSizes sizes = BPTextSizes::getSizes();
  QString baseStyle = QString(R"(
    QFrame#bp_wifi_item {
      background-color: #242424;
      border-radius: 0px;
      border: none;
      border-bottom: 1px solid #3a3a3a;
    }
    QFrame#bp_wifi_item:hover {
      background-color: #2a2a2a;
    }
    QPushButton#bp_forget_btn {
      background-color: #d33939;
      color: white;
      font-size: %1px;
      font-weight: 500;
      border-radius: 12px;
      border: none;
    }
    QPushButton#bp_forget_btn:hover {
      background-color: #e84545;
    }
    QPushButton#bp_forget_btn:pressed {
      background-color: #a82a2a;
    }
    QPushButton#bp_connecting_label {
      background-color: #1a1a1a;
      color: #999999;
      font-size: %2px;
      font-weight: 400;
      border-radius: 12px;
      border: 1px solid #333333;
    }
  )").arg(sizes.descSize).arg(sizes.descSize);

  // Apply connected styling if connected
  if (isConnected) {
    baseStyle += R"(
      QFrame#bp_wifi_item {
        border: 2px solid #4A90E2;
        background-color: #2a3a4a;
        border-radius: 15px;
        margin: 5px 0px;
      }
      QFrame#bp_wifi_item:hover {
        background-color: #2f4555;
        border: 2px solid #4A90E2;
      }
    )";
  }

  setStyleSheet(baseStyle);
}

void BPWifiItem::mousePressEvent(QMouseEvent *event) {
  pressPos = event->pos();
  isDragging = false;

  if (forgetBtn->isVisible()) {
    QFrame::mousePressEvent(event);
    return;
  }
  event->accept();
}

void BPWifiItem::mouseMoveEvent(QMouseEvent *event) {
  // Detect if user is dragging (scrolling) vs tapping
  if (!isDragging && (event->pos() - pressPos).manhattanLength() > 10) {
    isDragging = true;
  }
  QFrame::mouseMoveEvent(event);
}

void BPWifiItem::mouseReleaseEvent(QMouseEvent *event) {
  if (forgetBtn->isVisible()) {
    QFrame::mouseReleaseEvent(event);
    return;
  }

  // Only emit connect if it wasn't a drag/scroll
  if (!isDragging) {
    emit connectToNetwork(network);
  }

  isDragging = false;
  event->accept();
}

// ========== BPWifiListControl Implementation ==========

// Constructor that creates its own WifiManager (for legacy JSON panel use)
BPWifiListControl::BPWifiListControl(const QString &title, const QString &desc, QWidget *parent) : QFrame(parent) {
  wifi = new WifiManager(this);
  ownsWifiManager = true;
  init(title, desc);
}

// Constructor with shared WifiManager (for native panels) - PREFERRED
BPWifiListControl::BPWifiListControl(const QString &title, const QString &desc, WifiManager *sharedWifi, QWidget *parent)
    : QFrame(parent), wifi(sharedWifi), ownsWifiManager(false) {
  init(title, desc);
}

void BPWifiListControl::init(const QString &title, const QString &desc) {
  // Load network icons
  loadNetworkIcons();

  // Connect WiFi signals
  connect(wifi, &WifiManager::refreshSignal, this, &BPWifiListControl::refreshNetworks);
  connect(wifi, &WifiManager::wrongPassword, this, [this](const QString &ssid) {
    ConfirmationDialog::alert(tr("Wrong password for \"%1\"").arg(ssid), this);
  });

  setStyleSheet(R"(
    BPWifiListControl {
      background-color: #242424;
      border-radius: 10px;
    }
  )");

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(25, 25, 25, 25);
  mainLayout->setSpacing(20);
  mainLayout->setSizeConstraint(QLayout::SetMinimumSize);

  // Get text sizes
  BPTextSizes sizes = BPTextSizes::getSizes();

  // Title and description (only if not empty)
  if (!title.isEmpty() || !desc.isEmpty()) {
    QVBoxLayout *headerLayout = new QVBoxLayout();
    headerLayout->setSpacing(10);

    if (!title.isEmpty()) {
      titleLabel = new QLabel(title, this);
      titleLabel->setStyleSheet(QString("font-size: %1px; color: white; font-weight: 500;").arg(sizes.titleSize));
      titleLabel->setWordWrap(true);
      headerLayout->addWidget(titleLabel);
    }

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet(QString("font-size: %1px; color: #AAAAAA;").arg(sizes.descSize));
      descLabel->setWordWrap(true);
      headerLayout->addWidget(descLabel);
    }

    mainLayout->addLayout(headerLayout);
  }

  // Scanning label - improved visibility
  scanningLabel = new QLabel(tr("🔍 Scanning for networks..."), this);
  scanningLabel->setStyleSheet(QString(R"(
    QLabel {
      font-size: %1px;
      color: #2196F3;
      padding: 40px;
      background-color: transparent;
      font-weight: 500;
    }
  )").arg(sizes.titleSize));
  scanningLabel->setAlignment(Qt::AlignCenter);
  scanningLabel->setVisible(false);
  mainLayout->addWidget(scanningLabel);

  // WiFi list container (no scroll view - size to content)
  wifiListContainer = new QWidget(this);
  wifiListContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  wifiListLayout = new QVBoxLayout(wifiListContainer);
  wifiListLayout->setSpacing(10);
  wifiListLayout->setContentsMargins(0, 0, 0, 0);
  wifiListLayout->setSizeConstraint(QLayout::SetMinimumSize);
  wifiListLayout->addStretch();  // Push networks to top

  mainLayout->addWidget(wifiListContainer);

  // Set size policy for the control itself
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

  // Watch for tethering toggle changes (only when visible)
  tetheringTimer = new QTimer(this);
  connect(tetheringTimer, &QTimer::timeout, this, [this]() {
    bool paramState = params.getBool("EnableTethering");
    bool wifiState = wifi->isTetheringEnabled();
    if (paramState != wifiState) {
      wifi->setTetheringEnabled(paramState);
    }
  });

  // Don't start WiFi manager or timers here - parent manages lifecycle if shared
}

void BPWifiListControl::loadNetworkIcons() {
  lockIcon = QPixmap(":/icons/lock_closed.svg").scaled(55, 55, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  checkmarkIcon = QPixmap(":/icons/checkmark.svg").scaled(55, 55, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  slashIcon = QPixmap(":/icons/circled_slash.svg").scaled(55, 55, Qt::KeepAspectRatio, Qt::SmoothTransformation);

  for (const auto &s : {"low", "medium", "high", "full"}) {
    QPixmap pix(QString(":/icons/wifi_strength_%1.svg").arg(s));
    strengthIcons.push_back(pix.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
}

BPWifiItem *BPWifiListControl::getWifiItem(int index) {
  if (index >= wifiItems.size()) {
    BPWifiItem *item = new BPWifiItem(wifiListContainer);
    connect(item, &BPWifiItem::connectToNetwork, this, &BPWifiListControl::onConnectToNetwork);
    connect(item, &BPWifiItem::forgetNetwork, this, &BPWifiListControl::onForgetNetwork);
    wifiItems.push_back(item);
    // Insert before the stretch item (which is at the end)
    wifiListLayout->insertWidget(wifiListLayout->count() - 1, item);
  }
  return wifiItems[index];
}

void BPWifiListControl::refreshNetworks() {
  // Sync tethering toggle state with wifi manager
  bool tetheringEnabled = wifi->isTetheringEnabled();
  if (params.getBool("EnableTethering") != tetheringEnabled) {
    params.putBool("EnableTethering", tetheringEnabled);
  }

  scanningLabel->setVisible(wifi->seenNetworks.isEmpty());

  // Update WiFi list
  int network_index = 0;
  for (const Network &network : wifi->seenNetworks) {
    BPWifiItem *item = getWifiItem(network_index++);

    // Determine status icon
    QPixmap statusIcon;
    if (network.connected == ConnectedType::CONNECTED) {
      statusIcon = checkmarkIcon;
    } else if (network.connected == ConnectedType::CONNECTING) {
      statusIcon = QPixmap();  // No icon while connecting
    } else if (network.security_type == SecurityType::UNSUPPORTED) {
      statusIcon = slashIcon;
    } else if (network.security_type > SecurityType::OPEN) {
      statusIcon = lockIcon;
    }

    // Determine strength icon (0-3 for low/medium/high/full)
    int strength_idx = std::min(std::max(0, static_cast<int>(network.strength / 20)), 3);
    QPixmap strengthIcon = strengthIcons.empty() ? QPixmap() : strengthIcons[strength_idx];

    // Show forget button for known networks
    bool showForget = (network.connected == ConnectedType::CONNECTED) ||
                      (network.connected == ConnectedType::DISCONNECTED && network.security_type != SecurityType::UNSUPPORTED);

    item->setNetwork(network, statusIcon, strengthIcon, showForget);
    item->setVisible(true);
  }

  // Hide unused items
  for (int i = network_index; i < wifiItems.size(); ++i) {
    wifiItems[i]->setVisible(false);
  }

  // Update container size to fit content
  wifiListContainer->adjustSize();
  adjustSize();
}

void BPWifiListControl::onConnectToNetwork(const Network n) {
  if (n.security_type == SecurityType::UNSUPPORTED) {
    ConfirmationDialog::alert(tr("Unsupported security type"), this);
    return;
  }

  if (n.connected == ConnectedType::CONNECTED) {
    return;  // Already connected
  }

  if (n.security_type == SecurityType::OPEN) {
    wifi->connect(n);
  } else {
    QString pass = InputDialog::getText(
      tr("Enter password"),
      this,
      tr("for \"%1\"").arg(QString::fromUtf8(n.ssid)),
      true
    );

    if (!pass.isEmpty()) {
      wifi->connect(n, false, pass);
    }
  }
}

void BPWifiListControl::onForgetNetwork(const Network n) {
  if (ConfirmationDialog::confirm(tr("Forget Wi-Fi Network \"%1\"?").arg(QString::fromUtf8(n.ssid)), tr("Forget"), this)) {
    wifi->forgetConnection(QString::fromUtf8(n.ssid));
    QTimer::singleShot(500, this, &BPWifiListControl::refreshNetworks);
  }
}

void BPWifiListControl::connectHiddenNetwork() {
  QString ssid = InputDialog::getText(tr("Enter SSID"), this, "", false, 1);
  if (!ssid.isEmpty()) {
    QString pass = InputDialog::getText(tr("Enter password"), this, tr("for \"%1\"").arg(ssid), true, -1);
    Network hidden_network;
    hidden_network.ssid = ssid.toUtf8();
    if (!pass.isEmpty()) {
      hidden_network.security_type = SecurityType::WPA;
      wifi->connect(hidden_network, true, pass);
    } else {
      wifi->connect(hidden_network, true);
    }
  }
}

void BPWifiListControl::scanNetworks() {
  wifi->requestScan();
  QTimer::singleShot(500, this, &BPWifiListControl::refreshNetworks);
}

void BPWifiListControl::changeTetheringPassword() {
  QString pass = InputDialog::getText(tr("Enter new tethering password"), this, "", true, 8, wifi->getTetheringPassword());
  if (!pass.isEmpty()) {
    wifi->changeTetheringPassword(pass);
  }
}

void BPWifiListControl::showEvent(QShowEvent *event) {
  QFrame::showEvent(event);

  // Only start WiFi manager if we own it (legacy JSON panel use)
  // Native panels manage their own WiFi manager lifecycle
  if (ownsWifiManager) {
    wifi->start();
  }

  tetheringTimer->start(1000);
  QTimer::singleShot(500, this, &BPWifiListControl::refreshNetworks);
}

void BPWifiListControl::hideEvent(QHideEvent *event) {
  QFrame::hideEvent(event);

  // Only stop WiFi manager if we own it
  if (ownsWifiManager) {
    wifi->stop();
  }

  tetheringTimer->stop();
}

void BPWifiListControl::editApn() {
  const QString cur_apn = QString::fromStdString(params.get("GsmApn"));
  QString apn = InputDialog::getText(tr("Enter APN"), this, tr("leave blank for automatic configuration"), false, -1, cur_apn).trimmed();

  if (apn.isEmpty()) {
    params.remove("GsmApn");
  } else {
    params.put("GsmApn", apn.toStdString());
  }
  wifi->updateGsmSettings(params.getBool("GsmRoaming"), apn, params.getBool("GsmMetered"));
}

void BPWifiListControl::configureWifiMetered() {
  // Show options dialog for metered setting
  QMap<QString, QString> options;
  options[tr("Default")] = "0";
  options[tr("Metered")] = "1";
  options[tr("Unmetered")] = "2";

  MeteredType current = wifi->currentNetworkMetered();
  QString currentKey = tr("Default");
  if (current == MeteredType::YES) {
    currentKey = tr("Metered");
  } else if (current == MeteredType::NO) {
    currentKey = tr("Unmetered");
  }

  QString selection = MultiOptionDialog::getSelection(tr("Wi-Fi Network Metered"), options.keys(), currentKey, this);
  if (!selection.isEmpty()) {
    int id = options[selection].toInt();
    MeteredType metered = MeteredType::UNKNOWN;
    if (id == 1) {
      metered = MeteredType::YES;
    } else if (id == 2) {
      metered = MeteredType::NO;
    }
    wifi->setCurrentNetworkMetered(metered);
    QTimer::singleShot(500, this, &BPWifiListControl::refreshNetworks);
  }
}

// ========== BPWifiMeteredControl Implementation ==========

BPWifiMeteredControl::BPWifiMeteredControl(const QString &title, const QString &desc, QWidget *parent)
    : QFrame(parent), titleText(title), descText(desc) {

  setStyleSheet(R"(
    BPWifiMeteredControl {
      background-color: #242424;
      border-radius: 10px;
      min-height: 150px;
    }
  )");

  QHBoxLayout *mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(25, 25, 25, 25);
  mainLayout->setSpacing(50);

  // Get text sizes
  BPTextSizes sizes = BPTextSizes::getSizes();

  // Left side: Segmented buttons (Default / Metered / Unmetered) - BP style
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(0);
  buttonLayout->setContentsMargins(0, 0, 0, 0);

  QStringList buttonLabels = {tr("Default"), tr("Metered"), tr("Unmetered")};

  for (int i = 0; i < buttonLabels.size(); ++i) {
    QPushButton *btn = new QPushButton(buttonLabels[i], this);
    btn->setFixedHeight(80);
    btn->setMinimumWidth(180);

    // Apply BP segmented button styling (matching BPSegmentedControl exactly)
    QString btnStyle = QString(R"(
      QPushButton {
        background-color: #363636;
        border: 1px solid #404040;
        border-right: 1px solid #505050;
        color: white;
        font-size: %1px;
        padding: 5px 15px;
        border-radius: 0px;
      }
      QPushButton:checked {
        background-color: #2196F3;
        border: 1px solid #2196F3;
      }
      QPushButton:hover:!checked {
        background-color: #404040;
      }
      QPushButton:disabled {
        background-color: #202020;
        border-color: #303030;
        color: #666666;
      }
    )").arg(sizes.segmentedButtonSize);

    // Add border radius to first and last buttons
    if (i == 0) {
      btnStyle += "QPushButton { border-top-left-radius: 35px; border-bottom-left-radius: 35px; }";
    } else if (i == buttonLabels.size() - 1) {
      btnStyle += R"(
        QPushButton {
          border-top-right-radius: 35px;
          border-bottom-right-radius: 35px;
          border-right: 1px solid #404040;
        }
      )";
    }

    btn->setStyleSheet(btnStyle);
    btn->setCheckable(true);
    btn->setAutoExclusive(true);

    // Connect button click
    connect(btn, &QPushButton::clicked, this, [this, i]() {
      BPWifiListControl *wifiList = findWifiListControl();
      if (!wifiList) {
        BPLog::bpWarn() << "[BPWifiMeteredControl] Could not find BPWifiListControl" << std::endl;
        return;
      }

      WifiManager *wifi = wifiList->getWifiManager();
      if (!wifi) {
        BPLog::bpWarn() << "[BPWifiMeteredControl] Could not get WifiManager" << std::endl;
        return;
      }

      MeteredType metered = MeteredType::UNKNOWN;
      if (i == 1) {
        metered = MeteredType::YES;
      } else if (i == 2) {
        metered = MeteredType::NO;
      }

      auto pending_call = wifi->setCurrentNetworkMetered(metered);
      if (pending_call) {
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(*pending_call);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
          // Refresh after setting
          QTimer::singleShot(300, this, &BPWifiMeteredControl::refresh);
          watcher->deleteLater();
        });
      } else {
        QTimer::singleShot(300, this, &BPWifiMeteredControl::refresh);
      }
    });

    buttons.push_back(btn);
    buttonLayout->addWidget(btn);
  }

  mainLayout->addLayout(buttonLayout);

  // Right side: Title & Description
  QVBoxLayout *textLayout = new QVBoxLayout();
  textLayout->setSpacing(10);

  titleLabel = new QLabel(title, this);
  titleLabel->setStyleSheet(QString("font-size: %1px; color: white; font-weight: 500;").arg(sizes.titleSize));
  titleLabel->setWordWrap(true);
  textLayout->addWidget(titleLabel);

  if (!desc.isEmpty()) {
    descLabel = new QLabel(desc, this);
    descLabel->setStyleSheet(QString("font-size: %1px; color: #AAAAAA;").arg(sizes.descSize));
    descLabel->setWordWrap(true);
    textLayout->addWidget(descLabel);
  }

  textLayout->addStretch();
  mainLayout->addLayout(textLayout, 1);

  // Periodic refresh to track network changes (only when visible)
  refreshTimer = new QTimer(this);
  connect(refreshTimer, &QTimer::timeout, this, &BPWifiMeteredControl::refresh);

  // Don't start timer here - wait for showEvent
}

void BPWifiMeteredControl::refresh() {
  BPWifiListControl *wifiList = findWifiListControl();
  if (!wifiList) {
    // Disable all buttons if we can't find wifi manager
    for (auto btn : buttons) {
      btn->setEnabled(false);
    }
    return;
  }

  WifiManager *wifi = wifiList->getWifiManager();
  if (!wifi) {
    for (auto btn : buttons) {
      btn->setEnabled(false);
    }
    return;
  }

  // Get current metered state
  currentMetered = wifi->currentNetworkMetered();

  // Update button states
  for (auto btn : buttons) {
    btn->setEnabled(true);
  }

  // Block signals to avoid triggering clicked events during UI update
  for (auto btn : buttons) {
    btn->blockSignals(true);
  }

  // Set the appropriate button as checked
  int selectedIndex = 0;  // Default
  if (currentMetered == MeteredType::YES) {
    selectedIndex = 1;  // Metered
  } else if (currentMetered == MeteredType::NO) {
    selectedIndex = 2;  // Unmetered
  }

  for (int i = 0; i < buttons.size(); ++i) {
    buttons[i]->setChecked(i == selectedIndex);
  }

  // Re-enable signals
  for (auto btn : buttons) {
    btn->blockSignals(false);
  }
}

void BPWifiMeteredControl::updateSelectedButton() {
  refresh();
}

BPWifiListControl* BPWifiMeteredControl::findWifiListControl() {
  // Search up the widget tree for BPWifiListControl
  QWidget *current = this->parentWidget();
  BPWifiListControl *wifiList = nullptr;

  while (current && !wifiList) {
    wifiList = current->findChild<BPWifiListControl*>();
    if (!wifiList) {
      current = current->parentWidget();
    }
  }

  return wifiList;
}

void BPWifiMeteredControl::showEvent(QShowEvent *event) {
  QFrame::showEvent(event);
  // Start refresh timer only when widget becomes visible
  // Defer initial refresh to avoid blocking on DBus calls during showEvent
  QTimer::singleShot(1000, this, &BPWifiMeteredControl::refresh);
  refreshTimer->start(5000);  // Refresh every 5 seconds
}

void BPWifiMeteredControl::hideEvent(QHideEvent *event) {
  QFrame::hideEvent(event);
  // Stop refresh timer when widget is hidden
  refreshTimer->stop();
}
