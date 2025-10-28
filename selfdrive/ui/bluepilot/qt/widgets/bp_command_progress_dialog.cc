// selfdrive/ui/bluepilot/qt/widgets/bp_command_progress_dialog.cc

#include "bp_command_progress_dialog.h"

#include <QScreen>
#include <QGuiApplication>

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#endif

BPCommandProgressDialog::BPCommandProgressDialog(const QString &title, QWidget *parent)
  : QDialog(parent), commandRunning(true) {

  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setStyleSheet("background-color: black;");
  setModal(true);

  setupUI(title);
  setupFullscreen();
}

void BPCommandProgressDialog::setupUI(const QString &title) {
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(45, 35, 45, 45);
  layout->setSpacing(30);

  // Title
  titleLabel = new QLabel(title, this);
  titleLabel->setStyleSheet(R"(
    QLabel {
      font-size: 90px;
      font-weight: 600;
      background-color: black;
      color: white;
    }
  )");
  layout->addWidget(titleLabel);

  // Progress label
  progressLabel = new QLabel(tr("Starting..."), this);
  progressLabel->setStyleSheet(R"(
    QLabel {
      font-size: 42px;
      font-weight: 500;
      color: #4CAF50;
    }
  )");
  layout->addWidget(progressLabel);

  // Output text area
  outputText = new QTextEdit(this);
  outputText->setReadOnly(true);
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
  )");
  layout->addWidget(outputText);

  // Button layout
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(20);

  // Cancel button
  cancelButton = new QPushButton(tr("Cancel"), this);
  cancelButton->setFixedHeight(100);
  cancelButton->setStyleSheet(R"(
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
  connect(cancelButton, &QPushButton::clicked, [this]() {
    BPUpdaterClient *client = qobject_cast<BPUpdaterClient*>(parent());
    if (client) {
      client->cancelCommand();
    }
    cancelButton->setEnabled(false);
    cancelButton->setText(tr("Cancelling..."));
  });
  buttonLayout->addWidget(cancelButton);

  // Close button (initially disabled)
  closeButton = new QPushButton(tr("Running..."), this);
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
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(closeButton);

  layout->addLayout(buttonLayout);
}

void BPCommandProgressDialog::setupFullscreen() {
  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen) {
    setFixedSize(2160, 1080);
  }

  show();

#ifdef QCOM2
  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  if (native && windowHandle()) {
    wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", windowHandle()));
    if (s) {
      wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
      wl_surface_commit(s);
    }
  }
  setWindowState(Qt::WindowFullScreen);
#endif
}

void BPCommandProgressDialog::setProgress(int progress) {
  progressLabel->setText(tr("Progress: %1%").arg(progress));
}

void BPCommandProgressDialog::appendOutput(const QString &output) {
  outputText->append(output);

  // Auto-scroll to bottom
  QTextCursor cursor = outputText->textCursor();
  cursor.movePosition(QTextCursor::End);
  outputText->setTextCursor(cursor);
}

void BPCommandProgressDialog::onSuccess(QJsonObject result) {
  commandRunning = false;

  // Update UI for success
  progressLabel->setText(tr("✓ Completed Successfully"));
  progressLabel->setStyleSheet(R"(
    QLabel {
      font-size: 42px;
      font-weight: 500;
      color: #4CAF50;
    }
  )");

  cancelButton->setVisible(false);

  closeButton->setEnabled(true);
  closeButton->setText(tr("Close"));
  closeButton->setStyleSheet(R"(
    QPushButton {
      background-color: #4CAF50;
      font-size: 55px;
      font-weight: 400;
      border-radius: 20px;
      color: white;
    }
    QPushButton:pressed {
      background-color: #45A049;
    }
  )");

  // Show duration if available
  if (result.contains("duration")) {
    double duration = result["duration"].toDouble();
    appendOutput(QString("\n\nCompleted in %1 seconds").arg(duration, 0, 'f', 1));
  }
}

void BPCommandProgressDialog::onFailure(const QString &error) {
  commandRunning = false;

  // Update UI for failure
  progressLabel->setText(tr("✗ Failed"));
  progressLabel->setStyleSheet(R"(
    QLabel {
      font-size: 42px;
      font-weight: 500;
      color: #EA4646;
    }
  )");

  cancelButton->setVisible(false);

  closeButton->setEnabled(true);
  closeButton->setText(tr("Close"));
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

  appendOutput(QString("\n\nError: %1").arg(error));
}
