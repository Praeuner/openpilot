#include "alerts_bp.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <map>
#include <algorithm>

#include "selfdrive/ui/qt/util.h"
#include "common/params.h"
#include <QColor>
#include <QRectF>

OnroadAlertsBP::OnroadAlertsBP(QWidget *parent) : QWidget(parent) {
  // Allow this widget to draw over other UI elements without stealing input
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_TransparentForMouseEvents, true);

  // Setup opacity animation for smooth transitions
  opacity_animation = new QPropertyAnimation(this, "alertOpacity");
  opacity_animation->setDuration(300);
  opacity_animation->setEasingCurve(QEasingCurve::OutCubic);

  // Connect valueChanged signal - use proper lambda capture with safety check
  connect(this, &OnroadAlertsBP::valueChanged, this, [this]() {
    if (!is_destroying) {
      update();
    }
  });
}

OnroadAlertsBP::~OnroadAlertsBP() {
  // Mark as destroying to prevent update() calls during destruction
  is_destroying = true;

  // CRITICAL: Disconnect all signals FIRST to prevent accessing destroyed widget
  disconnect(this, nullptr, this, nullptr);

  // Stop animations before deletion to prevent issues
  if (opacity_animation) {
    opacity_animation->stop();
    delete opacity_animation;
    opacity_animation = nullptr;
  }
}

void OnroadAlertsBP::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);

  // Match the parent window geometry (fullscreen overlay inside parent)
  if (parentWidget()) {
    setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
  }
}

void OnroadAlertsBP::updateState(const UIState &s) {
  // Safety check: don't process updates if destroying
  if (is_destroying) {
    return;
  }

  // Store developer UI state for positioning
  dev_ui_info = s.scene.dev_ui_info;

  Alert a = getAlert(*(s.sm), s.scene.started_frame);
  if (!alert.equal(a)) {
    alert = a;

    // Stop any running animations before starting new ones
    if (opacity_animation && opacity_animation->state() == QAbstractAnimation::Running) {
      opacity_animation->stop();
    }

    // Animate opacity for smooth alert transitions
    if (opacity_animation) {
      opacity_animation->setStartValue(alert_opacity);
      opacity_animation->setEndValue(alert.size == cereal::SelfdriveState::AlertSize::NONE ? 0.0 : 1.0);
      opacity_animation->start();
    }

    // Ensure widget is on top when showing an alert
    if (alert.size != cereal::SelfdriveState::AlertSize::NONE) {
      // Force geometry update to cover entire screen
      if (parentWidget()) {
        setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
      }
      // Widget is already on top via stacked layout order - no need for raise()
    }

    update();
  }
}

void OnroadAlertsBP::clear() {
  alert = {};

  // Safety check: don't start animations if destroying
  if (!is_destroying && opacity_animation) {
    // Stop any running animation first
    if (opacity_animation->state() == QAbstractAnimation::Running) {
      opacity_animation->stop();
    }

    // Animate out
    opacity_animation->setStartValue(alert_opacity);
    opacity_animation->setEndValue(0.0);
    opacity_animation->start();
  }


  if (!is_destroying) {
    update();
  }
}

OnroadAlertsBP::Alert OnroadAlertsBP::getAlert(const SubMaster &sm, uint64_t started_frame) {
  const cereal::SelfdriveState::Reader &ss = sm["selfdriveState"].getSelfdriveState();
  const uint64_t selfdrive_frame = sm.rcv_frame("selfdriveState");

  Alert a = {};
  if (selfdrive_frame >= started_frame) {  // Don't get old alert.
    a = {ss.getAlertText1().cStr(), ss.getAlertText2().cStr(),
         ss.getAlertType().cStr(), ss.getAlertSize(), ss.getAlertStatus()};
  }

  if (!sm.updated("selfdriveState") && (sm.frame - started_frame) > 5 * UI_FREQ) {
    const int SELFDRIVE_STATE_TIMEOUT = 5;
    const int ss_missing = (nanos_since_boot() - sm.rcv_time("selfdriveState")) / 1e9;

    // Handle selfdrive timeout
    if (selfdrive_frame < started_frame) {
      a = {tr("BluePilot Unavailable"), tr("Waiting to start"),
           "selfdriveWaiting", cereal::SelfdriveState::AlertSize::MID,
           cereal::SelfdriveState::AlertStatus::NORMAL};
    } else if (ss_missing > SELFDRIVE_STATE_TIMEOUT && !Hardware::PC()) {
      if (ss.getEnabled() && (ss_missing - SELFDRIVE_STATE_TIMEOUT) < 10) {
        a = {tr("TAKE CONTROL IMMEDIATELY"), tr("System Unresponsive"),
             "selfdriveUnresponsive", cereal::SelfdriveState::AlertSize::FULL,
             cereal::SelfdriveState::AlertStatus::CRITICAL};
      } else {
        a = {tr("System Unresponsive"), tr("Reboot Device"),
             "selfdriveUnresponsivePermanent", cereal::SelfdriveState::AlertSize::MID,
             cereal::SelfdriveState::AlertStatus::NORMAL};
      }
    }
  }
  return a;
}


void OnroadAlertsBP::drawBrowserTabCard(QPainter &p, const QRect &rect) {
  // Draw full-screen alert overlay with transparent center portal for road view
  // Alert color frames the edges and appears in bottom card, center stays transparent

  p.setPen(Qt::NoPen);

  // Create full screen rectangle for base color and gradient
  QRect fullScreen(0, 0, width(), height());

  // Draw main background color across entire screen
  p.setBrush(alert_colors[alert.status]);
  p.drawRect(fullScreen);

  // Add gradient overlay across full screen - perfect alignment with border
  QLinearGradient gradient(QPointF(0, 0), QPointF(0, height()));
  if (alert.status == cereal::SelfdriveState::AlertStatus::USER_PROMPT) {
    gradient.setColorAt(0, QColor(255, 255, 255, 20));
    gradient.setColorAt(0.3, QColor(255, 255, 255, 10));
    gradient.setColorAt(1, QColor(0, 0, 0, 15));
  } else {
    gradient.setColorAt(0, QColor(255, 255, 255, 35));
    gradient.setColorAt(0.3, QColor(255, 255, 255, 15));
    gradient.setColorAt(1, QColor(0, 0, 0, 30));
  }
  p.setBrush(gradient);
  p.drawRect(fullScreen);

  // Create a mask to define what's visible
  // We want: opaque alert card at bottom + transparent center portal for road view
  QImage mask(width(), height(), QImage::Format_ARGB32);
  mask.fill(QColor(255, 255, 255, 255)); // Start with everything opaque

  QPainter maskPainter(&mask);
  maskPainter.setRenderHint(QPainter::Antialiasing, true);
  maskPainter.setCompositionMode(QPainter::CompositionMode_Source);

  // Cut out transparent portal in the center (where road view shows through)
  const int borderWidth = 30;  // UI_BORDER_SIZE
  const int cornerRadius = 30;  // Rounded corners for portal
  const int portalGap = 20;  // Minimal gap between transparent portal and alert card
  QRect centerPortal = QRect(borderWidth, borderWidth,
                              width() - 2*borderWidth,
                              rect.top() - borderWidth - portalGap);

  maskPainter.setPen(Qt::NoPen);
  maskPainter.setBrush(QColor(0, 0, 0, 0)); // Fully transparent for center
  QPainterPath centerPath;
  centerPath.addRoundedRect(centerPortal, cornerRadius, cornerRadius);
  maskPainter.drawPath(centerPath);

  maskPainter.end();

  // Apply the mask to create the transparent center cutout
  p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
  p.drawImage(0, 0, mask);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);

  // Text will appear directly over the alert-colored background at the bottom
  // No additional card or shadow needed
}

void OnroadAlertsBP::drawModernCard(QPainter &p, const QRect &rect, bool isFullscreen) {
  if (isFullscreen) {
    // For fullscreen, solid opaque fill - no transparency
    p.setPen(Qt::NoPen);

    // Get fully opaque color for fullscreen
    QColor fullscreenColor = alert_colors[alert.status];
    fullscreenColor.setAlpha(255);  // Fully opaque, no transparency

    p.fillRect(rect, fullscreenColor);
    return;
  }

  // Use browser tab design for non-fullscreen alerts
  drawBrowserTabCard(p, rect);
}

void OnroadAlertsBP::drawScreenBorder(QPainter &p, const QColor &borderColor) {
  // Draw a solid, opaque screen border with sharp outer corners and rounded inner corners
  const int borderWidth = 36;
  const int innerCornerRadius = 30;  // Rounded inner edge for smooth transition

  p.setPen(Qt::NoPen);

  // Draw solid opaque border - no transparency for seamless blend
  QColor solidBorderColor = borderColor;
  solidBorderColor.setAlpha(255);  // Fully opaque

  // Create border path
  QPainterPath borderPath;

  // Outer rectangle with sharp corners (screen edges)
  QPainterPath outer;
  outer.addRect(rect());

  // Inner rectangle with rounded corners (content area)
  QPainterPath inner;
  inner.addRoundedRect(rect().adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth),
                       innerCornerRadius, innerCornerRadius);

  // Subtract inner from outer to create border frame with rounded inner edge
  borderPath = outer.subtracted(inner);

  // Draw base color first
  p.setBrush(solidBorderColor);
  p.drawPath(borderPath);

  // Add same gradient overlay as alert card for seamless blending
  // Use consistent full screen height gradient for perfect vertical alignment
  QLinearGradient gradient(QPointF(0, 0), QPointF(0, height()));
  if (alert.status == cereal::SelfdriveState::AlertStatus::USER_PROMPT) {
    gradient.setColorAt(0, QColor(255, 255, 255, 20));
    gradient.setColorAt(0.3, QColor(255, 255, 255, 10));
    gradient.setColorAt(1, QColor(0, 0, 0, 15));
  } else {
    gradient.setColorAt(0, QColor(255, 255, 255, 35));
    gradient.setColorAt(0.3, QColor(255, 255, 255, 15));
    gradient.setColorAt(1, QColor(0, 0, 0, 30));
  }

  p.setBrush(gradient);
  p.drawPath(borderPath);
}

void OnroadAlertsBP::drawBlurryBorder(QPainter &p, const QColor &borderColor) {
  // Legacy method - now redirects to screen border for consistency
  drawScreenBorder(p, borderColor);
}

// Determine which rendering mode to use for this alert
BPAlertRenderMode OnroadAlertsBP::determineRenderMode(const Alert &a) const {
  // Debug logging
  qDebug() << "Alert Type:" << a.type << "Text1:" << a.text1 << "Text2:" << a.text2 << "Size:" << (int)a.size;

  // Critical alerts always use fullscreen
  if (a.status == cereal::SelfdriveState::AlertStatus::CRITICAL ||
      a.size == cereal::SelfdriveState::AlertSize::FULL) {
    qDebug() << "  -> Using FULLSCREEN_TAKEOVER";
    return BPAlertRenderMode::FULLSCREEN_TAKEOVER;
  }

  // Lane change and blindspot alerts use pill
  QString alertType = a.type;
  if (alertType.contains("laneChange", Qt::CaseInsensitive) ||
      alertType.contains("Blindspot", Qt::CaseInsensitive) ||
      alertType == "preLaneChangeLeft" ||
      alertType == "preLaneChangeRight") {
    qDebug() << "  -> Using PILL_BOTTOM";
    return BPAlertRenderMode::PILL_BOTTOM;
  }

  // Everything else uses browser tab card
  qDebug() << "  -> Using BROWSER_TAB_CARD";
  return BPAlertRenderMode::BROWSER_TAB_CARD;
}

// Determine pill size based on alert content
PillAlertSize OnroadAlertsBP::getPillSize(const Alert &a) const {
  // If alert has text2, use 2-line pill
  if (!a.text2.isEmpty()) {
    return PillAlertSize::PILL_MEDIUM;
  }
  return PillAlertSize::PILL_SMALL;
}

// Calculate pill dimensions with dynamic font scaling
PillDimensions OnroadAlertsBP::calculatePillDimensions(const QString &text1, const QString &text2, PillAlertSize size) const {
  // Use parent width if available, otherwise use widget width (accounts for sidebar)
  int contentWidth = parentWidget() ? parentWidget()->width() : width();
  const int maxWidth = contentWidth - 40;  // 20px margin per side
  const int horizontalPadding = (size == PillAlertSize::PILL_SMALL) ? 100 : 120;  // 50px or 60px per side
  const int maxTextWidth = maxWidth - horizontalPadding;

  PillDimensions result;
  result.height = (size == PillAlertSize::PILL_SMALL) ? 84 : 140;  // Increased for more vertical padding
  result.fontSize2 = 0;  // Default for single-line

  if (size == PillAlertSize::PILL_SMALL) {
    // Single line
    int baseFontSize = 64;
    QFont font = InterFont(baseFontSize, QFont::DemiBold);
    QFontMetrics fm(font);
    int textWidth = fm.horizontalAdvance(text1);

    // Scale font down if text too wide
    if (textWidth > maxTextWidth) {
      float scale = (float)maxTextWidth / textWidth;
      result.fontSize1 = (int)(baseFontSize * scale);

      // Recalculate width with scaled font
      QFont scaledFont = InterFont(result.fontSize1, QFont::DemiBold);
      QFontMetrics scaledFm(scaledFont);
      result.width = scaledFm.horizontalAdvance(text1) + horizontalPadding;
    } else {
      result.fontSize1 = baseFontSize;
      result.width = textWidth + horizontalPadding;
    }

  } else {
    // Two lines - measure both, scale proportionally if needed
    int baseFontSize1 = 72;
    int baseFontSize2 = 56;

    QFont font1 = InterFont(baseFontSize1, QFont::Bold);
    QFont font2 = InterFont(baseFontSize2);
    QFontMetrics fm1(font1);
    QFontMetrics fm2(font2);

    int textWidth1 = fm1.horizontalAdvance(text1);
    int textWidth2 = fm2.horizontalAdvance(text2);
    int maxTextWidthNeeded = std::max(textWidth1, textWidth2);

    // Scale both lines proportionally if needed
    if (maxTextWidthNeeded > maxTextWidth) {
      float scale = (float)maxTextWidth / maxTextWidthNeeded;
      result.fontSize1 = (int)(baseFontSize1 * scale);
      result.fontSize2 = (int)(baseFontSize2 * scale);

      // Recalculate width with scaled fonts
      QFont scaledFont1 = InterFont(result.fontSize1, QFont::Bold);
      QFont scaledFont2 = InterFont(result.fontSize2);
      QFontMetrics scaledFm1(scaledFont1);
      QFontMetrics scaledFm2(scaledFont2);

      int scaledWidth1 = scaledFm1.horizontalAdvance(text1);
      int scaledWidth2 = scaledFm2.horizontalAdvance(text2);
      result.width = std::max(scaledWidth1, scaledWidth2) + horizontalPadding;
    } else {
      result.fontSize1 = baseFontSize1;
      result.fontSize2 = baseFontSize2;
      result.width = maxTextWidthNeeded + horizontalPadding;
    }
  }

  return result;
}

// Calculate pill rectangle position (bottom-centered in parent container)
QRect OnroadAlertsBP::calculatePillRect(int pillWidth, int pillHeight) const {
  // Use parent dimensions to properly center even when sidebar is visible
  int contentWidth = parentWidget() ? parentWidget()->width() : width();
  int contentHeight = parentWidget() ? parentWidget()->height() : height();

  int x = (contentWidth - pillWidth) / 2;  // Center horizontally in parent
  int y = contentHeight - pillHeight - 50; // 50px from bottom edge

  return QRect(x, y, pillWidth, pillHeight);
}


// Get pill background color based on alert status
QColor OnroadAlertsBP::getPillBackgroundColor(cereal::SelfdriveState::AlertStatus status) const {
  if (status == cereal::SelfdriveState::AlertStatus::USER_PROMPT) {
    return QColor(220, 100, 20, 255);  // Orange warning (fully opaque)
  }
  return QColor(45, 46, 48, 255);  // Dark neutral (fully opaque)
}

// Get pill border color based on alert status
QColor OnroadAlertsBP::getPillBorderColor(cereal::SelfdriveState::AlertStatus status) const {
  if (status == cereal::SelfdriveState::AlertStatus::USER_PROMPT) {
    return QColor(255, 140, 60, 200);  // Warm glow
  }
  return QColor(80, 82, 85, 200);  // Subtle border
}

// Draw pill alert (multi-line, no wrapping per line, dynamic font scaling)
void OnroadAlertsBP::drawPillAlert(QPainter &p, const QRect &rect, const PillDimensions &dims) {
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(Qt::NoPen);

  // Draw drop shadow layers for depth
  p.setBrush(QColor(0, 0, 0, 100));
  p.drawRoundedRect(rect.adjusted(0, 6, 0, 6), rect.height() / 2, rect.height() / 2);

  p.setBrush(QColor(0, 0, 0, 60));
  p.drawRoundedRect(rect.adjusted(0, 4, 0, 4), rect.height() / 2, rect.height() / 2);

  p.setBrush(QColor(0, 0, 0, 30));
  p.drawRoundedRect(rect.adjusted(0, 2, 0, 2), rect.height() / 2, rect.height() / 2);

  // Background with status-based color
  QColor bgColor = getPillBackgroundColor(alert.status);
  p.setBrush(bgColor);
  p.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);

  // Border
  QColor borderColor = getPillBorderColor(alert.status);
  p.setPen(QPen(borderColor, 2));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);

  // Text rendering with scaled fonts (no wrapping per line)
  p.setRenderHint(QPainter::TextAntialiasing);

  if (alert.text2.isEmpty()) {
    // Single line with scaled font
    p.setFont(InterFont(dims.fontSize1, QFont::DemiBold));

    // Subtle shadow
    p.setPen(QColor(0, 0, 0, 100));
    p.drawText(rect.adjusted(2, 2, 2, 2), Qt::AlignCenter | Qt::TextSingleLine, alert.text1);

    // Main text (no wrapping)
    p.setPen(Qt::white);
    p.drawText(rect, Qt::AlignCenter | Qt::TextSingleLine, alert.text1);

  } else {
    // Two lines with scaled fonts (no wrapping per line)
    // Line 1
    p.setFont(InterFont(dims.fontSize1, QFont::Bold));
    QRect line1Rect(rect.x(), rect.y(), rect.width(), rect.height() * 0.55);

    p.setPen(QColor(0, 0, 0, 100));
    p.drawText(line1Rect.adjusted(2, 2, 2, 2), Qt::AlignHCenter | Qt::AlignBottom | Qt::TextSingleLine, alert.text1);

    p.setPen(Qt::white);
    p.drawText(line1Rect, Qt::AlignHCenter | Qt::AlignBottom | Qt::TextSingleLine, alert.text1);

    // Line 2
    p.setFont(InterFont(dims.fontSize2));
    p.setPen(QColor(220, 220, 220));
    QRect line2Rect(rect.x(), rect.y() + rect.height() * 0.55, rect.width(), rect.height() * 0.45);
    p.drawText(line2Rect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine, alert.text2);
  }
}

void OnroadAlertsBP::paintEvent(QPaintEvent *event) {
  // Check if custom alerts are enabled
  bool bpAlertsEnabled = Params().getBool("BPUseBluepilotAlerts");
  if (!bpAlertsEnabled) {
    qDebug() << "BPUseBluepilotAlerts is disabled - not rendering BluePilot alerts";
    return;
  }

  if (alert.size == cereal::SelfdriveState::AlertSize::NONE || alert_opacity < 0.01) {
    return;
  }

  qDebug() << "Rendering alert with opacity:" << alert_opacity;

  bool isFullscreen = (alert.size == cereal::SelfdriveState::AlertSize::FULL);

  QPainter p(this);

  // Determine rendering mode
  BPAlertRenderMode renderMode = determineRenderMode(alert);

  if (renderMode == BPAlertRenderMode::PILL_BOTTOM) {
    // Pill alert rendering - no backdrop, just the pill with drop shadow
    PillAlertSize pillSize = getPillSize(alert);
    PillDimensions dims = calculatePillDimensions(alert.text1, alert.text2, pillSize);
    QRect pillRect = calculatePillRect(dims.width, dims.height);

    p.setOpacity(alert_opacity);

    // Draw pill with drop shadow
    drawPillAlert(p, pillRect, dims);
    return;
  }

  // Existing rendering modes (fullscreen and browser tab)
  // For non-fullscreen alerts, we now draw full-screen background with portal window
  // No need to draw separate border - it's integrated into the alert drawing

  // Developer UI adjustments (ported from sunnypilot)
  const int v_adjustment = dev_ui_info > 1 && !isFullscreen ? 40 : 0;
  const int h_adjustment = dev_ui_info > 0 && !isFullscreen ? 230 : 0;

  int margin = 40;
  int h = 0;
  QRect r;

  if (isFullscreen) {
    r = QRect(0, 0, width(), height());
    margin = 0;
  } else {
    // Calculate dynamic height based on text content (ported from sunnypilot)
    QFont topFont;
    QFont bottomFont;
    QRect topTextBoundingRect;
    QRect bottomTextBoundingRect;

    if (alert.size == cereal::SelfdriveState::AlertSize::SMALL) {
      topFont = InterFont(74, QFont::DemiBold);
      QFontMetrics fmTop(topFont);
      topTextBoundingRect = fmTop.boundingRect(
        QRect(0 + margin, height() - 400 + margin - v_adjustment, width() - margin * 2 - h_adjustment, 0),
        Qt::TextWordWrap, alert.text1);
      h = topTextBoundingRect.height() + margin * 2;  // Reduced from 3 to 2 for shorter card

    } else if (alert.size == cereal::SelfdriveState::AlertSize::MID) {
      topFont = InterFont(88, QFont::Bold);
      bottomFont = InterFont(66);
      QFontMetrics fmTop(topFont);
      QFontMetrics fmBottom(bottomFont);
      topTextBoundingRect = fmTop.boundingRect(
        QRect(0 + margin, 0, width() - margin * 2 - h_adjustment, 0),
        Qt::TextWordWrap, alert.text1);
      bottomTextBoundingRect = fmBottom.boundingRect(
        QRect(0 + margin, 0, width() - margin * 2 - h_adjustment, 0),
        Qt::TextWordWrap, alert.text2);
      h = topTextBoundingRect.height() + bottomTextBoundingRect.height() + margin * 2.5;  // Reduced from 4 to 2.5 for shorter card
    }

    // Portal window with margins from screen edges
    // This creates a nice rounded window floating at the bottom
    const int sideMargin = 40;  // Margin from left/right edges
    const int bottomMargin = 40; // Margin from bottom edge
    r = QRect(sideMargin, height() - h - bottomMargin - v_adjustment,
              width() - (sideMargin * 2), h);
  }

  p.setOpacity(alert_opacity);

  // Draw modern card (browser tab for non-fullscreen)
  drawModernCard(p, r, isFullscreen);

  // Reset opacity for text
  p.setOpacity(alert_opacity);

  // Text rendering with modern typography
  p.setPen(QColor(0xff, 0xff, 0xff));
  p.setRenderHint(QPainter::TextAntialiasing);

  if (alert.size == cereal::SelfdriveState::AlertSize::SMALL) {
    // Single line alert with modern font
    p.setFont(InterFont(74, QFont::DemiBold));

    // Multi-layer 3D text shadow effect
    p.setPen(QColor(0, 0, 0, 120));
    p.drawText(r.adjusted(3, 3, 3, 3), Qt::AlignCenter | Qt::TextWordWrap, alert.text1);

    p.setPen(QColor(0, 0, 0, 80));
    p.drawText(r.adjusted(2, 2, 2, 2), Qt::AlignCenter | Qt::TextWordWrap, alert.text1);

    p.setPen(QColor(0, 0, 0, 40));
    p.drawText(r.adjusted(1, 1, 1, 1), Qt::AlignCenter | Qt::TextWordWrap, alert.text1);

    // Main text with slight highlight
    p.setPen(Qt::white);
    p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, alert.text1);

  } else if (alert.size == cereal::SelfdriveState::AlertSize::MID) {
    // Draw main text with 3D shadow layers
    p.setFont(InterFont(88, QFont::Bold));

    // Center text vertically in the alert card for better visual balance
    QRect text1Rect = QRect(r.x(), r.top(), r.width(), r.height() * 0.6);

    p.setPen(QColor(0, 0, 0, 120));
    p.drawText(text1Rect.adjusted(3, 3, 3, 3), Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, alert.text1);

    p.setPen(QColor(0, 0, 0, 80));
    p.drawText(text1Rect.adjusted(2, 2, 2, 2), Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, alert.text1);

    p.setPen(Qt::white);
    p.drawText(text1Rect, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, alert.text1);

    // Draw secondary text in the bottom portion
    p.setFont(InterFont(66));
    p.setPen(QColor(220, 220, 220));

    QRect text2Rect = QRect(r.x(), r.top() + r.height() * 0.6, r.width(), r.height() * 0.4);
    p.drawText(text2Rect, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, alert.text2);

  } else if (alert.size == cereal::SelfdriveState::AlertSize::FULL) {
    bool longText = alert.text1.length() > 15;

    // Main alert text with dramatic 3D shadow
    p.setFont(InterFont(longText ? 132 : 177, QFont::Bold));

    QRect text1Rect = QRect(0, r.y() + (longText ? 240 : 270), width(), 600);

    // Deep shadow layers for fullscreen
    p.setPen(QColor(0, 0, 0, 160));
    p.drawText(text1Rect.adjusted(4, 4, 4, 4), Qt::AlignHCenter | Qt::TextWordWrap, alert.text1);

    p.setPen(QColor(0, 0, 0, 120));
    p.drawText(text1Rect.adjusted(3, 3, 3, 3), Qt::AlignHCenter | Qt::TextWordWrap, alert.text1);

    p.setPen(QColor(0, 0, 0, 60));
    p.drawText(text1Rect.adjusted(2, 2, 2, 2), Qt::AlignHCenter | Qt::TextWordWrap, alert.text1);

    p.setPen(Qt::white);
    p.drawText(text1Rect, Qt::AlignHCenter | Qt::TextWordWrap, alert.text1);

    // Secondary text
    p.setFont(InterFont(88));
    p.setPen(QColor(220, 220, 220));
    p.drawText(QRect(0, r.height() - (longText ? 361 : 420), width(), 300),
               Qt::AlignHCenter | Qt::TextWordWrap, alert.text2);
  }
}
