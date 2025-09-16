#pragma once

#include <QPainter>
#include <QRect>
#include <QString>
#include "selfdrive/ui/ui.h"

class StandstillTimerOverlay {
public:
  struct StandstillState {
    bool standstill = false;
    double standstill_start_time = 0.0;
    float standstill_elapsed = 0.0;
    bool prev_standstill = false;
    double standstill_exit_time = 0.0;
    float vehicle_speed = 0.0f;
  };

  static void render(QPainter &painter, const QRect &rect, const UIState &s,
                    StandstillState &standstill_state);

private:
  static void drawColoredText(QPainter &painter, int x, int y, const QString &text, QColor color);

  static constexpr float STANDSTILL_THRESHOLD = 0.1f;
};