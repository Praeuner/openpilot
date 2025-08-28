#include "selfdrive/ui/qt/widgets/wifi.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>

WiFiPromptWidget::WiFiPromptWidget(QWidget *parent) : QFrame(parent) {
  // Setup Firehose Mode
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(56, 40, 56, 40);
  main_layout->setSpacing(42);

  QLabel *title = new QLabel(tr("<span style='font-family: \"Noto Color Emoji\";'>🔥</span> Firehose Mode <span style='font-family: Noto Color Emoji;'>🔥</span>"));
  title->setStyleSheet("font-size: 64px; font-weight: 500;");
  main_layout->addWidget(title);

  QLabel *desc = new QLabel(tr("Maximize your training data uploads to improve openpilot's driving models."));
  desc->setStyleSheet("font-size: 40px; font-weight: 400;");
  desc->setWordWrap(true);
  main_layout->addWidget(desc);

  QPushButton *settings_btn = new QPushButton(tr("Open"));
  connect(settings_btn, &QPushButton::clicked, [=]() { emit openSettings(1, "FirehosePanel"); });
  settings_btn->setStyleSheet(R"(
    QPushButton {
      font-size: 48px;
      font-weight: 500;
      border-radius: 15px;
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #465BEA, stop: 1 #3049F4);
      padding: 32px;
      border: 1px solid rgba(255, 255, 255, 0.1);
      color: white;
    }
    QPushButton:pressed {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #3049F4, stop: 1 #1a2f8f);
    }
    QPushButton:hover {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #5a6fef, stop: 1 #3a59f9);
    }
  )");
  main_layout->addWidget(settings_btn);

  setStyleSheet(R"(
    WiFiPromptWidget {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #2c2c2c, stop: 1 #1a1a1a);
      border-radius: 15px;
      border: 1px solid rgba(255, 255, 255, 0.1);
    }
  )");
}
