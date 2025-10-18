// selfdrive/ui/bluepilot/qt/offroad/panels/updater/bp_progress_overlay.cc

#include "bp_progress_overlay.h"
#include "../bp_utils.h"

#include <QPainter>
#include <QDateTime>

BPProgressOverlay::BPProgressOverlay(QWidget *parent)
    : QWidget(parent), isComplete(false), wasSuccessful(false) {
  setupUI();
  hide();
}

void BPProgressOverlay::setupUI() {
  setAttribute(Qt::WA_StyledBackground, true);
  setAttribute(Qt::WA_TransparentForMouseEvents, false);

  BPTextSizes sizes = BPTextSizes::getSizes();

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(50, 50, 50, 50);
  mainLayout->setSpacing(0);

  // Add spacer to center content
  mainLayout->addStretch(1);

  // Content widget (centered card)
  contentWidget = new QWidget(this);
  contentWidget->setObjectName("bp_progress_content");
  contentWidget->setStyleSheet(R"(
    QWidget#bp_progress_content {
      background-color: #1B1B1B;
      border-radius: 20px;
      border: 2px solid #4A90E2;
    }
  )");

  QVBoxLayout *contentLayout = new QVBoxLayout(contentWidget);
  contentLayout->setContentsMargins(40, 30, 40, 30);
  contentLayout->setSpacing(20);

  // Title
  titleLabel = new QLabel("Operation in Progress");
  titleLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    color: white;
    font-weight: 600;
  )").arg(sizes.titleSize + 10));
  titleLabel->setAlignment(Qt::AlignCenter);
  contentLayout->addWidget(titleLabel);

  // Status label
  statusLabel = new QLabel("Starting...");
  statusLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    color: #AAAAAA;
    padding: 10px;
  )").arg(sizes.descSize));
  statusLabel->setAlignment(Qt::AlignCenter);
  statusLabel->setWordWrap(true);
  contentLayout->addWidget(statusLabel);

  // Output text area
  outputText = new QTextEdit();
  outputText->setReadOnly(true);
  outputText->setStyleSheet(QString(R"(
    QTextEdit {
      font-family: monospace;
      font-size: %1px;
      color: #C9C9C9;
      background-color: #0A0A0A;
      border: 1px solid #333333;
      border-radius: 10px;
      padding: 15px;
    }
    QTextEdit QScrollBar:vertical {
      width: 12px;
      background: #1B1B1B;
      border-radius: 6px;
    }
    QTextEdit QScrollBar::handle:vertical {
      background-color: #4A90E2;
      border-radius: 6px;
      min-height: 30px;
    }
  )").arg(sizes.descSize - 5));
  outputText->setMinimumHeight(300);
  outputText->setMaximumHeight(400);
  contentLayout->addWidget(outputText);

  // Button layout
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(20);

  // Cancel button
  cancelButton = new QPushButton("Cancel");
  cancelButton->setMinimumHeight(90);
  cancelButton->setMinimumWidth(200);
  cancelButton->setStyleSheet(QString(R"(
    QPushButton {
      background-color: #d33939;
      color: white;
      font-size: %1px;
      font-weight: 500;
      border-radius: 15px;
      border: none;
    }
    QPushButton:hover {
      background-color: #e84545;
    }
    QPushButton:pressed {
      background-color: #a82a2a;
    }
  )").arg(sizes.buttonTextSize));
  connect(cancelButton, &QPushButton::clicked, this, &BPProgressOverlay::cancelRequested);
  buttonLayout->addWidget(cancelButton);

  // Retry button (hidden initially)
  retryButton = new QPushButton("Retry");
  retryButton->setMinimumHeight(90);
  retryButton->setMinimumWidth(200);
  retryButton->setStyleSheet(QString(R"(
    QPushButton {
      background-color: #FF9800;
      color: white;
      font-size: %1px;
      font-weight: 500;
      border-radius: 15px;
      border: none;
    }
    QPushButton:hover {
      background-color: #FFB042;
    }
    QPushButton:pressed {
      background-color: #E68900;
    }
  )").arg(sizes.buttonTextSize));
  retryButton->setVisible(false);
  connect(retryButton, &QPushButton::clicked, this, &BPProgressOverlay::retryRequested);
  buttonLayout->addWidget(retryButton);

  // Close button (hidden initially)
  closeButton = new QPushButton("Close");
  closeButton->setMinimumHeight(90);
  closeButton->setMinimumWidth(200);
  closeButton->setStyleSheet(QString(R"(
    QPushButton {
      background-color: #4A90E2;
      color: white;
      font-size: %1px;
      font-weight: 500;
      border-radius: 15px;
      border: none;
    }
    QPushButton:hover {
      background-color: #5BA3F5;
    }
    QPushButton:pressed {
      background-color: #3A7DC2;
    }
  )").arg(sizes.buttonTextSize));
  closeButton->setVisible(false);
  connect(closeButton, &QPushButton::clicked, this, &BPProgressOverlay::closeRequested);
  buttonLayout->addWidget(closeButton);

  contentLayout->addLayout(buttonLayout);

  // Set content widget size constraints
  contentWidget->setMinimumWidth(800);
  contentWidget->setMaximumWidth(1200);

  mainLayout->addWidget(contentWidget, 0, Qt::AlignCenter);
  mainLayout->addStretch(1);
}

void BPProgressOverlay::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(0, 0, 0, 180));  // Semi-transparent black
  QWidget::paintEvent(event);
}

void BPProgressOverlay::showOperation(const QString &operation) {
  BPTextSizes sizes = BPTextSizes::getSizes();

  titleLabel->setText(operation);
  statusLabel->setText("Starting...");
  statusLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    color: #AAAAAA;
    padding: 10px;
  )").arg(sizes.descSize));

  outputText->clear();
  appendOutput(QString("[%1] %2 started")
    .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
    .arg(operation));

  isComplete = false;
  wasSuccessful = false;

  cancelButton->setVisible(true);
  retryButton->setVisible(false);
  closeButton->setVisible(false);

  show();
  raise();
  fadeIn();
}

void BPProgressOverlay::updateProgress(const QString &message) {
  if (isComplete) return;

  statusLabel->setText(message);
  appendOutput(QString("[%1] %2")
    .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
    .arg(message));
}

void BPProgressOverlay::showComplete(bool success, const QString &message) {
  BPTextSizes sizes = BPTextSizes::getSizes();

  isComplete = true;
  wasSuccessful = success;

  QString icon = success ? "✅" : "❌";
  QString color = success ? "#50d332" : "#d33939";

  titleLabel->setText(icon + " " + (success ? "Success" : "Failed"));
  statusLabel->setText(message);
  statusLabel->setStyleSheet(QString(R"(
    font-size: %1px;
    color: %2;
    padding: 10px;
    font-weight: 600;
  )").arg(sizes.descSize).arg(color));

  appendOutput(QString("[%1] %2: %3")
    .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
    .arg(success ? "COMPLETED" : "FAILED")
    .arg(message));

  cancelButton->setVisible(false);
  retryButton->setVisible(!success);
  closeButton->setVisible(true);

  // Auto-close after 3 seconds if successful
  if (success) {
    QTimer::singleShot(3000, this, [this]() {
      if (isComplete && wasSuccessful) {
        emit closeRequested();
      }
    });
  }
}

void BPProgressOverlay::showError(const QString &error) {
  showComplete(false, error);
}

void BPProgressOverlay::fadeIn() {
  QPropertyAnimation *animation = new QPropertyAnimation(this, "windowOpacity");
  animation->setDuration(200);
  animation->setStartValue(0.0);
  animation->setEndValue(1.0);
  animation->start(QPropertyAnimation::DeleteWhenStopped);
}

void BPProgressOverlay::fadeOut() {
  QPropertyAnimation *animation = new QPropertyAnimation(this, "windowOpacity");
  animation->setDuration(200);
  animation->setStartValue(1.0);
  animation->setEndValue(0.0);
  connect(animation, &QPropertyAnimation::finished, this, &QWidget::hide);
  animation->start(QPropertyAnimation::DeleteWhenStopped);
}

void BPProgressOverlay::appendOutput(const QString &text) {
  outputText->append(text);
  // Auto-scroll to bottom
  QTextCursor cursor = outputText->textCursor();
  cursor.movePosition(QTextCursor::End);
  outputText->setTextCursor(cursor);
}
