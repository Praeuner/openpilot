"""
BluePilot Standstill Timer Overlay
Displays elapsed time when vehicle is stopped
"""

import math
from dataclasses import dataclass
import pyray as rl
from openpilot.common.params import Params
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached


@dataclass
class StandstillState:
    """State for standstill timer"""
    enabled: bool = False
    is_standstill: bool = False
    elapsed_time: float = 0.0


class StandstillTimerOverlay:
    """
    Renders standstill timer overlay.

    Features:
    - Octagon shape (matching stop sign style)
    - MM:SS elapsed time display
    - Activates when vehicle speed < 0.1 m/s
    - Positioned to share space with stop sign overlay
    """

    # Constants
    UI_FREQ = 20  # 20 Hz UI update rate
    STANDSTILL_THRESHOLD = 0.1  # m/s threshold for standstill
    OCTAGON_SIZE = 190

    # Colors
    COLOR_RED = rl.Color(255, 90, 81, 200)
    COLOR_WHITE = rl.Color(255, 255, 255, 255)

    def __init__(self):
        self._font_bold = gui_app.font(FontWeight.BOLD)
        self._state = StandstillState()
        self._params = Params()
        self._last_params_check = 0

    def render(self, rect: rl.Rectangle, bp_state) -> None:
        """Render standstill timer overlay"""
        # Update state
        self._update_state(bp_state)

        # Check if enabled and at standstill
        if not self._state.enabled or not self._state.is_standstill:
            return

        # Calculate position (same as stop sign)
        sidebar_offset = 100 if bp_state.sidebar_visible else 0
        x = int(rect.width / 12 * 10 - sidebar_offset)
        y = int(rect.height / 12 * 1.53)

        # Draw the timer
        self._draw_standstill_timer(x, y, self._state.elapsed_time)

    def _update_state(self, bp_state) -> None:
        """Update standstill state from car state"""
        # Check if feature is enabled (cached check)
        self._state.enabled = getattr(bp_state, 'show_standstill_timer', False)

        if not self._state.enabled:
            self._state.is_standstill = False
            self._state.elapsed_time = 0.0
            return

        # Get vehicle speed
        sm = ui_state.sm
        if not sm.valid.get('carState', False):
            return

        vehicle_speed = sm['carState'].vEgo
        currently_stopped = abs(vehicle_speed) < self.STANDSTILL_THRESHOLD

        if currently_stopped and not self._state.is_standstill:
            # Just entered standstill
            self._state.is_standstill = True
            self._state.elapsed_time = 0.0
        elif currently_stopped and self._state.is_standstill:
            # Continue counting
            self._state.elapsed_time += 1.0 / self.UI_FREQ
        elif not currently_stopped:
            # Vehicle is moving, reset
            self._state.is_standstill = False
            self._state.elapsed_time = 0.0

    def _draw_standstill_timer(self, x: int, y: int, elapsed_time: float) -> None:
        """Draw the octagonal standstill timer"""
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
        fill_color = self.COLOR_RED

        # Draw as triangle fan from center
        center = rl.Vector2(x, y)
        for i in range(8):
            v1 = vertices[i]
            v2 = vertices[(i + 1) % 8]
            rl.draw_triangle(center, v1, v2, fill_color)

        # Draw border
        for i in range(8):
            v1 = vertices[i]
            v2 = vertices[(i + 1) % 8]
            rl.draw_line_ex(v1, v2, 6, self.COLOR_WHITE)

        # Format time string (M:SS)
        minutes = int(elapsed_time / 60.0)
        seconds = int(elapsed_time) % 60
        time_str = f"{minutes}:{seconds:02d}"

        # Draw timer text centered
        font_size = 55
        time_size = measure_text_cached(self._font_bold, time_str, font_size)
        time_x = x - time_size.x / 2
        time_y = y - time_size.y / 2

        rl.draw_text_ex(self._font_bold, time_str,
                        rl.Vector2(time_x, time_y), font_size, 0, self.COLOR_WHITE)
