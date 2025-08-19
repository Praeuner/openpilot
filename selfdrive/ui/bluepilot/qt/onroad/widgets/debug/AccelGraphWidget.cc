#include "AccelGraphWidget.h"
#include "selfdrive/ui/qt/util.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>

AccelGraphWidget::AccelGraphWidget(QWidget *parent) : QWidget(parent) { setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); }

void AccelGraphWidget::setData(const std::deque<std::pair<float, float>> &accelData, float maxAccel, float actualAccel, float desiredAccel, float longitudinalActuatorDelay,
                               const std::vector<float> &accelTrajectory) {
  m_accelData = accelData;
  m_maxAccel = maxAccel;
  m_actualAccel = actualAccel;
  m_desiredAccel = desiredAccel;
  m_longitudinalActuatorDelay = longitudinalActuatorDelay;
  m_accelTrajectory = accelTrajectory;
  update();
}

void AccelGraphWidget::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);

  // Draw container background with automotive styling
  QPainterPath path;
  path.addRoundedRect(rect().adjusted(10, 10, -10, -10), 15, 15);
  
  // Metallic gradient background
  QLinearGradient containerGradient(rect().topLeft(), rect().bottomLeft());
  containerGradient.setColorAt(0, QColor(44, 62, 80, 220));  // Metallic blue-gray
  containerGradient.setColorAt(0.5, QColor(32, 33, 35, 220)); // Dark center
  containerGradient.setColorAt(1, QColor(26, 37, 47, 220));   // Dark edge
  p.fillPath(path, containerGradient);
  
  // Automotive-style border for acceleration
  QLinearGradient borderGradient(rect().topLeft(), rect().bottomLeft());
  borderGradient.setColorAt(0, QColor(46, 204, 113, 150));   // Green for positive accel
  borderGradient.setColorAt(0.5, QColor(241, 196, 15, 150)); // Yellow transition
  borderGradient.setColorAt(1, QColor(231, 76, 60, 150));    // Red for braking
  p.setPen(QPen(QBrush(borderGradient), 2));
  p.drawPath(path);

  // Layout calculations - matching lateral widget with reduced spacing
  int total_h = height();
  int time_labels_h = 30;
  int legend_h = 100; // Reduced from 120 to 100
  int top_margin = 40;
  int bottom_margin = 20; // Reduced from 30 to 20
  int graph_h = total_h - time_labels_h - legend_h - top_margin - bottom_margin;
  if (graph_h < 100)
    graph_h = 100;

  int side_margin = 60; // Increased to match lateral
  int graph_x = side_margin;
  int graph_w = width() - 2 * side_margin;
  int graph_y = top_margin;
  int time_labels_y = graph_y + graph_h + 15;       // Reduced from 20 to 15
  int legend_y = time_labels_y + time_labels_h + 5; // Reduced from 10 to 5

  // Heading with automotive styling
  p.setFont(InterFont(34, QFont::Bold));
  p.setPen(QColor(236, 240, 241, 230));
  p.drawText(graph_x, 30, "Acceleration");

  // Graph background with automotive inset effect
  QLinearGradient graphBg(0, graph_y, 0, graph_y + graph_h);
  graphBg.setColorAt(0, QColor(20, 25, 30, 200));  // Darker blue-black
  graphBg.setColorAt(1, QColor(15, 20, 25, 200));  // Even darker
  p.fillRect(graph_x, graph_y, graph_w, graph_h, graphBg);
  
  // Add inset shadow effect
  p.setPen(QPen(QColor(0, 0, 0, 100), 2));
  p.drawRect(graph_x, graph_y, graph_w, graph_h);
  p.setPen(QPen(QColor(255, 255, 255, 20), 1));
  p.drawRect(graph_x + 1, graph_y + 1, graph_w - 2, graph_h - 2);

  // Zero line with gradient effect
  int zero_y = graph_y + graph_h / 2;
  QLinearGradient zeroGradient(graph_x, zero_y, graph_x + graph_w, zero_y);
  zeroGradient.setColorAt(0, QColor(236, 240, 241, 100));
  zeroGradient.setColorAt(0.5, QColor(236, 240, 241, 255));
  zeroGradient.setColorAt(1, QColor(236, 240, 241, 100));
  p.setPen(QPen(QBrush(zeroGradient), 2));
  p.drawLine(graph_x, zero_y, graph_x + graph_w, zero_y);

  // Grid lines with automotive styling
  p.setPen(QPen(QColor(189, 195, 199, 80), 1, Qt::DotLine));  // Silver metallic

  // Create more vertical markers at 1/8 increments of height
  for (int i = 1; i < 8; i++) {
    if (i != 4) { // Skip the center line (i=4 corresponds to 4/8 = 1/2) as it's drawn separately
      float y = graph_y + (i * graph_h / 8);
      p.drawLine(graph_x, y, graph_x + graph_w, y);
    }
  }

  // Time markers
  int points_per_second = 20;
  float point_spacing = (float)graph_w / std::min(100, (int)m_accelData.size());
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

      // Only draw labels for every second up to 5s, then every 5s
      if (i <= 5 || i % 5 == 0) {
        p.setPen(Qt::white);
        p.setFont(InterFont(24, QFont::Normal));
        p.drawText(x - 10, time_labels_y, i == 0 ? "Now" : QString("-%1s").arg(i));
      }
    }
  }

  // Scale with additional markers
  QFont scaleFont("Arial", 26); // Increased to match lateral
  p.setFont(scaleFont);
  p.setPen(Qt::white);
  int scale_x = 20;
  p.drawText(scale_x, zero_y + 6, "0 m/s²");
  p.drawText(scale_x, graph_y + 20, QString("+%1").arg(m_maxAccel, 0, 'f', 1));
  p.drawText(scale_x, graph_y + graph_h - 8, QString("-%1").arg(m_maxAccel, 0, 'f', 1));
  // Add intermediate scale markers
  p.drawText(scale_x, graph_y + graph_h / 4 + 6, QString("+%1").arg(m_maxAccel / 2, 0, 'f', 1));
  p.drawText(scale_x, graph_y + 3 * graph_h / 4 + 6, QString("-%1").arg(m_maxAccel / 2, 0, 'f', 1));

  // Draw paths
  QPainterPath desiredPath, actualPath, trajectoryPath;
  bool first = true;
  for (int i = 0; i < m_accelData.size(); ++i) {
    float x = graph_x + graph_w - i * point_spacing;
    float desiredY = zero_y - (m_accelData[i].first / m_maxAccel) * (graph_h / 2);
    float actualY = zero_y - (m_accelData[i].second / m_maxAccel) * (graph_h / 2);

    desiredY = std::max((float)graph_y, std::min(desiredY, (float)(graph_y + graph_h)));
    actualY = std::max((float)graph_y, std::min(actualY, (float)(graph_y + graph_h)));

    if (first) {
      desiredPath.moveTo(x, desiredY);
      actualPath.moveTo(x, actualY);
      first = false;
    } else {
      desiredPath.lineTo(x, desiredY);
      actualPath.lineTo(x, actualY);
    }
  }

  if (!m_accelTrajectory.empty()) {
    float x_start = graph_x + graph_w;
    float y_start = zero_y - (m_accelTrajectory[0] / m_maxAccel) * (graph_h / 2);
    y_start = std::max((float)graph_y, std::min(y_start, (float)(graph_y + graph_h)));
    trajectoryPath.moveTo(x_start, y_start);
    float future_point_spacing = points_per_second * point_spacing / 10.0f;
    for (int i = 1; i < m_accelTrajectory.size(); ++i) {
      float x = x_start + i * future_point_spacing;
      float y = zero_y - (m_accelTrajectory[i] / m_maxAccel) * (graph_h / 2);
      y = std::max((float)graph_y, std::min(y, (float)(graph_y + graph_h)));
      trajectoryPath.lineTo(x, y);
    }
  }

  // Draw the paths with thicker, smoother lines
  p.setPen(QPen(QColor(0, 255, 0, 200), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(desiredPath);
  p.setPen(QPen(QColor(255, 255, 0, 200), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(actualPath);
  p.setPen(QPen(QColor(255, 150, 0, 200), 3, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(trajectoryPath);

  // Legend with 3-column layout, matching lateral
  QFont legendFont("Arial", 26);
  p.setFont(legendFont);

  // Calculate column width based on graph width
  int column_width = graph_w / 3;

  // First row
  int legend_row1 = legend_y;

  // Column 1: Desired Acceleration
  p.fillRect(graph_x, legend_row1, 20, 20, QColor(0, 255, 0, 200));
  p.setPen(Qt::white);
  p.drawText(graph_x + 30, legend_row1 + 16, QString("Desired: %1 m/s²").arg(m_desiredAccel, 0, 'f', 2));

  // Column 2: Actual Acceleration
  p.fillRect(graph_x + column_width, legend_row1, 20, 20, QColor(255, 255, 0, 200));
  p.drawText(graph_x + column_width + 30, legend_row1 + 16, QString("Actual: %1 m/s²").arg(m_actualAccel, 0, 'f', 2));

  // Column 3: Actuator Delay (value only, no plot)
  p.drawText(graph_x + 2 * column_width + 30, legend_row1 + 16, QString("Long Delay: %1s").arg(m_longitudinalActuatorDelay, 0, 'f', 3));

  // Second row for trajectory - with reduced spacing
  int legend_row2 = legend_row1 + 35; // Reduced from 40 to 35
  p.fillRect(graph_x, legend_row2, 20, 20, QColor(255, 150, 0, 200));
  p.drawText(graph_x + 30, legend_row2 + 16, "Trajectory");
}