// selfdrive/ui/bluepilot/qt/offroad/panels/bp_network_panel.cc

#include "bp_network_panel.h"

#include <QScrollArea>
#include <QJsonArray>
#include <QJsonObject>
#include "selfdrive/ui/qt/widgets/input.h"
#include "selfdrive/ui/qt/widgets/scrollview.h"
#include "selfdrive/ui/sunnypilot/ui.h"
#include "system/hardware/hw.h"

BPNetworkPanel::BPNetworkPanel(QWidget *parent) : QWidget(parent) {
  // Create WiFi manager (not started yet)
  wifi = new WifiManager(this);

  setupUI();

  // Refresh timer for periodic updates (only active when visible)
  refreshTimer = new QTimer(this);
  connect(refreshTimer, &QTimer::timeout, this, &BPNetworkPanel::refreshAll);
}

void BPNetworkPanel::setupUI() {
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(40, 40, 40, 40);
  mainLayout->setSpacing(30);

  // Create all groups
  createWifiNetworksGroup();
  createWifiManagementGroup();
  createTetheringGroup();
  createCellularGroup();

  mainLayout->addStretch();

  setStyleSheet(R"(
    BPNetworkPanel {
      background-color: #1B1B1B;
    }
    BPNetworkPanel QGroupBox BPToggleControl,
    BPNetworkPanel QGroupBox BPCommandControl,
    BPNetworkPanel QGroupBox BPWifiListControl,
    BPNetworkPanel QGroupBox BPWifiMeteredControl {
      background-color: transparent;
    }
  )");
}

QGroupBox* BPNetworkPanel::createStyledGroupBox(const QString &title) {
  QGroupBox *group = new QGroupBox(title, this);
  group->setStyleSheet(R"(
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
  group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  return group;
}

void BPNetworkPanel::createWifiNetworksGroup() {
  wifiNetworksGroup = createStyledGroupBox(tr("Available Wi-Fi Networks"));
  QVBoxLayout *layout = new QVBoxLayout(wifiNetworksGroup);
  layout->setSpacing(15);
  layout->setContentsMargins(10, 10, 10, 10);

  // Top row: IP Address (left), Hidden Network button, and Scan button (right)
  QHBoxLayout *topRow = new QHBoxLayout();
  topRow->setSpacing(15);
  topRow->setContentsMargins(15, 5, 15, 5);

  // IP Address label with lighter gray inset background
  ipAddressLabel = new QLabel("IP: 192.168.1.1", this);
  ipAddressLabel->setStyleSheet(R"(
    QLabel {
      background-color: rgba(255, 255, 255, 0.05);
      color: #2196F3;
      font-size: 36px;
      font-weight: 500;
      padding: 16px 25px;
      border-radius: 12px;
      border: 1px solid rgba(255, 255, 255, 0.1);
    }
  )");
  ipAddressLabel->setAlignment(Qt::AlignCenter);
  ipAddressLabel->setMaximumWidth(320);
  topRow->addWidget(ipAddressLabel);

  topRow->addStretch();

  // Hidden Network Button
  hiddenNetworkBtn = new QPushButton(tr("HIDDEN"), this);
  hiddenNetworkBtn->setMinimumHeight(90);
  hiddenNetworkBtn->setMinimumWidth(200);
  hiddenNetworkBtn->setStyleSheet(R"(
    QPushButton {
      background-color: #363636;
      border: none;
      border-radius: 15px;
      color: #FFFFFF;
      font-size: 38px;
      font-weight: 600;
      padding: 20px 35px;
    }
    QPushButton:hover {
      background-color: #404040;
    }
    QPushButton:pressed {
      background-color: #505050;
    }
  )");
  connect(hiddenNetworkBtn, &QPushButton::clicked, this, [this]() {
    wifiList->connectHiddenNetwork();
  });
  topRow->addWidget(hiddenNetworkBtn);

  // Scan button - larger size
  scanButton = new QPushButton(tr("SCAN"), this);
  scanButton->setMinimumHeight(90);
  scanButton->setMinimumWidth(200);
  scanButton->setStyleSheet(R"(
    QPushButton {
      background-color: #2196F3;
      border: none;
      border-radius: 15px;
      color: #FFFFFF;
      font-size: 38px;
      font-weight: 600;
      padding: 20px 35px;
    }
    QPushButton:hover {
      background-color: #1976D2;
    }
    QPushButton:pressed {
      background-color: #0D47A1;
    }
  )");
  connect(scanButton, &QPushButton::clicked, this, [this]() {
    wifiList->scanNetworks();
  });
  topRow->addWidget(scanButton);

  layout->addLayout(topRow);

  // WiFi List - pass shared WifiManager (stock panel pattern)
  // Remove title and description
  wifiList = new BPWifiListControl(
    "",  // No title
    "",  // No description
    wifi,  // Pass shared WifiManager
    this
  );
  // Override background to be transparent within group
  wifiList->setStyleSheet("BPWifiListControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(wifiList);

  // Divider
  layout->addWidget(BPUIHelpers::createDivider());

  // WiFi Metered Control - moved from management group
  wifiMeteredControl = new BPWifiMeteredControl(
    tr("Wi-Fi Network Metered"),
    tr("Prevent large data uploads when on a metered Wi-Fi connection"),
    this
  );
  wifiMeteredControl->setStyleSheet("BPWifiMeteredControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(wifiMeteredControl);

  mainLayout->addWidget(wifiNetworksGroup);
}

void BPNetworkPanel::createWifiManagementGroup() {
  // WiFi Management group is now empty - all controls moved to WiFi Networks group
  // Keeping this method for now to avoid breaking the setupUI call
}

void BPNetworkPanel::createTetheringGroup() {
  tetheringGroup = createStyledGroupBox(tr("Tethering"));
  QVBoxLayout *layout = new QVBoxLayout(tetheringGroup);
  layout->setSpacing(10);
  layout->setContentsMargins(10, 10, 10, 10);

  // Tethering Toggle
  tetheringToggle = new BPToggleControl(
    "EnableTethering",
    tr("Wi-Fi Tethering"),
    tr("Share your internet connection with other devices"),
    this
  );
  connect(tetheringToggle, &BPToggleControl::toggleFlipped, this, &BPNetworkPanel::onTetheringToggled);
  tetheringToggle->setStyleSheet("BPToggleControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(tetheringToggle);

  // Divider
  tetheringPasswordDivider = BPUIHelpers::createDivider();
  layout->addWidget(tetheringPasswordDivider);

  // Tethering Password Button
  tetheringPasswordBtn = new BPCommandControl(
    tr("Tethering Password"),
    tr("Change the password for the Wi-Fi hotspot"),
    tr("CHANGE"),
    "tethering_password",  // dummy command ID
    "",  // no action
    QJsonObject(),  // no action data
    "",  // no working dir
    false,  // no confirm
    "", "", "",  // no confirm text
    QJsonArray(),  // no action buttons
    this
  );
  connect(tetheringPasswordBtn, &BPCommandControl::commandRequested, this, [this]() {
    wifiList->changeTetheringPassword();
  });
  tetheringPasswordBtn->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(tetheringPasswordBtn);

  mainLayout->addWidget(tetheringGroup);
}

void BPNetworkPanel::createCellularGroup() {
  cellularGroup = createStyledGroupBox(tr("Cellular Settings"));
  QVBoxLayout *layout = new QVBoxLayout(cellularGroup);
  layout->setSpacing(10);
  layout->setContentsMargins(10, 10, 10, 10);

  // GSM Roaming Toggle
  gsmRoamingToggle = new BPToggleControl(
    "GsmRoaming",
    tr("Enable Roaming"),
    tr("Allow cellular data when roaming"),
    this
  );
  connect(gsmRoamingToggle, &BPToggleControl::toggleFlipped, this, &BPNetworkPanel::onGsmRoamingToggled);
  gsmRoamingToggle->setStyleSheet("BPToggleControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(gsmRoamingToggle);

  // Divider
  layout->addWidget(BPUIHelpers::createDivider());

  // GSM Metered Toggle
  gsmMeteredToggle = new BPToggleControl(
    "GsmMetered",
    tr("Cellular Metered"),
    tr("Prevent large uploads on metered connection"),
    this
  );
  connect(gsmMeteredToggle, &BPToggleControl::toggleFlipped, this, &BPNetworkPanel::onGsmMeteredToggled);
  gsmMeteredToggle->setStyleSheet("BPToggleControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(gsmMeteredToggle);

  // Divider
  layout->addWidget(BPUIHelpers::createDivider());

  // APN Setting Button
  apnSettingBtn = new BPCommandControl(
    tr("APN Setting"),
    tr("Configure Access Point Name for cellular data"),
    tr("EDIT"),
    "apn_setting",  // dummy command ID
    "",  // no action
    QJsonObject(),  // no action data
    "",  // no working dir
    false,  // no confirm
    "", "", "",  // no confirm text
    QJsonArray(),  // no action buttons
    this
  );
  connect(apnSettingBtn, &BPCommandControl::commandRequested, this, [this]() {
    wifiList->editApn();
  });
  apnSettingBtn->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(apnSettingBtn);

  mainLayout->addWidget(cellularGroup);

  // Initially hide cellular group - will be shown by setPrimeType() if applicable
  cellularGroup->setVisible(false);
}

void BPNetworkPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  // Defer all WiFi operations to avoid blocking UI thread during showEvent
  QTimer::singleShot(0, this, [this]() {
    // Start WiFi manager only when visible (stock panel pattern)
    wifi->start();

    // Update cellular visibility based on current prime type
    if (uiState() && uiState()->prime_state) {
      setPrimeType(uiState()->prime_state->currentType());
    } else {
      // Default: show on tici for unknown/none prime types
      if (cellularGroup) {
        cellularGroup->setVisible(Hardware::TICI());
      }
    }

    // Start refresh timer after initial setup
    refreshTimer->start(2000);  // Refresh every 2 seconds

    // Initial refresh (deferred)
    QTimer::singleShot(500, this, &BPNetworkPanel::refreshAll);
  });
}

void BPNetworkPanel::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);

  // Stop WiFi manager when hidden (stock panel pattern)
  wifi->stop();

  // Stop refresh timer
  refreshTimer->stop();
}

void BPNetworkPanel::refreshAll() {
  // Refresh toggles
  if (tetheringToggle) {
    tetheringToggle->refresh();
  }
  if (gsmRoamingToggle) {
    gsmRoamingToggle->refresh();
  }
  if (gsmMeteredToggle) {
    gsmMeteredToggle->refresh();
  }

  // Update IP address
  updateIpAddress();

  // Update tethering password button visibility
  updateTetheringPasswordVisibility();
}

void BPNetworkPanel::updateIpAddress() {
  if (!ipAddressLabel) return;

  QString ipAddress = wifi->ipv4_address;
  if (ipAddress.isEmpty()) {
    ipAddressLabel->setText(tr("IP: Not Connected"));
  } else {
    ipAddressLabel->setText(tr("IP: ") + ipAddress);
  }
}

void BPNetworkPanel::updateTetheringPasswordVisibility() {
  bool tetheringEnabled = params.getBool("EnableTethering");
  if (tetheringPasswordBtn) {
    tetheringPasswordBtn->setVisible(tetheringEnabled);
  }
  if (tetheringPasswordDivider) {
    tetheringPasswordDivider->setVisible(tetheringEnabled);
  }
}

void BPNetworkPanel::onTetheringToggled(bool enabled) {
  wifi->setTetheringEnabled(enabled);
  updateTetheringPasswordVisibility();
}

void BPNetworkPanel::onGsmRoamingToggled(bool enabled) {
  wifi->updateGsmSettings(
    enabled,
    QString::fromStdString(params.get("GsmApn")),
    params.getBool("GsmMetered")
  );
}

void BPNetworkPanel::onGsmMeteredToggled(bool enabled) {
  wifi->updateGsmSettings(
    params.getBool("GsmRoaming"),
    QString::fromStdString(params.get("GsmApn")),
    enabled
  );
}

void BPNetworkPanel::setPrimeType(PrimeState::Type type) {
  // Show cellular settings for Prime Lite and devices without Prime
  bool showCellular = (type == PrimeState::PRIME_TYPE_NONE ||
                       type == PrimeState::PRIME_TYPE_UNKNOWN ||
                       type == PrimeState::PRIME_TYPE_PURPLE ||
                       type == PrimeState::PRIME_TYPE_LITE);

  if (cellularGroup) {
    cellularGroup->setVisible(showCellular && Hardware::TICI());
  }
}

