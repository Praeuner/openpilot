/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once
#include <QPainter>
#include "selfdrive/ui/qt/onroad/buttons.h"

class ExperimentalButtonSP : public ExperimentalButton {
  Q_OBJECT

public:
  explicit ExperimentalButtonSP(QWidget *parent = nullptr);
  void updateState(const UIState &s) override;

protected:
  void paintEvent(QPaintEvent *event) override {
    QPainter p(this);

    // Apply rotation
    p.translate(width() / 2, height() / 2);
    p.rotate(-steeringAngle);
    p.translate(-width() / 2, -height() / 2);

    // Draw whichever pixmap matches the current mode
    const QPixmap &img = experimental_mode ? experimental_img : engage_img;
    drawIcon(p, QPoint(btn_size / 2, btn_size / 2), img, QColor(0, 0, 0, 166), (isDown() || !engageable) ? 0.6f : 1.0f);
  }

  float steeringAngle = 0.0;
  bool showAnimatedWheel = false;

private:
  void drawButton(QPainter &p) override;

  bool dynamic_experimental_control;
  int dec_mpc_mode;
};
