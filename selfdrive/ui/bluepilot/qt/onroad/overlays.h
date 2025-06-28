#pragma once

#include <QPainter>
#include <QRect>
#include "selfdrive/ui/ui.h"

class BluepilotOverlays {
public:
  static void drawAll(QPainter &painter, const QRect &rect, const UIState &s);
  
private:
  static void drawBlinkers(QPainter &painter, const QRect &rect, const UIState &s);
  static void drawStandstillTimer(QPainter &painter, const QRect &rect, const UIState &s);
  static void drawStopSign(QPainter &painter, const QRect &rect, const UIState &s);
  
  // Blinker state
  static int blinker_frame;
  static int blinker_state;
  
  // Standstill timer state
  static double standstill_start_time;
  static float standstillElapsedTime;
  
  // Helper methods
  static void drawLeftTurnSignal(QPainter &painter, int x, int y, int size, int state);
  static void drawRightTurnSignal(QPainter &painter, int x, int y, int size, int state);
  static int blinkerPulse(int frame);
  static void drawColoredText(QPainter &p, int x, int y, const QString &text, QColor color);
};
