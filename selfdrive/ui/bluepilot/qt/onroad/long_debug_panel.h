#pragma once

#include <QWidget>
#include <deque>
#include <utility>
#include <vector>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui.h"
#define UIState UIStateSP
#else
#include "selfdrive/ui/ui.h"
#endif

class AccelGraphWidget;
class ControlGraphWidget;

class LongDebugPanel : public QWidget {
  Q_OBJECT
public:
  LongDebugPanel(QWidget *parent = nullptr);
  void updateState(const UIState &s);
  void paintEvent(QPaintEvent *event);

private:
  std::deque<std::pair<float, float>> m_accelData;
  std::deque<std::pair<float, float>> m_controlData;
  std::vector<float> m_accelTrajectory;

  float m_maxAccel = 2.0f;
  float m_actualAccel = 0.0f;
  float m_desiredAccel = 0.0f;
  float m_longitudinalActuatorDelay = 0.0f;
  float m_currentSpeed = 0.0f;
  float m_targetSpeed = 0.0f;
  float m_gasSignal = 0.0f;
  float m_brakeSignal = 0.0f;
  bool m_shouldStop = false;
  bool m_allowThrottle = true;
  bool m_allowBrake = true;

  static constexpr int MAX_DATA_POINTS = 100;

  AccelGraphWidget *accelGraph;
  ControlGraphWidget *controlGraph;
};