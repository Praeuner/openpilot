// selfdrive/ui/bluepilot/qt/offroad/panels/updater/bp_status_card.h

#pragma once

#ifndef BP_STATUS_CARD_H
#define BP_STATUS_CARD_H

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "bp_git_worker.h"

// Modern status card showing repository state
class BPStatusCard : public QFrame {
  Q_OBJECT

public:
  explicit BPStatusCard(QWidget *parent = nullptr);

  void updateStatus(const GitStatus &status);
  void setCheckingState(bool checking);

private:
  QLabel *branchLabel;
  QLabel *commitLabel;
  QLabel *statusIconLabel;
  QLabel *statusTextLabel;
  QLabel *updatesLabel;

  void setupUI();
  QString getStatusColor(const GitStatus &status) const;
  QString getStatusIcon(const GitStatus &status) const;
  QString getStatusText(const GitStatus &status) const;
};

#endif // BP_STATUS_CARD_H
