/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include "selfdrive/ui/sunnypilot/qt/onroad/buttons.h"

class ExperimentalButtonBP : public ExperimentalButtonSP {
  Q_OBJECT

public:
  explicit ExperimentalButtonBP(QWidget *parent = nullptr);
  void updateState(const UIState &s) override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  float steeringAngle = 0.0;
  bool showAnimatedWheel = false;
};
