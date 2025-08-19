#include "LongControlGraphWidget.h"
#include "selfdrive/ui/qt/util.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>

ControlGraphWidget::ControlGraphWidget(QWidget *parent) : QWidget(parent) { setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); }

void ControlGraphWidget::setData(const std::deque<std::pair<float, float>> &controlData, float gasSignal, float brakeSignal, bool allowThrottle, bool allowBrake, bool shouldStop) {
  m_controlData = controlData;
  m_gasSignal = gasSignal;
  m_brakeSignal = brakeSignal;
  m_allowThrottle = allowThrottle;
  m_allowBrake = allowBrake;
  m_shouldStop = shouldStop;
  update();
}

void ControlGraphWidget::paintEvent(QPaintEvent *event) {
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
  
  // Automotive-style border for control signals
  QLinearGradient borderGradient(rect().topLeft(), rect().bottomLeft());
  borderGradient.setColorAt(0, QColor(46, 204, 113, 150));  // Green for gas
  borderGradient.setColorAt(1, QColor(231, 76, 60, 150));   // Red for brake
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
  p.drawText(graph_x, 30, "Control Signals");

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

  // Grid lines with automotive styling
  p.setPen(QPen(QColor(189, 195, 199, 80), 1, Qt::DotLine));  // Silver metallic

  // More vertical markers at 1/8 increments
  for (int i = 1; i < 8; i++) {
    int grid_y = graph_y + (i * graph_h / 8);
    p.drawLine(graph_x, grid_y, graph_x + graph_w, grid_y);
  }

  // Time markers
  int points_per_second = 20;
  float point_spacing = (float)graph_w / std::min(100, (int)m_controlData.size());
  float line_spacing = points_per_second * point_spacing;

  // Extend to 10 seconds with improved visibility
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

      // Only draw labels for key intervals
      if (i <= 5 || i % 5 == 0) {
        p.setPen(Qt::white);
        p.setFont(InterFont(24, QFont::Normal));
        p.drawText(x - 10, time_labels_y, i == 0 ? "Now" : QString("-%1s").arg(i));
      }
    }
  }

  // Scale with additional markers
  p.setFont(InterFont(26, QFont::DemiBold));
  p.setPen(Qt::white);
  p.drawText(graph_x - 50, graph_y + graph_h - 5, "0.0");
  p.drawText(graph_x - 50, graph_y + 5, "1.0");
  // Add intermediate scale markers at 0.25, 0.5, 0.75
  p.drawText(graph_x - 50, graph_y + 3 * graph_h / 4 - 5, "0.25");
  p.drawText(graph_x - 50, graph_y + graph_h / 2 - 5, "0.5");
  p.drawText(graph_x - 50, graph_y + graph_h / 4 - 5, "0.75");

  // Draw paths with smoother lines
  QPainterPath gasPath, brakePath;
  bool first = true;
  for (int i = 0; i < m_controlData.size(); ++i) {
    float x = graph_x + graph_w - i * point_spacing;
    float gasY = graph_y + graph_h - (m_controlData[i].first * graph_h);
    float brakeY = graph_y + graph_h - (m_controlData[i].second * graph_h);
    gasY = std::max((float)graph_y, std::min(gasY, (float)(graph_y + graph_h)));
    brakeY = std::max((float)graph_y, std::min(brakeY, (float)(graph_y + graph_h)));
    if (first) {
      gasPath.moveTo(x, gasY);
      brakePath.moveTo(x, brakeY);
      first = false;
    } else {
      gasPath.lineTo(x, gasY);
      brakePath.lineTo(x, brakeY);
    }
  }

  p.setPen(QPen(QColor(0, 255, 0, 200), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(gasPath);
  p.setPen(QPen(QColor(255, 0, 0, 200), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(brakePath);

  // Legend with 3-column layout, matching lateral
  QFont legendFont("Arial", 26);
  p.setFont(legendFont);

  // Calculate column width based on graph width
  int column_width = graph_w / 3;

  // First row
  int legend_row1 = legend_y;

  // Column 1: Gas Signal with value
  p.fillRect(graph_x, legend_row1, 20, 20, QColor(0, 255, 0, 200));
  p.setPen(Qt::white);
  p.drawText(graph_x + 30, legend_row1 + 16, QString("Gas: %1").arg(m_gasSignal, 0, 'f', 3));

  // Column 2: Brake Signal with value
  p.fillRect(graph_x + column_width, legend_row1, 20, 20, QColor(255, 0, 0, 200));
  p.drawText(graph_x + column_width + 30, legend_row1 + 16, QString("Brake: %1").arg(m_brakeSignal, 0, 'f', 3));

  // Column 3: Should Stop status
  QString stopStatus = m_shouldStop ? "Should Stop: Yes" : "Should Stop: No";
  p.drawText(graph_x + 2 * column_width + 30, legend_row1 + 16, stopStatus);

  // Second row for status indicators - with reduced spacing
  int legend_row2 = legend_row1 + 35; // Reduced from 40 to 35

  // Throttle status
  QString throttleStatus = m_allowThrottle ? "Throttle: Allowed" : "Throttle: Blocked";
  p.drawText(graph_x + 30, legend_row2 + 16, throttleStatus);

  // Brake status
  QString brakeStatus = m_allowBrake ? "Brake: Allowed" : "Brake: Blocked";
  p.drawText(graph_x + column_width + 30, legend_row2 + 16, brakeStatus);
}