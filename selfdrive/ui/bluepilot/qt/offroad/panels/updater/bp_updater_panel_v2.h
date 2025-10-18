// selfdrive/ui/bluepilot/qt/offroad/panels/updater/bp_updater_panel_v2.h

#pragma once

#ifndef BP_UPDATER_PANEL_V2_H
#define BP_UPDATER_PANEL_V2_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QThread>
#include <QTimer>
#include <QGroupBox>
#include <QVBoxLayout>

#include "common/params.h"
#include "bp_git_worker.h"
#include "bp_status_card.h"
#include "bp_progress_overlay.h"

// Forward declarations
class BPCommandControl;

// Modern, simplified updater panel matching BP panel design
class BPUpdaterPanelV2 : public QWidget {
  Q_OBJECT

public:
  explicit BPUpdaterPanelV2(QWidget *parent = nullptr);
  ~BPUpdaterPanelV2();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void onStatusReady(const GitStatus &status);
  void onUpdatesCheckComplete(bool hasUpdates, int count);
  void onBranchListReady(const QStringList &branches);
  void onCommitHistoryReady(const QStringList &commits);
  void onOperationStarted(const QString &operation);
  void onOperationProgress(const QString &message);
  void onOperationComplete(bool success, const QString &message);
  void onErrorOccurred(const QString &error);
  void onCanOperateChanged(bool canOperate);

  void handleCheckUpdates();
  void handleUpdate();
  void handleSwitchBranch();
  void handleReset();
  void handleRepair();
  void handleUnshallow();
  void handleViewHistory();

  void updateButtonStates();
  void checkOnroadStatus();

private:
  Params params;
  QThread *workerThread;
  BPGitWorker *gitWorker;

  // UI Components
  QVBoxLayout *mainLayout;
  BPStatusCard *statusCard;
  BPProgressOverlay *progressOverlay;

  // Group Boxes
  QGroupBox *statusGroup;
  QGroupBox *actionsGroup;
  QGroupBox *advancedGroup;

  // Command Controls
  BPCommandControl *checkUpdatesControl;
  BPCommandControl *updateControl;
  BPCommandControl *switchBranchControl;
  BPCommandControl *viewHistoryControl;
  BPCommandControl *resetControl;
  BPCommandControl *repairControl;
  BPCommandControl *unshallowControl;

  QTimer *onroadCheckTimer;
  bool isOnroad;
  bool operationInProgress;
  GitStatus currentStatus;

  void setupUI();
  void setupWorker();
  void createStatusGroup();
  void createActionsGroup();
  void createAdvancedGroup();

  QGroupBox* createStyledGroupBox(const QString &title);

  void showBranchSelector(const QStringList &branches);
  void showHistoryDialog(const QStringList &commits);

  bool canPerformOperations() const;
};

#endif // BP_UPDATER_PANEL_V2_H
