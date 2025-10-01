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
    float smoothed_size = 120.0f;

    // Position smoothing
    QPointF smoothed_position;
    bool has_previous_position = false;
    float position_smoothing_factor = 0.15f;
    float size_smoothing_factor = 0.1f;

    // Animation tracking
    int stop_frame_count = 0;
    bool prev_stop_sign_visible = false;
  };

  static void render(QPainter &painter, const QRect &rect, const UIState &s,
                    StopState &stop_state, const QRectF &clip_region,
                    const QPolygonF lane_line_vertices[4], float vehicle_speed);

  static void drawStopSignOverlay(QPainter &painter, const QPointF &point, int size,
                                 float distance, float v_ego, float alpha, StopState &stop_state,
                                 const QRectF &clip_region);

  static void initOctagonTemplate();

private:
  static QPolygonF octagon_template;
  static bool octagon_initialized;
};