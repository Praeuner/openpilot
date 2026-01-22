"""
BluePilot Enhanced HUD Renderer
Extends stock HudRenderer with brake status coloring and enhanced visualizations
"""

import pyray as rl
from dataclasses import dataclass
from openpilot.common.params import Params
from openpilot.selfdrive.ui.onroad.hud_renderer import (
    HudRenderer, COLORS, FONT_SIZES, UI_CONFIG
)
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.text_measure import measure_text_cached
from system.ui.bluepilot.onroad.exp_button_bp import ExpButtonBP


@dataclass
class BrakeStatus:
    """Brake status tracking for HUD color customization"""
    brake_pressed: bool = False
    show_brake_status: bool = False


class HudRendererBP(HudRenderer):
    """
    BluePilot enhanced HUD renderer with:
    - Brake status coloring (speed turns red when braking)
    - Animated steering wheel button
    - DEC mode indicator
    """

    # Colors for brake status
    COLOR_BRAKE = rl.Color(255, 128, 128, 255)  # Light red when braking

    def __init__(self):
        super().__init__()
        self._brake_status = BrakeStatus()
        self._params = Params()
        self._last_params_check = 0

        # Replace stock ExpButton with BluePilot version
        self._exp_button = ExpButtonBP(UI_CONFIG.button_size, UI_CONFIG.wheel_icon_size)

    def _update_state(self) -> None:
        """Update HUD state including brake status"""
        super()._update_state()
        self._update_brake_status()
        self._update_settings()

    def _update_settings(self):
        """Update settings from params (cached)"""
        import time
        current_time = time.monotonic()
        if current_time - self._last_params_check < 1.0:
            return
        self._last_params_check = current_time

        self._brake_status.show_brake_status = self._params.get_bool("ShowBrakeStatus")

    def _update_brake_status(self):
        """Update brake status from car state"""
        sm = ui_state.sm

        if sm.recv_frame["carState"] < ui_state.started_frame:
            self._brake_status.brake_pressed = False
            return

        car_state = sm['carState']

        # Default to brake pressed signal
        self._brake_status.brake_pressed = car_state.brakePressed

        # Check if we have carStateBP with brake light status
        if sm.valid.get('carStateBP', False):
            try:
                car_state_bp = sm['carStateBP']
                if hasattr(car_state_bp, 'brakeLightStatus'):
                    brake_light = car_state_bp.brakeLightStatus
                    if hasattr(brake_light, 'dataAvailable') and brake_light.dataAvailable:
                        self._brake_status.brake_pressed = brake_light.brakeLightsOn
            except (AttributeError, KeyError):
                pass

    def _get_speed_color(self) -> rl.Color:
        """Get speed display color based on brake status"""
        if not self._brake_status.show_brake_status:
            return COLORS.white

        if self._brake_status.brake_pressed:
            return self.COLOR_BRAKE

        return COLORS.white

    def _draw_current_speed(self, rect: rl.Rectangle) -> None:
        """Draw current speed with brake status coloring"""
        speed_text = str(round(self.speed))
        speed_color = self._get_speed_color()

        speed_text_size = measure_text_cached(self._font_bold, speed_text, FONT_SIZES.current_speed)
        speed_pos = rl.Vector2(rect.x + rect.width / 2 - speed_text_size.x / 2, 180 - speed_text_size.y / 2)
        rl.draw_text_ex(self._font_bold, speed_text, speed_pos, FONT_SIZES.current_speed, 0, speed_color)

        # Draw unit (always white/translucent)
        unit_text = "km/h" if ui_state.is_metric else "mph"
        unit_text_size = measure_text_cached(self._font_medium, unit_text, FONT_SIZES.speed_unit)
        unit_pos = rl.Vector2(rect.x + rect.width / 2 - unit_text_size.x / 2, 290 - unit_text_size.y / 2)
        rl.draw_text_ex(self._font_medium, unit_text, unit_pos, FONT_SIZES.speed_unit, 0, COLORS.white_translucent)
