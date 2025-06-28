#include "selfdrive/ui/bluepilot/qt/onroad/model_utils.h"
#include <cmath>
#include <chrono>

void BluepilotModelUtils::smoothPath(QPolygonF &track_vertices, const QPolygonF &prev_vertices) {
  if (prev_vertices.isEmpty() || prev_vertices.size() != track_vertices.size()) {
    return;
  }

  QPolygonF smoothed;
  for (int i = 0; i < track_vertices.size(); i++) {
    QPointF smoothed_point;
    smoothed_point.setX(prev_vertices[i].x() * (1.0 - PATH_SMOOTHING_FACTOR) +
                       track_vertices[i].x() * PATH_SMOOTHING_FACTOR);
    smoothed_point.setY(prev_vertices[i].y() * (1.0 - PATH_SMOOTHING_FACTOR) +
                       track_vertices[i].y() * PATH_SMOOTHING_FACTOR);
    smoothed.append(smoothed_point);
  }

  track_vertices = smoothed;
}

bool BluepilotModelUtils::hasCustomColor() {
  QString pathColor = getPathColor();
  return pathColor != "Stock" && !pathColor.isEmpty();
}

QString BluepilotModelUtils::getPathColor() {
  return QString::fromStdString(Params().get("CustomModelPathColor"));
}

QLinearGradient BluepilotModelUtils::getPathGradient(int height, float v_ego) {
  QLinearGradient bg(0, height, 0, 0);
  QString pathColor = getPathColor();

  if (pathColor == "Rainbow") {
    float time_offset = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0f;

    bg.setSpread(QGradient::PadSpread);

    // Create rainbow gradient points
    for (int i = 0; i <= 100; i += 10) {
      float lin_grad_point = i / 100.0f;
      float eased_point = pow(lin_grad_point, 1.5f);
      float path_hue = fmod(eased_point * 360.0 + (v_ego * 20.0) + (time_offset * 100.0), 360.0);
      float alpha = std::max(0.0f, 0.8f - eased_point * 0.8f);
      bg.setColorAt(eased_point, QColor::fromHslF(path_hue / 360.0, 1.0f, 0.55f, alpha));
    }
  } else if (pathColor == "Blue") {
    bg.setColorAt(0.0, QColor(0, 102, 204, 102));
    bg.setColorAt(0.5, QColor(51, 153, 255, 89));
    bg.setColorAt(1.0, QColor(51, 153, 255, 0));
  } else if (pathColor == "Green") {
    bg.setColorAt(0.0, QColor(0, 204, 102, 102));
    bg.setColorAt(0.5, QColor(51, 255, 153, 89));
    bg.setColorAt(1.0, QColor(51, 255, 153, 0));
  } else if (pathColor == "Purple") {
    bg.setColorAt(0.0, QColor(153, 51, 204, 102));
    bg.setColorAt(0.5, QColor(178, 102, 255, 89));
    bg.setColorAt(1.0, QColor(178, 102, 255, 0));
  } else if (pathColor == "Orange") {
    bg.setColorAt(0.0, QColor(255, 128, 0, 102));
    bg.setColorAt(0.5, QColor(255, 153, 51, 89));
    bg.setColorAt(1.0, QColor(255, 153, 51, 0));
  } else if (pathColor == "Red") {
    bg.setColorAt(0.0, QColor(204, 0, 0, 102));
    bg.setColorAt(0.5, QColor(255, 51, 51, 89));
    bg.setColorAt(1.0, QColor(255, 51, 51, 0));
  } else if (pathColor == "Cyan") {
    bg.setColorAt(0.0, QColor(0, 204, 204, 102));
    bg.setColorAt(0.5, QColor(51, 255, 255, 89));
    bg.setColorAt(1.0, QColor(51, 255, 255, 0));
  } else if (pathColor == "Yellow") {
    bg.setColorAt(0.0, QColor(204, 204, 0, 102));
    bg.setColorAt(0.5, QColor(255, 255, 51, 89));
    bg.setColorAt(1.0, QColor(255, 255, 51, 0));
  }

  return bg;
}