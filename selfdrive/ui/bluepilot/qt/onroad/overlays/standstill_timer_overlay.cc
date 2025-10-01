#include "selfdrive/ui/bluepilot/qt/onroad/overlays/standstill_timer_overlay.h"
#include "selfdrive/ui/qt/util.h"
#include "common/timing.h"
#include <QFontMetrics>
#include <QColor>
#include <QPen>
#include <algorithm>

void StandstillTimerOverlay::render(QPainter &painter, const QRect &rect, const UIState &s,
                                   StandstillState &standstill_state) {
  // Check if standstill timer is enabled via parameter
  if (!s.scene.stand_still_timer) {
    // Reset timer state when parameter is disabled
    standstill_state.standstill_elapsed = 0.0;
    standstill_state.standstill_start_time = 0.0;
    standstill_state.standstill_exit_time = 0.0;
    standstill_state.prev_standstill = false;
    return;
  }

  double current_time = millis_since_boot() / 1000.0;

  // Enhanced standstill detection with multiple criteria
  bool velocity_standstill = standstill_state.vehicle_speed < STANDSTILL_THRESHOLD;
  bool combined_standstill = standstill_state.standstill && velocity_standstill;

  // Additional check: if speed is very low but CAN doesn't report standstill
  if (!standstill_state.standstill && standstill_state.vehicle_speed < 0.05f) {
    combined_standstill = true;
  }

  // Reset timer when transitioning from offroad to onroad (new session)
  static bool session_initialized = false;
  if (!session_initialized) {
    standstill_state.standstill_elapsed = 0.0;
    standstill_state.standstill_start_time = current_time;
    standstill_state.standstill_exit_time = 0.0;
    standstill_state.prev_standstill = false;
    session_initialized = true;
  }

  if (!standstill_state.prev_standstill && combined_standstill) {
    // Just entered standstill - start the timer
    standstill_state.standstill_start_time = current_time;
    standstill_state.standstill_exit_time = 0.0;
    standstill_state.standstill_elapsed = 0.0;
  } else if (combined_standstill) {
    // Update the elapsed time while in standstill
    standstill_state.standstill_elapsed = current_time - standstill_state.standstill_start_time;
    standstill_state.standstill_exit_time = 0.0;

    // Add a sanity check to prevent unreasonable values
    if (standstill_state.standstill_elapsed > 86400.0) { // 24 hours max
      standstill_state.standstill_start_time = current_time - 86400.0;
      standstill_state.standstill_elapsed = 86400.0;
    }
  } else {
    // Not in standstill - reset immediately
    standstill_state.standstill_elapsed = 0.0;
    standstill_state.standstill_start_time = current_time;
    standstill_state.standstill_exit_time = 0.0;
  }

  // Draw stand still timer if active
  if (combined_standstill && standstill_state.standstill_elapsed > 0.1) {
    int minute = (int)(standstill_state.standstill_elapsed / 60);
    int second = (int)(standstill_state.standstill_elapsed) - (minute * 60);

    QString labelText = "STOP";
    QString timeText = QString("%1:%2").arg(minute).arg(second, 2, 10, QChar('0'));

    // Calculate required text widths for dynamic background sizing
    painter.setFont(InterFont(80, QFont::DemiBold));
    int labelWidth = painter.fontMetrics().boundingRect(labelText).width();

    painter.setFont(InterFont(95, QFont::DemiBold));
    int timeWidth = painter.fontMetrics().boundingRect(timeText).width();

    // Use the wider text for background sizing, add horizontal padding
    int textWidth = std::max(labelWidth, timeWidth);
    int horizontalPadding = 40; // 20px padding on each side
    int backgroundWidth = textWidth + horizontalPadding;
    int backgroundHeight = 180;

    // Ensure minimum background width for visual consistency
    int minBackgroundWidth = 240;
    if (backgroundWidth < minBackgroundWidth) {
      backgroundWidth = minBackgroundWidth;
    }

    // Adjust positioning for developer UI panels
    bool dev_ui_right_panel = (s.scene.dev_ui_info != 0);
    bool dev_ui_bottom_panel = (s.scene.dev_ui_info == 2);

    int x = rect.right() - 200;
    int y = rect.center().y() - 45;

    if (dev_ui_right_panel) {
      x -= 100; // Move left by 100px to avoid right panel collision
    }
    if (dev_ui_bottom_panel) {
      y -= 70; // Move up by 70px to avoid bottom panel collision
    }

    // Center the background rectangle around the timer position
    QRect backgroundRect(x - backgroundWidth/2, y - 70, backgroundWidth, backgroundHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 120));
    painter.drawRoundedRect(backgroundRect, 15, 15);

    // Calculate center of background rectangle for proper text centering
    int centerX = backgroundRect.center().x();

    painter.setFont(InterFont(80, QFont::DemiBold));
    drawColoredText(painter, centerX, y, labelText, QColor(255, 175, 3, 240));

    painter.setFont(InterFont(95, QFont::DemiBold));
    drawColoredText(painter, centerX, y + 90, timeText, QColor(255, 255, 255, 240));
  }

  standstill_state.prev_standstill = standstill_state.standstill;
}

void StandstillTimerOverlay::drawColoredText(QPainter &painter, int x, int y, const QString &text, QColor color) {
  QRect real_rect = painter.fontMetrics().boundingRect(text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});
  painter.setPen(color);
  painter.drawText(real_rect.x(), real_rect.bottom(), text);
}
