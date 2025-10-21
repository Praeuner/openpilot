/**
 * Copyright (c) 2024-, BluePilot, and a number of other contributors.
 *
 * This file is part of BluePilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include <QColor>

// Forward declarations
class UIState;

namespace cereal {
  struct CarState;
  struct CarStateBP;
}

namespace BluepilotHudOverrides {

/**
 * Brake status tracking for HUD color customization
 */
struct BrakeStatus {
  bool brake_pressed;
  bool show_brake_status;

  BrakeStatus() : brake_pressed(false), show_brake_status(false) {}
};

/**
 * Updates brake status from UIState and message data.
 *
 * @param status Brake status struct to update
 * @param s UIState containing scene and SubMaster
 */
void updateBrakeStatus(BrakeStatus &status, const UIState &s);

/**
 * Determines the color for the speed display based on braking status.
 *
 * @param status Current brake status
 * @param alpha Alpha channel value for the color (0-255)
 * @return QColor for the speed display
 */
QColor getSpeedColor(const BrakeStatus &status, int alpha = 255);

} // namespace BluepilotHudOverrides
