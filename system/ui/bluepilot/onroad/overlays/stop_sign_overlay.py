"""
BluePilot Stop Sign Overlay
Displays detected stop signs with distance
"""

import math
from dataclasses import dataclass
import pyray as rl
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached


@dataclass
class StopState:
    """State for stop sign detection"""
    active: bool = False
    stability_counter: int = 0
    stopping_distance: float = 0.0
    display_distance: float = 0.0
    fade_alpha: float = 0.0
    last_valid_x: float = 0.0
    last_valid_y: float = 0.0


class StopSignOverlay:
    """
    Renders stop sign detection overlay.

    Features:
    - Octagon stop sign shape
    - STOP text with distance below
    - Fade animation on detection/loss
    - Positioned to share space with standstill timer
    """

    # Constants
    OCTAGON_SIZE = 190
    FADE_SPEED = 0.1  # Alpha change per frame

    # Colors
    COLOR_RED = rl.Color(255, 90, 81, 200)
    COLOR_WHITE = rl.Color(255, 255, 255, 255)

    def __init__(self):
        self._font_bold = gui_app.font(FontWeight.BOLD)
        self._stop_state = StopState()

    def render(self, rect: rl.Rectangle, bp_state) -> None:
        """Render stop sign overlay"""
        # Update stop state from model data
        self._update_stop_state()

        # Check visibility
        if not self._stop_state.active and self._stop_state.fade_alpha < 0.02:
            return

        # Calculate position (same as standstill timer)
        sidebar_offset = 100 if bp_state.sidebar_visible else 0
        x = int(rect.width / 12 * 10 - sidebar_offset)
        y = int(rect.height / 12 * 1.53)

        # Get metric setting
        is_metric = ui_state.is_metric

        # Draw the stop sign
        self._draw_stop_sign(x, y, self._stop_state.display_distance,
                             self._stop_state.fade_alpha, is_metric)

    def _update_stop_state(self) -> None:
        """Update stop state from model data"""
        # Note: Stop detection data would come from modelV2 or a custom message
        # For now, this is a placeholder - the actual implementation would
        # check for stop sign detection in the model output
        # sm = ui_state.sm  # TODO: Use when stop detection data is available

        # Fade out if not active
        if not self._stop_state.active:
            self._stop_state.fade_alpha = max(0.0, self._stop_state.fade_alpha - self.FADE_SPEED)
        else:
            self._stop_state.fade_alpha = min(1.0, self._stop_state.fade_alpha + self.FADE_SPEED)

    def set_stop_detected(self, active: bool, distance: float) -> None:
        """Set stop sign detection state (called from external source)"""
        self._stop_state.active = active
        self._stop_state.stopping_distance = distance
        if active:
            self._stop_state.display_distance = distance

    def _draw_stop_sign(self, x: int, y: int, distance: float, alpha: float, is_metric: bool) -> None:
        """Draw the octagonal stop sign"""
        if alpha < 0.02 or distance <= 0:
            return

        size = self.OCTAGON_SIZE
        angle = math.pi / 8.0

        # Create octagon vertices
        vertices = []
        for i in range(8):
            curr_angle = angle + i * math.pi / 4.0
            px = x + size / 2 * math.cos(curr_angle)
            py = y + size / 2 * math.sin(curr_angle)
            vertices.append(rl.Vector2(px, py))

        # Draw filled octagon
        fill_color = rl.Color(255, 90, 81, int(200 * alpha))

        # Draw as triangle fan from center
        center = rl.Vector2(x, y)
        for i in range(8):
            v1 = vertices[i]
            v2 = vertices[(i + 1) % 8]
            rl.draw_triangle(center, v1, v2, fill_color)

        # Draw border
        border_color = rl.Color(255, 255, 255, int(255 * alpha))
        for i in range(8):
            v1 = vertices[i]
            v2 = vertices[(i + 1) % 8]
            rl.draw_line_ex(v1, v2, 6, border_color)

        # Draw "STOP" text
        stop_font_size = 55
        stop_text = "STOP"
        stop_size = measure_text_cached(self._font_bold, stop_text, stop_font_size)
        stop_x = x - stop_size.x / 2
        stop_y = y - 20 - stop_size.y / 2

        text_color = rl.Color(255, 255, 255, int(255 * alpha))
        rl.draw_text_ex(self._font_bold, stop_text,
                        rl.Vector2(stop_x, stop_y), stop_font_size, 0, text_color)

        # Draw distance text
        if is_metric:
            distance_str = f"{distance:.1f} m"
        else:
            distance_ft = distance * 3.28084
            distance_str = f"{distance_ft:.1f} ft"

        dist_font_size = 40
        dist_size = measure_text_cached(self._font_bold, distance_str, dist_font_size)
        dist_x = x - dist_size.x / 2
        dist_y = y + 30 - dist_size.y / 2

        rl.draw_text_ex(self._font_bold, distance_str,
                        rl.Vector2(dist_x, dist_y), dist_font_size, 0, text_color)
