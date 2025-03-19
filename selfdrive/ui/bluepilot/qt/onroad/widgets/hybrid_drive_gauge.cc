// selfdrive/ui/bluepilot/qt/onroad/widgets/hybrid_drive_gauge.cc

#include "selfdrive/ui/bluepilot/qt/onroad/widgets/hybrid_drive_gauge.h"
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QWidget>
#include <iostream>

HybridDriveGauge::HybridDriveGauge(QWidget *parent) : QWidget(parent) { setupAnimation(); }

HybridDriveGauge::~HybridDriveGauge() { delete m_bracketAnimation; }

void HybridDriveGauge::setupAnimation() {
  m_bracketAnimation = new QPropertyAnimation(this, "bracketScale");
  m_bracketAnimation->setDuration(200); // 200ms animation
  m_bracketAnimation->setEasingCurve(QEasingCurve::OutElastic);
}

void HybridDriveGauge::checkBracketProximity(float currentValue, float threshold) {
  bool isNearBracket = std::abs(std::abs(currentValue) - threshold) < 5.0f; // Within 5% of threshold

  if (isNearBracket != m_wasNearBracket) {
    m_wasNearBracket = isNearBracket;

    if (isNearBracket) {
      m_bracketAnimation->setStartValue(1.0f);
      m_bracketAnimation->setEndValue(1.3f); // Scale up by 30%
    } else {
      m_bracketAnimation->setStartValue(1.3f);
      m_bracketAnimation->setEndValue(1.0f);
    }

    m_bracketAnimation->start();
  }
}

void HybridDriveGauge::drawGaugeImpl(QPainter &p, QRect rect, float hevThrottleDemandPercent, float hevThrottleThresholdPercent, QString hevPowerFlowMode,
                                     QString hevEngineOnReason) {
  // Check if the bracket should be animated
  checkBracketProximity(hevThrottleDemandPercent, hevThrottleThresholdPercent);

  p.save();

  // --- Layout: split gauge into top (power bar) and bottom (text) ---
  // Ratios for top/bottom sections
  const float powerBarRatio = 0.55f;
  // const float textRatio = 0.35f;

  // Dimensions for each section
  const int totalHeight = rect.height();
  const int powerBarHeight = int(totalHeight * powerBarRatio);
  const int textHeight = totalHeight - powerBarHeight; // Ensures full usage

  // --- Prepare fonts ---
  QFont font("Inter");
  int maxWidth = rect.width() - 30;
  int fontSize = int(rect.width() * 0.06);
  font.setPixelSize(fontSize);
  p.setFont(font);

  // --- Prepare text strings ---
  QString modeText = hevPowerFlowMode;
  QString reasonText = hevEngineOnReason;
  QString combinedText = modeText.isEmpty() ? reasonText : (reasonText.isEmpty() ? modeText : modeText + " | " + reasonText);

  // Scale text down if needed
  QFontMetrics fm(font);
  int textWidth = fm.horizontalAdvance(combinedText);
  while (textWidth > maxWidth && fontSize > 12) {
    fontSize--;
    font.setPixelSize(fontSize);
    fm = QFontMetrics(font);
    textWidth = fm.horizontalAdvance(combinedText);
  }
  // make the font weight bold
  font.setWeight(QFont::Bold);
  p.setFont(font);

  // --- Draw overall rounded border (outer frame) ---
  QColor borderColor = getBorderAndBackgroundColor(hevThrottleDemandPercent, hevPowerFlowMode, true);
  p.setPen(QPen(borderColor, 2));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(rect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

  // --- Draw the semi-transparent background for the entire gauge ---
  p.setPen(Qt::NoPen);
  QColor bgColor(0, 0, 0, 80);
  p.setBrush(bgColor);
  p.drawRoundedRect(rect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

  // -----------------------
  // 1) Draw the power bar
  // -----------------------
  {
    // Leave some margin so we don’t collide with the outer frame
    const int margin = 5;
    QRect barRect(rect.left() + margin, rect.top() + margin, rect.width() - 2 * margin, powerBarHeight - 2 * margin);

    drawPowerBar(p, barRect, hevThrottleDemandPercent, hevThrottleThresholdPercent, hevPowerFlowMode);
  }

  // -------------------------------------------------
  // 2) Draw the text background with only bottom
  //    corners rounded, and then draw the text
  // -------------------------------------------------
  {
    QRect textRect(rect.left(),                 // same x
                   rect.top() + powerBarHeight, // directly after the power bar
                   rect.width(), textHeight);

    // Decide background color
    QColor textBgColor = getBorderAndBackgroundColor(hevThrottleDemandPercent, hevPowerFlowMode, false);
    p.setBrush(textBgColor);

    // Create a path that has bottom corners rounded, top corners straight
    QPainterPath textBgPath;
    textBgPath.setFillRule(Qt::WindingFill);

    // Add a rounded rect for the bottom corners
    textBgPath.addRoundedRect(textRect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

    // Add a rectangle to "unround" the top corners
    // (this rectangle covers the top rounding)
    QRectF topRect = textRect.adjusted(0, 0, 0, -BAR_ROUND_RADIUS);
    textBgPath.addRect(topRect);

    p.fillPath(textBgPath, p.brush());

    // Draw text centered (both horizontally and vertically)
    p.setPen(QColor(255, 255, 255, 230));
    p.drawText(textRect, Qt::AlignCenter, combinedText);
  }

  p.restore();
}

void HybridDriveGauge::drawPowerBar(QPainter &p, QRect rect, float value, float threshold, const QString &mode) {
  const int centerX = rect.center().x();
  bool isEvMode = mode.contains("Electric", Qt::CaseInsensitive);

  // Draw bar background with transparency
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(40, 40, 40, 30));

  if (isEvMode) {
    // In EV mode, only draw background for EV range
    int evWidth = rect.width() * (threshold / 100.0);
    QRect evRect = rect;
    evRect.setWidth(evWidth);
    evRect.moveCenter(QPoint(centerX, rect.center().y()));
    p.drawRoundedRect(evRect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);
  } else {
    p.drawRoundedRect(rect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);
  }

  // Draw active bar
  if (value != 0) {
    int width = rect.width() * (std::abs(value) / 100.0);
    QRect barRect = rect;
    barRect.setLeft(centerX - width / 2);
    barRect.setRight(centerX + width / 2);
    p.setBrush(getPowerBarColor(value, threshold, mode));
    p.drawRoundedRect(barRect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);
  }

  // Draw threshold markers
  if (isEvMode) {
    drawThresholdBrackets(p, rect, threshold, value);
  }

  // Draw center line
  p.setPen(QPen(Qt::white, 2));
  p.drawLine(centerX, rect.top(), centerX, rect.bottom());

  // Draw scale markers
  if (!isEvMode) {
    p.setPen(QPen(Qt::white, 1));
    for (int i = -75; i <= 75; i += 25) {
      int x = centerX + (rect.width() * i / 200);
      int markerHeight = (i % 50 == 0) ? 10 : 5;
      p.drawLine(x, rect.top(), x, rect.top() + markerHeight);
      p.drawLine(x, rect.bottom() - markerHeight, x, rect.bottom());
    }
  }
}

void HybridDriveGauge::drawThresholdBrackets(QPainter &p, QRect rect, float threshold, float currentValue) {
  const int centerX = rect.center().x();
  float halfThreshold = threshold / 2.0;

  // Calculate how close we are to the threshold as a percentage
  float proximityPercent = (std::abs(currentValue) / threshold) * 100.0f;

  // Define color based on proximity
  QColor bracketColor;
  if (proximityPercent < 80.0f) {
    bracketColor = QColor(255, 255, 255, 180); // White until 80%
  } else {
    // Calculate transition from white to orange from 80% to 100%
    float t = (proximityPercent - 80.0f) / 20.0f; // 0 to 1 for last 20%
    bracketColor = QColor(255,                    // Red stays at 255
                          255 - (t * 140),        // Green transitions from 255 to 115
                          255 - (t * 255),        // Blue transitions from 255 to 0
                          180 + (t * 75)          // Alpha increases for more visibility
    );
  }

  QPen bracketPen(bracketColor, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(bracketPen);

  // Draw left and right brackets with scaling
  for (int side = -1; side <= 1; side += 2) {
    int x = centerX + side * (rect.width() * halfThreshold / 100.0);

    // Apply scaling transformation
    p.save();
    QPointF center(x, rect.center().y());
    p.translate(center);
    p.scale(m_bracketScale, m_bracketScale);
    p.translate(-center);

    int bracketWidth = 10;
    int bracketDepth = 6;
    int curveSize = 3;

    // Top bracket
    QPainterPath topPath;
    if (side < 0) {
      topPath.moveTo(x + bracketWidth, rect.top());
      topPath.lineTo(x + curveSize, rect.top());
      topPath.quadTo(x, rect.top(), x, rect.top() + bracketDepth);
    } else {
      topPath.moveTo(x - bracketWidth, rect.top());
      topPath.lineTo(x - curveSize, rect.top());
      topPath.quadTo(x, rect.top(), x, rect.top() + bracketDepth);
    }

    // Bottom bracket
    QPainterPath bottomPath;
    if (side < 0) {
      bottomPath.moveTo(x + bracketWidth, rect.bottom());
      bottomPath.lineTo(x + curveSize, rect.bottom());
      bottomPath.quadTo(x, rect.bottom(), x, rect.bottom() - bracketDepth);
    } else {
      bottomPath.moveTo(x - bracketWidth, rect.bottom());
      bottomPath.lineTo(x - curveSize, rect.bottom());
      bottomPath.quadTo(x, rect.bottom(), x, rect.bottom() - bracketDepth);
    }

    p.drawPath(topPath);
    p.drawPath(bottomPath);

    p.restore();
  }
}

QColor HybridDriveGauge::getPowerBarColor(float value, float threshold, const QString &mode) {
  bool isEvMode = mode.contains("Electric", Qt::CaseInsensitive);
  // bool isIdleMode = mode.contains("Idle", Qt::CaseInsensitive);

  if (value < 0 || isEvMode) {
    // Simple white bar for regen and EV modes
    return QColor(255, 255, 255);
  } else {
    // Engine on - gradient from white to yellow to orange to red starting at 50%
    if (value > 50) {
      float gradientPosition = (value - 50) / 50.0; // Now using 50% range (50-100)
      gradientPosition = std::min(1.0f, std::max(0.0f, gradientPosition));

      if (gradientPosition < 0.33) {
        float t = gradientPosition * 3;
        return QColor(255, 255, 255 * (1.0 - t));
      } else if (gradientPosition < 0.66) {
        float t = (gradientPosition - 0.33) * 3;
        return QColor(255, 255 * (1.0 - t * 0.5), 0);
      } else {
        float t = (gradientPosition - 0.66) * 3;
        return QColor(255, 128 * (1.0 - t), 0);
      }
    } else {
      return QColor(255, 255, 255); // White for 0-50%
    }
  }
}

QColor HybridDriveGauge::getBorderAndBackgroundColor(float value, const QString &mode, bool isBorder) {
  bool isIdleMode = mode.contains("Idle", Qt::CaseInsensitive);
  bool isEvMode = mode.contains("Electric", Qt::CaseInsensitive);
  bool isHybridMode = mode.contains("Hybrid", Qt::CaseInsensitive);
  bool isRegenMode = value < 0 && !isIdleMode;

  QColor color;
  if (isRegenMode) {
    color = QColor(0, 220, 100); // Vibrant green for regen
  } else if (isEvMode) {
    color = QColor(0, 140, 255); // Vibrant blue for EV
  } else if (isHybridMode) {
    color = QColor(148, 0, 211); // Violet purple for hybrid
  } else {
    color = isBorder ? QColor(255, 255, 255) : QColor(0, 0, 0); // White border or black background
  }

  color.setAlpha(isBorder ? 240 : 200);
  return color;
}

void HybridBatteryGauge::drawGauge(QPainter &p, QRect rect, float battSocActual, float battSocMin, float battSocMax, float battVoltActual, float battVoltLow, float battVoltHigh,
                                   float battAmpsActual) {
  p.save();

  // Compute a scaling factor based on the battery gauge's height.
  // For gauge_scale == 1, we expect the height to be 100.
  float scaleFactor = rect.height() / 100.0f;

  // Optionally, adjust the container width as before.
  rect.setWidth(rect.width() * 1.45);

  // Use a scaled corner radius
  int cornerRadius = qRound(10 * scaleFactor);

  // Draw main border and background with scaled parameters.
  p.setPen(QPen(QColor(255, 255, 255, 240), 2 * scaleFactor));
  p.setBrush(QColor(0, 0, 0, 80));
  p.drawRoundedRect(rect, cornerRadius, cornerRadius);

  // Split into main area and text bar using the same ratio as HybridDriveGauge.
  const float powerBarRatio = 0.55f;
  QRect mainArea = rect;
  mainArea.setHeight(int(rect.height() * powerBarRatio));

  // Draw battery in main area.
  const int batteryWidth = int(rect.width() * 0.5);
  const int batteryHeight = int(mainArea.height() * 0.6);
  QRect batteryRect(0, 0, batteryWidth, batteryHeight);
  batteryRect.moveCenter(mainArea.center());
  // Use a scaled left margin.
  batteryRect.moveLeft(mainArea.left() + qRound(20 * scaleFactor));

  // Draw battery outline and fill.
  p.setPen(QPen(QColor(255, 255, 255, 200), 2 * scaleFactor));
  p.drawRect(batteryRect);

  // Draw positive terminal tab
  int tabWidth = qRound(batteryHeight * 0.2);               // Tab width relative to battery height
  int tabHeight = qRound(batteryHeight * 0.4);              // Tab height relative to battery height
  QRect tabRect(batteryRect.right(),                        // Start at battery's right edge
                batteryRect.center().y() - (tabHeight / 2), // Vertically centered
                tabWidth, tabHeight);
  p.setBrush(QColor(255, 255, 255, 200));                      // White fill
  p.setPen(QPen(QColor(255, 255, 255, 200), 2 * scaleFactor)); // Match battery outline
  p.drawRect(tabRect);

  // Battery fill (draw after tab so it doesn't overlap)
  float fillPerc = (battSocActual - battSocMin) / (battSocMax - battSocMin);
  int displayPercentage = qRound(fillPerc * 100);
  QRect fillRect = batteryRect.adjusted(qRound(2 * scaleFactor), qRound(2 * scaleFactor), -qRound(2 * scaleFactor), -qRound(2 * scaleFactor));
  fillRect.setWidth(int(fillRect.width() * fillPerc));
  p.setBrush(getBatteryColor(battSocActual, battSocMin, battSocMax));
  p.setPen(Qt::NoPen);
  p.drawRect(fillRect);

  // Draw percentage text (e.g. "75%") centered vertically with the battery.
  // Scale the font size with scaleFactor.
  int percentFontSize = qRound(24 * scaleFactor);
  QFont percentFont("Inter", percentFontSize, QFont::Bold);
  p.setFont(percentFont);
  p.setPen(QColor(255, 255, 255, 230));
  // Use a scaled margin for the text offset.
  int textMargin = qRound(15 * scaleFactor);
  QRect textRect = mainArea;
  textRect.setLeft(batteryRect.right() + textMargin);
  p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, QString::number(displayPercentage) + "%");

  // Draw bottom text section for voltage and amps.
  const int textAreaHeight = int(rect.height() * (1.0f - powerBarRatio)); // Remaining space.
  QRect textRect2(rect.left(), rect.bottom() - textAreaHeight, rect.width(), textAreaHeight);
  QColor textBgColor(0, 0, 0, 200);
  p.setBrush(textBgColor);

  // FIX: Adjust text background rect to prevent overlapping with main border
  // Inset the rectangle by the border width to avoid overlap
  int borderWidth = qRound(2 * scaleFactor);
  QRect adjustedTextRect = textRect2.adjusted(borderWidth, 0, -borderWidth, -borderWidth);

  // Create a path for text background with only bottom corners rounded.
  QPainterPath textBgPath;
  textBgPath.setFillRule(Qt::WindingFill);
  textBgPath.addRoundedRect(adjustedTextRect, cornerRadius, cornerRadius);
  // "Unround" the top corners.
  QRectF topRect = adjustedTextRect.adjusted(0, 0, 0, -cornerRadius);
  textBgPath.addRect(topRect);
  p.fillPath(textBgPath, p.brush());

  // Prepare voltage and amps text.
  QString voltText = QString::number(battVoltActual, 'f', 0) + "V";
  QString ampText = QString("%1%2A").arg(battAmpsActual < 0 ? "+" : "-").arg(QString::number(qAbs(battAmpsActual), 'f', 1));

  // Use a scaled margin.
  QRect metricsRect = adjustedTextRect.adjusted(textMargin, 0, -textMargin, 0);

  // Set font for voltage text with scaled size.
  int textFontSize = qRound(24 * scaleFactor);
  QFont textFont("Inter", textFontSize, QFont::Bold);
  p.setFont(textFont);
  p.setPen(getVoltageColor(battVoltActual, battVoltLow, battVoltHigh));
  p.drawText(metricsRect, Qt::AlignVCenter | Qt::AlignLeft, voltText);

  // Set font for amps text (using the same scaled size).
  p.setFont(textFont);
  p.setPen(battAmpsActual < 0 ? QColor(0, 255, 0) : QColor(255, 255, 255));
  p.drawText(metricsRect, Qt::AlignVCenter | Qt::AlignRight, ampText);

  p.restore();
}

QColor HybridBatteryGauge::getBatteryColor(float value, float min, float max) {
  float perc = (value - min) / (max - min);
  if (perc > 0.5)
    return QColor(0, 255, 0, 200);
  if (perc > 0.25)
    return QColor(255, 255, 0, 200);
  return QColor(255, 0, 0, 200);
}

QColor HybridBatteryGauge::getVoltageColor(float voltage, float lowLimit, float highLimit) {
  if (voltage <= lowLimit + 10)
    return QColor(255, 0, 0, 255);
  if (voltage >= highLimit - 10)
    return QColor(255, 255, 0, 255);
  return QColor(255, 255, 255, 255);
}
