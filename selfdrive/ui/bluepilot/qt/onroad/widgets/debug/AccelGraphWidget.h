#pragma once

#include <QWidget>
#include <deque>
#include <utility>
#include <vector>

class AccelGraphWidget : public QWidget {
  Q_OBJECT
public:
  AccelGraphWidget(QWidget *parent = nullptr);
  void setData(const std::deque<std::pair<float, float>> &accelData, float maxAccel, float actualAccel, float desiredAccel, float longitudinalActuatorDelay,
               const std::vector<float> &accelTrajectory);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  std::deque<std::pair<float, float>> m_accelData; // Desired and actual acceleration
  float m_maxAccel;
  float m_actualAccel;
  float m_desiredAccel;
  float m_longitudinalActuatorDelay;
  std::vector<float> m_accelTrajectory;
};