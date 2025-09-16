#pragma once

#include <QPainter>
#include <QRect>
#include <QPointF>
#include <QColor>
#include "selfdrive/ui/ui.h"

class GForceOverlay {
public:
  struct GForceState {
    bool show_gforce = false;
    float lateral_g = 0.0f;         // Left/right G-force
    float longitudinal_g = 0.0f;    // Forward/backward G-force
    float smoothed_lateral = 0.0f;
    float smoothed_longitudinal = 0.0f;
    float max_lateral = 0.0f;       // Peak values for current session
    float max_longitudinal = 0.0f;
    float max_braking = 0.0f;
    QPointF history_lateral[50];    // Trail effect points
    QPointF history_longitudinal[50];
    int history_index = 0;
  };

  static void render(QPainter &painter, const QRect &rect, const UIState &s, GForceState &gforce_state);
  static void updateGForceData(const UIState &s, GForceState &gforce_state);

private:
  static void drawGForceMeter(QPainter &painter, const QRect &rect, const UIState &s, GForceState &gforce_state);
  static QColor getGForceColor(float g_value);

  static constexpr float GRAVITY_MS2 = 9.81f;  // Standard gravity in m/s²
};
