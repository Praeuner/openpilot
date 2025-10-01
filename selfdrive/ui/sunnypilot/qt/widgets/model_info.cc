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
#include <QLabel>
#include <QMouseEvent>
#include <QFontMetrics>
#include <algorithm>

#include "common/model.h"
#include "common/params.h"
#include "cereal/messaging/messaging.h"
#include "selfdrive/ui/sunnypilot/ui.h"

ModelInfoWidget::ModelInfoWidget(QWidget* parent) : QFrame(parent) {
  main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(20, 30, 20, 30);
  main_layout->setSpacing(15);

  // Title
  QLabel* title_label = new QLabel(tr("Driving Model"));
  title_label->setProperty("type", "title");
  main_layout->addWidget(title_label);

  // Model name container
  QFrame* model_container = new QFrame;
  model_container->setProperty("type", "model_container");
  QVBoxLayout* model_layout = new QVBoxLayout(model_container);
  model_layout->setContentsMargins(20, 15, 20, 15);
  model_layout->setSpacing(2);

  model_name_label = new QLabel();
  model_name_label->setProperty("type", "model_name");
  model_name_label->setWordWrap(false);
  model_name_label->setAlignment(Qt::AlignCenter);
  model_name_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  model_layout->addWidget(model_name_label);

  main_layout->addWidget(model_container);
  main_layout->addStretch();

  // Make the entire widget clickable
  setCursor(Qt::PointingHandCursor);

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

    ModelInfoWidget:hover {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #323232, stop: 1 #1f1f1f);
      border: 1px solid rgba(255, 255, 255, 0.15);
    }

    QLabel[type="title"] {
      font-size: 48px;
      font-weight: 600;
      color: #ffffff;
      padding: 10px 0px;
    }

    QLabel[type="model_name"] {
      font-size: 38px;
      font-weight: 500;
      color: #18b4ff;
      padding: 8px 0px;
      min-height: 45px;
    }

    QFrame[type="model_container"] {
      background-color: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 12px;
    }
  )");
}

void ModelInfoWidget::updateModelName() {
  QString model_name = getActiveModelName();
  model_name_label->setText(model_name);
  
  // Scale font size to fit width if necessary
  QFont font = model_name_label->font();
  font.setPixelSize(38);
  QFontMetrics fm(font);
  
  int container_width = width() - 80; // Account for margins and padding
  int text_width = fm.horizontalAdvance(model_name);
  
  if (text_width > container_width && container_width > 0) {
    int new_size = 38 * container_width / text_width;
    new_size = std::max(new_size, 24); // Minimum font size
    font.setPixelSize(new_size);
  }
  
  model_name_label->setFont(font);
}

void ModelInfoWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    emit openSettings(6); // Models panel is at index 6
  }
}

void ModelInfoWidget::resizeEvent(QResizeEvent* event) {
  QFrame::resizeEvent(event);
  updateModelName(); // Re-scale text when widget is resized
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
