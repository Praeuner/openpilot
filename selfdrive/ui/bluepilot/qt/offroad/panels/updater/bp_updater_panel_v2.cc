// selfdrive/ui/bluepilot/qt/offroad/panels/updater/bp_updater_panel_v2.cc

#include "bp_updater_panel_v2.h"
#include "../bp_utils.h"
#include "../bp_panel_dialogs.h"
#include "../bp_panel_controls.h"
#include "../bp_updater_panel.h"  // For BPUpdateConfirmDialog

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QDialog>
#include <QTableWidget>
#include <QListWidget>
#include <QHeaderView>
#include <QGuiApplication>
#include <QGroupBox>

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#endif

BPUpdaterPanelV2::BPUpdaterPanelV2(QWidget *parent)
    : QWidget(parent), isOnroad(false), operationInProgress(false) {

  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  setupWorker();
  setupUI();

  // Check onroad status periodically
  onroadCheckTimer = new QTimer(this);
  connect(onroadCheckTimer, &QTimer::timeout, this, &BPUpdaterPanelV2::checkOnroadStatus);
  onroadCheckTimer->start(2000);  // Check every 2 seconds
}

BPUpdaterPanelV2::~BPUpdaterPanelV2() {
  if (workerThread) {
    workerThread->quit();
    workerThread->wait(3000);
    delete workerThread;
  }
}

void BPUpdaterPanelV2::setupWorker() {
  // Create worker thread
  workerThread = new QThread(this);
  gitWorker = new BPGitWorker();
  gitWorker->moveToThread(workerThread);

  // Connect worker signals
  connect(gitWorker, &BPGitWorker::statusReady, this, &BPUpdaterPanelV2::onStatusReady);
  connect(gitWorker, &BPGitWorker::updatesCheckComplete, this, &BPUpdaterPanelV2::onUpdatesCheckComplete);
  connect(gitWorker, &BPGitWorker::branchListReady, this, &BPUpdaterPanelV2::onBranchListReady);
  connect(gitWorker, &BPGitWorker::commitHistoryReady, this, &BPUpdaterPanelV2::onCommitHistoryReady);
  connect(gitWorker, &BPGitWorker::operationStarted, this, &BPUpdaterPanelV2::onOperationStarted);
  connect(gitWorker, &BPGitWorker::operationProgress, this, &BPUpdaterPanelV2::onOperationProgress);
  connect(gitWorker, &BPGitWorker::operationComplete, this, &BPUpdaterPanelV2::onOperationComplete);
  connect(gitWorker, &BPGitWorker::errorOccurred, this, &BPUpdaterPanelV2::onErrorOccurred);
  connect(gitWorker, &BPGitWorker::canOperateChanged, this, &BPUpdaterPanelV2::onCanOperateChanged);

  // Start thread
  workerThread->start();
}

QGroupBox* BPUpdaterPanelV2::createStyledGroupBox(const QString &title) {
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

void BPUpdaterPanelV2::setupUI() {
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(40, 40, 40, 40);
  mainLayout->setSpacing(30);

  setStyleSheet(R"(
    BPUpdaterPanelV2 {
      background-color: #1B1B1B;
    }
    BPUpdaterPanelV2 QGroupBox BPCommandControl {
      background-color: transparent;
    }
  )");

  createStatusGroup();
  createActionsGroup();
  createAdvancedGroup();

  mainLayout->addStretch();

  // Progress overlay
  progressOverlay = new BPProgressOverlay(this);
  connect(progressOverlay, &BPProgressOverlay::cancelRequested, this, [this]() {
    progressOverlay->hide();
  });
  connect(progressOverlay, &BPProgressOverlay::retryRequested, this, [this]() {
    progressOverlay->hide();
  });
  connect(progressOverlay, &BPProgressOverlay::closeRequested, this, [this]() {
    progressOverlay->hide();
    QMetaObject::invokeMethod(gitWorker, "checkStatus", Qt::QueuedConnection);
  });
}

void BPUpdaterPanelV2::createStatusGroup() {
  statusGroup = createStyledGroupBox(tr("Repository Status"));
  QVBoxLayout *layout = new QVBoxLayout(statusGroup);
  layout->setSpacing(10);
  layout->setContentsMargins(10, 10, 10, 10);

  // Status card
  statusCard = new BPStatusCard(this);
  statusCard->setStyleSheet("BPStatusCard { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(statusCard);

  mainLayout->addWidget(statusGroup);
}

void BPUpdaterPanelV2::createActionsGroup() {
  actionsGroup = createStyledGroupBox(tr("Update Actions"));
  QVBoxLayout *layout = new QVBoxLayout(actionsGroup);
  layout->setSpacing(10);
  layout->setContentsMargins(10, 10, 10, 10);

  // Check for Updates
  checkUpdatesControl = new BPCommandControl(
    tr("Check for Updates"),
    tr("Fetch latest changes from the remote repository"),
    tr("CHECK"),
    "check_updates",
    "", QJsonObject(), "", false, "", "", "", QJsonArray(),
    this
  );
  connect(checkUpdatesControl, &BPCommandControl::commandRequested, this, &BPUpdaterPanelV2::handleCheckUpdates);
  checkUpdatesControl->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(checkUpdatesControl);

  // Update Repository
  updateControl = new BPCommandControl(
    tr("Update Repository"),
    tr("Pull latest changes and update all submodules"),
    tr("UPDATE"),
    "update_repo",
    "", QJsonObject(), "", false, "", "", "", QJsonArray(),
    this
  );
  connect(updateControl, &BPCommandControl::commandRequested, this, &BPUpdaterPanelV2::handleUpdate);
  updateControl->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  updateControl->setEnabled(false);
  layout->addWidget(updateControl);

  // Switch Branch
  switchBranchControl = new BPCommandControl(
    tr("Switch Branch"),
    tr("Change to a different branch"),
    tr("SWITCH"),
    "switch_branch",
    "", QJsonObject(), "", false, "", "", "", QJsonArray(),
    this
  );
  connect(switchBranchControl, &BPCommandControl::commandRequested, this, &BPUpdaterPanelV2::handleSwitchBranch);
  switchBranchControl->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(switchBranchControl);

  // View History
  viewHistoryControl = new BPCommandControl(
    tr("Commit History"),
    tr("View recent commit history"),
    tr("VIEW"),
    "view_history",
    "", QJsonObject(), "", false, "", "", "", QJsonArray(),
    this
  );
  connect(viewHistoryControl, &BPCommandControl::commandRequested, this, &BPUpdaterPanelV2::handleViewHistory);
  viewHistoryControl->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(viewHistoryControl);

  mainLayout->addWidget(actionsGroup);
}

void BPUpdaterPanelV2::createAdvancedGroup() {
  advancedGroup = createStyledGroupBox(tr("Advanced Operations"));
  QVBoxLayout *layout = new QVBoxLayout(advancedGroup);
  layout->setSpacing(10);
  layout->setContentsMargins(10, 10, 10, 10);

  // Reset Repository
  resetControl = new BPCommandControl(
    tr("Reset Repository"),
    tr("Discard all local changes and reset to HEAD"),
    tr("RESET"),
    "reset_repo",
    "", QJsonObject(), "", false, "", "", "", QJsonArray(),
    this
  );
  connect(resetControl, &BPCommandControl::commandRequested, this, &BPUpdaterPanelV2::handleReset);
  resetControl->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(resetControl);

  // Repair Repository
  repairControl = new BPCommandControl(
    tr("Repair Repository"),
    tr("Run git fsck and gc to repair and optimize the repository"),
    tr("REPAIR"),
    "repair_repo",
    "", QJsonObject(), "", false, "", "", "", QJsonArray(),
    this
  );
  connect(repairControl, &BPCommandControl::commandRequested, this, &BPUpdaterPanelV2::handleRepair);
  repairControl->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(repairControl);

  // Unshallow Repository
  unshallowControl = new BPCommandControl(
    tr("Unshallow Repository"),
    tr("Fetch complete git history (uses significant disk space)"),
    tr("UNSHALLOW"),
    "unshallow_repo",
    "", QJsonObject(), "", false, "", "", "", QJsonArray(),
    this
  );
  connect(unshallowControl, &BPCommandControl::commandRequested, this, &BPUpdaterPanelV2::handleUnshallow);
  unshallowControl->setStyleSheet("BPCommandControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(unshallowControl);

  mainLayout->addWidget(advancedGroup);
}

void BPUpdaterPanelV2::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  // Defer status refresh to avoid blocking showEvent
  QTimer::singleShot(0, this, [this]() {
    QMetaObject::invokeMethod(gitWorker, "checkStatus", Qt::QueuedConnection);
  });
}

void BPUpdaterPanelV2::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
}

void BPUpdaterPanelV2::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);

  if (progressOverlay) {
    progressOverlay->setGeometry(rect());
  }
}

void BPUpdaterPanelV2::checkOnroadStatus() {
  bool wasOnroad = isOnroad;
  isOnroad = params.getBool("IsOnroad");

  if (isOnroad != wasOnroad) {
    updateButtonStates();
  }
}

bool BPUpdaterPanelV2::canPerformOperations() const {
  return !isOnroad && !operationInProgress;
}

void BPUpdaterPanelV2::updateButtonStates() {
  bool canOperate = canPerformOperations();

  if (checkUpdatesControl) checkUpdatesControl->setEnabled(canOperate);
  if (updateControl) updateControl->setEnabled(canOperate && currentStatus.hasUpdatesAvailable);
  if (switchBranchControl) switchBranchControl->setEnabled(canOperate);
  if (viewHistoryControl) viewHistoryControl->setEnabled(canOperate);
  if (resetControl) resetControl->setEnabled(canOperate && currentStatus.hasLocalChanges);
  if (repairControl) repairControl->setEnabled(canOperate);
  if (unshallowControl) unshallowControl->setEnabled(canOperate);
}

void BPUpdaterPanelV2::onStatusReady(const GitStatus &status) {
  currentStatus = status;
  statusCard->updateStatus(status);
  updateButtonStates();
}

void BPUpdaterPanelV2::onUpdatesCheckComplete(bool hasUpdates, int count) {
  updateButtonStates();
}

void BPUpdaterPanelV2::onBranchListReady(const QStringList &branches) {
  showBranchSelector(branches);
}

void BPUpdaterPanelV2::onCommitHistoryReady(const QStringList &commits) {
  showHistoryDialog(commits);
}

void BPUpdaterPanelV2::onOperationStarted(const QString &operation) {
  operationInProgress = true;
  updateButtonStates();
  progressOverlay->showOperation(operation);
}

void BPUpdaterPanelV2::onOperationProgress(const QString &message) {
  progressOverlay->updateProgress(message);
}

void BPUpdaterPanelV2::onOperationComplete(bool success, const QString &message) {
  operationInProgress = false;
  updateButtonStates();
  progressOverlay->showComplete(success, message);
}

void BPUpdaterPanelV2::onErrorOccurred(const QString &error) {
  progressOverlay->showError(error);
  operationInProgress = false;
  updateButtonStates();
}

void BPUpdaterPanelV2::onCanOperateChanged(bool canOperate) {
  updateButtonStates();
}

void BPUpdaterPanelV2::handleCheckUpdates() {
  if (!canPerformOperations()) return;

  statusCard->setCheckingState(true);
  QMetaObject::invokeMethod(gitWorker, "checkForUpdates", Qt::QueuedConnection);
}

void BPUpdaterPanelV2::handleUpdate() {
  if (!canPerformOperations()) return;

  if (BPUpdateConfirmDialog::confirm(
    tr("Update Repository"),
    tr("This will update the repository to the latest version. Continue?"),
    tr("Update"), tr("Cancel"), this)) {
    QMetaObject::invokeMethod(gitWorker, "updateRepository", Qt::QueuedConnection);
  }
}

void BPUpdaterPanelV2::handleSwitchBranch() {
  if (!canPerformOperations()) return;

  QMetaObject::invokeMethod(gitWorker, "fetchBranches",
    Qt::QueuedConnection, Q_ARG(bool, true));
}

void BPUpdaterPanelV2::handleReset() {
  if (!canPerformOperations()) return;

  if (BPUpdateConfirmDialog::confirm(
    tr("Reset Repository"),
    tr("⚠️ This will discard all local changes and reset to HEAD. This cannot be undone. Continue?"),
    tr("Reset"), tr("Cancel"), this)) {
    QMetaObject::invokeMethod(gitWorker, "resetRepository", Qt::QueuedConnection);
  }
}

void BPUpdaterPanelV2::handleRepair() {
  if (!canPerformOperations()) return;

  if (BPUpdateConfirmDialog::confirm(
    tr("Repair Repository"),
    tr("This will run git fsck and gc to repair the repository. This may take several minutes. Continue?"),
    tr("Repair"), tr("Cancel"), this)) {
    QMetaObject::invokeMethod(gitWorker, "repairRepository", Qt::QueuedConnection);
  }
}

void BPUpdaterPanelV2::handleUnshallow() {
  if (!canPerformOperations()) return;

  if (BPUpdateConfirmDialog::confirm(
    tr("Unshallow Repository"),
    tr("This will fetch the complete git history. This may take a long time and use significant disk space. Continue?"),
    tr("Unshallow"), tr("Cancel"), this)) {
    QMetaObject::invokeMethod(gitWorker, "unshallowRepository", Qt::QueuedConnection);
  }
}

void BPUpdaterPanelV2::handleViewHistory() {
  if (!canPerformOperations()) return;

  QMetaObject::invokeMethod(gitWorker, "fetchCommitHistory",
    Qt::QueuedConnection, Q_ARG(int, 30));
}

void BPUpdaterPanelV2::showBranchSelector(const QStringList &branches) {
  if (branches.isEmpty()) {
    BPUpdateConfirmDialog::alert(tr("No branches found"), this);
    return;
  }

  QString current = currentStatus.currentBranch;
  QDialog *dialog = new QDialog(this);
  dialog->setWindowTitle(tr("Select Branch"));
  dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

#ifdef QCOM2
  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  wl_surface *s = reinterpret_cast<wl_surface *>(
    native->nativeResourceForWindow("surface", dialog->windowHandle()));
  if (s) {
    wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
    wl_surface_commit(s);
  }
  dialog->setWindowState(Qt::WindowFullScreen);
#endif

  BPTextSizes sizes = BPTextSizes::getSizes();

  QVBoxLayout *layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(50, 50, 50, 50);
  layout->setSpacing(20);

  QLabel *title = new QLabel(tr("Select Branch"));
  title->setStyleSheet(QString(R"(
    font-size: %1px;
    color: white;
    font-weight: 600;
  )").arg(sizes.titleSize + 10));
  layout->addWidget(title);

  QListWidget *listWidget = new QListWidget();
  listWidget->setStyleSheet(QString(R"(
    QListWidget {
      background-color: #1B1B1B;
      border: 1px solid #4A90E2;
      border-radius: 10px;
      font-size: %1px;
      color: white;
      padding: 10px;
    }
    QListWidget::item {
      padding: 20px;
      border-radius: 8px;
    }
    QListWidget::item:selected {
      background-color: #4A90E2;
    }
    QListWidget::item:hover {
      background-color: #2A2A2A;
    }
  )").arg(sizes.titleSize - 5));

  for (const QString &branch : branches) {
    QListWidgetItem *item = new QListWidgetItem(branch);
    if (branch == current) {
      item->setText(branch + " ✓");
      item->setForeground(QColor("#50d332"));
    }
    listWidget->addItem(item);
  }

  layout->addWidget(listWidget);

  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(20);

  QPushButton *cancelBtn = new QPushButton(tr("Cancel"));
  cancelBtn->setMinimumHeight(90);
  cancelBtn->setStyleSheet(QString(R"(
    QPushButton {
      background-color: #363636;
      color: white;
      font-size: %1px;
      font-weight: 500;
      border-radius: 15px;
    }
  )").arg(sizes.buttonTextSize));
  connect(cancelBtn, &QPushButton::clicked, dialog, &QDialog::reject);
  buttonLayout->addWidget(cancelBtn);

  QPushButton *selectBtn = new QPushButton(tr("Switch"));
  selectBtn->setMinimumHeight(90);
  selectBtn->setStyleSheet(QString(R"(
    QPushButton {
      background-color: #4A90E2;
      color: white;
      font-size: %1px;
      font-weight: 500;
      border-radius: 15px;
    }
  )").arg(sizes.buttonTextSize));
  connect(selectBtn, &QPushButton::clicked, dialog, &QDialog::accept);
  buttonLayout->addWidget(selectBtn);

  layout->addLayout(buttonLayout);

  if (dialog->exec() == QDialog::Accepted) {
    QListWidgetItem *selectedItem = listWidget->currentItem();
    if (selectedItem) {
      QString branch = selectedItem->text().remove(" ✓");
      if (branch != current) {
        QMetaObject::invokeMethod(gitWorker, "switchBranch",
          Qt::QueuedConnection, Q_ARG(QString, branch));
      }
    }
  }

  dialog->deleteLater();
}

void BPUpdaterPanelV2::showHistoryDialog(const QStringList &commits) {
  QDialog *dialog = new QDialog(this);
  dialog->setWindowTitle(tr("Commit History"));
  dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

#ifdef QCOM2
  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  wl_surface *s = reinterpret_cast<wl_surface *>(
    native->nativeResourceForWindow("surface", dialog->windowHandle()));
  if (s) {
    wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
    wl_surface_commit(s);
  }
  dialog->setWindowState(Qt::WindowFullScreen);
#endif

  BPTextSizes sizes = BPTextSizes::getSizes();

  QVBoxLayout *layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(50, 50, 50, 50);
  layout->setSpacing(20);

  QLabel *title = new QLabel(tr("Recent Commits"));
  title->setStyleSheet(QString(R"(
    font-size: %1px;
    color: white;
    font-weight: 600;
  )").arg(sizes.titleSize + 10));
  layout->addWidget(title);

  QTableWidget *table = new QTableWidget();
  table->setColumnCount(3);
  table->setHorizontalHeaderLabels({tr("Hash"), tr("Message"), tr("Author (Time)")});
  table->setStyleSheet(QString(R"(
    QTableWidget {
      background-color: #1B1B1B;
      border: 1px solid #4A90E2;
      border-radius: 10px;
      font-size: %1px;
      color: white;
      gridline-color: #333333;
    }
    QHeaderView::section {
      background-color: #2A2A2A;
      color: white;
      font-weight: 600;
      border: none;
      padding: 15px;
    }
    QTableWidget::item {
      padding: 15px;
    }
  )").arg(sizes.descSize));

  table->setRowCount(commits.size());
  for (int i = 0; i < commits.size(); i++) {
    QStringList parts = commits[i].split('|');
    if (parts.size() >= 4) {
      table->setItem(i, 0, new QTableWidgetItem(parts[0]));
      table->setItem(i, 1, new QTableWidgetItem(parts[1]));
      table->setItem(i, 2, new QTableWidgetItem(QString("%1 (%2)").arg(parts[3], parts[2])));
    }
  }

  table->horizontalHeader()->setStretchLastSection(true);
  table->verticalHeader()->setVisible(false);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);

  layout->addWidget(table);

  QPushButton *closeBtn = new QPushButton(tr("Close"));
  closeBtn->setMinimumHeight(90);
  closeBtn->setStyleSheet(QString(R"(
    QPushButton {
      background-color: #4A90E2;
      color: white;
      font-size: %1px;
      font-weight: 500;
      border-radius: 15px;
    }
  )").arg(sizes.buttonTextSize));
  connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
  layout->addWidget(closeBtn);

  dialog->exec();
  dialog->deleteLater();
}
