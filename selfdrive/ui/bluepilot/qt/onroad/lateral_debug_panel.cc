#include "lateral_debug_panel.h"
#include "widgets/debug/LateralGraphWidget.h"
#include <QVBoxLayout>
#include <QLabel>
#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QLinearGradient>

LateralDebugPanel::LateralDebugPanel(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 20);

  // Title
  QLabel *title = new QLabel("Lateral Control", this);
  title->setStyleSheet("font-size: 34px; font-weight: bold; color: white; margin: 0px;");
  title->setFont(QFont("Arial", 34, QFont::Bold));
  title->setFixedHeight(60);
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  // Create a container widget with padding for the graph
  QWidget *graphContainer = new QWidget(this);
  QVBoxLayout *containerLayout = new QVBoxLayout(graphContainer);
  containerLayout->setContentsMargins(0, 15, 0, 15); // Reduced from 30px to 15px padding

  // Graph widget
  lateralGraph = new LateralGraphWidget(this);
  containerLayout->addWidget(lateralGraph);

  // Add the container to the main layout
  layout->addWidget(graphContainer);
  layout->setSpacing(10); // Reduced from 20 to 10
}

void LateralDebugPanel::updateState(const UIState &s) {
  if (!isVisible())
    return;
  if (!s.scene.started || !s.sm)
    return;

  auto &sm = *(s.sm);

  if (sm.valid("carState")) {
    m_actualSteerAngle = sm["carState"].getCarState().getSteeringAngleDeg();
  }
  if (sm.valid("carControl")) {
    try {
      m_desiredSteerAngle = sm["carControl"].getCarControl().getActuators().getSteeringAngleDeg();
    } catch (const std::exception &) {
      m_desiredSteerAngle = m_actualSteerAngle;
    }
  }
  if (sm.valid("carParams")) {
    try {
      m_steerActuatorDelay = sm["carParams"].getCarParams().getSteerActuatorDelay();
    } catch (const std::exception &) {
      m_steerActuatorDelay = 0.0;
    }
  }
  if (sm.valid("controlsState")) {
    try {
      auto controls = sm["controlsState"].getControlsState();
      m_actualCurvature = controls.getCurvature();
      m_desiredCurvature = controls.getDesiredCurvature();
    } catch (const std::exception &) {
      m_actualCurvature = 0.0;
      m_desiredCurvature = 0.0;
    }
  }

  m_steerData.push_front({m_desiredSteerAngle, m_actualSteerAngle});
  if (m_steerData.size() > MAX_DATA_POINTS)
    m_steerData.pop_back();

  for (const auto &data : m_steerData) {
    float maxAbs = std::max(std::abs(data.first), std::abs(data.second));
    if (maxAbs > m_maxAngle)
      m_maxAngle = maxAbs * 1.2f;
  }
  if (m_steerData.size() > 10) {
    float currentMax = 0;
    for (const auto &data : m_steerData) {
      currentMax = std::max(currentMax, std::max(std::abs(data.first), std::abs(data.second)));
    }
    if (currentMax < m_maxAngle * 0.7f)
      m_maxAngle *= 0.99f;
  }
  if (m_maxAngle < 5.0f)
    m_maxAngle = 5.0f;

  lateralGraph->setData(m_steerData, m_maxAngle, m_desiredSteerAngle, m_actualSteerAngle, m_steerActuatorDelay, m_desiredCurvature, m_actualCurvature);
}

void LateralDebugPanel::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Draw background with material design styling
  QPainterPath path;
  path.addRoundedRect(rect().adjusted(5, 5, -5, -5), 15, 15);

  // Material design background with slight gradient
  QLinearGradient gradient(0, 0, 0, height());
  gradient.setColorAt(0, QColor(30, 30, 30, 230));
  gradient.setColorAt(1, QColor(20, 20, 20, 230));

  p.fillPath(path, gradient);

  // Material design subtle border
  p.setPen(QPen(QColor(60, 60, 60, 150), 1));
  p.drawPath(path);

  QWidget::paintEvent(event);
}
