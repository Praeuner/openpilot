// selfdrive/ui/bluepilot/qt/onroad/widgets/hybrid_drive_gauge.cc
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QWidget>
#include <QColor>
#include "selfdrive/ui/bluepilot/qt/onroad/widgets/hybrid_drive_gauge.h"

void HybridDriveGauge::drawGauge(QPainter &p, QRect rect, float hevThrottleDemandPercent,
                                 float hevThrottleThresholdPercent,
                                 QString hevPowerFlowMode, QString hevEngineOnReason)
{
  p.save();

  // Setup fonts
  QFont font("Inter");
  font.setPixelSize(36); // Increased from 30
  p.setFont(font);

  // Draw white border
  p.setPen(QPen(QColor(255, 255, 255, 200), 2));
  p.setBrush(Qt::NoBrush); // Changed from Qt::NoPen to Qt::NoBrush
  p.drawRoundedRect(rect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

  // Draw background
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 100));
  p.drawRoundedRect(rect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

  // Draw power bar
  QRect barRect = rect.adjusted(10, 5, -10, -TEXT_HEIGHT - 5);
  drawPowerBar(p, barRect, hevThrottleDemandPercent, hevThrottleThresholdPercent, hevPowerFlowMode);

  // Draw mode text with smoked background
  QString modeText = hevPowerFlowMode;
  QString reasonText = hevEngineOnReason;
  QString combinedText = modeText + " | " + reasonText;

  // Draw text background
  QRect textRect = rect.adjusted(10, rect.height() - TEXT_HEIGHT, -10, -5);
  QColor bgColor(0, 0, 0, int(255 * TEXT_BG_OPACITY));
  p.setBrush(bgColor);
  p.drawRoundedRect(textRect, BAR_ROUND_RADIUS / 2, BAR_ROUND_RADIUS / 2);

  // Draw text
  p.setPen(Qt::white);
  p.drawText(textRect, Qt::AlignCenter, combinedText);

  p.restore();
}

void HybridDriveGauge::drawPowerBar(QPainter &p, QRect rect, float value, float threshold, const QString &mode)
{
  // Draw bar background
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(40, 40, 40));
  p.drawRoundedRect(rect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

  const int centerX = rect.center().x();
  bool isEvMode = mode.contains("EV", Qt::CaseInsensitive);

  // Adjust bar width based on mode
  int maxWidth = isEvMode ? int(rect.width() * (threshold / 100.0)) : rect.width();
  rect.setWidth(maxWidth);
  rect.moveCenter(QPoint(centerX, rect.center().y()));

  // Draw EV operation zone with slight transparency
  p.setBrush(QColor(0, 0, 255, 40)); // Light blue for EV zone

  // Draw EV threshold zone
  int thresholdWidth = rect.width() * (threshold / 100.0);
  QRect evZoneRect(centerX - thresholdWidth / 2, rect.top(), thresholdWidth, rect.height());
  p.drawRoundedRect(evZoneRect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

  // Draw active bar
  if (value != 0)
  {
    int width = rect.width() * (std::abs(value) / 100.0);
    QRect barRect = rect;
    barRect.setLeft(centerX - width / 2);
    barRect.setRight(centerX + width / 2);
    p.setBrush(getPowerBarColor(value, threshold, mode));
    p.drawRoundedRect(barRect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);
  }

  // Draw threshold markers
  drawThresholdMarker(p, rect, threshold);

  // Draw center line
  p.setPen(QPen(Qt::white, 2));
  p.drawLine(centerX, rect.top(), centerX, rect.bottom());

  // Draw scale markers
  p.setPen(QPen(Qt::white, 1));
  for (int i = -75; i <= 75; i += 25)
  {
    int x = centerX + (rect.width() * i / 200);
    int markerHeight = (i % 50 == 0) ? 10 : 5;
    p.drawLine(x, rect.top(), x, rect.top() + markerHeight);
    p.drawLine(x, rect.bottom() - markerHeight, x, rect.bottom());
  }
}

void HybridDriveGauge::drawThresholdMarker(QPainter &p, QRect rect, float threshold)
{
  const int centerX = rect.center().x();
  float halfThreshold = threshold / 2.0;

  // Draw left and right threshold markers
  for (int side = -1; side <= 1; side += 2)
  {
    int markerX = centerX + side * (rect.width() * halfThreshold / 100.0);

    QPainterPath path;
    // Draw top marker
    path.moveTo(markerX - MARKER_WIDTH, rect.top());
    path.lineTo(markerX + MARKER_WIDTH, rect.top());
    path.lineTo(markerX, rect.top() + MARKER_HEIGHT);

    // Draw bottom marker
    QPainterPath bottomPath;
    bottomPath.moveTo(markerX - MARKER_WIDTH, rect.bottom());
    bottomPath.lineTo(markerX + MARKER_WIDTH, rect.bottom());
    bottomPath.lineTo(markerX, rect.bottom() - MARKER_HEIGHT);

    // Draw with both fill and outline for better visibility
    p.setPen(QPen(Qt::black, 2));
    p.setBrush(QColor(255, 255, 0)); // Yellow fill
    p.drawPath(path);
    p.drawPath(bottomPath);
  }
}

QColor HybridDriveGauge::getPowerBarColor(float value, float threshold, const QString &mode)
{
  bool isEvMode = mode.contains("EV", Qt::CaseInsensitive);

  if (value < 0)
  {
    // Regeneration - green
    return QColor(0, 255, 0);
  }
  else if (isEvMode && std::abs(value) <= threshold)
  {
    // EV mode - blue
    return QColor(0, 100, 255);
  }
  else
  {
    // Engine on - white
    return QColor(255, 255, 255);
  }
}
