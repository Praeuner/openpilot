// selfdrive/ui/bluepilot/qt/offroad/panels/updater/bp_updater_panel_v2.cc

#include "bp_updater_panel_v2.h"
#include "../bp_utils.h"
#include "../bp_panel_dialogs.h"
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

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#endif

BPUpdaterPanelV2::BPUpdaterPanelV2(QWidget *parent)
    : QWidget(parent), isOnroad(false), operationInProgress(false) {

  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setContentsMargins(20, 20, 20, 20);

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

void BPUpdaterPanelV2::setupUI() {
  BPTextSizes sizes = BPTextSizes::getSizes();

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(25);

  // Header
  QLabel *headerLabel = new QLabel("Software Updates");
  headerLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    color: white;
    font-weight: 600;
    padding: 10px 0px;
  )").arg(sizes.titleSize + 15));
  mainLayout->addWidget(headerLabel);

  // Warning label (for onroad status)
  warningLabel = new QLabel();
  warningLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    color: #FF6B6B;
    background-color: rgba(255, 107, 107, 0.1);
    border: 2px solid #FF6B6B;
    border-radius: 12px;
    padding: 20px;
    font-weight: 500;
  )").arg(sizes.titleSize - 5));
  warningLabel->setAlignment(Qt::AlignCenter);
  warningLabel->setVisible(false);
  mainLayout->addWidget(warningLabel);

  // Status card
  statusCard = new BPStatusCard(this);
  mainLayout->addWidget(statusCard);

  // Actions section
  QFrame *actionsFrame = new QFrame(this);
  actionsFrame->setObjectName("bp_actions_frame");
  actionsFrame->setStyleSheet(R"(
    QFrame#bp_actions_frame {
      background-color: #242424;
      border-radius: 15px;
    }
  )");

  QVBoxLayout *actionsLayout = new QVBoxLayout(actionsFrame);
  actionsLayout->setContentsMargins(30, 25, 30, 25);
  actionsLayout->setSpacing(15);

  // Actions title
  QLabel *actionsTitle = new QLabel("Actions");
  actionsTitle->setStyleSheet(QString(R"(
    font-size: %1px;
    color: white;
    font-weight: 600;
  )").arg(sizes.titleSize));
  actionsLayout->addWidget(actionsTitle);

  // Primary actions (2 columns)
  QGridLayout *primaryGrid = new QGridLayout();
  primaryGrid->setSpacing(15);

  QString primaryButtonStyle = QString(R"(
    QPushButton {
      background-color: #4A90E2;
      color: white;
      font-size: %1px;
      font-weight: 500;
      border-radius: 15px;
      padding: 25px;
      border: none;
      min-height: 100px;
    }
    QPushButton:hover {
      background-color: #5BA3F5;
    }
    QPushButton:pressed {
      background-color: #3A7DC2;
    }
    QPushButton:disabled {
      background-color: #2A2A2A;
      color: #666666;
    }
  )").arg(sizes.buttonTextSize);

  checkUpdatesBtn = new QPushButton("🔍 Check for Updates");
  checkUpdatesBtn->setStyleSheet(primaryButtonStyle);
  connect(checkUpdatesBtn, &QPushButton::clicked, this, &BPUpdaterPanelV2::handleCheckUpdates);
  primaryGrid->addWidget(checkUpdatesBtn, 0, 0);

  updateBtn = new QPushButton("📥 Update");
  updateBtn->setStyleSheet(primaryButtonStyle);
  updateBtn->setEnabled(false);
  connect(updateBtn, &QPushButton::clicked, this, &BPUpdaterPanelV2::handleUpdate);
  primaryGrid->addWidget(updateBtn, 0, 1);

  switchBranchBtn = new QPushButton("🔀 Switch Branch");
  switchBranchBtn->setStyleSheet(primaryButtonStyle);
  connect(switchBranchBtn, &QPushButton::clicked, this, &BPUpdaterPanelV2::handleSwitchBranch);
  primaryGrid->addWidget(switchBranchBtn, 1, 0);

  historyBtn = new QPushButton("📜 View History");
  historyBtn->setStyleSheet(primaryButtonStyle);
  connect(historyBtn, &QPushButton::clicked, this, &BPUpdaterPanelV2::handleViewHistory);
  primaryGrid->addWidget(historyBtn, 1, 1);

  actionsLayout->addLayout(primaryGrid);

  // Secondary actions (advanced)
  QLabel *advancedTitle = new QLabel("Advanced");
  advancedTitle->setStyleSheet(QString(R"(
    font-size: %1px;
    color: #AAAAAA;
    font-weight: 500;
    padding-top: 10px;
  )").arg(sizes.descSize));
  actionsLayout->addWidget(advancedTitle);

  QString secondaryButtonStyle = QString(R"(
    QPushButton {
      background-color: #363636;
      color: white;
      font-size: %1px;
      font-weight: 500;
      border-radius: 12px;
      padding: 20px;
      border: none;
      min-height: 80px;
    }
    QPushButton:hover {
      background-color: #404040;
    }
    QPushButton:pressed {
      background-color: #505050;
    }
    QPushButton:disabled {
      background-color: #202020;
      color: #666666;
    }
  )").arg(sizes.buttonTextSize - 5);

  QGridLayout *secondaryGrid = new QGridLayout();
  secondaryGrid->setSpacing(15);

  resetBtn = new QPushButton("↩️ Reset");
  resetBtn->setStyleSheet(secondaryButtonStyle);
  connect(resetBtn, &QPushButton::clicked, this, &BPUpdaterPanelV2::handleReset);
  secondaryGrid->addWidget(resetBtn, 0, 0);

  repairBtn = new QPushButton("🔧 Repair");
  repairBtn->setStyleSheet(secondaryButtonStyle);
  connect(repairBtn, &QPushButton::clicked, this, &BPUpdaterPanelV2::handleRepair);
  secondaryGrid->addWidget(repairBtn, 0, 1);

  unshallowBtn = new QPushButton("📦 Unshallow");
  unshallowBtn->setStyleSheet(secondaryButtonStyle);
  connect(unshallowBtn, &QPushButton::clicked, this, &BPUpdaterPanelV2::handleUnshallow);
  secondaryGrid->addWidget(unshallowBtn, 0, 2);

  actionsLayout->addLayout(secondaryGrid);

  mainLayout->addWidget(actionsFrame);
  mainLayout->addStretch();

  // Progress overlay
  progressOverlay = new BPProgressOverlay(this);
  connect(progressOverlay, &BPProgressOverlay::cancelRequested, this, [this]() {
    // TODO: Implement cancellation
    progressOverlay->hide();
  });
  connect(progressOverlay, &BPProgressOverlay::retryRequested, this, [this]() {
    progressOverlay->hide();
    // Last operation will be retried based on context
  });
  connect(progressOverlay, &BPProgressOverlay::closeRequested, this, [this]() {
    progressOverlay->hide();
    // Refresh status after operation
    QMetaObject::invokeMethod(gitWorker, "checkStatus", Qt::QueuedConnection);
  });
}

void BPUpdaterPanelV2::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  // Refresh status when shown
  QMetaObject::invokeMethod(gitWorker, "checkStatus", Qt::QueuedConnection);
}

void BPUpdaterPanelV2::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
}

void BPUpdaterPanelV2::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);

  // Resize overlay to match panel
  if (progressOverlay) {
    progressOverlay->setGeometry(rect());
  }
}

void BPUpdaterPanelV2::checkOnroadStatus() {
  bool wasOnroad = isOnroad;
  isOnroad = params.getBool("IsOnroad");

  if (isOnroad != wasOnroad) {
    if (isOnroad) {
      warningLabel->setText(getOnroadWarning());
      warningLabel->setVisible(true);
    } else {
      warningLabel->setVisible(false);
    }
    updateButtonStates();
  }
}

QString BPUpdaterPanelV2::getOnroadWarning() const {
  return "⚠️ Vehicle is in motion - Updates disabled for safety";
}

bool BPUpdaterPanelV2::canPerformOperations() const {
  return !isOnroad && !operationInProgress;
}

void BPUpdaterPanelV2::updateButtonStates() {
  bool canOperate = canPerformOperations();

  checkUpdatesBtn->setEnabled(canOperate);
  updateBtn->setEnabled(canOperate && currentStatus.hasUpdatesAvailable);
  switchBranchBtn->setEnabled(canOperate);
  historyBtn->setEnabled(canOperate);
  resetBtn->setEnabled(canOperate && currentStatus.hasLocalChanges);
  repairBtn->setEnabled(canOperate);
  unshallowBtn->setEnabled(canOperate);
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
  // Show branch selector dialog
  if (branches.isEmpty()) {
    BPUpdateConfirmDialog::alert("No branches found", this);
    return;
  }

  QString current = currentStatus.currentBranch;
  QDialog *dialog = new QDialog(this);
  dialog->setWindowTitle("Select Branch");
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

  QLabel *title = new QLabel("Select Branch");
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

  QPushButton *cancelBtn = new QPushButton("Cancel");
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

  QPushButton *selectBtn = new QPushButton("Switch");
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

  showConfirmDialog(
    "Update Repository",
    "This will update the repository to the latest version. Continue?",
    [this]() {
      QMetaObject::invokeMethod(gitWorker, "updateRepository", Qt::QueuedConnection);
    }
  );
}

void BPUpdaterPanelV2::handleSwitchBranch() {
  if (!canPerformOperations()) return;

  // Request branch list
  QMetaObject::invokeMethod(gitWorker, "fetchBranches",
    Qt::QueuedConnection, Q_ARG(bool, true));
}

void BPUpdaterPanelV2::handleReset() {
  if (!canPerformOperations()) return;

  showConfirmDialog(
    "Reset Repository",
    "⚠️ This will discard all local changes and reset to HEAD. This cannot be undone. Continue?",
    [this]() {
      QMetaObject::invokeMethod(gitWorker, "resetRepository", Qt::QueuedConnection);
    }
  );
}

void BPUpdaterPanelV2::handleRepair() {
  if (!canPerformOperations()) return;

  showConfirmDialog(
    "Repair Repository",
    "This will run git fsck and gc to repair the repository. This may take several minutes. Continue?",
    [this]() {
      QMetaObject::invokeMethod(gitWorker, "repairRepository", Qt::QueuedConnection);
    }
  );
}

void BPUpdaterPanelV2::handleUnshallow() {
  if (!canPerformOperations()) return;

  showConfirmDialog(
    "Unshallow Repository",
    "This will fetch the complete git history. This may take a long time and use significant disk space. Continue?",
    [this]() {
      QMetaObject::invokeMethod(gitWorker, "unshallowRepository", Qt::QueuedConnection);
    }
  );
}

void BPUpdaterPanelV2::handleViewHistory() {
  if (!canPerformOperations()) return;

  QMetaObject::invokeMethod(gitWorker, "fetchCommitHistory",
    Qt::QueuedConnection, Q_ARG(int, 30));
}

void BPUpdaterPanelV2::showConfirmDialog(const QString &title, const QString &message,
                                        std::function<void()> onConfirm) {
  if (BPUpdateConfirmDialog::confirm(title, message, "Continue", "Cancel", this)) {
    onConfirm();
  }
}

void BPUpdaterPanelV2::showHistoryDialog(const QStringList &commits) {
  QDialog *dialog = new QDialog(this);
  dialog->setWindowTitle("Commit History");
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

  QLabel *title = new QLabel("Recent Commits");
  title->setStyleSheet(QString(R"(
    font-size: %1px;
    color: white;
    font-weight: 600;
  )").arg(sizes.titleSize + 10));
  layout->addWidget(title);

  QTableWidget *table = new QTableWidget();
  table->setColumnCount(3);
  table->setHorizontalHeaderLabels({"Hash", "Message", "Author (Time)"});
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

  QPushButton *closeBtn = new QPushButton("Close");
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
