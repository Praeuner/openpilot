/**
 * Copyright (c) 2024-, BluePilot, and a number of other contributors.
 *
 * This file is part of BluePilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/bluepilot/qt/onroad/hud_bp.h"

#include <algorithm>

#include "selfdrive/ui/ui.h"

namespace BluepilotHudOverrides {

void updateBrakeStatus(BrakeStatus &status, const UIState &s) {
  const SubMaster &sm = *(s.sm);
  const auto car_state = sm["carState"].getCarState();

  // Update brake status feature enabled flag
  status.show_brake_status = s.scene.show_brake_status;

  // Default to brake pressed signal
  status.brake_pressed = car_state.getBrakePressed();

  // Check if we have carStateBP with brake light status
  if (sm.rcv_frame("carStateBP") >= s.scene.started_frame && sm.valid("carStateBP")) {
    const auto &car_state_bp = sm["carStateBP"].getCarStateBP();
    if (car_state_bp.getBrakeLightStatus().getDataAvailable()) {
      // Use brake light status instead of brake pressed
      status.brake_pressed = car_state_bp.getBrakeLightStatus().getBrakeLightsOn();
    }
  }
}

QColor getSpeedColor(const BrakeStatus &status, int alpha) {
  // If brake status feature is disabled, return white
  if (!status.show_brake_status) {
    return QColor(0xff, 0xff, 0xff, alpha);
  }

  // If brake is pressed, fade to red
  if (status.brake_pressed) {
    // Clamp alpha to valid range (0-255)
    alpha = std::clamp(alpha, 0, 255);
    return QColor(0xff, 0x80, 0x80, alpha); // Lighter red color
  }

  // Otherwise return white
  return QColor(0xff, 0xff, 0xff, alpha);
}

} // namespace BluepilotHudOverrides
