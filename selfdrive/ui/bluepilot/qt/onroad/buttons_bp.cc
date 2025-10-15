/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/bluepilot/qt/onroad/buttons_bp.h"

#include <QPainter>

ExperimentalButtonBP::ExperimentalButtonBP(QWidget *parent) : ExperimentalButtonSP(parent) {
}

void ExperimentalButtonBP::updateState(const UIState &s) {
  ExperimentalButtonSP::updateState(s);

  const auto car_state = (*s.sm)["carState"].getCarState();
  showAnimatedWheel = s.scene.show_animated_wheel_angle;
  float angle = car_state.getSteeringAngleDeg();

  // Always update the steering angle if animation is enabled
  if (showAnimatedWheel && steeringAngle != angle) {
    steeringAngle = angle;
    update();
  } else if (!showAnimatedWheel && steeringAngle != 0) {
    steeringAngle = 0;
    update();
  }
}

void ExperimentalButtonBP::paintEvent(QPaintEvent *event) {
  QPainter p(this);

  // Apply rotation if animation is enabled
  if (showAnimatedWheel) {
    p.translate(width() / 2, height() / 2);
    p.rotate(-steeringAngle);
    p.translate(-width() / 2, -height() / 2);
  }

  // Call parent's drawButton to handle DEC logic
  drawButton(p);
}
