#include "SpeedGraphWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>

SpeedGraphWidget::SpeedGraphWidget(QWidget *parent) : QWidget(parent) { setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); }

void SpeedGraphWidget::setData(const std::deque<float> &speedData, float maxSpeed, float currentSpeed, float targetSpeed, const std::vector<float> &speedTrajectory) {
  m_speedData = speedData;
  m_maxSpeed = maxSpeed;
  m_currentSpeed = currentSpeed;
  m_targetSpeed = targetSpeed;
  m_speedTrajectory = speedTrajectory;
  update();
}

void SpeedGraphWidget::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Draw container background with rounded border
  QPainterPath path;
  path.addRoundedRect(rect().adjusted(5, 5, -5, -5), 10, 10);
  p.fillPath(path, QColor(50, 50, 50, 200));
  p.setPen(QPen(QColor(150, 150, 150, 150), 2));
  p.drawPath(path);

  // Layout calculations
  int total_h = height();
  int heading_h = 40, time_labels_h = 30, legend_h = 60, margins = 20;
  int graph_h = total_h - heading_h - time_labels_h - legend_h - margins;
  if (graph_h < 100)
    graph_h = 100;

  int side_margin = 20;
  int graph_x = side_margin;
  int graph_w = width() - 2 * side_margin;
  int graph_y = heading_h + 10;
  int time_labels_y = graph_y + graph_h + 10;
  int legend_y = time_labels_y + time_labels_h + 10;

  // Heading
  p.setFont(QFont("Arial", 28, QFont::Bold));
  p.setPen(Qt::white);
  p.drawText(graph_x, 30, "Speed");

  // Graph background
  QLinearGradient graphBg(0, graph_y, 0, graph_y + graph_h);
  graphBg.setColorAt(0, QColor(20, 20, 20, 180));
  graphBg.setColorAt(1, QColor(10, 10, 10, 180));
  p.fillRect(graph_x, graph_y, graph_w, graph_h, graphBg);

  // Grid lines
  p.setPen(QPen(QColor(100, 100, 100, 70), 1, Qt::DotLine));
  for (int i = 1; i < 4; ++i) {
    int grid_y = graph_y + (i * graph_h / 4);
    p.drawLine(graph_x, grid_y, graph_x + graph_w, grid_y);
  }

  // Time markers
  int points_per_second = 20;
  float point_spacing = (float)graph_w / std::min(100, (int)m_speedData.size());
  float line_spacing = points_per_second * point_spacing;
  for (int i = 0; i <= 5; ++i) {
    float x = graph_x + graph_w - i * line_spacing;
    if (x >= graph_x) {
      p.drawLine(x, graph_y, x, graph_y + graph_h);
      p.setPen(Qt::white);
      p.setFont(QFont("Arial", 18));
      p.drawText(x - 10, time_labels_y, i == 0 ? "Now" : QString("-%1s").arg(i));
    }
  }

  // Scale
  p.setFont(QFont("Arial", 22));
  p.setPen(Qt::white);
  p.drawText(graph_x - 50, graph_y + graph_h - 5, "0 m/s");
  p.drawText(graph_x - 50, graph_y + 5, QString("%1").arg(m_maxSpeed, 0, 'f', 1));

  // Draw paths
  QPainterPath speedPath, targetPath, trajectoryPath;
  bool first = true;
  for (int i = 0; i < m_speedData.size(); ++i) {
    float x = graph_x + graph_w - i * point_spacing;
    float speedY = graph_y + graph_h - (m_speedData[i] / m_maxSpeed) * graph_h;
    speedY = std::max((float)graph_y, std::min(speedY, (float)(graph_y + graph_h)));
    if (first) {
      speedPath.moveTo(x, speedY);
      first = false;
    } else {
      speedPath.lineTo(x, speedY);
    }
  }

  if (m_targetSpeed > 0) {
    float targetY = graph_y + graph_h - (m_targetSpeed / m_maxSpeed) * graph_h;
    targetY = std::max((float)graph_y, std::min(targetY, (float)(graph_y + graph_h)));
    targetPath.moveTo(graph_x, targetY);
    targetPath.lineTo(graph_x + graph_w, targetY);
  }

  if (!m_speedTrajectory.empty()) {
    float x_start = graph_x + graph_w;
    float y_start = graph_y + graph_h - (m_speedTrajectory[0] / m_maxSpeed) * graph_h;
    y_start = std::max((float)graph_y, std::min(y_start, (float)(graph_y + graph_h)));
    trajectoryPath.moveTo(x_start, y_start);
    float future_point_spacing = points_per_second * point_spacing / 10.0f;
    for (int i = 1; i < m_speedTrajectory.size(); ++i) {
      float x = x_start + i * future_point_spacing;
      float y = graph_y + graph_h - (m_speedTrajectory[i] / m_maxSpeed) * graph_h;
      y = std::max((float)graph_y, std::min(y, (float)(graph_y + graph_h)));
      trajectoryPath.lineTo(x, y);
    }
  }

  p.setPen(QPen(QColor(100, 200, 255, 200), 3));
  p.drawPath(speedPath);
  p.setPen(QPen(QColor(255, 100, 100, 200), 2, Qt::DashLine));
  p.drawPath(targetPath);
  p.setPen(QPen(QColor(150, 255, 150, 200), 2, Qt::DashLine));
  p.drawPath(trajectoryPath);

  // Current speed
  p.setFont(QFont("Arial", 22));
  p.setPen(Qt::white);
  float speedMph = m_currentSpeed * 2.23694f;
  p.drawText(graph_x + 10, graph_y + 30, QString("%1 m/s (%2 mph)").arg(m_currentSpeed, 0, 'f', 1).arg(speedMph, 0, 'f', 1));
  if (m_targetSpeed > 0) {
    float targetMph = m_targetSpeed * 2.23694f;
    p.drawText(graph_x + 10, graph_y + 60, QString("Target: %1 m/s (%2 mph)").arg(m_targetSpeed, 0, 'f', 1).arg(targetMph, 0, 'f', 1));
  }

  // Legend
  p.setFont(QFont("Arial", 22));
  int item_width = graph_w / 3;
  p.fillRect(graph_x, legend_y, 20, 20, QColor(100, 200, 255, 200));
  p.setPen(Qt::white);
  p.drawText(graph_x + 30, legend_y + 16, "Current");
  p.fillRect(graph_x + item_width, legend_y, 20, 20, QColor(255, 100, 100, 200));
  p.drawText(graph_x + item_width + 30, legend_y + 16, "Target");
  p.fillRect(graph_x + 2 * item_width, legend_y, 20, 20, QColor(150, 255, 150, 200));
  p.drawText(graph_x + 2 * item_width + 30, legend_y + 16, "Trajectory");
}