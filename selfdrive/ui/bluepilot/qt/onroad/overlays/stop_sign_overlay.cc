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


void StopSignOverlay::render(QPainter &painter, const QRect &rect, const UIState &s,
                            StopState &stop_state, const QRectF &clip_region,
                            const QPolygonF lane_line_vertices[4], float vehicle_speed) {
  // Show stop sign when active or fading out
  if (!stop_state.active && stop_state.fade_alpha < 0.02f) {
    return;
  }

  if (stop_state.active || stop_state.fade_alpha > 0.0f) {
    // Use same position as standstill timer
    // Account for wider BP sidebar (460px vs stock 300px = 160px difference)
    int sidebar_offset = 0;
    if (s.scene.sidebar_visible) {
      sidebar_offset = 100; // BP sidebar is 160px wider than stock
    }

    int x = rect.right() / 12 * 10 - sidebar_offset;
    int y = rect.bottom() / 12 * 1.53;

    // Get metric setting from UI state
    bool is_metric = s.scene.is_metric;

    drawStopSignOverlay(painter, QPointF(x, y), stop_state.display_distance, stop_state.fade_alpha, is_metric);
  }
}

void StopSignOverlay::drawStopSignOverlay(QPainter &painter, const QPointF &point,
                                         float distance, float alpha, bool is_metric) {
  if (alpha < 0.02f || distance <= 0.0f) return;

  painter.setRenderHint(QPainter::Antialiasing, true);

  // Use same fixed size as standstill timer
  const int size = 190;
  const float angle = M_PI / 8.0;

  int x = point.x();
  int y = point.y();

  // Create octagon polygon (same as standstill timer)
  QPolygon octagon;
  for (int i = 0; i < 8; i++) {
    float curr_angle = angle + i * M_PI / 4.0;
    int point_x = x + size / 2 * cos(curr_angle);
    int point_y = y + size / 2 * sin(curr_angle);
    octagon << QPoint(point_x, point_y);
  }

  // Draw octagon with same style as standstill timer
  painter.setPen(QPen(Qt::white, 6));
  painter.setBrush(QColor(255, 90, 81, int(200 * alpha))); // red pastel
  painter.drawPolygon(octagon);

  // Draw "STOP" text
  painter.setFont(InterFont(55, QFont::Bold));
  painter.setPen(QColor(255, 255, 255, int(255 * alpha)));
  QRect stopTextRect = painter.fontMetrics().boundingRect(QString("STOP"));
  stopTextRect.moveCenter({x, y - 20});
  painter.drawText(stopTextRect, Qt::AlignCenter, QString("STOP"));

  // Draw distance text where timer value would be
  QString distance_str;
  if (is_metric) {
    distance_str = QString("%1 m").arg(distance, 0, 'f', 1);
  } else {
    // Convert meters to feet
    float distance_ft = distance * 3.28084f;
    distance_str = QString("%1 ft").arg(distance_ft, 0, 'f', 1);
  }
  painter.setFont(InterFont(40, QFont::Bold));
  QRect distanceTextRect = painter.fontMetrics().boundingRect(distance_str);
  distanceTextRect.moveCenter({x, y + 30});
  painter.drawText(distanceTextRect, Qt::AlignCenter, distance_str);
}

