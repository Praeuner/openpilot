/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include <QLabel>
#include <QVBoxLayout>
#include <QResizeEvent>

#include "selfdrive/ui/sunnypilot/qt/widgets/controls.h"

class ModelInfoWidget : public QFrame {
  Q_OBJECT

public:
  explicit ModelInfoWidget(QWidget* parent = nullptr);

signals:
  void openSettings(int index = 0, const QString &param = "");

protected:
  void mousePressEvent(QMouseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  void updateModelName();
  QString getActiveModelName();

  QLabel* model_name_label;
  QVBoxLayout* main_layout;
};
