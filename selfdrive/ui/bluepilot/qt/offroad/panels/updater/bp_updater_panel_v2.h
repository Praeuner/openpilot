// selfdrive/ui/bluepilot/qt/offroad/panels/updater/bp_updater_panel_v2.h

#pragma once

#ifndef BP_UPDATER_PANEL_V2_H
#define BP_UPDATER_PANEL_V2_H

#include <QWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QThread>
#include <QTimer>

#include "common/params.h"
#include "bp_git_worker.h"
#include "bp_status_card.h"
#include "bp_progress_overlay.h"

// Modern, simplified updater panel
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
  BPStatusCard *statusCard;
  BPProgressOverlay *progressOverlay;

  QLabel *warningLabel;
  QPushButton *checkUpdatesBtn;
  QPushButton *updateBtn;
  QPushButton *switchBranchBtn;
  QPushButton *resetBtn;
  QPushButton *repairBtn;
  QPushButton *unshallowBtn;
  QPushButton *historyBtn;

  QTimer *onroadCheckTimer;
  bool isOnroad;
  bool operationInProgress;
  GitStatus currentStatus;

  void setupUI();
  void setupWorker();
  void showConfirmDialog(const QString &title, const QString &message,
                        std::function<void()> onConfirm);
  void showBranchSelector();
  void showHistoryDialog(const QStringList &commits);

  QString getOnroadWarning() const;
  bool canPerformOperations() const;
};

#endif // BP_UPDATER_PANEL_V2_H
