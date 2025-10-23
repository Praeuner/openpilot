// selfdrive/ui/bluepilot/qt/offroad/panels/bp_software_panel.cc

#include "bp_software_panel.h"

#include <QScrollArea>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <QEventLoop>
#include <QTableWidget>
#include <QHeaderView>
#include <QScroller>
#include <QScrollerProperties>
#include <QTextEdit>
#include <QScrollBar>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <unistd.h>

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

// Helper functions for BP_CHANGES.json
static QJsonObject loadBPChangesJson() {
  QString gitRoot = BPGitManager::getGitRoot();
  QString changesPath = QDir(gitRoot).filePath("BP_CHANGES.json");

  QFile file(changesPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    BPLog::bpError() << "[bp.software.panel] loadBPChangesJson | Failed to open BP_CHANGES.json" << std::endl;
    return QJsonObject();
  }

  QByteArray data = file.readAll();
  QJsonDocument doc = QJsonDocument::fromJson(data);

  if (doc.isNull() || !doc.isObject()) {
    BPLog::bpError() << "[bp.software.panel] loadBPChangesJson | Invalid JSON in BP_CHANGES.json" << std::endl;
    return QJsonObject();
  }

  return doc.object();
}

static QString getCurrentBPVersion() {
  QString gitRoot = BPGitManager::getGitRoot();
  QString versionPath = QDir(gitRoot).filePath("BPVERSION");

  QFile file(versionPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }

  QString version = QString::fromUtf8(file.readAll()).trimmed();
  return version;
}

static QString formatBPChanges(const QJsonObject &versionData, int maxItems = 6) {
  QStringList allChanges;

  // Combine changes and fixes
  QJsonArray changes = versionData["changes"].toArray();
  QJsonArray fixes = versionData["fixes"].toArray();

  int count = 0;
  for (const auto &item : changes) {
    if (count >= maxItems) break;
    allChanges << QString("• %1").arg(item.toString());
    count++;
  }

  for (const auto &item : fixes) {
    if (count >= maxItems) break;
    allChanges << QString("• %1").arg(item.toString());
    count++;
  }

  int totalItems = changes.size() + fixes.size();
  if (totalItems > maxItems) {
    // Make the "show more" text clickable
    allChanges << QString("<a href=\"#\" style=\"color: #2196F3; text-decoration: none;\">... tap to show all %1 changes</a>").arg(totalItems);
  }

  return allChanges.join("<br>");
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
  currentVersionDesc->setTextInteractionFlags(Qt::TextBrowserInteraction);
  currentVersionDesc->setOpenExternalLinks(false);
  connect(currentVersionDesc, &QLabel::linkActivated, this, &BPSoftwarePanel::onRecentChangesClicked);
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

  // Sunnypilot Changes button
  sunnypilotChangesBtn = new BPCommandControl(
    tr("Sunnypilot Changes"),
    tr("View upstream changes from Sunnypilot"),
    tr("VIEW"),
    "sp_changes",
    "",
    QJsonObject(),
    "",
    false,
    "", "", "",
    QJsonArray(),
    this
  );
  connect(sunnypilotChangesBtn, &BPCommandControl::commandRequested, this, &BPSoftwarePanel::onSunnypilotChangesClicked);
  sunnypilotChangesBtn->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(sunnypilotChangesBtn);

  // Check for Updates button (styled like BPCommandControl)
  QFrame *downloadFrame = new QFrame(this);
  downloadFrame->setStyleSheet(R"(
    QFrame {
      background-color: #242424;
      border-radius: 10px;
    }
  )");

  QHBoxLayout *downloadLayout = new QHBoxLayout(downloadFrame);
  downloadLayout->setContentsMargins(25, 25, 25, 25);
  downloadLayout->setSpacing(50);

  downloadBtn = new BPButton(tr("CHECK"), this);
  downloadBtn->setMinimumWidth(250);
  downloadBtn->setMinimumHeight(100);
  connect(downloadBtn, &QPushButton::clicked, this, &BPSoftwarePanel::onDownloadClicked);
  downloadLayout->addWidget(downloadBtn);
  downloadLayout->setAlignment(downloadBtn, Qt::AlignVCenter);

  QVBoxLayout *downloadTextLayout = new QVBoxLayout();
  downloadTextLayout->setContentsMargins(0, 0, 0, 0);
  downloadTextLayout->setSpacing(5);

  QLabel *downloadTitleLabel = new QLabel(tr("Check for Updates"), this);
  downloadTitleLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  downloadTitleLabel->setWordWrap(true);
  downloadTextLayout->addWidget(downloadTitleLabel);

  downloadStatusLabel = new QLabel(tr("Check for available updates"), this);
  downloadStatusLabel->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  downloadStatusLabel->setWordWrap(true);
  downloadTextLayout->addWidget(downloadStatusLabel);

  downloadLayout->addLayout(downloadTextLayout, 1);
  downloadLayout->setAlignment(downloadTextLayout, Qt::AlignVCenter);
  layout->addWidget(downloadFrame);
}

void BPSoftwarePanel::createUpdateControlsGroup() {
  updateControlsGroup = createStyledGroupBox(tr("Install Update"));
  QVBoxLayout *layout = new QVBoxLayout(updateControlsGroup);
  layout->setSpacing(20);
  layout->setContentsMargins(25, 25, 25, 25);

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
  layout->setSpacing(15);
  layout->setContentsMargins(10, 10, 10, 10);

  // Branch selector button (styled like BPCommandControl)
  QFrame *branchFrame = new QFrame(this);
  branchFrame->setStyleSheet(R"(
    QFrame {
      background-color: #242424;
      border-radius: 10px;
    }
  )");

  QHBoxLayout *branchLayout = new QHBoxLayout(branchFrame);
  branchLayout->setContentsMargins(25, 25, 25, 25);
  branchLayout->setSpacing(50);

  branchBtn = new BPButton(tr("SELECT"), this);
  branchBtn->setMinimumWidth(250);
  branchBtn->setMinimumHeight(100);
  connect(branchBtn, &QPushButton::clicked, this, &BPSoftwarePanel::onBranchClicked);
  branchLayout->addWidget(branchBtn);
  branchLayout->setAlignment(branchBtn, Qt::AlignVCenter);

  QVBoxLayout *branchTextLayout = new QVBoxLayout();
  branchTextLayout->setContentsMargins(0, 0, 0, 0);
  branchTextLayout->setSpacing(5);

  QLabel *branchTitleLabel = new QLabel(tr("Target Branch"), this);
  branchTitleLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  branchTitleLabel->setWordWrap(true);
  branchTextLayout->addWidget(branchTitleLabel);

  branchStatusLabel = new QLabel(tr("Select the branch to update from"), this);
  branchStatusLabel->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  branchStatusLabel->setWordWrap(true);
  branchTextLayout->addWidget(branchStatusLabel);

  branchLayout->addLayout(branchTextLayout, 1);
  branchLayout->setAlignment(branchTextLayout, Qt::AlignVCenter);
  layout->addWidget(branchFrame);

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

  // Get BP version and changes
  QString bpVersion = getCurrentBPVersion();
  QJsonObject changesData = loadBPChangesJson();
  QJsonObject versions = changesData["versions"].toObject();

  // Update current version with BP changes
  if (!bpVersion.isEmpty()) {
    currentVersionLabel->setText(tr("Current Version: BluePilot %1").arg(bpVersion));

    if (versions.contains(bpVersion)) {
      QJsonObject versionData = versions[bpVersion].toObject();
      QString bpChanges = formatBPChanges(versionData, 3);
      currentVersionDesc->setText(bpChanges);
    } else {
      currentVersionDesc->setText(tr("No change information available"));
    }
  } else {
    currentVersionLabel->setText(tr("Current Version"));
    currentVersionDesc->setText(tr("Version information unavailable"));
  }

  // Update new version (if available) - show BP changes for new version too
  bool updateAvailable = params.getBool("UpdateAvailable");
  QString newVersion = QString::fromStdString(params.get("UpdaterNewDescription"));

  // Find the new version widget and show/hide it
  QWidget *newVersionWidget = newVersionLabel->parentWidget();
  if (newVersionWidget) {
    newVersionWidget->setVisible(!is_onroad && updateAvailable);
  }

  if (updateAvailable) {
    newVersionLabel->setText(tr("New Version: %1").arg(newVersion));

    // Try to extract BP version from new version string (if it contains BP version)
    // For now, show a generic message
    newVersionDesc->setText(tr("Update available. View Recent Changes for details."));
  }
}

void BPSoftwarePanel::updateDownloadButton() {
  // Update download button state (button is now in version info group)
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
  // Hide the entire Update Controls group when no update is available or onroad
  updateControlsGroup->setVisible(!is_onroad && updateAvailable);

  if (updateAvailable) {
    QString newVersion = QString::fromStdString(params.get("UpdaterNewDescription"));
    installStatusLabel->setText(newVersion);
  }
}

void BPSoftwarePanel::updateBranchSelector() {
  QString targetBranch = QString::fromStdString(params.get("UpdaterTargetBranch"));
  branchStatusLabel->setText(targetBranch.isEmpty() ? tr("No branch selected") : targetBranch);
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
  QString bpVersion = getCurrentBPVersion();
  BPRecentChangesDialog *dialog = new BPRecentChangesDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);

  if (dialog->loadAndDisplayChanges(bpVersion)) {
    dialog->setupFullscreen();  // BPDialogBase method handles rotation properly
  } else {
    dialog->deleteLater();
  }
}

void BPSoftwarePanel::onSunnypilotChangesClicked() {
  // Get Sunnypilot release notes from params
  QString currentNotes = QString::fromStdString(params.get("UpdaterCurrentReleaseNotes"));
  QString newNotes = QString::fromStdString(params.get("UpdaterNewReleaseNotes"));
  bool updateAvailable = params.getBool("UpdateAvailable");

  // Create dialog
  QDialog *dialog = new QDialog(this);
  dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  dialog->setStyleSheet("background-color: black;");
  dialog->setModal(true);

  QVBoxLayout *layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(45, 35, 45, 45);
  layout->setSpacing(30);

  // Title
  QLabel *titleLabel = new QLabel(tr("Sunnypilot Changes"), dialog);
  titleLabel->setStyleSheet(R"(
    QLabel {
      font-size: 50px;
      font-weight: 600;
      margin: 0px;
      padding: 0px;
      background-color: transparent;
      color: white;
    }
  )");
  layout->addWidget(titleLabel);

  // Scroll area for content
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

  // Enable touch scrolling
  QScroller::grabGesture(scrollArea->viewport(), QScroller::TouchGesture);

  QWidget *scrollContent = new QWidget(scrollArea);
  QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);
  scrollLayout->setContentsMargins(20, 20, 20, 20);
  scrollLayout->setSpacing(30);

  // Current version section
  if (!currentNotes.isEmpty()) {
    QLabel *currentHeader = new QLabel(tr("Current Version"), scrollContent);
    currentHeader->setStyleSheet("font-size: 42px; font-weight: 600; color: #2196F3;");
    scrollLayout->addWidget(currentHeader);

    QLabel *currentText = new QLabel(currentNotes, scrollContent);
    currentText->setStyleSheet("font-size: 32px; color: #E0E0E0; padding: 10px;");
    currentText->setWordWrap(true);
    scrollLayout->addWidget(currentText);
  }

  // New version section (if available)
  if (updateAvailable && !newNotes.isEmpty()) {
    scrollLayout->addSpacing(20);

    QLabel *newHeader = new QLabel(tr("New Version"), scrollContent);
    newHeader->setStyleSheet("font-size: 42px; font-weight: 600; color: #4CAF50;");
    scrollLayout->addWidget(newHeader);

    QLabel *newText = new QLabel(newNotes, scrollContent);
    newText->setStyleSheet("font-size: 32px; color: #E0E0E0; padding: 10px;");
    newText->setWordWrap(true);
    scrollLayout->addWidget(newText);
  }

  // If no notes available
  if (currentNotes.isEmpty() && (newNotes.isEmpty() || !updateAvailable)) {
    QLabel *noData = new QLabel(tr("No Sunnypilot release notes available"), scrollContent);
    noData->setStyleSheet("font-size: 36px; color: #888888; padding: 40px;");
    noData->setAlignment(Qt::AlignCenter);
    scrollLayout->addWidget(noData);
  }

  scrollLayout->addStretch();
  scrollArea->setWidget(scrollContent);
  layout->addWidget(scrollArea);

  // Close button
  QPushButton *closeButton = new QPushButton(tr("Close"), dialog);
  closeButton->setStyleSheet(R"(
    QPushButton {
      border-radius: 10px;
      font-size: 55px;
      padding: 15px;
      background-color: #465BEA;
      color: white;
      min-height: 60px;
    }
    QPushButton:pressed { background-color: #3049F4; }
  )");
  connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
  layout->addWidget(closeButton);

  // Set fullscreen
  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen) {
    dialog->setFixedSize(2160, 1080);
  }

  dialog->show();

  // Apply Wayland transform for QCOM2
#ifdef QCOM2
  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  if (native && dialog->windowHandle()) {
    wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", dialog->windowHandle()));
    if (s) {
      wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
      wl_surface_commit(s);
    }
  }
  dialog->setWindowState(Qt::WindowFullScreen);
#endif

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
  if (commandInProgress) {
    showBPAlert(tr("A command is already running. Please wait."), this);
    return;
  }

  // Confirm before proceeding
  if (!showBPConfirmation(
        tr("Manual Update"),
        tr("This will fetch and pull the latest changes, then rebuild.\n\n"
           "Any local changes will be discarded. Continue?"),
        tr("Update"),
        tr("Cancel"),
        this)) {
    return;
  }

  // Kill the update daemon to prevent conflicts
  QProcess::execute("killall", QStringList() << "system.updated.updated");

  // Build the update command
  QString command = "git reset --hard HEAD && "
                    "git clean -fd && "
                    "rm -f .git/index.lock && "
                    "git fetch && "
                    "git pull && "
                    "scons -j$(nproc)";

  // Use the advanced command dialog with all features:
  // - 30 minute timeout (1800000ms)
  // - Kill button enabled
  // - Retry button enabled
  // - Reboot button enabled (will auto-detect UI-only changes)
  // - Restart UI button enabled (shown instead of reboot for UI-only changes)
  showCommandOutputDialog(
    tr("Manual Update"),
    command,
    BPGitManager::getGitRoot(),
    1800000,  // 30 minute timeout for builds
    true,      // showKillBtn
    true,      // showRetryBtn
    true,      // showRebootBtn
    true       // showRestartUIBtn
  );
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
  dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  dialog->setStyleSheet("background-color: black;");
  dialog->setModal(true);

  QVBoxLayout *layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(45, 35, 45, 45);
  layout->setSpacing(30);

  // Add title
  QLabel *titleLabel = new QLabel(title);
  titleLabel->setStyleSheet(R"(
    QLabel {
      font-size: 50px;
      font-weight: 600;
      margin: 0px;
      padding: 0px;
      background-color: transparent;
      color: white;
    }
  )");
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
      font-size: 55px;
      padding: 15px;
      background-color: #465BEA;
      color: white;
      min-height: 60px;
    }
    QPushButton:pressed { background-color: #3049F4; }
  )");
  connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
  layout->addWidget(closeButton);

  // Apply fullscreen settings
  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen) {
    dialog->setFixedSize(2160, 1080);
  }

  dialog->show();

  // Apply Wayland transform for QCOM2
#ifdef QCOM2
  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  if (native && dialog->windowHandle()) {
    wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", dialog->windowHandle()));
    if (s) {
      wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
      wl_surface_commit(s);
    }
  }
  dialog->setWindowState(Qt::WindowFullScreen);
#endif

  dialog->exec();
}

void BPSoftwarePanel::showRecentChanges() {
  onRecentChangesClicked();
}

// ========== Power Management Methods (from bp_updater_panel) ==========

bool BPSoftwarePanel::isPowerSaveActive() const {
#ifdef QCOM2
  // Check if power save is active by counting CPU cores
  QProcess process;
  process.start("nproc", QStringList());
  if (process.waitForFinished(5000)) {
    QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    int coreCount = output.toInt();
    return coreCount <= 4; // Power save is active if 4 or fewer cores are available
  }
  return false;
#else
  // On non-QCOM2 platforms, power save is not applicable
  return false;
#endif
}

void BPSoftwarePanel::disablePowerSave() {
#ifdef QCOM2
  powerSaveWasActive = isPowerSaveActive();
  if (powerSaveWasActive) {
    // Use absolute path to ensure script is found
    QString scriptPath = BPGitManager::getGitRoot() + "/scripts/disable-powersave.py";
    if (QFile::exists(scriptPath)) {
      QProcess::execute("python3", QStringList() << scriptPath);

      // Wait for cores to come online (max 10 seconds)
      int attempts = 0;
      while (sysconf(_SC_NPROCESSORS_ONLN) < 8 && attempts < 20) {
        QThread::msleep(500);  // Wait 500ms between checks
        attempts++;
      }

      // Log the final core count for debugging
      int finalCoreCount = sysconf(_SC_NPROCESSORS_ONLN);
      if (finalCoreCount >= 8) {
        BPLog::bpInfo() << "[bp.software.panel] disablePowerSave | Power save disabled successfully. All " << finalCoreCount << " cores are online." << std::endl;
      } else {
        BPLog::bpError() << "[bp.software.panel] disablePowerSave | Warning: Only " << finalCoreCount << " cores are online after disabling power save." << std::endl;
      }
    } else {
      BPLog::bpError() << "[bp.software.panel] disablePowerSave | Power save script not found at: " << scriptPath.toStdString() << std::endl;
    }
  }
#else
  // On non-QCOM2 platforms, do nothing
  powerSaveWasActive = false;
#endif
}

void BPSoftwarePanel::restorePowerSave() {
#ifdef QCOM2
  if (powerSaveWasActive) {
    // Use absolute path to ensure script is found
    QString scriptPath = BPGitManager::getGitRoot() + "/scripts/manage-powersave.py";
    if (QFile::exists(scriptPath)) {
      QProcess::execute("python3", QStringList() << scriptPath << "--enable");

      // Wait for cores to go offline (max 5 seconds)
      int attempts = 0;
      while (sysconf(_SC_NPROCESSORS_ONLN) > 4 && attempts < 10) {
        QThread::msleep(500);  // Wait 500ms between checks
        attempts++;
      }

      // Log the final core count for debugging
      int finalCoreCount = sysconf(_SC_NPROCESSORS_ONLN);
      if (finalCoreCount <= 4) {
        BPLog::bpInfo() << "[bp.software.panel] restorePowerSave | Power save restored successfully. " << finalCoreCount << " cores are now online." << std::endl;
      } else {
        BPLog::bpError() << "[bp.software.panel] restorePowerSave | Warning: " << finalCoreCount << " cores are still online after restoring power save." << std::endl;
      }
    } else {
      BPLog::bpError() << "[bp.software.panel] restorePowerSave | Power save script not found at: " << scriptPath.toStdString() << std::endl;
    }
    powerSaveWasActive = false;
  }
#else
  // On non-QCOM2 platforms, do nothing
  powerSaveWasActive = false;
#endif
}

bool BPSoftwarePanel::checkIfUIOnlyChanges() const {
  // Check if the last pull/update only touched UI files under selfdrive/ui/
  QProcess process;
  process.setWorkingDirectory(BPGitManager::getGitRoot());

  // Get the list of changed files in the last commit (HEAD vs HEAD~1)
  // This checks what was just pulled/updated
  process.start("/bin/bash", QStringList() << "-c" << "git diff --name-only HEAD@{1} HEAD 2>/dev/null || git diff --name-only HEAD~1 HEAD 2>/dev/null");

  if (!process.waitForFinished(5000)) {
    process.kill();
    process.waitForFinished(1000);
    return false; // Default to requiring full reboot on timeout
  }

  QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();

  if (output.isEmpty()) {
    // No changes detected, safe to just restart UI
    return true;
  }

  // Check each changed file
  QStringList changedFiles = output.split('\n', QString::SkipEmptyParts);
  for (const QString &file : changedFiles) {
    QString trimmedFile = file.trimmed();
    if (trimmedFile.isEmpty()) {
      continue;
    }

    // If any file is outside selfdrive/ui/, we need a full reboot
    if (!trimmedFile.startsWith("selfdrive/ui/")) {
      return false;
    }
  }

  // All changed files are under selfdrive/ui/
  return true;
}

void BPSoftwarePanel::setupFullscreenDialog(QDialog *dialog) {
#ifdef QCOM2
  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", dialog->windowHandle()));
  if (s) {
    wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
    wl_surface_commit(s);
  }
  dialog->setWindowState(Qt::WindowFullScreen);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->layout()->activate();
  void *egl = native->nativeResourceForWindow("egldisplay", dialog->windowHandle());
  assert(egl != nullptr);
#endif
}

// ========== Advanced Command Execution Dialog (from bp_updater_panel) ==========

void BPSoftwarePanel::showCommandOutputDialog(const QString &title, const QString &command, const QString &workingDir,
                                               int timeoutMs, bool showKillBtn, bool showRetryBtn, bool showRebootBtn,
                                               bool showRestartUIBtn) {
  // Clean up any existing dialog
  if (currentDialog) {
    currentDialog->close();
    currentDialog->deleteLater();
    currentDialog = nullptr;
  }

  // Set commandInProgress to true to make sure the UI stays awake while the command is running
  commandInProgress = true;

#ifdef QCOM2
  // Disable power save mode for better performance during git/scons operations
  disablePowerSave();
#endif

  // Create and set up process
  QProcess *process = new QProcess(this);
  if (!workingDir.isEmpty()) {
    process->setWorkingDirectory(workingDir);
  } else {
    process->setWorkingDirectory(BPGitManager::getGitRoot());
  }

  // Create command output dialog with proper flags
  currentDialog = new QDialog(this);
  currentDialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  currentDialog->setStyleSheet("background-color: black;");

  // Create main layout
  QVBoxLayout *layout = new QVBoxLayout(currentDialog);
  layout->setContentsMargins(45, 35, 45, 45);
  layout->setSpacing(0);

  // Create title section with horizontal layout
  QWidget *titleSection = new QWidget(currentDialog);
  QHBoxLayout *titleLayout = new QHBoxLayout(titleSection);
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(0);

  // Add title label (without cores)
  QLabel *titleLabel = new QLabel(title);
  titleLabel->setStyleSheet("font-size: 90px; font-weight: 600; background-color: black; color: white;");
  titleLayout->addWidget(titleLabel);

  // Add spacer to push cores display to the right
  titleLayout->addStretch();

    // Add cores display with microchip icon
  int numCores = sysconf(_SC_NPROCESSORS_ONLN);
  QLabel *coresLabel = new QLabel(QString("🔲 %1").arg(numCores));
  coresLabel->setStyleSheet("font-size: 60px; font-weight: 600; background-color: black; color: #888888; padding: 10px;");
  coresLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  titleLayout->addWidget(coresLabel);

  // Create timer to update core count frequently
  QTimer *coresUpdateTimer = new QTimer(currentDialog);
  coresUpdateTimer->setInterval(500); // Update every 500ms for more responsiveness
  coresUpdateTimer->setSingleShot(false);

    // Connect timer to update core count
  QObject::connect(coresUpdateTimer, &QTimer::timeout, [=]() {
    int numCores = sysconf(_SC_NPROCESSORS_ONLN);
    coresLabel->setText(QString("🔲 %1").arg(numCores));
  });

  // Start the timer
  coresUpdateTimer->start();

  // Ensure timer is stopped when dialog is destroyed
  QObject::connect(currentDialog, &QDialog::destroyed, [=]() {
    if (coresUpdateTimer) {
      coresUpdateTimer->stop();
    }
  });

  layout->addWidget(titleSection);
  layout->addSpacing(30);

  // Create elapsed timer to track runtime
  QElapsedTimer *elapsedTimer = new QElapsedTimer();
  elapsedTimer->start();

  // Format time values as m:ss
  auto formatTime = [](int totalSeconds) {
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    return QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0'));
  };

  // Create output text area
  QTextEdit *outputText = new QTextEdit(currentDialog);
  outputText->setReadOnly(true);
  outputText->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::LinksAccessibleByMouse);
  outputText->setStyleSheet(R"(
        QTextEdit {
            font-family: monospace;
            font-size: 35px;
            font-weight: 200;
            color: #C9C9C9;
            background-color: #1B1B1B;
            padding: 50px;
            border: none;
        }
        QTextEdit QScrollBar:vertical {
            width: 20px;
            background: #1B1B1B;
            margin: 0px;
        }
        QTextEdit QScrollBar::handle:vertical {
            background-color: white;
            min-height: 30px;
            border-radius: 5px;
            margin: 2px;
            width: 16px;
        }
        QTextEdit QScrollBar::add-line:vertical,
        QTextEdit QScrollBar::sub-line:vertical {
            height: 0px;
            background: none;
        }
        QTextEdit QScrollBar::add-page:vertical,
        QTextEdit QScrollBar::sub-page:vertical {
            background: none;
        }
    )");
  outputText->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  outputText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  outputText->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);  // Enable touch scrolling
  layout->addWidget(outputText);

  // Create button layout
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(20);

  // Add kill button (if enabled)
  QPushButton *killButton = nullptr;
  if (showKillBtn) {
    killButton = new QPushButton(tr("Stop Command"), currentDialog);
    killButton->setFixedHeight(100);
    killButton->setStyleSheet(R"(
            QPushButton {
                background-color: #EA4646;
                font-size: 55px;
                font-weight: 400;
                border-radius: 20px;
                color: white;
            }
            QPushButton:pressed {
                background-color: #F43030;
            }
        )");
    buttonLayout->addWidget(killButton);
  }

  // Add retry button (if enabled)
  QPushButton *retryButton = nullptr;
  if (showRetryBtn) {
    retryButton = new QPushButton(tr("Retry"), currentDialog);
    retryButton->setFixedHeight(100);
    retryButton->setVisible(false); // Hide initially
    retryButton->setStyleSheet(R"(
            QPushButton {
                background-color: #7B1FA2;
                font-size: 55px;
                font-weight: 400;
                border-radius: 20px;
                color: white;
            }
            QPushButton:pressed {
                background-color: #6A1B9A;
            }
            QPushButton:disabled {
                background-color: #4F4F4F;
                color: #888888;
            }
        )");
    buttonLayout->addWidget(retryButton);
  }

  // Add reboot button (if enabled)
  QPushButton *rebootButton = nullptr;
  if (showRebootBtn) {
    rebootButton = new QPushButton(tr("Reboot"), currentDialog);
    rebootButton->setFixedHeight(100);
    rebootButton->setVisible(false); // Hide initially
    rebootButton->setStyleSheet(R"(
            QPushButton {
                background-color: #33Ab4C;
                font-size: 55px;
                font-weight: 400;
                border-radius: 20px;
                color: white;
            }
            QPushButton:pressed {
                background-color: #2A9040;
            }
        )");
    buttonLayout->addWidget(rebootButton);
  }

  // Add restart UI button (if enabled)
  QPushButton *restartUIButton = nullptr;
  if (showRestartUIBtn) {
    restartUIButton = new QPushButton(tr("Restart UI"), currentDialog);
    restartUIButton->setFixedHeight(100);
    restartUIButton->setVisible(false); // Hide initially
    restartUIButton->setStyleSheet(R"(
            QPushButton {
                background-color: #465BEA;
                font-size: 55px;
                font-weight: 400;
                border-radius: 20px;
                color: white;
            }
            QPushButton:pressed {
                background-color: #3049F4;
            }
        )");
    buttonLayout->addWidget(restartUIButton);
  }

  // Close button (initially disabled)
  QPushButton *closeButton = new QPushButton(tr("Command Running..."), currentDialog);
  closeButton->setEnabled(false);
  closeButton->setFixedHeight(100);
  closeButton->setStyleSheet(R"(
        QPushButton {
            background-color: #465BEA;
            font-size: 55px;
            font-weight: 400;
            border-radius: 20px;
            color: white;
        }
        QPushButton:pressed {
            background-color: #3049F4;
        }
        QPushButton:disabled {
            background-color: #4F4F4F;
            color: white;
        }
    )");
  buttonLayout->addWidget(closeButton);

  layout->addSpacing(50);
  layout->addLayout(buttonLayout);

  // Add timeout timer
  QTimer *timeoutTimer = new QTimer(currentDialog);
  timeoutTimer->setSingleShot(true);
  timeoutTimer->setInterval(timeoutMs);

  // Create runtime display timer that updates every second
  QTimer *runtimeTimer = new QTimer(currentDialog);
  runtimeTimer->setInterval(1000); // Update every second
  runtimeTimer->setTimerType(Qt::PreciseTimer);

  // Update the runtime timer on the button and title
  connect(runtimeTimer, &QTimer::timeout, [=]() {
    // Always update the elapsed time display every second while the timer is running
    int elapsedSecs = elapsedTimer->elapsed() / 1000;
    int timeoutSecs = timeoutMs / 1000;

    // Format as Command Running: (MM:SS/TT:TT)
    QString timerText = tr("Command Running: (%1/%2)").arg(formatTime(elapsedSecs)).arg(formatTime(timeoutSecs));

    // Set the button text with formatting and ensure it repaints immediately
    closeButton->setText(timerText);
    closeButton->repaint();

    // Title stays static - elapsed time shown in button
    titleLabel->setText(title);
  });

  // Start the runtime timer immediately and trigger initial update
  runtimeTimer->start();

  // Set initial title (no elapsed time)
  titleLabel->setText(title);

  // Trigger initial timer update to show 0:00 immediately
  QTimer::singleShot(0, [=]() {
    int elapsedSecs = elapsedTimer->elapsed() / 1000;
    int timeoutSecs = timeoutMs / 1000;
    QString timerText = tr("Command Running: (%1/%2)").arg(formatTime(elapsedSecs)).arg(formatTime(timeoutSecs));
    closeButton->setText(timerText);
  });

  // Connect process signals for output
  connect(process, &QProcess::readyReadStandardOutput, [=]() {
    QString output = QString::fromUtf8(process->readAllStandardOutput());
    outputText->append(output);
  });

  connect(process, &QProcess::readyReadStandardError, [=]() {
    QString error = QString::fromUtf8(process->readAllStandardError());
    outputText->append("<span style='color: #ff7c30;'>" + error.toHtmlEscaped() + "</span>");
  });

  // Connect timeout handler
  connect(timeoutTimer, &QTimer::timeout, [=]() {
    if (process->state() != QProcess::NotRunning) {
      outputText->append("\n<span style='color: #ff7c30;'>Process timed out after " + QString::number(timeoutMs / 1000) + " seconds</span>");
      process->kill();
      commandInProgress = false;
      if (killButton)
        killButton->hide();
      if (retryButton) {
        retryButton->setVisible(true);
        retryButton->setEnabled(true);
      }
      closeButton->setEnabled(true);
      closeButton->setText(tr("Close (Timed Out)"));
      closeButton->setFixedHeight(100);
      closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #EA4646;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 20px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #F43030;
                }
            )");

      // Stop the runtime timer
      runtimeTimer->stop();

      // Title stays static - timeout status shown in button
      titleLabel->setText(title);
    }
  });

  // Connect kill button
  if (killButton) {
    connect(killButton, &QPushButton::clicked, [=]() {
      if (process->state() != QProcess::NotRunning) {
        outputText->append("\n<span style='color: #ff7c30;'>Process terminated by user</span>");
        process->kill();
        killButton->hide();
        if (retryButton)
          retryButton->setEnabled(true);
        closeButton->setEnabled(true);
        closeButton->setText(tr("Close (Terminated)"));
        closeButton->setStyleSheet(R"(
                    QPushButton {
                        background-color: #EA4646;
                        font-size: 55px;
                        font-weight: 400;
                        border-radius: 20px;
                        color: white;
                    }
                    QPushButton:pressed {
                        background-color: #F43030;
                    }
                )");

        // Stop the runtime timer
        runtimeTimer->stop();

        // Show terminated message - title stays static
        int elapsedSecs = elapsedTimer->elapsed() / 1000;
        QString finalTime = QString("Terminated at %1").arg(formatTime(elapsedSecs));
        titleLabel->setText(title);
      }
    });
  }

  // Handle process completion
  connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=](int exitCode, QProcess::ExitStatus exitStatus) {
#ifdef QCOM2
    // Restore power save mode after command completion
    restorePowerSave();
#endif
    timeoutTimer->stop();
    closeButton->setEnabled(true);
    commandInProgress = false;
    if (killButton)
      killButton->hide();

    // Stop the runtime timer
    runtimeTimer->stop();

    // Show final runtime
    int elapsedSecs = elapsedTimer->elapsed() / 1000;
    QString finalTime = QString("Total Runtime: %1").arg(formatTime(elapsedSecs));

    // Title stays static - completion status shown in button
    titleLabel->setText(title);

    if (exitStatus == QProcess::CrashExit) {
      // Show retry button for crash
      if (retryButton) {
        retryButton->setVisible(true);
        retryButton->setEnabled(true);
      }
      closeButton->setText(tr("Close (Crashed)"));
    } else if (exitCode != 0) {
      outputText->append(QString("\n<span style='color: #ff7c30;'>Command failed with exit code: %1</span>").arg(exitCode));
      closeButton->setText(tr("Close (Command Failed | Exit code: %1)").arg(exitCode));
      closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #EA4646;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 10px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #F43030;
                }
            )");
      // Show retry button for failure
      if (retryButton) {
        retryButton->setVisible(true);
        retryButton->setEnabled(true);
      }
    } else {
      outputText->append("\n<span style='color: #50d332;'>Command completed successfully</span>");
      closeButton->setText(tr("Close (Completed Successfully)"));
      closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #33Ab4C;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 10px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #2A9040;
                }
            )");

      // Hide retry button on success if retry is enabled
      if (retryButton) {
        retryButton->setVisible(false);
      }

      // Intelligently show reboot or restart UI button based on changes
      if (rebootButton || restartUIButton) {
        bool uiOnlyChanges = checkIfUIOnlyChanges();
        if (uiOnlyChanges && restartUIButton) {
          // Only UI changes detected - show restart UI button
          restartUIButton->setVisible(true);
          if (rebootButton) {
            rebootButton->setVisible(false);
          }
        } else if (rebootButton) {
          // Non-UI changes detected or restart UI not available - show reboot button
          rebootButton->setVisible(true);
          if (restartUIButton) {
            restartUIButton->setVisible(false);
          }
        }
      }

      // Refresh repo status after successful completion
      updateRepoStatus();
    }
  });

  // Add retry button functionality
  if (retryButton) {
    connect(retryButton, &QPushButton::clicked, [=]() {
#ifdef QCOM2
      // Disable power save mode again for retry
      disablePowerSave();
#endif
      outputText->clear();
      outputText->append(tr("Retrying command:\n\n%1\n\n").arg(command));
      retryButton->setEnabled(false);
      retryButton->setVisible(false); // Hide when retrying
      closeButton->setEnabled(false);
      closeButton->setText(tr("Command Running..."));
      closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #465BEA;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 10px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #3049F4;
                }
                QPushButton:disabled {
                    background-color: #4F4F4F;
                    color: white;
                }
            )");
      if (killButton) {
        killButton->show();
      }

      // Reset elapsed timer
      elapsedTimer->restart();

      // Reset and start timer display
      runtimeTimer->start();

      // Reset and start timeout timer
      timeoutTimer->start();

      // Trigger immediate timer update to show 0:00 for retry
      QTimer::singleShot(0, [=]() {
        int elapsedSecs = elapsedTimer->elapsed() / 1000;
        int timeoutSecs = timeoutMs / 1000;
        QString timerText = tr("Command Running: (%1/%2)").arg(formatTime(elapsedSecs)).arg(formatTime(timeoutSecs));
        closeButton->setText(timerText);
      });

      // Start process again
      process->start("/bin/bash", QStringList() << "-c" << command);
    });
  }

  // Connect reboot button
  if (rebootButton) {
    connect(rebootButton, &QPushButton::clicked, [=]() {
      if (showBPConfirmation(tr("Reboot"), tr("Are you sure you want to reboot?"), tr("Reboot"), tr("Cancel"), currentDialog)) {
        params.putBool("DoReboot", true);
        QProcess::execute("reboot");
      }
    });
  }

  // Connect restart UI button
  if (restartUIButton) {
    connect(restartUIButton, &QPushButton::clicked, [=]() {
      if (showBPConfirmation(tr("Restart UI"), tr("Are you sure you want to restart the UI?"), tr("Restart"), tr("Cancel"), currentDialog)) {
        // Exit with code 18 - manager will restart the UI automatically
        qApp->exit(18);
      }
    });
  }

  // Connect close button and cleanup
  connect(closeButton, &QPushButton::clicked, currentDialog, &QDialog::accept);
  connect(currentDialog, &QDialog::finished, [=]() {
#ifdef QCOM2
    // Restore power save mode if dialog is closed manually
    restorePowerSave();
#endif
    timeoutTimer->stop();
    runtimeTimer->stop();
    delete elapsedTimer;
    process->deleteLater();
    if (currentDialog) {
      currentDialog->deleteLater();
      currentDialog = nullptr;
    }
  });

  // Set dialog size and show
  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen) {
    currentDialog->setFixedSize(2160, 1080);
  }
  currentDialog->show();
  setupFullscreenDialog(currentDialog);

  // Start timeout timer
  timeoutTimer->start();

  // Start process
  outputText->append(tr("Executing command:\n\n%1\n\n").arg(command));
  process->start("/bin/bash", QStringList() << "-c" << command);
}
