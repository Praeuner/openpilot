// selfdrive/ui/bluepilot/qt/offroad/panels/bp_software_panel.cc

#include "bp_software_panel.h"

#include <QScrollArea>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QEventLoop>
#include <QTableWidget>
#include <QHeaderView>
#include <QScroller>
#include <QScrollerProperties>

#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/widgets/input.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_dialogs.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_recent_changes.h"
#include "selfdrive/ui/bluepilot/qt/offroad/software/bp_git_manager.h"
#include "selfdrive/ui/bluepilot/bp_logging.h"
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
  // Initialize git manager
  gitManager = new BPGitManager(this);

  // Setup UI with tabs
  setupUI();

  // Setup ParamWatcher for reactive updates (like stock SoftwarePanel)
  fs_watch = new ParamWatcher(this);
  QObject::connect(fs_watch, &ParamWatcher::paramChanged, [=](const QString &param_name, const QString &param_value) {
    updateLabels();
  });

  // Connect to offroad transition
  connect(uiState(), &UIState::offroadTransition, this, [this](bool offroad) {
    is_onroad = !offroad;
    updateLabels();
    updateDisableUpdatesToggle(offroad);
  });

  // Timer for repo status updates (Advanced tab)
  repoStatusTimer = new QTimer(this);
  connect(repoStatusTimer, &QTimer::timeout, this, &BPSoftwarePanel::updateRepoStatus);
}

void BPSoftwarePanel::setupUI() {
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(40, 20, 40, 20);
  mainLayout->setSpacing(20);

  // Create all groups in order
  createVersionInfoGroup();
  createUpdateControlsGroup();
  createBranchSelectionGroup();
  createRepoStatusGroup();
  createGitOperationsGroup();
  createSystemGroup();
  createAdvancedWarning();

  // Add all groups to main layout
  mainLayout->addWidget(versionInfoGroup);
  mainLayout->addWidget(updateControlsGroup);
  mainLayout->addWidget(branchSelectionGroup);
  mainLayout->addWidget(repoStatusGroup);
  mainLayout->addWidget(gitOperationsGroup);
  mainLayout->addWidget(systemGroup);
  mainLayout->addWidget(advancedWarningLabel);
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

  // Recent Changes button (using BPCommandControl style)
  recentChangesBtn = new BPCommandControl(
    tr("Recent Changes"),
    tr("View the latest changes and updates to BluePilot"),
    tr("VIEW"),
    "recent_changes",
    "",
    QJsonObject(),
    "",
    false,
    "", "", "",
    QJsonArray(),
    this
  );
  connect(recentChangesBtn, &BPCommandControl::commandRequested, this, &BPSoftwarePanel::onRecentChangesClicked);
  recentChangesBtn->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(recentChangesBtn);
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

void BPSoftwarePanel::createSystemGroup() {
  systemGroup = createStyledGroupBox(tr("System"));
  QVBoxLayout *layout = new QVBoxLayout(systemGroup);
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
}

void BPSoftwarePanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  // Defer initialization to avoid blocking UI thread
  QTimer::singleShot(0, this, [this]() {
    // Update initial state
    is_onroad = uiState()->scene.started;

    // Initial update
    updateLabels();
    updateDisableUpdatesToggle(!is_onroad);

    // Start repo status updates
    updateRepoStatus();
    repoStatusTimer->start(5000);  // Update every 5 seconds

    // Nice for testing on PC
    installBtn->setEnabled(true);
  });
}

void BPSoftwarePanel::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);

  // Stop repo status updates when panel is hidden
  repoStatusTimer->stop();
}

void BPSoftwarePanel::updateLabels() {
  // Add params back in case the files got removed (like stock panel)
  fs_watch->addParam("LastUpdateTime");
  fs_watch->addParam("UpdateFailedCount");
  fs_watch->addParam("UpdaterState");
  fs_watch->addParam("UpdateAvailable");

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

// ========== Advanced Tab Methods (Stubs for now) ==========

void BPSoftwarePanel::createRepoStatusGroup() {
  repoStatusGroup = createStyledGroupBox(tr("Repository Status"));
  QVBoxLayout *layout = new QVBoxLayout(repoStatusGroup);
  layout->setSpacing(15);
  layout->setContentsMargins(25, 25, 25, 25);

  // Branch name with color
  repoBranchLabel = new QLabel(tr("Loading..."), this);
  repoBranchLabel->setStyleSheet("QLabel { color: #4CAF50; font-size: 40px; font-weight: 600; }");
  layout->addWidget(repoBranchLabel);

  // Commit message
  repoCommitLabel = new QLabel("", this);
  repoCommitLabel->setStyleSheet("QLabel { color: #E0E0E0; font-size: 34px; }");
  repoCommitLabel->setWordWrap(true);
  layout->addWidget(repoCommitLabel);

  // Timestamp and hash on same line
  QWidget *metadataWidget = new QWidget(this);
  QHBoxLayout *metadataLayout = new QHBoxLayout(metadataWidget);
  metadataLayout->setContentsMargins(0, 0, 0, 0);
  metadataLayout->setSpacing(20);

  repoTimestampLabel = new QLabel("", this);
  repoTimestampLabel->setStyleSheet("QLabel { color: #999999; font-size: 30px; }");
  metadataLayout->addWidget(repoTimestampLabel);

  repoHashLabel = new QLabel("", this);
  repoHashLabel->setStyleSheet("QLabel { color: #2196F3; font-size: 30px; font-family: monospace; }");
  metadataLayout->addWidget(repoHashLabel);

  metadataLayout->addStretch();
  layout->addWidget(metadataWidget);

  // Status with color
  repoStatusLabel = new QLabel("", this);
  repoStatusLabel->setStyleSheet("QLabel { color: #FFA726; font-size: 32px; font-weight: 500; }");
  layout->addWidget(repoStatusLabel);
}

void BPSoftwarePanel::createGitOperationsGroup() {
  gitOperationsGroup = createStyledGroupBox(tr("Git Operations"));
  QVBoxLayout *layout = new QVBoxLayout(gitOperationsGroup);
  layout->setSpacing(15);
  layout->setContentsMargins(10, 10, 10, 10);

  // Advanced badge helper - adds orange "ADVANCED" badge to the right
  auto addAdvancedBadge = [](BPCommandControl *control) {
    // Create a container widget to hold both the control and the badge
    QWidget *container = new QWidget();
    QHBoxLayout *hLayout = new QHBoxLayout(container);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(15);

    hLayout->addWidget(control, 1);  // Control takes up most space

    // Advanced badge
    QLabel *badge = new QLabel("ADVANCED");
    badge->setStyleSheet(R"(
      QLabel {
        background-color: #FF6B00;
        color: white;
        font-size: 26px;
        font-weight: 700;
        padding: 8px 20px;
        border-radius: 6px;
        letter-spacing: 1px;
      }
    )");
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedHeight(50);
    hLayout->addWidget(badge);

    return container;
  };

  // Manual Update button
  manualUpdateBtn = new BPCommandControl(
    tr("Manual Update"),
    tr("Bypass update daemon and perform direct git pull + build"),
    tr("UPDATE"),
    "manual_update",
    "",
    QJsonObject(),
    "",
    false,
    "", "", "",
    QJsonArray(),
    this
  );
  connect(manualUpdateBtn, &BPCommandControl::commandRequested, this, &BPSoftwarePanel::onManualUpdateClicked);
  manualUpdateBtn->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(addAdvancedBadge(manualUpdateBtn));

  // Repair button
  repairBtn = new BPCommandControl(
    tr("Repair Repository"),
    tr("Clean and reset the repository to a known good state"),
    tr("REPAIR"),
    "repair",
    "",
    QJsonObject(),
    "",
    false,
    "", "", "",
    QJsonArray(),
    this
  );
  connect(repairBtn, &BPCommandControl::commandRequested, this, &BPSoftwarePanel::onRepairClicked);
  repairBtn->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(addAdvancedBadge(repairBtn));

  // Reset button
  resetBtn = new BPCommandControl(
    tr("Reset Changes"),
    tr("Discard all local changes and reset to HEAD"),
    tr("RESET"),
    "reset",
    "",
    QJsonObject(),
    "",
    false,
    "", "", "",
    QJsonArray(),
    this
  );
  connect(resetBtn, &BPCommandControl::commandRequested, this, &BPSoftwarePanel::onResetClicked);
  resetBtn->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(addAdvancedBadge(resetBtn));

  // History button
  historyBtn = new BPCommandControl(
    tr("View Commit History"),
    tr("Show the last 30 commits for this branch"),
    tr("HISTORY"),
    "history",
    "",
    QJsonObject(),
    "",
    false,
    "", "", "",
    QJsonArray(),
    this
  );
  connect(historyBtn, &BPCommandControl::commandRequested, this, &BPSoftwarePanel::onHistoryClicked);
  historyBtn->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(addAdvancedBadge(historyBtn));
}

void BPSoftwarePanel::createAdvancedWarning() {
  QLabel *warningLabel = new QLabel(tr("WARNING: Advanced operations bypass the update daemon and are for experienced users only."), this);
  warningLabel->setStyleSheet(R"(
    QLabel {
      color: #FFA500;
      font-size: 30px;
      font-weight: 500;
      padding: 15px 20px;
      background-color: rgba(255, 165, 0, 0.15);
      border-radius: 8px;
      border-left: 4px solid #FFA500;
    }
  )");
  warningLabel->setWordWrap(true);
  warningLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  advancedWarningLabel = warningLabel;
}

void BPSoftwarePanel::updateRepoStatus() {
  // Update repo status asynchronously using gitManager
  gitManager->getRepoStatus([this](const BPGitManager::RepoStatus &status) {
    if (!status.isValid) {
      repoBranchLabel->setText(tr("Error: %1").arg(status.error));
      repoCommitLabel->setText("");
      repoTimestampLabel->setText("");
      repoHashLabel->setText("");
      repoStatusLabel->setText("");
      return;
    }

    // Update branch (green, bold)
    repoBranchLabel->setText(status.branch);

    // Update commit message
    repoCommitLabel->setText(status.commitMessage.isEmpty() ? tr("No commit message") : status.commitMessage);

    // Update timestamp and hash on same line
    repoTimestampLabel->setText(status.commitDate.isEmpty() ? tr("Unknown time") : status.commitDate);
    repoHashLabel->setText(status.commit);

    // Update status with color coding
    QStringList statusParts;
    QString statusColor = "#4CAF50"; // Green for clean

    if (status.hasLocalChanges) {
      statusParts << tr("Modified");
      statusColor = "#FF9800"; // Orange for modified
    } else {
      statusParts << tr("Clean");
    }

    if (status.hasUpdatesAvailable) {
      statusParts << tr("Updates Available");
      statusColor = "#2196F3"; // Blue for updates
    }

    repoStatusLabel->setText(tr("Status: %1").arg(statusParts.join(", ")));
    repoStatusLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 32px; font-weight: 500; }").arg(statusColor));
  });
}


void BPSoftwarePanel::onRecentChangesClicked() {
  BPRecentChangesDialog *dialog = new BPRecentChangesDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->exec();
}

void BPSoftwarePanel::onManualUpdateClicked() {
  if (commandInProgress) {
    showBPAlert(tr("A command is already running. Please wait."), this);
    return;
  }

  // Check for uncommitted changes
  gitManager->hasUncommittedChanges([this](bool hasChanges) {
    QString message;
    if (hasChanges) {
      message = tr("You have uncommitted changes that will be lost. Continue with update?");
    } else {
      message = tr("This will pull the latest changes and rebuild. Continue?");
    }

    if (showBPConfirmation(tr("Manual Update"), message, tr("Update"), tr("Cancel"), this)) {
      manualUpdate();
    }
  });
}

void BPSoftwarePanel::onRepairClicked() {
  if (commandInProgress) {
    showBPAlert(tr("A command is already running. Please wait."), this);
    return;
  }

  if (showBPConfirmation(
        tr("Repair Repository"),
        tr("This will reset and clean the repository. All local changes will be lost. Continue?"),
        tr("Repair"),
        tr("Cancel"),
        this)) {
    repairRepository();
  }
}

void BPSoftwarePanel::onResetClicked() {
  if (commandInProgress) {
    showBPAlert(tr("A command is already running. Please wait."), this);
    return;
  }

  if (showBPConfirmation(
        tr("Reset Changes"),
        tr("This will discard all uncommitted changes. This cannot be undone. Continue?"),
        tr("Reset"),
        tr("Cancel"),
        this)) {
    resetRepository();
  }
}

void BPSoftwarePanel::onHistoryClicked() {
  viewHistory();
}

void BPSoftwarePanel::manualUpdate() {
  commandInProgress = true;

  // Simple implementation for now - show progress dialog
  showBPAlert(
    tr("Manual update functionality will be available in a future update. "
       "For now, please use the Updates tab for software updates."),
    this
  );

  commandInProgress = false;
  updateRepoStatus();
}

void BPSoftwarePanel::repairRepository() {
  commandInProgress = true;

  // Run git reset and clean
  QtConcurrent::run([this]() {
    auto result = BPGitManager::executeCommand("git reset --hard HEAD && git clean -xdff", "", 60000);

    QMetaObject::invokeMethod(this, [this, result]() {
      commandInProgress = false;

      if (result.success) {
        showBPAlert(tr("Repository repaired successfully!"), this);
      } else {
        showBPAlert(
          tr("Repair failed: %1").arg(result.error.isEmpty() ? result.output : result.error),
          this
        );
      }

      updateRepoStatus();
    }, Qt::QueuedConnection);
  });
}

void BPSoftwarePanel::resetRepository() {
  commandInProgress = true;

  // Run git reset
  QtConcurrent::run([this]() {
    auto result = BPGitManager::executeCommand("git reset --hard HEAD", "", 30000);

    QMetaObject::invokeMethod(this, [this, result]() {
      commandInProgress = false;

      if (result.success) {
        showBPAlert(tr("Changes reset successfully!"), this);
      } else {
        showBPAlert(
          tr("Reset failed: %1").arg(result.error.isEmpty() ? result.output : result.error),
          this
        );
      }

      updateRepoStatus();
    }, Qt::QueuedConnection);
  });
}

void BPSoftwarePanel::viewHistory() {
  gitManager->getRepoStatus([this](const BPGitManager::RepoStatus &status) {
    QString title = tr("%1 - Last 30 Commits").arg(status.branch);
    showCommitHistory(title, BPGitManager::getGitRoot());
  });
}

void BPSoftwarePanel::showCommitHistory(const QString &title, const QString &workingDir) {
  QDialog *dialog = new QDialog(this);
  dialog->setWindowTitle(title);
  dialog->setModal(true);
  dialog->setAttribute(Qt::WA_DeleteOnClose);

  QVBoxLayout *layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(30, 30, 30, 30);
  layout->setSpacing(20);

  // Add title
  QLabel *titleLabel = new QLabel(title);
  titleLabel->setStyleSheet("QLabel { font-size: 45px; font-weight: 600; color: #FFFFFF; }");
  layout->addWidget(titleLabel);

  // Create scroll area
  QScrollArea *scrollArea = new QScrollArea(dialog);
  scrollArea->setWidgetResizable(true);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setStyleSheet(R"(
    QScrollArea { border: none; background-color: #1B1B1B; }
    QScrollBar:vertical {
      width: 10px;
      background: #1e1e1e;
      margin: 0px;
    }
    QScrollBar::handle:vertical {
      min-height: 30px;
      border-radius: 5px;
      background: #465BEA;
    }
  )");

  QWidget *scrollContent = new QWidget(scrollArea);
  QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);
  scrollLayout->setContentsMargins(0, 0, 0, 0);

  // Create table
  QTableWidget *table = new QTableWidget(scrollContent);
  table->setColumnCount(4);
  table->setHorizontalHeaderLabels({tr("Hash"), tr("Description"), tr("Time"), tr("Actions")});
  table->setShowGrid(false);
  table->setSelectionMode(QAbstractItemView::NoSelection);
  table->setFocusPolicy(Qt::NoFocus);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->horizontalHeader()->setStretchLastSection(false);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table->verticalHeader()->hide();
  table->verticalHeader()->setDefaultSectionSize(90);
  table->setAlternatingRowColors(true);
  table->setWordWrap(true);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
  table->setColumnWidth(0, 180);
  table->setColumnWidth(2, 300);
  table->setColumnWidth(3, 220);
  table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // Enable touch scrolling
  QScroller::grabGesture(table->viewport(), QScroller::LeftMouseButtonGesture);
  QScroller::grabGesture(scrollArea->viewport(), QScroller::LeftMouseButtonGesture);

  table->setStyleSheet(R"(
    QTableWidget {
      font-family: Inter, sans-serif;
      font-size: 32px;
      padding: 10px;
      background-color: #1B1B1B;
      color: #E0E0E0;
      border: none;
      alternate-background-color: #232323;
    }
    QHeaderView::section {
      background-color: #2D2D2D;
      color: #FFFFFF;
      padding: 12px;
      border: none;
      font-weight: 600;
      font-size: 34px;
    }
    QTableWidget::item {
      padding: 15px;
      border-right: 1px solid #404040;
    }
  )");

  // Fetch commits asynchronously
  QtConcurrent::run([dialog, table, workingDir, this]() {
    auto result = BPGitManager::executeCommand(
      "git log --all -n 30 --pretty=format:'%h|||%s|||%cr'",
      workingDir,
      10000
    );

    QMetaObject::invokeMethod(dialog, [dialog, table, result, workingDir, this]() {
      if (!result.success) {
        BPLog::bpError() << "[bp.software.panel] showCommitHistory | Git log failed: "
                         << result.error.toStdString() << std::endl;
        return;
      }

      QStringList commits = result.output.split("\n", QString::SkipEmptyParts);
      table->setRowCount(commits.size());

      for (int i = 0; i < commits.size(); ++i) {
        QStringList parts = commits[i].split("|||");
        if (parts.size() == 3) {
          auto createItem = [](const QString &text, Qt::Alignment alignment) {
            QTableWidgetItem *item = new QTableWidgetItem(text);
            item->setTextAlignment(alignment);
            return item;
          };

          table->setItem(i, 0, createItem(parts[0], Qt::AlignLeft | Qt::AlignVCenter));
          table->setItem(i, 1, createItem(parts[1], Qt::AlignLeft | Qt::AlignVCenter));
          table->setItem(i, 2, createItem(parts[2], Qt::AlignLeft | Qt::AlignVCenter));

          // Add checkout button
          QWidget *buttonContainer = new QWidget();
          QHBoxLayout *buttonLayout = new QHBoxLayout(buttonContainer);
          buttonLayout->setContentsMargins(5, 0, 5, 0);
          buttonLayout->setSpacing(5);

          QPushButton *checkoutButton = new QPushButton(tr("Checkout"));
          checkoutButton->setStyleSheet(R"(
            QPushButton {
              border-radius: 8px;
              font-size: 28px;
              padding: 10px 15px;
              background-color: #465BEA;
              color: white;
              min-width: 140px;
              min-height: 50px;
            }
            QPushButton:pressed { background-color: #3049F4; }
          )");

          QString commitHash = parts[0];
          connect(checkoutButton, &QPushButton::clicked, [dialog, commitHash, workingDir, this]() {
            if (showBPConfirmation(
                  tr("Checkout Commit"),
                  tr("Checkout commit %1?\n\nThis will discard all local changes.").arg(commitHash),
                  tr("Checkout"),
                  tr("Cancel"),
                  dialog)) {

              dialog->accept();

              QtConcurrent::run([commitHash, workingDir, this]() {
                QString command = QString("git checkout %1 -f && git reset --hard && git clean -fd").arg(commitHash);
                auto result = BPGitManager::executeCommand(command, workingDir, 60000);

                QMetaObject::invokeMethod(this, [result, this]() {
                  if (result.success) {
                    showBPAlert(tr("Checkout successful! Please restart the UI."), this);
                  } else {
                    showBPAlert(tr("Checkout failed: %1").arg(result.error), this);
                  }
                  updateRepoStatus();
                }, Qt::QueuedConnection);
              });
            }
          });

          buttonLayout->addWidget(checkoutButton);
          table->setCellWidget(i, 3, buttonContainer);
        }
      }
    }, Qt::QueuedConnection);
  });

  scrollLayout->addWidget(table);
  scrollArea->setWidget(scrollContent);
  layout->addWidget(scrollArea);

  // Close button
  QPushButton *closeButton = new QPushButton(tr("Close"), dialog);
  closeButton->setStyleSheet(R"(
    QPushButton {
      border-radius: 10px;
      font-size: 40px;
      padding: 15px;
      background-color: #465BEA;
      color: white;
      min-height: 70px;
    }
    QPushButton:pressed { background-color: #3049F4; }
  )");
  connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
  layout->addWidget(closeButton);

  // Setup fullscreen
  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen) {
    dialog->setFixedSize(2160, 1080);
  }

  dialog->exec();
}

void BPSoftwarePanel::showRecentChanges() {
  onRecentChangesClicked();
}
