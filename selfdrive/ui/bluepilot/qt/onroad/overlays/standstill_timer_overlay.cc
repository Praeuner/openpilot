/**
 * Copyright (c) 2024-, BluePilot, and a number of other contributors.
 *
 * This file is part of BluePilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/bluepilot/qt/onroad/overlays/standstill_timer_overlay.h"
#include "selfdrive/ui/ui.h"
#include "selfdrive/ui/qt/util.h"
#include "common/params.h"
#include <QPen>
#include <QPolygon>
#include <cmath>

namespace StandstillTimerOverlay {

static constexpr int UI_FREQ = 20;  // 20 Hz UI update rate
static constexpr float STANDSTILL_THRESHOLD = 0.1f;  // m/s threshold for standstill

void updateState(const UIState &s, StandstillState &state) {
  const SubMaster &sm = *(s.sm);

  // Check if standstill timer feature is enabled
  state.enabled = Params().getBool("StandstillTimer");
  if (!state.enabled) {
    state.is_standstill = false;
    state.elapsed_time = 0.0f;
    return;
  }

  // Check if we have valid carState data
  if (!sm.valid("carState")) {
    return;
  }

  const auto car_state = sm["carState"].getCarState();
  float vehicle_speed = car_state.getVEgo();

  // Determine if vehicle is at standstill
  bool currently_stopped = std::abs(vehicle_speed) < STANDSTILL_THRESHOLD;

  if (currently_stopped && !state.is_standstill) {
    // Just entered standstill
    state.is_standstill = true;
    state.elapsed_time = 0.0f;
  } else if (currently_stopped && state.is_standstill) {
    // Continue counting
    state.elapsed_time += 1.0f / UI_FREQ;
  } else if (!currently_stopped) {
    // Vehicle is moving, reset
    state.is_standstill = false;
    state.elapsed_time = 0.0f;
  }
}

void render(QPainter &painter, const QRect &rect, const UIState &s, StandstillState &state) {
  // Early exit if not enabled or not at standstill
  if (!state.enabled || !state.is_standstill) {
    return;
  }

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Use same position as stop sign overlay
  // Account for wider BP sidebar (460px vs stock 300px = 160px difference)
  int sidebar_offset = 0;
  if (s.scene.sidebar_visible) {
    sidebar_offset = 100; // BP sidebar is 160px wider than stock
  }

  int x = rect.right() / 12 * 10 - sidebar_offset;
  int y = rect.bottom() / 12 * 1.53;

  // Use same fixed size as stop sign overlay
  const int size = 190;
  const float angle = M_PI / 8.0;

  // Create octagon polygon (same as stop sign)
  QPolygon octagon;
  for (int i = 0; i < 8; i++) {
    float curr_angle = angle + i * M_PI / 4.0;
    int point_x = x + size / 2 * cos(curr_angle);
    int point_y = y + size / 2 * sin(curr_angle);
    octagon << QPoint(point_x, point_y);
  }

  // Draw octagon with red pastel color (same as original standstill timer)
  painter.setPen(QPen(Qt::white, 6));
  painter.setBrush(QColor(255, 90, 81, 200)); // red pastel
  painter.drawPolygon(octagon);

  // Calculate minutes and seconds
  int minutes = static_cast<int>(state.elapsed_time / 60.0f);
  int seconds = static_cast<int>(state.elapsed_time) % 60;

  // Format time string
  QString time_str = QString("%1:%2")
    .arg(minutes, 1, 10, QChar('0'))
    .arg(seconds, 2, 10, QChar('0'));

  // Draw timer text centered (no "STOPPED" label - just the timer)
  painter.setFont(InterFont(55, QFont::Bold));
  painter.setPen(Qt::white);
  QRect timerTextRect = painter.fontMetrics().boundingRect(time_str);
  timerTextRect.moveCenter({x, y});
  painter.drawText(timerTextRect, Qt::AlignCenter, time_str);

  painter.restore();
}

} // namespace StandstillTimerOverlay
