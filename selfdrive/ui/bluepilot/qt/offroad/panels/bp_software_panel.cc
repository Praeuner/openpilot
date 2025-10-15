// selfdrive/ui/bluepilot/qt/offroad/panels/bp_software_panel.cc

#include "bp_software_panel.h"

#include <QScrollArea>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QEventLoop>
#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/widgets/input.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_dialogs.h"
#include "selfdrive/ui/sunnypilot/ui.h"
#include "selfdrive/ui/sunnypilot/qt/util.h"
#include "system/hardware/hw.h"

// Helper function for synchronous confirmation dialogs
static bool showBPConfirmation(const QString &title, const QString &message, const QString &confirmText, const QString &cancelText, QWidget *parent) {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = title;
  config.prompt = message;
  config.confirmText = confirmText;
  config.cancelText = cancelText;
  config.richText = true;

  auto *dialog = BPConfirmationDialog::showConfirmation(config, parent);

  bool result = false;
  QEventLoop loop;
  QObject::connect(dialog, &BPConfirmationDialog::confirmed, [&](bool accepted) {
    result = accepted;
    loop.quit();
  });
  loop.exec();

  return result;
}

static void showBPAlert(const QString &message, QWidget *parent) {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = "Alert";
  config.prompt = message;
  config.confirmText = "OK";
  config.cancelText = "";

  BPConfirmationDialog::showMessage(config, parent);
}

BPSoftwarePanel::BPSoftwarePanel(QWidget *parent) : QWidget(parent) {
  setupUI();

  // Refresh timer for periodic updates (only active when visible)
  refreshTimer = new QTimer(this);
  connect(refreshTimer, &QTimer::timeout, this, &BPSoftwarePanel::refreshAll);

  // Connect to offroad transition
  connect(uiState(), &UIState::offroadTransition, this, [this](bool offroad) {
    is_onroad = !offroad;
    updateLabels();
    updateDisableUpdatesToggle(offroad);
  });
}

void BPSoftwarePanel::setupUI() {
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(40, 40, 40, 40);
  mainLayout->setSpacing(30);

  // Create all groups
  createVersionInfoGroup();
  createUpdateControlsGroup();
  createBranchSelectionGroup();
  createAdvancedGroup();

  mainLayout->addStretch();

  setStyleSheet(R"(
    BPSoftwarePanel {
      background-color: #1B1B1B;
    }
    BPSoftwarePanel QGroupBox BPCommandControl,
    BPSoftwarePanel QGroupBox BPToggleControl {
      background-color: transparent;
    }
  )");
}

QGroupBox* BPSoftwarePanel::createStyledGroupBox(const QString &title) {
  QGroupBox *group = new QGroupBox(title, this);
  group->setStyleSheet(R"(
    QGroupBox {
      background-color: #242424;
      border: none;
      border-radius: 40px;
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

void BPSoftwarePanel::createVersionInfoGroup() {
  versionInfoGroup = createStyledGroupBox(tr("Version Information"));
  QVBoxLayout *layout = new QVBoxLayout(versionInfoGroup);
  layout->setSpacing(20);
  layout->setContentsMargins(25, 25, 25, 25);

  // Onroad message
  onroadLabel = new QLabel(tr("Updates are only downloaded while the car is off."), this);
  onroadLabel->setStyleSheet(R"(
    QLabel {
      color: #FFD700;
      font-size: 38px;
      font-weight: 400;
      padding: 15px;
      background-color: rgba(255, 215, 0, 0.1);
      border-radius: 10px;
    }
  )");
  onroadLabel->setWordWrap(true);
  onroadLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(onroadLabel);

  // Current version info
  QWidget *currentVersionWidget = new QWidget(this);
  QVBoxLayout *currentLayout = new QVBoxLayout(currentVersionWidget);
  currentLayout->setSpacing(10);
  currentLayout->setContentsMargins(0, 0, 0, 0);

  currentVersionLabel = new QLabel(tr("Current Version"), this);
  currentVersionLabel->setStyleSheet(R"(
    QLabel {
      color: #FFFFFF;
      font-size: 42px;
      font-weight: 600;
    }
  )");
  currentLayout->addWidget(currentVersionLabel);

  currentVersionDesc = new QLabel("", this);
  currentVersionDesc->setStyleSheet(R"(
    QLabel {
      color: #AAAAAA;
      font-size: 32px;
      font-weight: 400;
    }
  )");
  currentVersionDesc->setWordWrap(true);
  currentLayout->addWidget(currentVersionDesc);

  layout->addWidget(currentVersionWidget);

  // New version info (initially hidden)
  QWidget *newVersionWidget = new QWidget(this);
  QVBoxLayout *newLayout = new QVBoxLayout(newVersionWidget);
  newLayout->setSpacing(10);
  newLayout->setContentsMargins(0, 0, 0, 0);

  newVersionLabel = new QLabel(tr("New Version Available"), this);
  newVersionLabel->setStyleSheet(R"(
    QLabel {
      color: #4CAF50;
      font-size: 42px;
      font-weight: 600;
    }
  )");
  newLayout->addWidget(newVersionLabel);

  newVersionDesc = new QLabel("", this);
  newVersionDesc->setStyleSheet(R"(
    QLabel {
      color: #AAAAAA;
      font-size: 32px;
      font-weight: 400;
    }
  )");
  newVersionDesc->setWordWrap(true);
  newLayout->addWidget(newVersionDesc);

  layout->addWidget(newVersionWidget);
  newVersionWidget->setVisible(false);

  mainLayout->addWidget(versionInfoGroup);
}

void BPSoftwarePanel::createUpdateControlsGroup() {
  updateControlsGroup = createStyledGroupBox(tr("Update Controls"));
  QVBoxLayout *layout = new QVBoxLayout(updateControlsGroup);
  layout->setSpacing(20);
  layout->setContentsMargins(25, 25, 25, 25);

  // Download/Check button with status
  QWidget *downloadWidget = new QWidget(this);
  QHBoxLayout *downloadLayout = new QHBoxLayout(downloadWidget);
  downloadLayout->setSpacing(20);
  downloadLayout->setContentsMargins(0, 0, 0, 0);

  downloadBtn = new BPButton(tr("CHECK"), this);
  downloadBtn->setMinimumWidth(250);
  downloadBtn->setMinimumHeight(100);
  downloadBtn->setStyleSheet(R"(
    BPButton {
      background-color: #2196F3;
      border-radius: 40px;
      font-size: 42px;
      font-weight: 600;
    }
    BPButton:hover {
      background-color: #1E88E5;
    }
    BPButton:pressed {
      background-color: #1976D2;
    }
    BPButton:disabled {
      background-color: #424242;
      color: #888888;
    }
  )");
  connect(downloadBtn, &QPushButton::clicked, this, &BPSoftwarePanel::onDownloadClicked);
  downloadLayout->addWidget(downloadBtn);

  QVBoxLayout *downloadStatusLayout = new QVBoxLayout();
  downloadStatusLayout->setSpacing(5);

  QLabel *downloadTitle = new QLabel(tr("Download Update"), this);
  downloadTitle->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  downloadStatusLayout->addWidget(downloadTitle);

  downloadStatusLabel = new QLabel(tr("Check for available updates"), this);
  downloadStatusLabel->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  downloadStatusLabel->setWordWrap(true);
  downloadStatusLayout->addWidget(downloadStatusLabel);

  downloadLayout->addLayout(downloadStatusLayout, 1);
  layout->addWidget(downloadWidget);

  // Install button with status
  QWidget *installWidget = new QWidget(this);
  QHBoxLayout *installLayout = new QHBoxLayout(installWidget);
  installLayout->setSpacing(20);
  installLayout->setContentsMargins(0, 0, 0, 0);

  installBtn = new BPButton(tr("INSTALL"), this);
  installBtn->setMinimumWidth(250);
  installBtn->setMinimumHeight(100);
  installBtn->setStyleSheet(R"(
    BPButton {
      background-color: #4CAF50;
      border-radius: 40px;
      font-size: 42px;
      font-weight: 600;
    }
    BPButton:hover {
      background-color: #45A049;
    }
    BPButton:pressed {
      background-color: #388E3C;
    }
    BPButton:disabled {
      background-color: #424242;
      color: #888888;
    }
  )");
  connect(installBtn, &QPushButton::clicked, this, &BPSoftwarePanel::onInstallClicked);
  installLayout->addWidget(installBtn);

  QVBoxLayout *installStatusLayout = new QVBoxLayout();
  installStatusLayout->setSpacing(5);

  QLabel *installTitle = new QLabel(tr("Install Update"), this);
  installTitle->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  installStatusLayout->addWidget(installTitle);

  installStatusLabel = new QLabel(tr("Install the downloaded update and reboot"), this);
  installStatusLabel->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  installStatusLabel->setWordWrap(true);
  installStatusLayout->addWidget(installStatusLabel);

  installLayout->addLayout(installStatusLayout, 1);
  layout->addWidget(installWidget);

  mainLayout->addWidget(updateControlsGroup);
}

void BPSoftwarePanel::createBranchSelectionGroup() {
  branchSelectionGroup = createStyledGroupBox(tr("Branch Selection"));
  QVBoxLayout *layout = new QVBoxLayout(branchSelectionGroup);
  layout->setSpacing(20);
  layout->setContentsMargins(25, 25, 25, 25);

  // Branch selector button with status
  QWidget *branchWidget = new QWidget(this);
  QHBoxLayout *branchLayout = new QHBoxLayout(branchWidget);
  branchLayout->setSpacing(20);
  branchLayout->setContentsMargins(0, 0, 0, 0);

  branchBtn = new BPButton(tr("SELECT"), this);
  branchBtn->setMinimumWidth(250);
  branchBtn->setMinimumHeight(100);
  branchBtn->setStyleSheet(R"(
    BPButton {
      background-color: #FF9800;
      border-radius: 40px;
      font-size: 42px;
      font-weight: 600;
    }
    BPButton:hover {
      background-color: #FB8C00;
    }
    BPButton:pressed {
      background-color: #F57C00;
    }
  )");
  connect(branchBtn, &QPushButton::clicked, this, &BPSoftwarePanel::onBranchClicked);
  branchLayout->addWidget(branchBtn);

  QVBoxLayout *branchStatusLayout = new QVBoxLayout();
  branchStatusLayout->setSpacing(5);

  QLabel *branchTitle = new QLabel(tr("Target Branch"), this);
  branchTitle->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  branchStatusLayout->addWidget(branchTitle);

  branchStatusLabel = new QLabel(tr("Select the branch to update from"), this);
  branchStatusLabel->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  branchStatusLabel->setWordWrap(true);
  branchStatusLayout->addWidget(branchStatusLabel);

  branchLayout->addLayout(branchStatusLayout, 1);
  layout->addWidget(branchWidget);

  mainLayout->addWidget(branchSelectionGroup);
}

void BPSoftwarePanel::createAdvancedGroup() {
  advancedGroup = createStyledGroupBox(tr("Advanced"));
  QVBoxLayout *layout = new QVBoxLayout(advancedGroup);
  layout->setSpacing(15);
  layout->setContentsMargins(10, 10, 10, 10);

  // Disable updates toggle
  disableUpdatesToggle = new BPToggleControl(
    "DisableUpdates",
    tr("Disable Updates"),
    tr("When enabled, software updates will be disabled. This requires a reboot to take effect."),
    this
  );
  connect(disableUpdatesToggle, &BPToggleControl::toggleFlipped, this, &BPSoftwarePanel::onDisableUpdatesToggled);
  disableUpdatesToggle->setStyleSheet("BPToggleControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(disableUpdatesToggle);

  // Uninstall button
  uninstallBtn = new BPCommandControl(
    tr("Uninstall %1").arg(getBrand()),
    tr("Completely remove the software from this device"),
    tr("UNINSTALL"),
    "uninstall",  // dummy command ID
    "",  // no action
    QJsonObject(),  // no action data
    "",  // no working dir
    false,  // no confirm
    "", "", "",  // no confirm text
    QJsonArray(),  // no action buttons
    this
  );
  connect(uninstallBtn, &BPCommandControl::commandRequested, this, &BPSoftwarePanel::onUninstallClicked);
  uninstallBtn->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(uninstallBtn);

  mainLayout->addWidget(advancedGroup);
}

void BPSoftwarePanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  // Defer initialization to avoid blocking UI thread
  QTimer::singleShot(0, this, [this]() {
    // Update initial state
    is_onroad = uiState()->scene.started;

    // Start refresh timer
    refreshTimer->start(1000);  // Refresh every second

    // Initial update
    updateLabels();
    updateDisableUpdatesToggle(!is_onroad);
  });
}

void BPSoftwarePanel::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);

  // Stop refresh timer
  refreshTimer->stop();
}

void BPSoftwarePanel::refreshAll() {
  updateLabels();
}

void BPSoftwarePanel::updateLabels() {
  if (!isVisible()) {
    return;
  }

  updateVersionInfo();
  updateDownloadButton();
  updateInstallButton();
  updateBranchSelector();
}

void BPSoftwarePanel::updateVersionInfo() {
  // Show/hide onroad label
  onroadLabel->setVisible(is_onroad);

  // Update current version
  QString currentVersion = QString::fromStdString(params.get("UpdaterCurrentDescription"));
  QString currentNotes = QString::fromStdString(params.get("UpdaterCurrentReleaseNotes"));

  if (!currentVersion.isEmpty()) {
    currentVersionLabel->setText(tr("Current Version: %1").arg(currentVersion));
  } else {
    currentVersionLabel->setText(tr("Current Version"));
  }
  currentVersionDesc->setText(currentNotes);

  // Update new version (if available)
  bool updateAvailable = params.getBool("UpdateAvailable");
  QString newVersion = QString::fromStdString(params.get("UpdaterNewDescription"));
  QString newNotes = QString::fromStdString(params.get("UpdaterNewReleaseNotes"));

  // Find the new version widget and show/hide it
  QWidget *newVersionWidget = newVersionLabel->parentWidget();
  if (newVersionWidget) {
    newVersionWidget->setVisible(!is_onroad && updateAvailable);
  }

  if (updateAvailable) {
    newVersionLabel->setText(tr("New Version: %1").arg(newVersion));
    newVersionDesc->setText(newNotes);
  }
}

void BPSoftwarePanel::updateDownloadButton() {
  // Update download button visibility and state
  downloadBtn->parentWidget()->setVisible(!is_onroad);

  updater_state = QString::fromStdString(params.get("UpdaterState"));
  bool failed = std::atoi(params.get("UpdateFailedCount").c_str()) > 0;

  if (updater_state != "idle") {
    downloadBtn->setEnabled(false);
    downloadStatusLabel->setText(updater_state);
  } else {
    if (failed) {
      downloadBtn->setText(tr("CHECK"));
      downloadStatusLabel->setText(tr("Failed to check for update"));
    } else if (params.getBool("UpdaterFetchAvailable")) {
      downloadBtn->setText(tr("DOWNLOAD"));
      downloadStatusLabel->setText(tr("Update available"));
    } else {
      QString lastUpdate = tr("never");
      auto tm = params.get("LastUpdateTime");
      if (!tm.empty()) {
        lastUpdate = timeAgo(QDateTime::fromString(QString::fromStdString(tm + "Z"), Qt::ISODate));
      }
      downloadBtn->setText(tr("CHECK"));
      downloadStatusLabel->setText(tr("Up to date, last checked %1").arg(lastUpdate));
    }
    downloadBtn->setEnabled(true);
  }
}

void BPSoftwarePanel::updateInstallButton() {
  bool updateAvailable = params.getBool("UpdateAvailable");
  installBtn->parentWidget()->setVisible(!is_onroad && updateAvailable);

  if (updateAvailable) {
    QString newVersion = QString::fromStdString(params.get("UpdaterNewDescription"));
    installStatusLabel->setText(newVersion);
  }
}

void BPSoftwarePanel::updateBranchSelector() {
  QString targetBranch = QString::fromStdString(params.get("UpdaterTargetBranch"));
  branchStatusLabel->setText(targetBranch);
}

void BPSoftwarePanel::checkForUpdates() {
  std::system("pkill -SIGUSR1 -f system.updated.updated");
}

void BPSoftwarePanel::onDownloadClicked() {
  downloadBtn->setEnabled(false);
  if (downloadBtn->text() == tr("CHECK")) {
    checkForUpdates();
  } else {
    std::system("pkill -SIGHUP -f system.updated.updated");
  }
}

void BPSoftwarePanel::onInstallClicked() {
  installBtn->setEnabled(false);
  params.putBool("DoReboot", true);
}

void BPSoftwarePanel::onBranchClicked() {
  if (Hardware::get_device_type() == cereal::InitData::DeviceType::TICI) {
    // TICI: filtered branch list
    auto current = params.get("GitBranch");
    QStringList allBranches = QString::fromStdString(params.get("UpdaterAvailableBranches")).split(",");
    QStringList branches;

    for (const QString &b : allBranches) {
      if (b.endsWith("-tici")) {
        branches.append(b);
      }
    }

    for (QString b : {current.c_str(), "master-tici", "staging-tici", "release-tici"}) {
      auto i = branches.indexOf(b);
      if (i >= 0) {
        branches.removeAt(i);
        branches.insert(0, b);
      }
    }

    QString cur = QString::fromStdString(params.get("UpdaterTargetBranch"));
    QString selection = BPSelectionDialog::getSelection(tr("Select a branch"), branches, cur, this);
    if (!selection.isEmpty()) {
      params.put("UpdaterTargetBranch", selection.toStdString());
      updateBranchSelector();
      checkForUpdates();
    }
  } else {
    // Non-TICI: search dialog
    InputDialog d(tr("Search Branch"), this, tr("Enter search keywords, or leave blank to list all branches."), false);
    d.setMinLength(0);
    const int ret = d.exec();
    if (ret) {
      searchBranches(d.text());
    }
  }
}

void BPSoftwarePanel::searchBranches(const QString &query) {
  QStringList branches = QString::fromStdString(params.get("UpdaterAvailableBranches")).split(",");
  QStringList results = searchFromList(query, branches);
  results.sort();

  if (results.isEmpty()) {
    showBPAlert(tr("No branches found for keywords: %1").arg(query), this);
    return;
  }

  QString selected_branch = BPSelectionDialog::getSelection(tr("Select a branch"), results, "", this);

  if (!selected_branch.isEmpty()) {
    params.put("UpdaterTargetBranch", selected_branch.toStdString());
    updateBranchSelector();
    checkForUpdates();
  }
}

void BPSoftwarePanel::onUninstallClicked() {
  if (showBPConfirmation(tr("Uninstall"), tr("Are you sure you want to uninstall?"), tr("Uninstall"), tr("Cancel"), this)) {
    params.putBool("DoUninstall", true);
  }
}

void BPSoftwarePanel::onDisableUpdatesToggled(bool enabled) {
  if (showBPConfirmation(tr("Reboot Required"),
      tr("%1 updates requires a reboot.<br>Reboot now?").arg(enabled ? "Disabling" : "Enabling"),
      tr("Reboot"), tr("Cancel"), this)) {
    params.putBool("DoReboot", true);
  } else {
    params.putBool("DisableUpdates", !enabled);
    disableUpdatesToggle->refresh();
  }
}

void BPSoftwarePanel::updateDisableUpdatesToggle(bool offroad) {
  bool enabled = offroad;
  disableUpdatesToggle->setEnabled(enabled);

  if (enabled) {
    disableUpdatesToggle->setDescription(tr("When enabled, software updates will be disabled. This requires a reboot to take effect."));
  } else {
    disableUpdatesToggle->setDescription(tr("Please enable always offroad mode or turn off vehicle to adjust this toggle"));
  }
}
