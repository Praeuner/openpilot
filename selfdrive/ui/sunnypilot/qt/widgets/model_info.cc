/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/sunnypilot/qt/widgets/model_info.h"

#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

#include "common/model.h"
#include "common/params.h"
#include "cereal/messaging/messaging.h"
#include "selfdrive/ui/sunnypilot/ui.h"

ModelInfoWidget::ModelInfoWidget(QWidget* parent) : QFrame(parent) {
  main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(15, 15, 15, 15);
  main_layout->setSpacing(10);

  // Title
  QLabel* title_label = new QLabel(tr("Driving Model"));
  title_label->setProperty("type", "title");
  main_layout->addWidget(title_label);

  // Model name container
  QFrame* model_container = new QFrame;
  model_container->setProperty("type", "model_container");
  QVBoxLayout* model_layout = new QVBoxLayout(model_container);
  model_layout->setContentsMargins(15, 8, 15, 8);
  model_layout->setSpacing(2);

  model_name_label = new QLabel();
  model_name_label->setProperty("type", "model_name");
  model_name_label->setWordWrap(true);
  model_name_label->setAlignment(Qt::AlignCenter);
  model_layout->addWidget(model_name_label);

  main_layout->addWidget(model_container);

  // Settings button
  settings_button = new QPushButton(tr("Model Settings"));
  settings_button->setProperty("type", "settings_button");
  settings_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  main_layout->addWidget(settings_button, 0, Qt::AlignCenter);

  // Connect button to open settings
  QObject::connect(settings_button, &QPushButton::clicked, [=]() {
    emit openSettings(6); // Models panel is at index 6
  });

  // Connect to UI state updates to refresh model name
  QObject::connect(uiStateSP(), &UIStateSP::uiUpdate, this, &ModelInfoWidget::updateModelName);

  // Initial update
  updateModelName();

  setStyleSheet(R"(
    ModelInfoWidget {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #2c2c2c, stop: 1 #1a1a1a);
      border-radius: 15px;
      border: 1px solid rgba(255, 255, 255, 0.1);
    }

    QLabel[type="title"] {
      font-size: 40px;
      font-weight: 600;
      color: #ffffff;
      padding: 8px 0px;
    }

    QLabel[type="model_name"] {
      font-size: 32px;
      font-weight: 500;
      color: #18b4ff;
      padding: 4px 0px;
      min-height: 30px;
    }

    QPushButton[type="settings_button"] {
      font-size: 30px;
      font-weight: 500;
      color: #ffffff;
      background-color: #364DEF;
      border: none;
      border-radius: 8px;
      padding: 16px 24px;
      margin-top: 5px;
    }

    QPushButton[type="settings_button"]:pressed {
      background-color: #2a3bc7;
    }

    QFrame[type="model_container"] {
      background-color: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 12px;
    }
  )");
}

void ModelInfoWidget::updateModelName() {
  model_name_label->setText(getActiveModelName());
}

QString ModelInfoWidget::getActiveModelName() {
  try {
    const SubMaster &sm = *(uiStateSP()->sm);
    cereal::ModelManagerSP::Reader model_manager = sm["modelManagerSP"].getModelManagerSP();

    if (model_manager.hasActiveBundle()) {
      return QString::fromStdString(model_manager.getActiveBundle().getDisplayName());
    }
  } catch (const std::exception& e) {
    qDebug() << "Error getting active model name:" << e.what();
  }

  return DEFAULT_MODEL;
}
