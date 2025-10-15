#include "alerts_bp.h"

#include <QPainter>
#include <QPainterPath>
#include <map>

#include "selfdrive/ui/qt/util.h"

OnroadAlertsBP::OnroadAlertsBP(QWidget *parent) : QWidget(parent) {
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


void OnroadAlertsBP::drawModernCard(QPainter &p, const QRect &rect, bool isFullscreen) {
  if (isFullscreen) {
    // For fullscreen, no fancy effects, just fill
    p.setPen(Qt::NoPen);
    p.setBrush(alert_colors[alert.status]);
    p.fillRect(rect, alert_colors[alert.status]);
    return;
  }

  // Simplified modern card with standard rounded corners
  const int radius = 30;

  // Draw shadow first (simplified)
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 80));
  QRect shadowRect = rect.adjusted(-2, -4, 2, 0);
  p.drawRoundedRect(shadowRect, radius, radius);

  // Draw main background
  p.setPen(Qt::NoPen);
  p.setBrush(alert_colors[alert.status]);
  p.drawRoundedRect(rect, radius, radius);

  // Add subtle gradient overlay
  QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
  if (alert.status == cereal::SelfdriveState::AlertStatus::USER_PROMPT) {
    gradient.setColorAt(0, QColor(255, 255, 255, 20));
    gradient.setColorAt(0.3, QColor(255, 255, 255, 10));
    gradient.setColorAt(1, QColor(0, 0, 0, 15));
  } else {
    gradient.setColorAt(0, QColor(255, 255, 255, 35));
    gradient.setColorAt(0.3, QColor(255, 255, 255, 15));
    gradient.setColorAt(1, QColor(0, 0, 0, 30));
  }

  p.setPen(Qt::NoPen);
  p.setBrush(gradient);
  p.drawRoundedRect(rect, radius, radius);
}

void OnroadAlertsBP::drawBlurryBorder(QPainter &p, const QColor &borderColor) {
  // Draw multiple layers of semi-transparent borders to create blur effect
  const int borderWidth = 12;
  const int layers = 6;

  p.setPen(Qt::NoPen);

  for (int i = 0; i < layers; i++) {
    int alpha = 80 - (i * 12); // Fade out as we go outward
    int offset = i * 2;

    QColor layerColor = borderColor;
    layerColor.setAlpha(alpha);

    // Draw border inset from edges
    QPainterPath borderPath;
    QRect borderRect = rect().adjusted(offset, offset, -offset, -offset);
    const int cornerRadius = 8 + i;

    // Outer edge
    QPainterPath outer;
    outer.addRoundedRect(borderRect, cornerRadius, cornerRadius);

    // Inner edge (smaller)
    QPainterPath inner;
    QRect innerRect = borderRect.adjusted(borderWidth - offset, borderWidth - offset,
                                          -(borderWidth - offset), -(borderWidth - offset));
    inner.addRoundedRect(innerRect, cornerRadius - 2, cornerRadius - 2);

    // Subtract inner from outer to get border
    borderPath = outer.subtracted(inner);

    p.setBrush(layerColor);
    p.drawPath(borderPath);
  }
}

void OnroadAlertsBP::paintEvent(QPaintEvent *event) {
  if (alert.size == cereal::SelfdriveState::AlertSize::NONE || alert_opacity < 0.01) {
    return;
  }

  static std::map<cereal::SelfdriveState::AlertSize, const int> alert_heights = {
    {cereal::SelfdriveState::AlertSize::SMALL, 271},
    {cereal::SelfdriveState::AlertSize::MID, 420},
    {cereal::SelfdriveState::AlertSize::FULL, height()},
  };

  int h = alert_heights[alert.size];
  bool isFullscreen = (alert.size == cereal::SelfdriveState::AlertSize::FULL);

  int margin = 40;
  if (alert.size == cereal::SelfdriveState::AlertSize::FULL) {
    margin = 0;
  }

  // Adjust for developer UI bottom panel (60px height)
  int bottom_offset = 0;
  if (dev_ui_info == 2) {  // Bottom panel is visible
    bottom_offset = 70;  // Move up by 70px to avoid bottom panel collision
  }

  // Position alert with standard margins
  QRect r;
  if (isFullscreen) {
    r = QRect(0, 0, width(), height());
  } else {
    // Standard positioning with margins on all sides
    r = QRect(margin, height() - h - margin - bottom_offset, width() - margin*2, h);
  }

  QPainter p(this);

  // Draw blurry border around entire display (only for non-fullscreen)
  if (!isFullscreen) {
    p.setOpacity(alert_opacity);
    drawBlurryBorder(p, alert_colors[alert.status]);
  }

  p.setOpacity(alert_opacity);

  // Draw modern card with material design
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
    p.drawText(r.adjusted(3, 3, 3, 3), Qt::AlignCenter, alert.text1);

    p.setPen(QColor(0, 0, 0, 80));
    p.drawText(r.adjusted(2, 2, 2, 2), Qt::AlignCenter, alert.text1);

    p.setPen(QColor(0, 0, 0, 40));
    p.drawText(r.adjusted(1, 1, 1, 1), Qt::AlignCenter, alert.text1);

    // Main text with slight highlight
    p.setPen(Qt::white);
    p.drawText(r, Qt::AlignCenter, alert.text1);

  } else if (alert.size == cereal::SelfdriveState::AlertSize::MID) {
    const QPoint c = r.center();


    // Draw main text with 3D shadow layers - match original positioning
    p.setFont(InterFont(88, QFont::Bold));

    QRect text1Rect = QRect(0, c.y() - 125, width(), 150);

    p.setPen(QColor(0, 0, 0, 120));
    p.drawText(text1Rect.adjusted(3, 3, 3, 3), Qt::AlignHCenter | Qt::AlignTop, alert.text1);

    p.setPen(QColor(0, 0, 0, 80));
    p.drawText(text1Rect.adjusted(2, 2, 2, 2), Qt::AlignHCenter | Qt::AlignTop, alert.text1);

    p.setPen(Qt::white);
    p.drawText(text1Rect, Qt::AlignHCenter | Qt::AlignTop, alert.text1);

    // Draw secondary text - match original positioning
    p.setFont(InterFont(66));
    p.setPen(QColor(220, 220, 220));

    QRect text2Rect = QRect(0, c.y() + 21, width(), 90);
    p.drawText(text2Rect, Qt::AlignHCenter, alert.text2);


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
