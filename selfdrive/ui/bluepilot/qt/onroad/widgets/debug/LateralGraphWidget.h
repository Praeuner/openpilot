#pragma once

#include <QWidget>
#include <deque>
#include <utility>

class LateralGraphWidget : public QWidget {
  Q_OBJECT
public:
  LateralGraphWidget(QWidget *parent = nullptr);
  void setData(const std::deque<std::pair<float, float>> &steerData, float maxAngle, float desiredSteerAngle, float actualSteerAngle, float steerActuatorDelay,
               float desiredCurvature, float actualCurvature);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  std::deque<std::pair<float, float>> m_steerData;
  float m_maxAngle;
  float m_desiredSteerAngle;
  float m_actualSteerAngle;
  float m_steerActuatorDelay;
  float m_desiredCurvature;
  float m_actualCurvature;
};
