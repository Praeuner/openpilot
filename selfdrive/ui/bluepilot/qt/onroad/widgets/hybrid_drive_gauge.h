// hybrid_drive_gauge.h
#pragma once
#include "selfdrive/ui/qt/util.h"
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QString>
#include <QWidget>

class HybridDriveGauge : public QWidget {
  Q_OBJECT
  Q_PROPERTY(float bracketScale READ bracketScale WRITE setBracketScale)

public:
  // Static interface for existing code
  static void drawGauge(QPainter &p, QRect rect, float hevThrottleDemandPercent, float hevThrottleThresholdPercent,
                        QString hevPowerFlowMode, QString hevEngineOnReason) {
    instance().drawGaugeImpl(p, rect, hevThrottleDemandPercent, hevThrottleThresholdPercent, hevPowerFlowMode,
                             hevEngineOnReason);
  }

  // Animation properties
  float bracketScale() const { return m_bracketScale; }

  void setBracketScale(float scale) {
    m_bracketScale = scale;
    update();
  }

private:
  // Singleton access
  static HybridDriveGauge &instance() {
    static HybridDriveGauge inst;
    return inst;
  }

  // Private constructor for singleton
  HybridDriveGauge(QWidget *parent = nullptr);
  ~HybridDriveGauge();

  // Implementation methods
  void drawGaugeImpl(QPainter &p, QRect rect, float hevThrottleDemandPercent, float hevThrottleThresholdPercent,
                     QString hevPowerFlowMode, QString hevEngineOnReason);
  void drawPowerBar(QPainter &p, QRect rect, float value, float threshold, const QString &mode);
  void drawThresholdBrackets(QPainter &p, QRect rect, float threshold, float currentValue);
  QColor getPowerBarColor(float value, float threshold, const QString &mode);
  QColor getBorderAndBackgroundColor(float value, const QString &mode, bool isBorder);

  void setupAnimation();
  void checkBracketProximity(float currentValue, float threshold);

  // Member variables
  float m_bracketScale = 1.0f;
  QPropertyAnimation *m_bracketAnimation = nullptr;
  bool m_wasNearBracket = false;

  // Constants
  static constexpr int BAR_HEIGHT = 60;
  static constexpr int TEXT_HEIGHT = 40;
  static constexpr float BAR_ROUND_RADIUS = 10.0;
  static constexpr int MARKER_WIDTH = 4;
  static constexpr int MARKER_HEIGHT = 20;
  static constexpr int TEXT_PADDING = 10;
  static constexpr int BORDER_ALPHA = 210;
  static constexpr int BACKGROUND_ALPHA = 100;
  static constexpr int POWER_BAR_BG_ALPHA = 60;
  static constexpr int TEXT_BG_ALPHA = 140;
  static constexpr int TEXT_ALPHA = 270;
  static constexpr int BRACKET_ALPHA = 240;
};

class HybridBatteryGauge {
public:
  static void drawGauge(QPainter &p, QRect rect, float battSocActual, float battSocMin, float battSocMax,
                        float battVoltActual, float battVoltLow, float battVoltHigh, float battAmpsActual);

private:
  static QColor getBatteryColor(float value, float min, float max);
  static QColor getVoltageColor(float voltage, float lowLimit, float highLimit);
  static float lastDisplayedAmps;
  static double lastAmpsUpdateTime;
  static constexpr double AMPS_UPDATE_INTERVAL = 0.5; // Update every 0.5 seconds
};
