#pragma once

#include <QWidget>
#include <deque>
#include <utility>

class ControlGraphWidget : public QWidget {
  Q_OBJECT
public:
  ControlGraphWidget(QWidget *parent = nullptr);
  void setData(const std::deque<std::pair<float, float>> &controlData, float gasSignal, float brakeSignal, bool allowThrottle, bool allowBrake, bool shouldStop);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  std::deque<std::pair<float, float>> m_controlData; // Gas and brake signals
  float m_gasSignal;
  float m_brakeSignal;
  bool m_allowThrottle;
  bool m_allowBrake;
  bool m_shouldStop;
};