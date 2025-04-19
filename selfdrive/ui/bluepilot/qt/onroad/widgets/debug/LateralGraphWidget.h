#pragma once

#include <QWidget>
#include <deque>
#include <utility>

class LateralGraphWidget : public QWidget {
  Q_OBJECT
public:
  LateralGraphWidget(QWidget *parent = nullptr);
  void setData(const std::deque<std::pair<float, float>> &steerData, float maxAngle, float desiredSteerAngle, float actualSteerAngle, float steerActuatorDelay,
               float desiredCurvature, float actualCurvature, bool hasFordVariables = false, float maxAbsPredictedCurvature = 0.0f, float predictedSteeringAngleDegSP = 0.0f,
               float pathAngleKp = 0.0f);

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
  bool m_hasFordVariables;
  float m_maxAbsPredictedCurvature;
  float m_predictedSteeringAngleDegSP;
  float m_pathAngleKp;
};
