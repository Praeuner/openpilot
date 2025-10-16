#pragma once

#include <QPainter>
#include <QRect>
#include <QPointF>
#include <QPolygonF>
#include "selfdrive/ui/ui.h"

class StopSignOverlay {
public:
  struct StopState {
    bool active = false;
    int stability_counter = 0;
    float stopping_distance = 0.0f;
    float display_distance = 0.0f;
    QPointF last_valid_position;
    float fade_alpha = 0.0f;
  };

  static void render(QPainter &painter, const QRect &rect, const UIState &s,
                    StopState &stop_state, const QRectF &clip_region,
                    const QPolygonF lane_line_vertices[4], float vehicle_speed);

  static void drawStopSignOverlay(QPainter &painter, const QPointF &point,
                                 float distance, float alpha, bool is_metric);
};