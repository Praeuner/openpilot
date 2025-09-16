#include "selfdrive/ui/bluepilot/qt/onroad/overlays/stop_sign_overlay.h"
#include "selfdrive/ui/qt/util.h"
#include "common/timing.h"
#include <QApplication>
#include <QPen>
#include <QFont>
#include <QRect>
#include <cmath>
#include <algorithm>
#include <limits>

QPolygonF StopSignOverlay::octagon_template;
bool StopSignOverlay::octagon_initialized = false;

void StopSignOverlay::render(QPainter &painter, const QRect &rect, const UIState &s,
                            StopState &stop_state, const QRectF &clip_region,
                            const QPolygonF lane_line_vertices[4], float vehicle_speed) {
  if (!stop_state.active && stop_state.fade_alpha < 0.02f) {
    return;
  }

  float v_ego = vehicle_speed;
  if (v_ego < 0.5f) return;

  if (stop_state.active || stop_state.fade_alpha > 0.0f) {
    // Get position for stop sign (use last valid if current is invalid)
    QPointF screen_point = stop_state.last_valid_position;
    const int stop_sign_size = static_cast<int>(stop_state.smoothed_size);

    // Position relative to lane lines as in original code
    if (!lane_line_vertices[2].isEmpty() && !screen_point.isNull()) {
      // Find the closest point on the right lane line to the stopping point
      int closest_idx = 0;
      float min_dist = std::numeric_limits<float>::max();
      for (int i = 0; i < lane_line_vertices[2].size(); ++i) {
        float dist = std::hypot(screen_point.x() - lane_line_vertices[2][i].x(),
                               screen_point.y() - lane_line_vertices[2][i].y());
        if (dist < min_dist) {
          min_dist = dist;
          closest_idx = i;
        }
      }

      // Use the lane line point for positioning
      QPointF lane_point = lane_line_vertices[2][closest_idx];

      // Position the stop sign to the right of the closest lane line point
      QPointF stop_point(lane_point.x() + stop_sign_size * 0.75, lane_point.y());

      // Ensure the stop sign stays within the clip region
      if (clip_region.contains(stop_point)) {
        drawStopSignOverlay(painter, stop_point, stop_sign_size,
                           stop_state.display_distance, v_ego, stop_state.fade_alpha,
                           stop_state, clip_region);
      } else {
        // Fallback positioning within clip region
        QPointF adjusted_point(std::clamp(stop_point.x(),
                                         clip_region.left() + stop_sign_size / 2,
                                         clip_region.right() - stop_sign_size / 2),
                              stop_point.y());

        if (clip_region.contains(adjusted_point)) {
          drawStopSignOverlay(painter, adjusted_point, stop_sign_size,
                             stop_state.display_distance, v_ego, stop_state.fade_alpha,
                             stop_state, clip_region);
        }
      }
    } else {
      // Fallback to screen point if available
      if (!screen_point.isNull()) {
        drawStopSignOverlay(painter, screen_point, stop_sign_size,
                           stop_state.display_distance, v_ego, stop_state.fade_alpha,
                           stop_state, clip_region);
      }
    }
  }
}

void StopSignOverlay::drawStopSignOverlay(QPainter &painter, const QPointF &point, int size,
                                         float distance, float v_ego, float alpha,
                                         StopState &stop_state, const QRectF &clip_region) {
  if (alpha < 0.02f || distance <= 0.0f || size <= 0 || size > 500) return;

  // Skip if point is way off-screen
  QRectF screen_bounds = painter.clipBoundingRect();
  QRectF extended_bounds = screen_bounds.adjusted(-200, -200, 200, 200);
  if (!extended_bounds.contains(point) && distance > 15.0f) {
    return;
  }

  painter.setRenderHint(QPainter::Antialiasing, true);

  if (!octagon_initialized) {
    initOctagonTemplate();
  }

  // Update animation state
  if (!stop_state.prev_stop_sign_visible) {
    stop_state.stop_frame_count = 0;
  } else {
    stop_state.stop_frame_count = std::min(stop_state.stop_frame_count + 1, 20);
  }

  float stop_sign_opacity = std::min(1.0f, stop_state.stop_frame_count / 10.0f);

  // Dynamic size with smoothing
  const float base_size = 120.0f;
  float distanceFactor = 1.0 - std::min(0.7f, (distance - 5.0f) / 45.0f);
  float target_size = base_size * distanceFactor;

  // Smooth size changes
  if (stop_state.has_previous_position) {
    stop_state.smoothed_size = stop_state.smoothed_size * (1.0f - stop_state.size_smoothing_factor) +
                               target_size * stop_state.size_smoothing_factor;
  } else {
    stop_state.smoothed_size = target_size;
  }

  int dynamicSize = static_cast<int>(stop_state.smoothed_size);

  // Calculate slide to corner
  float slideThreshold = 20.0f;
  float slideComplete = 10.0f;
  float slideAmount = 0.0f;

  if (distance < slideThreshold) {
    slideAmount = 1.0f - std::clamp((distance - slideComplete) / (slideThreshold - slideComplete), 0.0f, 1.0f);
  }

  // Calculate target position
  QPointF cornerPosition(painter.device()->width() - dynamicSize, painter.device()->height() - dynamicSize * 1.5);
  QPointF targetPosition;

  if (!clip_region.contains(point)) {
    targetPosition = cornerPosition;
  } else {
    targetPosition.setX(point.x() * (1.0f - slideAmount) + cornerPosition.x() * slideAmount);
    targetPosition.setY(point.y() * (1.0f - slideAmount) + cornerPosition.y() * slideAmount);
  }

  // Apply position smoothing
  QPointF finalPosition;
  if (stop_state.has_previous_position) {
    finalPosition.setX(stop_state.smoothed_position.x() * (1.0f - stop_state.position_smoothing_factor) +
                       targetPosition.x() * stop_state.position_smoothing_factor);
    finalPosition.setY(stop_state.smoothed_position.y() * (1.0f - stop_state.position_smoothing_factor) +
                       targetPosition.y() * stop_state.position_smoothing_factor);
  } else {
    finalPosition = targetPosition;
    stop_state.has_previous_position = true;
  }

  stop_state.smoothed_position = finalPosition;
  QPointF drawPoint = finalPosition;

  // Subtle pulsing effect
  float pulseRate = 0.3f + 0.5f * (1.0f - std::min(1.0f, distance / 50.0f));
  float pulsePhase = (static_cast<int>(millis_since_boot()) % 2000) / 2000.0f;
  float pulseOpacity = (0.85f + 0.15f * sin(pulsePhase * 2 * M_PI * pulseRate)) * stop_sign_opacity * alpha;

  // Draw octagon
  QPolygonF stopSign;
  const float max_radius = std::min(static_cast<float>(dynamicSize) / 2.0f, 250.0f);

  for (const QPointF &pt : octagon_template) {
    QPointF vertex(drawPoint.x() + max_radius * pt.x(), drawPoint.y() + max_radius * pt.y());
    if (std::isfinite(vertex.x()) && std::isfinite(vertex.y())) {
      stopSign << vertex;
    }
  }

  if (stopSign.size() == 8) {
    painter.setPen(QPen(Qt::white, 4));
    painter.setBrush(QColor(255, 0, 0, int(220 * pulseOpacity)));
    painter.drawPolygon(stopSign);

    // Draw "STOP" text
    painter.setPen(QColor(255, 255, 255, int(255 * pulseOpacity)));
    QFont stopFont = painter.font();
    stopFont.setPointSize(dynamicSize / 4);
    stopFont.setBold(true);
    painter.setFont(stopFont);

    QRect textRect(drawPoint.x() - dynamicSize/2, drawPoint.y() - dynamicSize/3, dynamicSize, dynamicSize * 2/3);
    painter.drawText(textRect, Qt::AlignCenter, "STOP");

    // Add distance countdown
    if (distance > 0) {
      QString distanceText = QString("%1 m").arg(distance, 0, 'f', 1);
      QFont distFont = painter.font();
      distFont.setPointSize(dynamicSize / 4.5);
      painter.setFont(distFont);
      painter.setPen(QPen(QColor(255, 255, 255, int(255 * pulseOpacity)), 1.5));

      QRect distRect(drawPoint.x() - dynamicSize * 0.75, drawPoint.y() + dynamicSize * 0.6,
                    dynamicSize * 1.5, dynamicSize / 3);
      painter.drawText(distRect, Qt::AlignCenter, distanceText);
    }

    // Draw time countdown arc
    if (v_ego > 0.1) {
      float raw_time_to_stop = distance / v_ego;
      int arcSize = dynamicSize + 20;

      float timeToStop;
      if (distance < 3.0f) {
        timeToStop = std::max(0.1f, distance * 0.5f);
      } else if (v_ego < 0.5f) {
        timeToStop = std::min(raw_time_to_stop, 10.0f);
      } else {
        timeToStop = std::clamp(raw_time_to_stop, 0.1f, 30.0f);
      }

      int startAngle = 90 * 16;
      int spanAngle = std::min(360, int(360 * (1.0 - std::min(1.0f, timeToStop / 10.0f)))) * 16;

      painter.setPen(QPen(QColor(255, 255, 255, int(255 * pulseOpacity)), 3));
      painter.drawArc(drawPoint.x() - arcSize / 2, drawPoint.y() - arcSize / 2, arcSize, arcSize, startAngle, spanAngle);

      // Time text
      QString timeText;
      if (timeToStop < 0.5f) {
        timeText = "STOP";
      } else if (timeToStop > 9.9f) {
        timeText = QString("%1 s").arg(qRound(timeToStop));
      } else {
        timeText = QString("%1 s").arg(timeToStop, 0, 'f', 1);
      }

      QFont timeFont = painter.font();
      timeFont.setPointSize(dynamicSize / 4.5);
      painter.setFont(timeFont);
      painter.setPen(QColor(255, 255, 255, int(255 * pulseOpacity)));
      QRect timeRect(drawPoint.x() - dynamicSize * 0.75, drawPoint.y() + dynamicSize * 0.9, dynamicSize * 1.5, dynamicSize / 3);
      painter.drawText(timeRect, Qt::AlignCenter, timeText);
    }
  }

  stop_state.prev_stop_sign_visible = true;
}

void StopSignOverlay::initOctagonTemplate() {
  if (octagon_initialized) return;

  octagon_template.clear();

  // Create regular octagon vertices
  for (int i = 0; i < 8; ++i) {
    float angle = (i * 45.0f - 22.5f) * M_PI / 180.0f;
    octagon_template << QPointF(cos(angle), sin(angle));
  }

  octagon_initialized = true;
}
