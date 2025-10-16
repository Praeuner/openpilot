#pragma once

#include <QPainter>
#include <QRect>
#include <QString>
#include <QColor>
#include <QFont>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPropertyAnimation>
#include <QWidget>
#include <QTimer>
#include "selfdrive/ui/ui.h"

class HybridGaugesOverlay {
public:
  struct HybridState {
    bool hybrid_available = false;
    bool battery_available = false;
    float throttle_demand = 0.0f, throttle_threshold = 0.0f;
    QString power_mode, engine_reason;
    float batt_soc_actual = 0.0f, batt_soc_min = 0.0f, batt_soc_max = 0.0f;
    float batt_volt_actual = 0.0f, batt_volt_low = 0.0f, batt_volt_high = 0.0f;
    float batt_amps_actual = 0.0f;
  };

  static void render(QPainter &painter, const QRect &rect, const UIState &s, const HybridState &hybrid_state);

private:
  // Hybrid Drive Gauge methods
  static void drawHybridDriveGauge(QPainter &p, QRect rect, float hevThrottleDemandPercent,
                                  float hevThrottleThresholdPercent, QString hevPowerFlowMode, QString hevEngineOnReason);
  static void drawPowerBar(QPainter &p, QRect rect, float value, float threshold, const QString &mode);
  static void drawThresholdBrackets(QPainter &p, QRect rect, float threshold, float currentValue);
  static QLinearGradient getPowerBarGradient(QRect rect, float value, float threshold, const QString &mode);
  static QRadialGradient getBackgroundGradient(QRect rect, const QString &mode);
  static QColor getBorderColor(float value, const QString &mode);
  static void drawInsetBorder(QPainter &p, QRect rect, QColor borderColor);
  static void drawMetallicBackground(QPainter &p, QRect rect, const QString &mode);
  static QLinearGradient createMetallicGradient(QRect rect, QColor baseColor);

  // Hybrid Battery Gauge methods
  static void drawHybridBatteryGauge(QPainter &p, QRect rect, float battSocActual, float battSocMin,
                                    float battSocMax, float battVoltActual, float battVoltLow,
                                    float battVoltHigh, float battAmpsActual);
  static QLinearGradient getBatteryGradient(QRect rect, float value, float min, float max);
  static QColor getVoltageColor(float voltage, float lowLimit, float highLimit);
  static QRadialGradient getBatteryBackgroundGradient(QRect rect, float batteryPercent);
  static void drawAutomotiveBattery(QPainter &p, QRect batteryRect, float fillPerc);

  // Constants
  static constexpr int BAR_HEIGHT = 60;
  static constexpr int TEXT_HEIGHT = 40;
  static constexpr float BAR_ROUND_RADIUS = 6.0;
  static constexpr int MARKER_WIDTH = 4;
  static constexpr int MARKER_HEIGHT = 20;
  static constexpr int TEXT_PADDING = 10;
  static constexpr int BORDER_WIDTH = 3;
  static constexpr int INSET_DEPTH = 2;
  static constexpr double AMPS_UPDATE_INTERVAL = 0.5;

  // Static variables for battery gauge
  static float lastDisplayedAmps;
  static double lastAmpsUpdateTime;

  // Static variables for bracket animation
  static float bracketScale;
  static bool wasNearBracket;
  static QPropertyAnimation* bracketAnimation;
  static QWidget* animationWidget; // Dummy widget for animation

  // Animation methods
  static void setupAnimation();
  static void checkBracketProximity(float currentValue, float threshold);

public:
  static void cleanupAnimation();
};
