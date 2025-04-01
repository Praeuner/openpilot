#pragma once

#include <QWidget>
#include <deque>
#include <utility>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui.h"
#define UIState UIStateSP
#else
#include "selfdrive/ui/ui.h"
#endif

class LateralGraphWidget;

class LateralDebugPanel : public QWidget {
  Q_OBJECT
public:
  LateralDebugPanel(QWidget *parent = nullptr);
  void updateState(const UIState &s);
  void paintEvent(QPaintEvent *event);

private:
  std::deque<std::pair<float, float>> m_steerData;
  static constexpr int MAX_DATA_POINTS = 100;

  float m_maxAngle = 20.0f;
  float m_actualSteerAngle = 0.0f;
  float m_desiredSteerAngle = 0.0f;
  float m_steerActuatorDelay = 0.0f;
  float m_actualCurvature = 0.0f;
  float m_desiredCurvature = 0.0f;

  LateralGraphWidget *lateralGraph;
};
