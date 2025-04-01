#pragma once

#include <QWidget>
#include <deque>
#include <vector>

class SpeedGraphWidget : public QWidget {
  Q_OBJECT
public:
  SpeedGraphWidget(QWidget *parent = nullptr);
  void setData(const std::deque<float> &speedData, float maxSpeed, float currentSpeed, float targetSpeed, const std::vector<float> &speedTrajectory);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  std::deque<float> m_speedData;
  float m_maxSpeed;
  float m_currentSpeed;
  float m_targetSpeed;
  std::vector<float> m_speedTrajectory;
};