#include "LateralGraphWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>

LateralGraphWidget::LateralGraphWidget(QWidget *parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  // Enable high quality antialiasing for smoother lines
  setAttribute(Qt::WA_OpaquePaintEvent, false);
  setAttribute(Qt::WA_TranslucentBackground, true);
}

void LateralGraphWidget::setData(const std::deque<std::pair<float, float>> &steerData, float maxAngle, float desiredSteerAngle, float actualSteerAngle, float steerActuatorDelay,
                                 float desiredCurvature, float actualCurvature, bool hasFordVariables, float maxAbsPredictedCurvature, float predictedSteeringAngleDegSP,
                                 float pathAngleKp) {
  m_steerData = steerData;
  m_maxAngle = maxAngle;
  m_desiredSteerAngle = desiredSteerAngle;
  m_actualSteerAngle = actualSteerAngle;
  m_steerActuatorDelay = steerActuatorDelay;
  m_desiredCurvature = desiredCurvature;
  m_actualCurvature = actualCurvature;
  m_hasFordVariables = hasFordVariables;
  m_maxAbsPredictedCurvature = maxAbsPredictedCurvature;
  m_predictedSteeringAngleDegSP = predictedSteeringAngleDegSP;
  m_pathAngleKp = pathAngleKp;
  update();
}

void LateralGraphWidget::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);

  // Draw container background with rounded border
  QPainterPath path;
  path.addRoundedRect(rect().adjusted(10, 10, -10, -10), 10, 10);
  p.fillPath(path, QColor(50, 50, 50, 200));     // Lighter background
  p.setPen(QPen(QColor(150, 150, 150, 150), 2)); // Light border
  p.drawPath(path);

  // Calculate layout
  int total_h = height();
  int time_labels_h = 30;
  int legend_h = 120; // Increased for two rows of legend
  int top_margin = 40;
  int bottom_margin = 30;
  int graph_h = total_h - time_labels_h - legend_h - top_margin - bottom_margin;
  if (graph_h < 100)
    graph_h = 100; // Minimum height

  int side_margin = 60;
  int graph_x = side_margin;
  int graph_w = width() - 2 * side_margin;
  int graph_y = top_margin;
  int time_labels_y = graph_y + graph_h + 20;
  int legend_y = time_labels_y + time_labels_h + 10;

  // Draw graph background
  QLinearGradient graphBg(0, graph_y, 0, graph_y + graph_h);
  graphBg.setColorAt(0, QColor(20, 20, 20, 180));
  graphBg.setColorAt(1, QColor(10, 10, 10, 180));
  p.fillRect(graph_x, graph_y, graph_w, graph_h, graphBg);

  // Draw zero line
  int zero_y = graph_y + graph_h / 2;
  p.setPen(QPen(Qt::white, 2));
  p.drawLine(graph_x, zero_y, graph_x + graph_w, zero_y);

  // Draw grid lines - using whiter color (increased from 100 to 160, and opacity from 70 to 90)
  p.setPen(QPen(QColor(160, 160, 160, 90), 1, Qt::DotLine));

  // Create more vertical markers at 1/8, 1/4, 3/8, 1/2, 5/8, 3/4, and 7/8 of height
  for (int i = 1; i < 8; i++) {
    if (i != 4) { // Skip the center line (i=4 corresponds to 4/8 = 1/2) as it's drawn separately
      float y = graph_y + (i * graph_h / 8);
      p.drawLine(graph_x, y, graph_x + graph_w, y);
    }
  }

  int points_per_second = 20;
  float point_spacing = (float)graph_w / std::min(100, (int)m_steerData.size());
  float line_spacing = points_per_second * point_spacing;

  // Extend loop to 10 seconds instead of 5
  for (int i = 0; i <= 10; ++i) {
    float x = graph_x + graph_w - i * line_spacing;
    if (x >= graph_x) {
      // Use more prominent lines for time markers
      if (i <= 5) {
        // For 0-5 seconds: thicker, more opaque lines
        p.setPen(QPen(QColor(180, 180, 180, 140), 1.5, Qt::DotLine));
      } else {
        // For 6-10 seconds: still visible but slightly less prominent
        p.setPen(QPen(QColor(160, 160, 160, 110), 1.2, Qt::DotLine));
      }
      p.drawLine(x, graph_y, x, graph_y + graph_h);

      // Only draw labels for every second up to 5s, then every 2.5s
      if (i <= 5 || i % 5 == 0) {
        p.setPen(Qt::white);
        QFont timeFont("Arial", 24);
        p.setFont(timeFont);
        p.drawText(x - 10, time_labels_y, i == 0 ? "Now" : QString("-%1s").arg(i));
      }
    }
  }

  // Draw scale
  QFont scaleFont("Arial", 26);
  p.setFont(scaleFont);
  p.setPen(Qt::white);
  int scale_x = 20;
  p.drawText(scale_x, zero_y + 6, "0°");
  p.drawText(scale_x, graph_y + 20, QString("+%1°").arg(m_maxAngle, 0, 'f', 0));
  p.drawText(scale_x, graph_y + graph_h - 8, QString("-%1°").arg(m_maxAngle, 0, 'f', 0));

  // Add additional angle markers at 1/4 and 3/4 positions
  p.drawText(scale_x, graph_y + graph_h / 4 + 6, QString("+%1°").arg(m_maxAngle / 2, 0, 'f', 0));
  p.drawText(scale_x, graph_y + 3 * graph_h / 4 + 6, QString("-%1°").arg(m_maxAngle / 2, 0, 'f', 0));

  // Draw paths with smoother lines
  QPainterPath desiredPath, actualPath, desiredCurvPath, actualCurvPath;
  bool first = true;

  // Create paths with points
  for (int i = 0; i < m_steerData.size(); ++i) {
    float x = graph_x + graph_w - i * point_spacing;
    float desiredY = zero_y - (m_steerData[i].first / m_maxAngle) * (graph_h / 2);
    float actualY = zero_y - (m_steerData[i].second / m_maxAngle) * (graph_h / 2);
    float desiredCurvY = zero_y - (m_desiredCurvature * 100.0f / m_maxAngle) * (graph_h / 2);
    float actualCurvY = zero_y - (m_actualCurvature * 100.0f / m_maxAngle) * (graph_h / 2);

    desiredY = std::max((float)graph_y, std::min(desiredY, (float)(graph_y + graph_h)));
    actualY = std::max((float)graph_y, std::min(actualY, (float)(graph_y + graph_h)));
    desiredCurvY = std::max((float)graph_y, std::min(desiredCurvY, (float)(graph_y + graph_h)));
    actualCurvY = std::max((float)graph_y, std::min(actualCurvY, (float)(graph_y + graph_h)));

    if (first) {
      desiredPath.moveTo(x, desiredY);
      actualPath.moveTo(x, actualY);
      desiredCurvPath.moveTo(x, desiredCurvY);
      actualCurvPath.moveTo(x, actualCurvY);
      first = false;
    } else {
      desiredPath.lineTo(x, desiredY);
      actualPath.lineTo(x, actualY);
      desiredCurvPath.lineTo(x, desiredCurvY);
      actualCurvPath.lineTo(x, actualCurvY);
    }
  }

  // Draw the paths with thicker, smoother lines
  p.setPen(QPen(QColor(0, 255, 0, 200), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(desiredPath);

  p.setPen(QPen(QColor(255, 255, 0, 200), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(actualPath);

  p.setPen(QPen(QColor(0, 255, 255, 200), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(desiredCurvPath);

  p.setPen(QPen(QColor(255, 100, 255, 200), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(actualCurvPath);

  // Draw legend with 3-column layout
  QFont legendFont("Arial", 26);
  p.setFont(legendFont);

  // Calculate column width based on total graph width
  int column_width = graph_w / 3;

  // First row
  int legend_row1 = legend_y;

  // Column 1: Angle Desired
  p.fillRect(graph_x, legend_row1, 20, 20, QColor(0, 255, 0, 200));
  p.setPen(Qt::white);
  p.drawText(graph_x + 30, legend_row1 + 16, QString("Angle Desired: %1°").arg(m_desiredSteerAngle, 0, 'f', 1));

  // Column 2: Desired Curvature
  p.fillRect(graph_x + column_width, legend_row1, 20, 20, QColor(0, 255, 255, 200));
  p.drawText(graph_x + column_width + 30, legend_row1 + 16, QString("Desired Curv: %1").arg(m_desiredCurvature, 0, 'f', 4));

  // Column 3: Steer Delay
  p.drawText(graph_x + 2 * column_width + 30, legend_row1 + 16, QString("Steer Delay: %1s").arg(m_steerActuatorDelay, 0, 'f', 3));

  // Second row
  int legend_row2 = legend_row1 + 40; // Space between rows

  // Column 1: Angle Actual
  p.fillRect(graph_x, legend_row2, 20, 20, QColor(255, 255, 0, 200));
  p.drawText(graph_x + 30, legend_row2 + 16, QString("Angle Actual: %1°").arg(m_actualSteerAngle, 0, 'f', 1));

  // Column 2: Actual Curvature
  p.fillRect(graph_x + column_width, legend_row2, 20, 20, QColor(255, 100, 255, 200));
  p.drawText(graph_x + column_width + 30, legend_row2 + 16, QString("Actual Curv: %1").arg(m_actualCurvature, 0, 'f', 4));

  if (m_hasFordVariables) {
    int legend_row3 = legend_row2 + 40; // Space between rows

    // Column 1: Predicted Steering Angle
    p.fillRect(graph_x, legend_row3, 20, 20, QColor(100, 200, 255, 200));
    p.drawText(graph_x + 30, legend_row3 + 16,
              QString("Pred Angle: %1°").arg(m_predictedSteeringAngleDegSP, 0, 'f', 1));

    // Column 2: Max Abs Predicted Curvature
    p.fillRect(graph_x + column_width, legend_row3, 20, 20, QColor(255, 160, 0, 200));
    p.drawText(graph_x + column_width + 30, legend_row3 + 16,
              QString("Max Pred Curv: %1").arg(m_maxAbsPredictedCurvature, 0, 'f', 4));

    // Column 3: Path Angle Kp
    p.drawText(graph_x + 2 * column_width + 30, legend_row3 + 16,
              QString("Path Angle Kp: %1").arg(m_pathAngleKp, 0, 'f', 3));
  }

  // Column 3: Empty (as per requested layout)
}