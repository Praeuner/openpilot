// selfdrive/ui/bluepilot/qt/offroad/panels/updater/bp_status_card.cc

#include "bp_status_card.h"
#include "../bp_utils.h"

BPStatusCard::BPStatusCard(QWidget *parent) : QFrame(parent) {
  setupUI();
}

void BPStatusCard::setupUI() {
  setObjectName("bp_status_card");

  BPTextSizes sizes = BPTextSizes::getSizes();

  // Modern card styling matching bp_panel_controls
  setStyleSheet(QString(R"(
    QFrame#bp_status_card {
      background-color: #242424;
      border-radius: 15px;
      border: none;
    }
  )"));

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(30, 25, 30, 25);
  mainLayout->setSpacing(20);

  // Branch section
  QHBoxLayout *branchLayout = new QHBoxLayout();
  branchLayout->setSpacing(15);

  QLabel *branchIcon = new QLabel("🔀");
  branchIcon->setStyleSheet(QString("font-size: %1px;").arg(sizes.titleSize));
  branchLayout->addWidget(branchIcon);

  branchLabel = new QLabel("Loading...");
  branchLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    color: white;
    font-weight: 600;
  )").arg(sizes.titleSize));
  branchLayout->addWidget(branchLabel);
  branchLayout->addStretch();

  mainLayout->addLayout(branchLayout);

  // Commit info section
  commitLabel = new QLabel();
  commitLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    color: #AAAAAA;
    padding: 10px 0px;
  )").arg(sizes.descSize));
  commitLabel->setWordWrap(true);
  mainLayout->addWidget(commitLabel);

  // Status section with icon
  QHBoxLayout *statusLayout = new QHBoxLayout();
  statusLayout->setSpacing(15);

  statusIconLabel = new QLabel();
  statusIconLabel->setStyleSheet(QString("font-size: %1px;").arg(sizes.titleSize + 10));
  statusLayout->addWidget(statusIconLabel);

  statusTextLabel = new QLabel();
  statusTextLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    font-weight: 500;
  )").arg(sizes.titleSize - 5));
  statusLayout->addWidget(statusTextLabel);
  statusLayout->addStretch();

  mainLayout->addLayout(statusLayout);

  // Updates available label
  updatesLabel = new QLabel();
  updatesLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    color: #4A90E2;
    font-weight: 600;
    padding: 15px;
    background-color: rgba(74, 144, 226, 0.1);
    border-radius: 10px;
    border: 2px solid #4A90E2;
  )").arg(sizes.titleSize - 5));
  updatesLabel->setAlignment(Qt::AlignCenter);
  updatesLabel->setVisible(false);
  mainLayout->addWidget(updatesLabel);
}

void BPStatusCard::updateStatus(const GitStatus &status) {
  BPTextSizes sizes = BPTextSizes::getSizes();

  // Update branch
  branchLabel->setText(status.currentBranch.isEmpty() ? "Unknown Branch" : status.currentBranch);

  // Update commit info
  QString commitText;
  if (!status.lastCommitHash.isEmpty()) {
    commitText = QString("<span style='color: #4A90E2; font-family: monospace; font-weight: 600;'>%1</span> "
                        "<span style='color: #CCCCCC;'>%2</span> "
                        "<span style='color: #888888;'>(%3)</span>")
                    .arg(status.lastCommitHash)
                    .arg(status.lastCommitMessage)
                    .arg(status.lastCommitTime);
  } else {
    commitText = "No commit information available";
  }
  commitLabel->setText(commitText);

  // Update status
  QString statusColor = getStatusColor(status);
  QString statusIcon = getStatusIcon(status);
  QString statusText = getStatusText(status);

  statusIconLabel->setText(statusIcon);
  statusTextLabel->setText(statusText);
  statusTextLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    font-weight: 500;
    color: %2;
  )").arg(sizes.titleSize - 5).arg(statusColor));

  // Update availability info
  if (status.hasUpdatesAvailable && status.commitsBehind > 0) {
    updatesLabel->setText(QString("📥 %1 Update%2 Available")
                         .arg(status.commitsBehind)
                         .arg(status.commitsBehind > 1 ? "s" : ""));
    updatesLabel->setVisible(true);
  } else {
    updatesLabel->setVisible(false);
  }

  setCheckingState(false);
}

void BPStatusCard::setCheckingState(bool checking) {
  if (checking) {
    statusIconLabel->setText("🔄");
    statusTextLabel->setText("Checking...");
    statusTextLabel->setStyleSheet(QString(R"(
      font-size: %1px;
      font-weight: 500;
      color: #AAAAAA;
    )").arg(BPTextSizes::getSizes().titleSize - 5));
  }
}

QString BPStatusCard::getStatusColor(const GitStatus &status) const {
  if (status.hasLocalChanges) {
    return "#FF9800";  // Orange for modified
  } else if (status.hasUpdatesAvailable) {
    return "#4A90E2";  // Blue for updates available
  } else {
    return "#50d332";  // Green for clean/up-to-date
  }
}

QString BPStatusCard::getStatusIcon(const GitStatus &status) const {
  if (status.hasLocalChanges) {
    return "⚠️";  // Warning for local changes
  } else if (status.hasUpdatesAvailable) {
    return "📥";  // Download for updates
  } else {
    return "✅";  // Checkmark for clean
  }
}

QString BPStatusCard::getStatusText(const GitStatus &status) const {
  if (status.hasLocalChanges) {
    return "Local Changes Present";
  } else if (status.hasUpdatesAvailable) {
    return "Updates Available";
  } else {
    return "Up to Date";
  }
}
