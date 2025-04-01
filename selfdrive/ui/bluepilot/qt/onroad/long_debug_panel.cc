#include "long_debug_panel.h"
#include "widgets/debug/AccelGraphWidget.h"
#include "widgets/debug/LongControlGraphWidget.h"
#include <QVBoxLayout>
#include <QLabel>
#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QLinearGradient>

LongDebugPanel::LongDebugPanel(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 20);

  // Title
  QLabel *title = new QLabel("Longitudinal Control", this);
  title->setStyleSheet("font-size: 34px; font-weight: bold; color: white; margin: 0px;");
  title->setFont(QFont("Arial", 34, QFont::Bold));
  title->setFixedHeight(60);
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  // Create container widgets with padding for each graph
  auto createGraphContainer = [this](QWidget *graphWidget) -> QWidget * {
    QWidget *container = new QWidget(this);
    QVBoxLayout *containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 15, 0, 15); // Add 15px padding at top and bottom
    containerLayout->addWidget(graphWidget);
    return container;
  };

  // Graph widgets
  accelGraph = new AccelGraphWidget(this);
  controlGraph = new ControlGraphWidget(this);

  // Add each graph inside its container
  layout->addWidget(createGraphContainer(accelGraph));
  layout->addWidget(createGraphContainer(controlGraph));

  layout->setSpacing(10); // Reduced spacing between containers since they have internal padding
}

void LongDebugPanel::updateState(const UIState &s) {
  if (!isVisible() || !s.scene.started || !s.sm)
    return;

  auto &sm = *(s.sm);

  // Update car state data
  if (sm.valid("carState")) {
    auto car = sm["carState"].getCarState();
    m_actualAccel = car.getAEgo();
    m_currentSpeed = car.getVEgo();
  }

  // Update control data
  if (sm.valid("carControl")) {
    try {
      auto control = sm["carControl"].getCarControl();
      m_gasSignal = control.getActuators().getGas();
      m_brakeSignal = control.getActuators().getBrake();
      m_controlData.push_front({m_gasSignal, m_brakeSignal});
      if (m_controlData.size() > MAX_DATA_POINTS)
        m_controlData.pop_back();
      m_targetSpeed = control.getVCruise();
    } catch (const std::exception &) {
      m_gasSignal = 0.0f;
      m_brakeSignal = 0.0f;
    }
  }

  // Update longitudinal plan data
  if (sm.valid("longitudinalPlan")) {
    try {
      const auto &plan = sm["longitudinalPlan"].getLongitudinalPlan();
      m_accelTrajectory.clear();
      for (int i = 0; i < std::min(20, (int)plan.getAccels().size()); ++i) {
        m_accelTrajectory.push_back(plan.getAccels()[i]);
      }
      if (!m_accelTrajectory.empty()) {
        m_desiredAccel = m_accelTrajectory[0];
      }
      m_shouldStop = plan.getShouldStop();
      m_allowThrottle = plan.getAllowThrottle();
      m_allowBrake = plan.getAllowBrake();
    } catch (const std::exception &) {
      m_desiredAccel = m_actualAccel;
    }
  }

  // Update actuator delay
  if (sm.valid("carParams")) {
    try {
      m_longitudinalActuatorDelay = sm["carParams"].getCarParams().getLongitudinalActuatorDelay();
    } catch (const std::exception &) {
      m_longitudinalActuatorDelay = 0.0f;
    }
  }

  m_accelData.push_front({m_desiredAccel, m_actualAccel});
  if (m_accelData.size() > MAX_DATA_POINTS)
    m_accelData.pop_back();

  // Auto-adjust acceleration scale
  for (const auto &data : m_accelData) {
    float maxAbs = std::max(std::abs(data.first), std::abs(data.second));
    if (maxAbs > m_maxAccel)
      m_maxAccel = maxAbs * 1.2f;
  }
  if (m_accelData.size() > 10) {
    float currentMax = 0;
    for (const auto &data : m_accelData) {
      currentMax = std::max(currentMax, std::max(std::abs(data.first), std::abs(data.second)));
    }
    if (currentMax < m_maxAccel * 0.7f)
      m_maxAccel *= 0.99f;
  }
  if (m_maxAccel < 1.0f)
    m_maxAccel = 1.0f;

  // Update graphs
  accelGraph->setData(m_accelData, m_maxAccel, m_actualAccel, m_desiredAccel, m_longitudinalActuatorDelay, m_accelTrajectory);
  controlGraph->setData(m_controlData, m_gasSignal, m_brakeSignal, m_allowThrottle, m_allowBrake, m_shouldStop);
}

void LongDebugPanel::paintEvent(QPaintEvent *event) {
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
