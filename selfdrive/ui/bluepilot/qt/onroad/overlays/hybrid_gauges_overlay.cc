#include "selfdrive/ui/bluepilot/qt/onroad/overlays/hybrid_gauges_overlay.h"
#include "common/timing.h"
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QLinearGradient>
#include <QRadialGradient>
#include <iostream>
#include <algorithm>

// Static variable initialization
float HybridGaugesOverlay::lastDisplayedAmps = 0.0f;
double HybridGaugesOverlay::lastAmpsUpdateTime = 0.0;

// Animation state variables
float HybridGaugesOverlay::bracketScale = 1.0f;
bool HybridGaugesOverlay::wasNearBracket = false;
QPropertyAnimation* HybridGaugesOverlay::bracketAnimation = nullptr;
QWidget* HybridGaugesOverlay::animationWidget = nullptr;

void HybridGaugesOverlay::render(QPainter &painter, const QRect &rect, const UIState &s, const HybridState &hybrid_state) {
  if (!s.scene.show_hybrid_drive_overlay || !hybrid_state.hybrid_available) {
    return;
  }

  int gauge_scale = s.scene.hybrid_drive_gauge_size;
  int gauge_width = rect.width() * 0.39;
  int gauge_height = 130;

  if (gauge_scale == 1) {
    gauge_width = rect.width() * 0.30;
    gauge_height = 100;
  } else if (gauge_scale == 2) {
    gauge_width = rect.width() * 0.345;
    gauge_height = 115;
  } else if (gauge_scale == 3) {
    gauge_width = rect.width() * 0.39;
    gauge_height = 130;
  } else {
    gauge_width = rect.width() * 0.30;
    gauge_height = 100;
  }

  int bottom_margin = 30;
  int y_position = rect.height() - gauge_height - bottom_margin;
  QRect gauge_rect((rect.width() - gauge_width) / 2, y_position, gauge_width, gauge_height);

  drawHybridDriveGauge(painter, gauge_rect, hybrid_state.throttle_demand, hybrid_state.throttle_threshold,
                      hybrid_state.power_mode, hybrid_state.engine_reason);

  if (s.scene.show_hybrid_battery_overlay && hybrid_state.battery_available) {
    int batt_width = gauge_width * 0.25;
    QRect battery_rect(gauge_rect.right() + 10, y_position, batt_width, gauge_height);

    drawHybridBatteryGauge(painter, battery_rect,
                           hybrid_state.batt_soc_actual,
                           hybrid_state.batt_soc_min,
                           hybrid_state.batt_soc_max,
                           hybrid_state.batt_volt_actual,
                           hybrid_state.batt_volt_low,
                           hybrid_state.batt_volt_high,
                           hybrid_state.batt_amps_actual);
  }
}

// ============================================================================
// HYBRID DRIVE GAUGE IMPLEMENTATION
// ============================================================================

void HybridGaugesOverlay::drawHybridDriveGauge(QPainter &p, QRect rect, float hevThrottleDemandPercent,
                                              float hevThrottleThresholdPercent, QString hevPowerFlowMode, QString hevEngineOnReason) {
  // Setup animation if not already done
  setupAnimation();

  // Check if the bracket should be animated
  checkBracketProximity(hevThrottleDemandPercent, hevThrottleThresholdPercent);

  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);

  // Layout: split gauge into top (power bar) and bottom (text)
  const float powerBarRatio = 0.55f;
  const int totalHeight = rect.height();
  const int powerBarHeight = int(totalHeight * powerBarRatio);
  const int textHeight = totalHeight - powerBarHeight;

  // Prepare fonts with better scaling
  QFont font("Inter");
  int maxWidth = rect.width() - 40; // More margin for text
  int fontSize = int(rect.width() * 0.06);
  font.setPixelSize(fontSize);

  // Prepare text strings
  QString modeText = hevPowerFlowMode;
  QString reasonText = hevEngineOnReason;
  QString combinedText = modeText.isEmpty() ? reasonText : (reasonText.isEmpty() ? modeText : modeText + " | " + reasonText);

  // PERFORMANCE: Scale text down with iteration limit to prevent runaway loops
  QFontMetrics fm(font);
  int textWidth = fm.horizontalAdvance(combinedText);
  int iteration_limit = 8; // Prevent excessive iterations
  while (textWidth > maxWidth && fontSize > 8 && iteration_limit-- > 0) {
    fontSize--;
    font.setPixelSize(fontSize);
    fm = QFontMetrics(font);
    textWidth = fm.horizontalAdvance(combinedText);
  }

  // If still too wide, try without the mode prefix
  if (textWidth > maxWidth && !reasonText.isEmpty() && !modeText.isEmpty()) {
    combinedText = reasonText; // Use just the reason text
    fm = QFontMetrics(font);
    textWidth = fm.horizontalAdvance(combinedText);

    // PERFORMANCE: Scale reason text with iteration limit
    iteration_limit = 8; // Reset limit
    while (textWidth > maxWidth && fontSize > 8 && iteration_limit-- > 0) {
      fontSize--;
      font.setPixelSize(fontSize);
      fm = QFontMetrics(font);
      textWidth = fm.horizontalAdvance(combinedText);
    }
  }

  font.setWeight(QFont::Bold);
  p.setFont(font); // Apply the scaled font

  // Draw automotive-style background and border
  drawMetallicBackground(p, rect, hevPowerFlowMode);
  drawInsetBorder(p, rect, getBorderColor(hevThrottleDemandPercent, hevPowerFlowMode));

  // Draw the power bar
  {
    const int margin = 8; // Increased margin for automotive look
    QRect barRect(rect.left() + margin, rect.top() + margin, rect.width() - 2 * margin, powerBarHeight - 2 * margin);
    drawPowerBar(p, barRect, hevThrottleDemandPercent, hevThrottleThresholdPercent, hevPowerFlowMode);
  }

  // Draw the text area with automotive styling
  {
    QRect textRect(rect.left(), rect.top() + powerBarHeight, rect.width(), textHeight);

    // Create metallic text background
    QRect textBgRect = textRect.adjusted(BORDER_WIDTH, 0, -BORDER_WIDTH, -BORDER_WIDTH);
    QLinearGradient textBgGradient = createMetallicGradient(textBgRect, QColor(44, 62, 80));

    QPainterPath textBgPath;
    textBgPath.setFillRule(Qt::WindingFill);
    textBgPath.addRoundedRect(textBgRect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

    // "Unround" the top corners
    QRectF topRect = textBgRect.adjusted(0, 0, 0, -BAR_ROUND_RADIUS);
    textBgPath.addRect(topRect);

    p.fillPath(textBgPath, textBgGradient);

    // Add inner shadow to text area
    QRect shadowRect = textBgRect.adjusted(2, 2, -2, -2);
    QColor shadowColor(0, 0, 0, 50);
    p.setPen(QPen(shadowColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(shadowRect, BAR_ROUND_RADIUS - 2, BAR_ROUND_RADIUS - 2);

    // Draw text with automotive-style shadow
    p.setPen(QColor(0, 0, 0, 100)); // Text shadow
    p.drawText(textRect.adjusted(1, 1, 1, 1), Qt::AlignCenter, combinedText);

    p.setPen(QColor(236, 240, 241, 230)); // Main text color
    p.drawText(textRect, Qt::AlignCenter, combinedText);
  }

  p.restore();
}

QRadialGradient HybridGaugesOverlay::getBackgroundGradient(QRect rect, const QString &mode) {
  QRadialGradient gradient(rect.center(), rect.width() * 0.7);

  bool isEvMode = mode.contains("Electric", Qt::CaseInsensitive);
  bool isHybridMode = mode.contains("Hybrid", Qt::CaseInsensitive);

  if (isEvMode) {
    gradient.setColorAt(0, QColor(30, 144, 255)); // Electric blue center
    gradient.setColorAt(1, QColor(25, 25, 60));   // Dark blue edge
  } else if (isHybridMode) {
    gradient.setColorAt(0, QColor(100, 149, 237)); // Cornflower blue center
    gradient.setColorAt(1, QColor(25, 25, 60));    // Dark blue edge
  } else {
    gradient.setColorAt(0, QColor(70, 130, 180));  // Steel blue center
    gradient.setColorAt(1, QColor(25, 25, 60));    // Dark blue edge
  }

  return gradient;
}

QLinearGradient HybridGaugesOverlay::createMetallicGradient(QRect rect, QColor baseColor) {
  QLinearGradient gradient(rect.topLeft(), rect.bottomRight());

  QColor highlight = baseColor.lighter(150);
  QColor shadow = baseColor.darker(150);

  gradient.setColorAt(0, highlight);
  gradient.setColorAt(0.3, baseColor);
  gradient.setColorAt(0.7, baseColor);
  gradient.setColorAt(1, shadow);

  return gradient;
}

void HybridGaugesOverlay::drawInsetBorder(QPainter &p, QRect rect, QColor borderColor) {
  // Outer border (highlight)
  p.setPen(QPen(borderColor, BORDER_WIDTH));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(rect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

  // Inner shadow effect
  QRect innerRect = rect.adjusted(BORDER_WIDTH, BORDER_WIDTH, -BORDER_WIDTH, -BORDER_WIDTH);
  QColor shadowColor = borderColor.darker(200);
  shadowColor.setAlpha(100);
  p.setPen(QPen(shadowColor, 1));
  p.drawRoundedRect(innerRect, BAR_ROUND_RADIUS - 2, BAR_ROUND_RADIUS - 2);
}

void HybridGaugesOverlay::drawMetallicBackground(QPainter &p, QRect rect, const QString &mode) {
  // Draw neutral metallic background (not mode-dependent)
  p.setPen(Qt::NoPen);
  QRadialGradient neutralBg(rect.center(), rect.width() * 0.7);
  neutralBg.setColorAt(0, QColor(44, 62, 80)); // Neutral center
  neutralBg.setColorAt(1, QColor(26, 37, 47)); // Dark edge
  p.setBrush(neutralBg);
  p.drawRoundedRect(rect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

  // Add inner highlight for metallic effect
  QRect highlightRect = rect.adjusted(BORDER_WIDTH, BORDER_WIDTH, -BORDER_WIDTH, -BORDER_WIDTH * 3);
  QLinearGradient highlight(highlightRect.topLeft(), highlightRect.bottomLeft());
  highlight.setColorAt(0, QColor(255, 255, 255, 20));
  highlight.setColorAt(0.3, QColor(255, 255, 255, 5));
  highlight.setColorAt(1, QColor(255, 255, 255, 0));

  p.setBrush(highlight);
  p.drawRoundedRect(highlightRect, BAR_ROUND_RADIUS - 2, BAR_ROUND_RADIUS - 2);
}

QColor HybridGaugesOverlay::getBorderColor(float value, const QString &mode) {
  bool isEvMode = mode.contains("Electric", Qt::CaseInsensitive);
  bool isHybridMode = mode.contains("Hybrid", Qt::CaseInsensitive);
  bool isRegenMode = value < 0;

  if (isRegenMode) {
    return QColor(0, 255, 127); // Electric green for regen
  } else if (isEvMode) {
    return QColor(30, 144, 255); // Electric blue for EV
  } else if (isHybridMode) {
    return QColor(100, 149, 237); // Cornflower blue for hybrid
  } else {
    return QColor(70, 130, 180);   // Steel blue default
  }
}

QLinearGradient HybridGaugesOverlay::getPowerBarGradient(QRect rect, float value, float threshold, const QString &mode) {
  QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());

  bool isEvMode = mode.contains("Electric", Qt::CaseInsensitive);

  if (value < 0) {
    // Regen braking - pure green gradient
    gradient.setColorAt(0, QColor(0, 255, 0));   // Bright green
    gradient.setColorAt(1, QColor(0, 200, 0));   // Darker green
  } else if (isEvMode) {
    // Electric mode - pure blue gradient
    gradient.setColorAt(0, QColor(0, 150, 255)); // Electric blue
    gradient.setColorAt(1, QColor(0, 100, 200)); // Darker blue
  } else {
    // Engine on - progressive blend from light to orange based on demand
    // No color change until 33% demand, then blend from 33-100%
    float normalizedValue = std::clamp(value / 100.0f, 0.0f, 1.0f); // Normalize to 0-1

    // Calculate blend factor - no change until 33%, then blend from 33-100%
    float blendFactor = 0.0f;
    if (normalizedValue > 0.33f) {
      blendFactor = (normalizedValue - 0.33f) / 0.67f; // Scale 33-100% to 0-1
    }

    // Light gray colors for low demand
    QColor lightTop(220, 220, 220);    // Light gray
    QColor lightBottom(180, 180, 180); // Darker gray

    // Orange colors for high demand
    QColor orangeTop(255, 140, 0);     // Orange
    QColor orangeBottom(255, 69, 0);   // Red-orange

    // Blend the colors progressively
    QColor blendedTop(
      lightTop.red() + (orangeTop.red() - lightTop.red()) * blendFactor,
      lightTop.green() + (orangeTop.green() - lightTop.green()) * blendFactor,
      lightTop.blue() + (orangeTop.blue() - lightTop.blue()) * blendFactor
    );

    QColor blendedBottom(
      lightBottom.red() + (orangeBottom.red() - lightBottom.red()) * blendFactor,
      lightBottom.green() + (orangeBottom.green() - lightBottom.green()) * blendFactor,
      lightBottom.blue() + (orangeBottom.blue() - lightBottom.blue()) * blendFactor
    );

    gradient.setColorAt(0, blendedTop);
    gradient.setColorAt(1, blendedBottom);
  }

  return gradient;
}

void HybridGaugesOverlay::drawPowerBar(QPainter &p, QRect rect, float value, float threshold, const QString &mode) {
  const int centerX = rect.center().x();
  bool isEvMode = mode.contains("Electric", Qt::CaseInsensitive);

  // Draw bar background with automotive inset style
  p.setPen(Qt::NoPen);
  QLinearGradient bgGradient(rect.topLeft(), rect.bottomLeft());
  bgGradient.setColorAt(0, QColor(52, 73, 94));
  bgGradient.setColorAt(1, QColor(44, 62, 80));

  if (isEvMode) {
    // In EV mode, only draw background for EV range
    int evWidth = rect.width() * (threshold / 100.0);
    QRect evRect = rect;
    evRect.setWidth(evWidth);
    evRect.moveCenter(QPoint(centerX, rect.center().y()));
    p.setBrush(bgGradient);
    p.drawRoundedRect(evRect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

    // Add inset shadow
    QRect shadowRect = evRect.adjusted(2, 2, -2, -2);
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawRoundedRect(shadowRect, BAR_ROUND_RADIUS - 2, BAR_ROUND_RADIUS - 2);
  } else {
    p.setBrush(bgGradient);
    p.drawRoundedRect(rect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

    // Add inset shadow
    QRect shadowRect = rect.adjusted(2, 2, -2, -2);
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawRoundedRect(shadowRect, BAR_ROUND_RADIUS - 2, BAR_ROUND_RADIUS - 2);
  }

  // Draw active bar with automotive gradient
  if (value != 0) {
    int width = rect.width() * (std::abs(value) / 100.0);
    QRect barRect = rect;
    barRect.setLeft(centerX - width / 2);
    barRect.setRight(centerX + width / 2);

    p.setBrush(getPowerBarGradient(barRect, value, threshold, mode));
    p.drawRoundedRect(barRect, BAR_ROUND_RADIUS, BAR_ROUND_RADIUS);

    // Add highlight on top edge for 3D effect
    QRect highlightRect = barRect.adjusted(2, 2, -2, -barRect.height()/2);
    QLinearGradient highlight(highlightRect.topLeft(), highlightRect.bottomLeft());
    highlight.setColorAt(0, QColor(255, 255, 255, 30));
    highlight.setColorAt(1, QColor(255, 255, 255, 0));
    p.setBrush(highlight);
    p.drawRoundedRect(highlightRect, BAR_ROUND_RADIUS - 2, BAR_ROUND_RADIUS - 2);
  }

  // Draw threshold markers with automotive styling
  if (isEvMode) {
    drawThresholdBrackets(p, rect, threshold, value);
  }

  // Draw center line with metallic effect
  QLinearGradient centerGradient(QPointF(centerX, rect.top()), QPointF(centerX, rect.bottom()));
  centerGradient.setColorAt(0, QColor(236, 240, 241, 150));
  centerGradient.setColorAt(0.5, QColor(255, 255, 255, 200));
  centerGradient.setColorAt(1, QColor(236, 240, 241, 150));

  p.setPen(QPen(QBrush(centerGradient), 2));
  p.drawLine(centerX, rect.top(), centerX, rect.bottom());

  // Draw scale markers with automotive styling
  if (!isEvMode) {
    for (int i = -75; i <= 75; i += 25) {
      int x = centerX + (rect.width() * i / 200);
      int markerHeight = (i % 50 == 0) ? 10 : 5;

      QColor markerColor = (i % 50 == 0) ? QColor(236, 240, 241, 200) : QColor(189, 195, 199, 150);
      p.setPen(QPen(markerColor, 1));

      p.drawLine(x, rect.top(), x, rect.top() + markerHeight);
      p.drawLine(x, rect.bottom() - markerHeight, x, rect.bottom());
    }
  }
}

void HybridGaugesOverlay::drawThresholdBrackets(QPainter &p, QRect rect, float threshold, float currentValue) {
  const int centerX = rect.center().x();
  float halfThreshold = threshold / 2.0;

  // Calculate proximity for warning effect
  float proximityPercent = (std::abs(currentValue) / threshold) * 100.0f;

  // Automotive warning colors
  QColor bracketColor;
  if (proximityPercent < 80.0f) {
    bracketColor = QColor(243, 156, 18, 200); // Amber
  } else {
    // Transition to red for warning
    float t = (proximityPercent - 80.0f) / 20.0f;
    bracketColor = QColor(243 - (t * 43),      // R: 243 -> 200
                          156 - (t * 156),     // G: 156 -> 0
                          18,                  // B: stays 18
                          200 + (t * 55));     // Alpha increases
  }

  // Create gradient for metallic bracket effect
  QLinearGradient bracketGradient(QPointF(0, rect.top()), QPointF(0, rect.bottom()));
  bracketGradient.setColorAt(0, bracketColor.lighter(130));
  bracketGradient.setColorAt(0.5, bracketColor);
  bracketGradient.setColorAt(1, bracketColor.darker(130));

  QPen bracketPen(QBrush(bracketGradient), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(bracketPen);

  // Draw left and right brackets with automotive styling
  for (int side = -1; side <= 1; side += 2) {
    int x = centerX + side * (rect.width() * halfThreshold / 100.0);

    // Apply scaling transformation for dynamic bracket animation
    p.save();
    QPointF center(x, rect.center().y());
    p.translate(center);
    p.scale(bracketScale, bracketScale);  // Dynamic scaling based on animation
    p.translate(-center);

    int bracketWidth = 12;  // Slightly wider for automotive look
    int bracketDepth = 8;   // Deeper for more prominence
    int curveSize = 4;      // Larger curve for smoother appearance

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

    // Add glow effect for warning state
    if (proximityPercent > 80.0f) {
      p.setPen(QPen(bracketColor, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      p.setOpacity(0.3);
      p.drawPath(topPath);
      p.drawPath(bottomPath);
      p.setOpacity(1.0);
    }

    p.restore(); // Restore transformation
  }
}

// ============================================================================
// HYBRID BATTERY GAUGE IMPLEMENTATION
// ============================================================================

QRadialGradient HybridGaugesOverlay::getBatteryBackgroundGradient(QRect rect, float batteryPercent) {
  QRadialGradient gradient(rect.center(), rect.width() * 0.7);

  if (batteryPercent > 50.0f) {
    gradient.setColorAt(0, QColor(39, 174, 96)); // Green center
    gradient.setColorAt(1, QColor(30, 132, 73)); // Darker green edge
  } else if (batteryPercent > 25.0f) {
    gradient.setColorAt(0, QColor(241, 196, 15)); // Yellow center
    gradient.setColorAt(1, QColor(212, 172, 13)); // Darker yellow edge
  } else {
    gradient.setColorAt(0, QColor(231, 76, 60)); // Red center
    gradient.setColorAt(1, QColor(192, 57, 43)); // Darker red edge
  }

  return gradient;
}

QLinearGradient HybridGaugesOverlay::getBatteryGradient(QRect rect, float value, float min, float max) {
  QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());

  float perc = (value - min) / (max - min);
  if (perc > 0.5) {
    gradient.setColorAt(0, QColor(46, 204, 113)); // Green
    gradient.setColorAt(1, QColor(39, 174, 96));  // Darker green
  } else if (perc > 0.25) {
    gradient.setColorAt(0, QColor(241, 196, 15)); // Yellow
    gradient.setColorAt(1, QColor(243, 156, 18)); // Orange
  } else {
    gradient.setColorAt(0, QColor(231, 76, 60));  // Red
    gradient.setColorAt(1, QColor(192, 57, 43));  // Dark red
  }

  return gradient;
}

void HybridGaugesOverlay::drawAutomotiveBattery(QPainter &p, QRect batteryRect, float fillPerc) {
  // Draw battery outline with automotive styling
  QLinearGradient outlineGradient(batteryRect.topLeft(), batteryRect.bottomLeft());
  outlineGradient.setColorAt(0, QColor(236, 240, 241));
  outlineGradient.setColorAt(1, QColor(189, 195, 199));

  p.setPen(QPen(QBrush(outlineGradient), 2));
  p.setBrush(Qt::NoBrush);
  p.drawRect(batteryRect);

  // Draw positive terminal with metallic effect
  int tabWidth = qRound(batteryRect.height() * 0.2);
  int tabHeight = qRound(batteryRect.height() * 0.4);
  QRect tabRect(batteryRect.right(), batteryRect.center().y() - (tabHeight / 2), tabWidth, tabHeight);

  QLinearGradient tabGradient(tabRect.topLeft(), tabRect.bottomLeft());
  tabGradient.setColorAt(0, QColor(236, 240, 241));
  tabGradient.setColorAt(1, QColor(189, 195, 199));

  p.setBrush(tabGradient);
  p.setPen(QPen(QBrush(outlineGradient), 2));
  p.drawRect(tabRect);

  // Draw battery background with inset effect
  QRect bgRect = batteryRect.adjusted(2, 2, -2, -2);
  QLinearGradient bgGradient(bgRect.topLeft(), bgRect.bottomLeft());
  bgGradient.setColorAt(0, QColor(52, 73, 94));
  bgGradient.setColorAt(1, QColor(44, 62, 80));

  p.setBrush(bgGradient);
  p.setPen(Qt::NoPen);
  p.drawRect(bgRect);

  // Draw battery fill with automotive gradient
  if (fillPerc > 0) {
    QRect fillRect = bgRect;
    fillRect.setWidth(int(fillRect.width() * fillPerc));

    // Create dynamic gradient based on charge level
    QLinearGradient fillGradient = getBatteryGradient(fillRect, fillPerc * 100, 0, 100);

    p.setBrush(fillGradient);
    p.drawRect(fillRect);

    // Add highlight for 3D effect
    QRect highlightRect = fillRect.adjusted(1, 1, -1, -fillRect.height()/2);
    QLinearGradient highlight(highlightRect.topLeft(), highlightRect.bottomLeft());
    highlight.setColorAt(0, QColor(255, 255, 255, 40));
    highlight.setColorAt(1, QColor(255, 255, 255, 0));
    p.setBrush(highlight);
    p.drawRect(highlightRect);
  }
}

void HybridGaugesOverlay::drawHybridBatteryGauge(QPainter &p, QRect rect, float battSocActual, float battSocMin,
                                                float battSocMax, float battVoltActual, float battVoltLow,
                                                float battVoltHigh, float battAmpsActual) {
  // Get current time for amp display smoothing
  double currentTime = millis_since_boot() / 1000.0;
  float displayAmps = battAmpsActual;

  if (currentTime - lastAmpsUpdateTime < AMPS_UPDATE_INTERVAL && std::abs(battAmpsActual - lastDisplayedAmps) < 1.0f) {
    displayAmps = lastDisplayedAmps;
  } else {
    lastDisplayedAmps = battAmpsActual;
    lastAmpsUpdateTime = currentTime;
  }

  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);

  // Scale factor for responsive sizing
  float scaleFactor = rect.height() / 100.0f;

  // Slight width adjustment for larger sizes only
  float widthMultiplier = 1.45f;
  if (scaleFactor > 1.2f) {
    widthMultiplier = 1.5f;  // Modest increase for large gauges
  }
  rect.setWidth(rect.width() * widthMultiplier);

  int cornerRadius = qRound(6 * scaleFactor); // Reduced for automotive look

  // Calculate battery percentage for dynamic theming
  // Clamp values to prevent division by zero and ensure valid range
  float clampedActual = std::clamp(battSocActual, battSocMin, battSocMax);
  float clampedMin = std::clamp(battSocMin, 0.0f, 100.0f);
  float clampedMax = std::clamp(battSocMax, clampedMin + 1.0f, 100.0f); // Ensure max > min

  float fillPerc = (clampedActual - clampedMin) / (clampedMax - clampedMin);
  float batteryPercent = std::clamp(fillPerc * 100.0f, 0.0f, 100.0f);

  // Draw automotive-style background (neutral metallic look)
  p.setPen(Qt::NoPen);
  QRadialGradient neutralBg(rect.center(), rect.width() * 0.7);
  neutralBg.setColorAt(0, QColor(44, 62, 80)); // Neutral center
  neutralBg.setColorAt(1, QColor(26, 37, 47)); // Dark edge
  p.setBrush(neutralBg);
  p.drawRoundedRect(rect, cornerRadius, cornerRadius);

  // Draw main border with color matching battery state
  QLinearGradient borderGradient(rect.topLeft(), rect.bottomLeft());
  if (batteryPercent > 50.0f) {
    borderGradient.setColorAt(0, QColor(46, 204, 113));
    borderGradient.setColorAt(1, QColor(39, 174, 96));
  } else if (batteryPercent > 25.0f) {
    borderGradient.setColorAt(0, QColor(241, 196, 15));
    borderGradient.setColorAt(1, QColor(212, 172, 13));
  } else {
    borderGradient.setColorAt(0, QColor(231, 76, 60));
    borderGradient.setColorAt(1, QColor(192, 57, 43));
  }

  p.setPen(QPen(QBrush(borderGradient), 3 * scaleFactor));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(rect, cornerRadius, cornerRadius);

  // Add inner highlight for metallic effect
  QRect highlightRect = rect.adjusted(3, 3, -3, -rect.height()/2);
  QLinearGradient highlight(highlightRect.topLeft(), highlightRect.bottomLeft());
  highlight.setColorAt(0, QColor(255, 255, 255, 20));
  highlight.setColorAt(1, QColor(255, 255, 255, 0));
  p.setBrush(highlight);
  p.setPen(Qt::NoPen);
  p.drawRoundedRect(highlightRect, cornerRadius - 2, cornerRadius - 2);

  // Split layout
  const float powerBarRatio = 0.55f;
  QRect mainArea = rect;
  mainArea.setHeight(int(rect.height() * powerBarRatio));

  // Draw battery icon
  const int batteryWidth = int(rect.width() * 0.5);
  const int batteryHeight = int(mainArea.height() * 0.6);
  QRect batteryRect(0, 0, batteryWidth, batteryHeight);
  batteryRect.moveCenter(mainArea.center());
  batteryRect.moveLeft(mainArea.left() + qRound(20 * scaleFactor));

  int displayPercentage = qRound(fillPerc * 100);

  drawAutomotiveBattery(p, batteryRect, fillPerc);

  // Draw percentage text with automotive styling
  int percentFontSize = qRound(24 * scaleFactor);
  QFont percentFont("Inter", percentFontSize, QFont::Bold);
  p.setFont(percentFont);

  // Text shadow for automotive look
  p.setPen(QColor(0, 0, 0, 150));
  int textMargin = qRound(15 * scaleFactor);
  QRect textRect = mainArea;
  textRect.setLeft(batteryRect.right() + textMargin);
  p.drawText(textRect.adjusted(1, 1, 1, 1), Qt::AlignVCenter | Qt::AlignLeft, QString::number(displayPercentage) + "%");

  // Main text
  p.setPen(QColor(236, 240, 241, 230));
  p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, QString::number(displayPercentage) + "%");

  // Draw bottom text section with automotive styling
  const int textAreaHeight = int(rect.height() * (1.0f - powerBarRatio));
  QRect textRect2(rect.left(), rect.bottom() - textAreaHeight, rect.width(), textAreaHeight);

  // Automotive metallic text background
  int borderWidth = qRound(3 * scaleFactor);
  QRect adjustedTextRect = textRect2.adjusted(borderWidth, 0, -borderWidth, -borderWidth);

  QLinearGradient textBgGradient(adjustedTextRect.topLeft(), adjustedTextRect.bottomLeft());
  textBgGradient.setColorAt(0, QColor(44, 62, 80));
  textBgGradient.setColorAt(1, QColor(52, 73, 94));

  QPainterPath textBgPath;
  textBgPath.setFillRule(Qt::WindingFill);
  textBgPath.addRoundedRect(adjustedTextRect, cornerRadius, cornerRadius);
  QRectF topRect = adjustedTextRect.adjusted(0, 0, 0, -cornerRadius);
  textBgPath.addRect(topRect);
  p.fillPath(textBgPath, textBgGradient);

  // Prepare text
  QString voltText = QString::number(battVoltActual, 'f', 0) + "V";
  QString ampText = QString("%1%2A").arg(displayAmps < 0 ? "+" : "-").arg(QString::number(qAbs(displayAmps), 'f', 1));

  // Create separate rectangles for voltage and amps with proper spacing
  int textPadding = qRound(12 * scaleFactor); // Increased padding between texts
  int sidePadding = qRound(8 * scaleFactor);   // Padding from edges

  // Calculate available width and split it
  int availableWidth = adjustedTextRect.width() - (2 * sidePadding) - textPadding;
  int voltWidth = availableWidth * 0.45; // 45% for voltage
  int ampWidth = availableWidth * 0.55;  // 55% for amps (typically longer text)

  QRect voltRect(adjustedTextRect.left() + sidePadding, adjustedTextRect.top(),
                voltWidth, adjustedTextRect.height());
  QRect ampRect(adjustedTextRect.right() - sidePadding - ampWidth, adjustedTextRect.top(),
               ampWidth, adjustedTextRect.height());

  // Draw voltage text with automotive styling
  int textFontSize = qRound(22 * scaleFactor); // Slightly smaller to fit better
  QFont textFont("Inter", textFontSize, QFont::Bold);
  p.setFont(textFont);

  // Voltage text with shadow
  QColor voltColor = getVoltageColor(battVoltActual, battVoltLow, battVoltHigh);
  p.setPen(QColor(0, 0, 0, 100));
  p.drawText(voltRect.adjusted(1, 1, 1, 1), Qt::AlignVCenter | Qt::AlignLeft, voltText);
  p.setPen(voltColor);
  p.drawText(voltRect, Qt::AlignVCenter | Qt::AlignLeft, voltText);

  // Amps text with shadow
  QColor ampColor = displayAmps < 0 ? QColor(46, 204, 113) : QColor(236, 240, 241);
  p.setPen(QColor(0, 0, 0, 100));
  p.drawText(ampRect.adjusted(1, 1, 1, 1), Qt::AlignVCenter | Qt::AlignRight, ampText);
  p.setPen(ampColor);
  p.drawText(ampRect, Qt::AlignVCenter | Qt::AlignRight, ampText);

  p.restore();
}

QColor HybridGaugesOverlay::getVoltageColor(float voltage, float lowLimit, float highLimit) {
  if (voltage <= lowLimit + 10)
    return QColor(231, 76, 60, 255);   // Red
  if (voltage >= highLimit - 10)
    return QColor(241, 196, 15, 255);  // Yellow
  return QColor(236, 240, 241, 255);   // White
}

// ============================================================================
// ANIMATION IMPLEMENTATION
// ============================================================================

void HybridGaugesOverlay::setupAnimation() {
  if (bracketAnimation == nullptr) {
    // Create a dummy widget for the animation
    animationWidget = new QWidget();
    animationWidget->setObjectName("HybridGaugesAnimationWidget");

    // Create the animation - animate a custom property
    bracketAnimation = new QPropertyAnimation(animationWidget, "geometry");
    bracketAnimation->setDuration(200); // 200ms animation
    bracketAnimation->setEasingCurve(QEasingCurve::OutElastic);

    // Connect animation to update bracket scale
    QObject::connect(bracketAnimation, &QPropertyAnimation::valueChanged, [](const QVariant &value) {
      QRect rect = value.toRect();
      // Use the width change to determine scale (1.0 to 1.3)
      bracketScale = 1.0f + (rect.width() - 100) * 0.003f; // Scale from 1.0 to 1.3
      bracketScale = std::clamp(bracketScale, 1.0f, 1.3f);
    });
  }
}

void HybridGaugesOverlay::checkBracketProximity(float currentValue, float threshold) {
  bool isNearBracket = std::abs(std::abs(currentValue) - threshold) < 5.0f; // Within 5% of threshold

  if (isNearBracket != wasNearBracket) {
    wasNearBracket = isNearBracket;

    if (bracketAnimation) {
      if (isNearBracket) {
        // Scale up animation
        QRect startRect(0, 0, 100, 100);
        QRect endRect(0, 0, 130, 100); // 30% wider for 1.3x scale
        bracketAnimation->setStartValue(startRect);
        bracketAnimation->setEndValue(endRect);
      } else {
        // Scale down animation
        QRect startRect(0, 0, 130, 100);
        QRect endRect(0, 0, 100, 100); // Back to normal size
        bracketAnimation->setStartValue(startRect);
        bracketAnimation->setEndValue(endRect);
      }

      bracketAnimation->start();
    }
  }
}

void HybridGaugesOverlay::cleanupAnimation() {
  if (bracketAnimation) {
    bracketAnimation->stop();
    delete bracketAnimation;
    bracketAnimation = nullptr;
  }
  if (animationWidget) {
    delete animationWidget;
    animationWidget = nullptr;
  }
}
