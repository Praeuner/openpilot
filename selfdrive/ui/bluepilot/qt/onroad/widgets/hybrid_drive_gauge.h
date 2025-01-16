// selfdrive/ui/bluepilot/qt/onroad/widgets/hybrid_drive_gauge.h
#pragma once

#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QWidget>
#include <QColor>

class HybridDriveGauge
{
public:
  static void drawGauge(QPainter &p, QRect rect, float hevThrottleDemandPercent,
                        float hevThrottleThresholdPercent,
                        QString hevPowerFlowMode, QString hevEngineOnReason);

private:
  static void drawPowerBar(QPainter &p, QRect rect, float value, float threshold, const QString &mode);
  static void drawThresholdMarker(QPainter &p, QRect rect, float threshold);
  static QColor getPowerBarColor(float value, float threshold, const QString &mode);

  static constexpr int BAR_HEIGHT = 60;
  static constexpr int TEXT_HEIGHT = 40; // Increased from 30
  static constexpr float BAR_ROUND_RADIUS = 10.0;
  static constexpr int MARKER_WIDTH = 4;
  static constexpr int MARKER_HEIGHT = 20;
  static constexpr int TEXT_PADDING = 10;
  static constexpr float TEXT_BG_OPACITY = 0.6;
};
