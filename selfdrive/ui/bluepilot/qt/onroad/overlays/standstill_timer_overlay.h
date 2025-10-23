/**
 * Copyright (c) 2024-, BluePilot, and a number of other contributors.
 *
 * This file is part of BluePilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include <QPainter>
#include <QRect>
#include <QString>
#include <QColor>

// Forward declarations
struct UIState;

namespace StandstillTimerOverlay {

/**
 * Standstill timer state tracking
 */
struct StandstillState {
  bool is_standstill = false;
  float elapsed_time = 0.0f;
  bool enabled = false;

  StandstillState() = default;
};

/**
 * Renders the standstill timer overlay
 *
 * @param painter QPainter for drawing
 * @param rect Surface rectangle
 * @param s UIState containing scene data
 * @param state Standstill state to update and render
 */
void render(QPainter &painter, const QRect &rect, const UIState &s, StandstillState &state);

/**
 * Updates the standstill state based on current vehicle state
 *
 * @param s UIState containing vehicle data
 * @param state Standstill state to update
 */
void updateState(const UIState &s, StandstillState &state);

} // namespace StandstillTimerOverlay
