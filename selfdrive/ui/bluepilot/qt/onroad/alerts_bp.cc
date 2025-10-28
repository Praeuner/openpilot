#include "selfdrive/ui/bluepilot/qt/onroad/alerts_bp.h"

#include <QPainter>
#include <QFontMetrics>
#include <map>
#include <algorithm>

#include "selfdrive/ui/qt/util.h"
#include "common/params.h"

OnroadAlertsBP::OnroadAlertsBP(QWidget *parent) : QWidget(parent) {
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_TransparentForMouseEvents, true);

  // Setup pulse timer for warning animations
  pulse_timer = new QTimer(this);
  connect(pulse_timer, &QTimer::timeout, this, [this]() {
    // Pulse between 0.7 and 1.0 opacity for warning pills
    const float pulse_speed = 0.015f;  // Slower pulse (was 0.03f)
    const float pulse_min = 0.7f;
    const float pulse_max = 1.0f;

    if (pulse_increasing) {
      pulse_opacity += pulse_speed;
      if (pulse_opacity >= pulse_max) {
        pulse_opacity = pulse_max;
        pulse_increasing = false;
      }
    } else {
      pulse_opacity -= pulse_speed;
      if (pulse_opacity <= pulse_min) {
        pulse_opacity = pulse_min;
        pulse_increasing = true;
      }
    }
    update();
  });
}

OnroadAlertsBP::~OnroadAlertsBP() {
  // Stop and clean up pulse timer
  if (pulse_timer) {
    pulse_timer->stop();
    disconnect(pulse_timer, nullptr, this, nullptr);
    delete pulse_timer;
    pulse_timer = nullptr;
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
  Alert a = getAlert(*(s.sm), s.scene.started_frame);
  if (!alert.equal(a)) {
    alert = a;

    // Start/stop pulse animation for warning pills
    if (pulse_timer) {
      bool should_pulse = (alert.size != cereal::SelfdriveState::AlertSize::NONE &&
                           alert.size != cereal::SelfdriveState::AlertSize::FULL &&
                           alert.status == cereal::SelfdriveState::AlertStatus::USER_PROMPT);

      if (should_pulse && !pulse_timer->isActive()) {
        pulse_opacity = 1.0;
        pulse_increasing = false;
        pulse_timer->start(16); // ~60fps
      } else if (!should_pulse && pulse_timer->isActive()) {
        pulse_timer->stop();
        pulse_opacity = 1.0;
      }
    }

    // Ensure widget covers entire parent when showing an alert
    if (alert.size != cereal::SelfdriveState::AlertSize::NONE) {
      if (parentWidget()) {
        setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
      }
    }

    update();
  }
}

void OnroadAlertsBP::clear() {
  alert = {};
  if (pulse_timer && pulse_timer->isActive()) {
    pulse_timer->stop();
  }
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
      // car is started, but selfdriveState hasn't been seen at all
      a = {tr("BluePilot Unavailable"), tr("Waiting to start"),
           "selfdriveWaiting", cereal::SelfdriveState::AlertSize::MID,
           cereal::SelfdriveState::AlertStatus::NORMAL};
    } else if (ss_missing > SELFDRIVE_STATE_TIMEOUT && !Hardware::PC()) {
      // car is started, but selfdrive is lagging or died
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

void OnroadAlertsBP::paintEvent(QPaintEvent *event) {
  if (alert.size == cereal::SelfdriveState::AlertSize::NONE) {
    return;
  }

  QPainter p(this);

  // Fullscreen alerts for critical situations
  if (alert.size == cereal::SelfdriveState::AlertSize::FULL) {
    drawFullscreenAlert(p);
    return;
  }

  // All other alerts use pill rendering
  PillAlertSize pillSize = getPillSize(alert);
  PillDimensions dims = calculatePillDimensions(alert.text1, alert.text2, pillSize);
  QRect pillRect = calculatePillRect(dims.width, dims.height);

  drawPillAlert(p, pillRect, dims, pulse_opacity);
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
  int contentWidth = parentWidget() ? parentWidget()->width() : width();
  const int maxWidth = contentWidth - 40;  // 20px margin per side
  const int horizontalPadding = (size == PillAlertSize::PILL_SMALL) ? 140 : 160;  // 70px or 80px per side
  const int maxTextWidth = maxWidth - horizontalPadding;
  const int verticalPadding = (size == PillAlertSize::PILL_SMALL) ? 24 : 40;  // Top + bottom padding for text

  PillDimensions result;
  result.fontSize2 = 0;  // Default for single-line

  if (size == PillAlertSize::PILL_SMALL) {
    // Single line - 74pt DemiBold
    int baseFontSize = 74;
    QFont font = InterFont(baseFontSize, QFont::DemiBold);
    QFontMetrics fm(font);
    int textWidth = fm.horizontalAdvance(text1);
    int textHeight = fm.height();

    // Scale font down if text too wide
    if (textWidth > maxTextWidth) {
      float scale = (float)maxTextWidth / textWidth;
      result.fontSize1 = (int)(baseFontSize * scale);

      QFont scaledFont = InterFont(result.fontSize1, QFont::DemiBold);
      QFontMetrics scaledFm(scaledFont);
      result.width = scaledFm.horizontalAdvance(text1) + horizontalPadding;
      textHeight = scaledFm.height();
    } else {
      result.fontSize1 = baseFontSize;
      result.width = textWidth + horizontalPadding;
    }

    result.height = textHeight + verticalPadding;

  } else {
    // Two lines - 88pt Bold / 66pt Regular
    int baseFontSize1 = 88;
    int baseFontSize2 = 66;

    QFont font1 = InterFont(baseFontSize1, QFont::Bold);
    QFont font2 = InterFont(baseFontSize2);
    QFontMetrics fm1(font1);
    QFontMetrics fm2(font2);

    int textWidth1 = fm1.horizontalAdvance(text1);
    int textWidth2 = fm2.horizontalAdvance(text2);
    int maxTextWidthNeeded = std::max(textWidth1, textWidth2);
    int textHeight1 = fm1.height();
    int textHeight2 = fm2.height();

    // Scale both lines proportionally if needed
    if (maxTextWidthNeeded > maxTextWidth) {
      float scale = (float)maxTextWidth / maxTextWidthNeeded;
      result.fontSize1 = (int)(baseFontSize1 * scale);
      result.fontSize2 = (int)(baseFontSize2 * scale);

      QFont scaledFont1 = InterFont(result.fontSize1, QFont::Bold);
      QFont scaledFont2 = InterFont(result.fontSize2);
      QFontMetrics scaledFm1(scaledFont1);
      QFontMetrics scaledFm2(scaledFont2);

      int scaledWidth1 = scaledFm1.horizontalAdvance(text1);
      int scaledWidth2 = scaledFm2.horizontalAdvance(text2);
      result.width = std::max(scaledWidth1, scaledWidth2) + horizontalPadding;
      textHeight1 = scaledFm1.height();
      textHeight2 = scaledFm2.height();
    } else {
      result.fontSize1 = baseFontSize1;
      result.fontSize2 = baseFontSize2;
      result.width = maxTextWidthNeeded + horizontalPadding;
    }

    const int lineSpacing = 8;
    result.height = textHeight1 + textHeight2 + lineSpacing + verticalPadding;
  }

  return result;
}

// Calculate pill rectangle position (bottom-centered)
QRect OnroadAlertsBP::calculatePillRect(int pillWidth, int pillHeight) const {
  int contentWidth = parentWidget() ? parentWidget()->width() : width();
  int contentHeight = parentWidget() ? parentWidget()->height() : height();

  int x = (contentWidth - pillWidth) / 2;
  int y = contentHeight - pillHeight - 50; // 50px from bottom

  return QRect(x, y, pillWidth, pillHeight);
}

// Get pill background color based on alert status
QColor OnroadAlertsBP::getPillBackgroundColor(cereal::SelfdriveState::AlertStatus status) const {
  if (status == cereal::SelfdriveState::AlertStatus::USER_PROMPT) {
    return QColor(220, 100, 20, 255);  // Orange warning
  }
  return QColor(45, 46, 48, 255);  // Dark neutral
}

// Get pill border color based on alert status
QColor OnroadAlertsBP::getPillBorderColor(cereal::SelfdriveState::AlertStatus status) const {
  if (status == cereal::SelfdriveState::AlertStatus::USER_PROMPT) {
    return QColor(255, 140, 60, 200);  // Warm glow
  }
  return QColor(80, 82, 85, 200);  // Subtle border
}

// Draw pill alert with optional pulsing effect
void OnroadAlertsBP::drawPillAlert(QPainter &p, const QRect &rect, const PillDimensions &dims, float pulseOpacity) {
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(Qt::NoPen);

  // Draw drop shadow layers for depth
  p.setBrush(QColor(0, 0, 0, 100));
  p.drawRoundedRect(rect.adjusted(0, 6, 0, 6), rect.height() / 2, rect.height() / 2);

  p.setBrush(QColor(0, 0, 0, 60));
  p.drawRoundedRect(rect.adjusted(0, 4, 0, 4), rect.height() / 2, rect.height() / 2);

  p.setBrush(QColor(0, 0, 0, 30));
  p.drawRoundedRect(rect.adjusted(0, 2, 0, 2), rect.height() / 2, rect.height() / 2);

  // Background with status-based color and pulse effect
  QColor bgColor = getPillBackgroundColor(alert.status);

  // Apply pulse opacity to warnings
  if (alert.status == cereal::SelfdriveState::AlertStatus::USER_PROMPT) {
    bgColor.setAlphaF(bgColor.alphaF() * pulseOpacity);
  }

  p.setBrush(bgColor);
  p.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);

  // Border with pulse effect
  QColor borderColor = getPillBorderColor(alert.status);
  if (alert.status == cereal::SelfdriveState::AlertStatus::USER_PROMPT) {
    borderColor.setAlphaF(borderColor.alphaF() * pulseOpacity);
  }

  p.setPen(QPen(borderColor, 2));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);

  // Text rendering with scaled fonts
  p.setRenderHint(QPainter::TextAntialiasing);
  const int verticalPaddingPerSide = alert.text2.isEmpty() ? 12 : 20;
  QRect textRect = rect.adjusted(0, verticalPaddingPerSide, 0, -verticalPaddingPerSide);

  if (alert.text2.isEmpty()) {
    // Single line
    p.setFont(InterFont(dims.fontSize1, QFont::DemiBold));

    // Subtle shadow
    p.setPen(QColor(0, 0, 0, 100));
    p.drawText(textRect.adjusted(2, 2, 2, 2), Qt::AlignCenter | Qt::TextSingleLine, alert.text1);

    // Main text
    p.setPen(Qt::white);
    p.drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine, alert.text1);

  } else {
    // Two lines
    p.setFont(InterFont(dims.fontSize1, QFont::Bold));
    QRect line1Rect(textRect.x(), textRect.y(), textRect.width(), textRect.height() * 0.55);

    p.setPen(QColor(0, 0, 0, 100));
    p.drawText(line1Rect.adjusted(2, 2, 2, 2), Qt::AlignHCenter | Qt::AlignBottom | Qt::TextSingleLine, alert.text1);

    p.setPen(Qt::white);
    p.drawText(line1Rect, Qt::AlignHCenter | Qt::AlignBottom | Qt::TextSingleLine, alert.text1);

    // Line 2
    p.setFont(InterFont(dims.fontSize2));
    p.setPen(QColor(220, 220, 220));
    QRect line2Rect(textRect.x(), textRect.y() + textRect.height() * 0.55, textRect.width(), textRect.height() * 0.45);
    p.drawText(line2Rect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine, alert.text2);
  }
}

// Draw fullscreen alert (mirrors stock alert style)
void OnroadAlertsBP::drawFullscreenAlert(QPainter &p) {
  static std::map<cereal::SelfdriveState::AlertSize, const int> alert_heights = {
    {cereal::SelfdriveState::AlertSize::SMALL, 271},
    {cereal::SelfdriveState::AlertSize::MID, 420},
    {cereal::SelfdriveState::AlertSize::FULL, height()},
  };
  int h = alert_heights[alert.size];

  int margin = 40;
  int radius = 30;
  if (alert.size == cereal::SelfdriveState::AlertSize::FULL) {
    margin = 0;
    radius = 0;
  }
  QRect r = QRect(0 + margin, height() - h + margin, width() - margin*2, h - margin*2);

  // Draw background + gradient (stock style)
  p.setPen(Qt::NoPen);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);
  p.setBrush(QBrush(alert_colors[alert.status]));
  p.drawRoundedRect(r, radius, radius);

  QLinearGradient g(0, r.y(), 0, r.bottom());
  g.setColorAt(0, QColor::fromRgbF(0, 0, 0, 0.05));
  g.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0.35));

  p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
  p.setBrush(QBrush(g));
  p.drawRoundedRect(r, radius, radius);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);

  // Text (stock style)
  const QPoint c = r.center();
  p.setPen(QColor(0xff, 0xff, 0xff));
  p.setRenderHint(QPainter::TextAntialiasing);

  if (alert.size == cereal::SelfdriveState::AlertSize::SMALL) {
    p.setFont(InterFont(74, QFont::DemiBold));
    p.drawText(r, Qt::AlignCenter, alert.text1);
  } else if (alert.size == cereal::SelfdriveState::AlertSize::MID) {
    p.setFont(InterFont(88, QFont::Bold));
    p.drawText(QRect(0, c.y() - 125, width(), 150), Qt::AlignHCenter | Qt::AlignTop, alert.text1);
    p.setFont(InterFont(66));
    p.drawText(QRect(0, c.y() + 21, width(), 90), Qt::AlignHCenter, alert.text2);
  } else if (alert.size == cereal::SelfdriveState::AlertSize::FULL) {
    bool l = alert.text1.length() > 15;
    p.setFont(InterFont(l ? 132 : 177, QFont::Bold));
    p.drawText(QRect(0, r.y() + (l ? 240 : 270), width(), 600), Qt::AlignHCenter | Qt::TextWordWrap, alert.text1);
    p.setFont(InterFont(88));
    p.drawText(QRect(0, r.height() - (l ? 361 : 420), width(), 300), Qt::AlignHCenter | Qt::TextWordWrap, alert.text2);
  }
}
