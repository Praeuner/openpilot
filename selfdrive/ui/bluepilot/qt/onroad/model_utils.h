#pragma once

#include <QPolygonF>
#include <QLinearGradient>
#include <QString>
#include "common/params.h"

class BluepilotModelUtils {
public:
  static void smoothPath(QPolygonF &track_vertices, const QPolygonF &prev_vertices);
  static bool hasCustomColor();
  static QLinearGradient getPathGradient(int height, float v_ego);
  static void enhanceLeadDrawing(QPainter &painter, const QPointF &vd, bool isRadar);
  
private:
  static constexpr float PATH_SMOOTHING_FACTOR = 0.2f;
  static QString getPathColor();
};
