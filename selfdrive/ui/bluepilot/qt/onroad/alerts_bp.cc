#include "alerts_bp.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <map>

#include "selfdrive/ui/qt/util.h"

OnroadAlertsBP::OnroadAlertsBP(QWidget *parent) : QWidget(parent) {
  // Setup opacity animation for smooth transitions
  opacity_animation = new QPropertyAnimation(this, "alertOpacity");
  opacity_animation->setDuration(300);
  opacity_animation->setEasingCurve(QEasingCurve::OutCubic);

  // Setup progress animation for smooth percentage changes
  progress_animation = new QPropertyAnimation(this, "progressValue");
  progress_animation->setDuration(500);
  progress_animation->setEasingCurve(QEasingCurve::InOutCubic);

  connect(this, &OnroadAlertsBP::valueChanged, [=] { update(); });
}

OnroadAlertsBP::~OnroadAlertsBP() {
  delete opacity_animation;
  delete progress_animation;
}

void OnroadAlertsBP::updateState(const UIState &s) {
  Alert a = getAlert(*(s.sm), s.scene.started_frame);
  if (!alert.equal(a)) {
    alert = a;

    // Animate opacity for smooth alert transitions
    opacity_animation->setStartValue(alert_opacity);
    opacity_animation->setEndValue(alert.size == cereal::SelfdriveState::AlertSize::NONE ? 0.0 : 1.0);
    opacity_animation->start();

    // Check for percentage in text2 and animate progress
    float new_progress = extractPercentage(alert.text2);
    if (new_progress >= 0) {
      progress_animation->setStartValue(progress_value);
      progress_animation->setEndValue(new_progress);
      progress_animation->start();
      target_progress = new_progress;
    } else {
      target_progress = -1;
    }

    update();
  }
}

void OnroadAlertsBP::clear() {
  alert = {};

  // Animate out
  opacity_animation->setStartValue(alert_opacity);
  opacity_animation->setEndValue(0.0);
  opacity_animation->start();

  target_progress = -1;
  update();
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

float OnroadAlertsBP::extractPercentage(const QString &text) {
  // Regular expression to find percentage values (e.g., "50%", "100%")
  QRegularExpression re("(\\d+(?:\\.\\d+)?)\\s*%");
  QRegularExpressionMatch match = re.match(text);

  if (match.hasMatch()) {
    bool ok;
    float value = match.captured(1).toFloat(&ok);
    if (ok && value >= 0 && value <= 100) {
      return value / 100.0f; // Return as 0-1 range
    }
  }

  return -1; // No percentage found
}

void OnroadAlertsBP::drawRadialProgress(QPainter &p, const QRect &rect, float percentage) {
  const int size = 100; // Larger dial
  const int strokeWidth = 10;
  const int centerX = rect.center().x();  // Center in container
  const int centerY = rect.center().y();

  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);

  // Draw outer shadow ring
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 30));
  p.drawEllipse(QPoint(centerX + 2, centerY + 2), size/2 + 4, size/2 + 4);

  // Draw background track with gradient
  QConicalGradient bgGradient(centerX, centerY, 0);
  bgGradient.setColorAt(0, QColor(60, 60, 60, 140));
  bgGradient.setColorAt(1, QColor(80, 80, 80, 140));
  p.setPen(QPen(QBrush(bgGradient), strokeWidth, Qt::SolidLine, Qt::RoundCap));
  p.setBrush(Qt::NoBrush);
  p.drawEllipse(QPoint(centerX, centerY), size/2, size/2);

  // Draw progress arc with animated gradient
  if (percentage > 0) {
    // Create smooth gradient for progress
    QPainterPath progressPath;
    QRectF arcRect(centerX - size/2, centerY - size/2, size, size);

    // Calculate sweep angle
    float sweepAngle = percentage * 360.0f;

    // Use linear gradient along the arc for smooth color transition
    QConicalGradient progressGradient(centerX, centerY, -90);

    // Multi-color gradient for visual appeal
    if (percentage < 0.5) {
      progressGradient.setColorAt(0, QColor(24, 144, 255)); // Blue
      progressGradient.setColorAt(percentage, QColor(3, 132, 252)); // Darker blue
    } else {
      progressGradient.setColorAt(0, QColor(24, 144, 255)); // Blue
      progressGradient.setColorAt(0.25, QColor(3, 200, 252)); // Cyan
      progressGradient.setColorAt(0.5, QColor(42, 199, 122)); // Green
      progressGradient.setColorAt(percentage, QColor(42, 199, 122));
    }
    progressGradient.setColorAt(percentage + 0.001, Qt::transparent);

    // Draw main progress arc
    p.setPen(QPen(QBrush(progressGradient), strokeWidth, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arcRect, 90 * 16, -sweepAngle * 16);

    // Draw bright leading edge dot
    float angleRad = qDegreesToRadians(-90 + sweepAngle);
    int dotX = centerX + (size/2) * cos(angleRad);
    int dotY = centerY + (size/2) * sin(angleRad);

    // Leading edge glow
    QRadialGradient dotGlow(dotX, dotY, 15);
    dotGlow.setColorAt(0, QColor(255, 255, 255, 180));
    dotGlow.setColorAt(0.5, QColor(accent_color.red(), accent_color.green(), accent_color.blue(), 100));
    dotGlow.setColorAt(1, Qt::transparent);
    p.setPen(Qt::NoPen);
    p.setBrush(dotGlow);
    p.drawEllipse(QPoint(dotX, dotY), 15, 15);

    // Inner bright dot
    p.setBrush(Qt::white);
    p.drawEllipse(QPoint(dotX, dotY), 4, 4);
  }

  // Draw inner circle background
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(25, 25, 25, 200));
  p.drawEllipse(QPoint(centerX, centerY), size/2 - 15, size/2 - 15);

  // Draw percentage text with better styling
  p.setPen(Qt::white);
  p.setFont(InterFont(36, QFont::Bold));
  QString percentText = QString("%1").arg(int(percentage * 100));
  QRect textRect(centerX - size/2, centerY - 15, size, 20);
  p.drawText(textRect, Qt::AlignCenter, percentText);

  // Draw % symbol smaller
  p.setFont(InterFont(20, QFont::DemiBold));
  p.setPen(QColor(180, 180, 180));
  QRect symbolRect(centerX - size/2, centerY + 5, size, 20);
  p.drawText(symbolRect, Qt::AlignCenter, "%");

  // Outer glow effect
  if (percentage > 0) {
    QRadialGradient outerGlow(centerX, centerY, size + 10);
    QColor glowColor = percentage > 0.8 ? good_color : accent_color;
    outerGlow.setColorAt(0, QColor(glowColor.red(), glowColor.green(), glowColor.blue(), 15));
    outerGlow.setColorAt(0.5, QColor(glowColor.red(), glowColor.green(), glowColor.blue(), 8));
    outerGlow.setColorAt(1, Qt::transparent);
    p.setPen(Qt::NoPen);
    p.setBrush(outerGlow);
    p.drawEllipse(QPoint(centerX, centerY), size + 10, size + 10);
  }

  p.restore();
}

void OnroadAlertsBP::drawModernCard(QPainter &p, const QRect &rect, bool isFullscreen) {
  if (isFullscreen) {
    // For fullscreen, no fancy effects, just fill
    p.setPen(Qt::NoPen);
    p.setBrush(alert_colors[alert.status]);
    p.fillRect(rect, alert_colors[alert.status]);
    return;
  }

  // Create card path with rounded corners
  QPainterPath path;
  const int radius = 20; // Modern rounded corners
  path.addRoundedRect(rect, radius, radius);

  // Draw stronger shadow for depth (3D effect)
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 100)); // Darker shadow
  p.drawRoundedRect(rect.adjusted(6, 6, 6, 6), radius, radius);

  // Draw bright outer border for visibility
  p.setPen(QPen(accent_colors[alert.status], 3, Qt::SolidLine)); // 3px colored border
  p.setBrush(alert_colors[alert.status]);
  p.drawPath(path);

  // Add material gradient overlay for depth
  QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
  gradient.setColorAt(0, QColor(255, 255, 255, 30)); // Brighter top
  gradient.setColorAt(0.5, QColor(255, 255, 255, 15));
  gradient.setColorAt(1, QColor(0, 0, 0, 50)); // Darker bottom

  p.setPen(Qt::NoPen);
  p.setBrush(gradient);
  p.drawPath(path);

  // Add bright inner glow for premium feel
  QPainterPath innerPath;
  QRect innerRect = rect.adjusted(3, 3, -3, -3);
  innerPath.addRoundedRect(innerRect, radius - 3, radius - 3);

  QLinearGradient innerGlow(rect.topLeft(), rect.topRight());
  innerGlow.setColorAt(0, QColor(255, 255, 255, 25)); // Brighter inner glow
  innerGlow.setColorAt(0.5, QColor(255, 255, 255, 10));
  innerGlow.setColorAt(1, QColor(255, 255, 255, 25));

  p.setPen(QPen(QBrush(innerGlow), 2)); // Thicker inner border
  p.setBrush(Qt::NoBrush);
  p.drawPath(innerPath);
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
  // int radius = 30;
  if (alert.size == cereal::SelfdriveState::AlertSize::FULL) {
    margin = 0;
    // radius = 0;
  }
  QRect r = QRect(0 + margin, height() - h + margin, width() - margin*2, h - margin*2);

  QPainter p(this);
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

    // Calculate equal containers for balanced layout
    bool hasProgress = (target_progress >= 0 && progress_value > 0.01);
    int containerWidth = r.width() / 2;

    // Left container for text (always centered)
    QRect textContainer = QRect(r.x(), r.y(), hasProgress ? containerWidth : r.width(), r.height());

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

    // Remove percentage from text2 if showing dial
    QString displayText = alert.text2;
    if (hasProgress) {
      QRegularExpression re("\\s*\\d+(?:\\.\\d+)?\\s*%");
      displayText = displayText.remove(re).trimmed();
    }

    QRect text2Rect = QRect(0, c.y() + 21, width(), 90);
    p.drawText(text2Rect, Qt::AlignHCenter, displayText);

    // Draw radial progress in right container if percentage detected
    if (hasProgress) {
      // Right container for radial dial
      QRect dialContainer = QRect(r.x() + containerWidth, r.y(), containerWidth, r.height());
      drawRadialProgress(p, dialContainer, progress_value);
    }

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
