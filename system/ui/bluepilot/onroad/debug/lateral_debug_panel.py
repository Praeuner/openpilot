"""
BluePilot Lateral Debug Panel
Displays steering angle graph and lateral control metrics
"""

import pyray as rl
from dataclasses import dataclass
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget
from system.ui.bluepilot.onroad.debug.graph_base import GraphWidget, GraphConfig


@dataclass
class LateralData:
    """Cached lateral control data"""
    actual_steer_angle: float = 0.0
    desired_steer_angle: float = 0.0
    steer_actuator_delay: float = 0.0
    actual_curvature: float = 0.0
    desired_curvature: float = 0.0
    valid: bool = False


class LateralDebugPanel(Widget):
    """
    Lateral control debug panel with:
    - Steering angle graph (desired vs actual)
    - Current values display
    - Curvature information
    - Actuator delay display
    """

    # Panel dimensions
    PANEL_WIDTH = 400
    PANEL_HEIGHT = 350

    # Colors
    COLOR_BG = rl.Color(44, 62, 80, 240)
    COLOR_BG_DARK = rl.Color(32, 33, 35, 240)
    COLOR_BORDER = rl.Color(100, 149, 237, 180)
    COLOR_ACCENT = rl.Color(24, 144, 255, 200)
    COLOR_TEXT = rl.Color(236, 240, 241, 255)
    COLOR_TEXT_DIM = rl.Color(180, 180, 180, 255)

    def __init__(self):
        super().__init__()
        self._font = gui_app.font(FontWeight.MEDIUM)
        self._font_bold = gui_app.font(FontWeight.BOLD)

        # Create steering angle graph
        graph_config = GraphConfig(
            title="Steering Angle",
            y_label="degrees",
            line1_color=rl.Color(100, 149, 237, 255),  # Cornflower blue (desired)
            line2_color=rl.Color(70, 130, 180, 255),   # Steel blue (actual)
            line1_label="Desired",
            line2_label="Actual"
        )
        self._steer_graph = GraphWidget(graph_config)

        # Data cache
        self._data = LateralData()

    def _render(self, rect: rl.Rectangle):
        """Render the lateral debug panel"""
        # Update data
        self._update_data()

        if not self._data.valid:
            return

        # Draw panel background with metallic styling
        self._draw_background(rect)

        # Draw title
        self._draw_title(rect)

        # Draw steering angle graph
        graph_rect = rl.Rectangle(
            rect.x + 15,
            rect.y + 60,
            rect.width - 30,
            rect.height - 160
        )
        self._steer_graph.render(graph_rect)

        # Draw metrics
        self._draw_metrics(rect)

    def _update_data(self):
        """Update data from SubMaster"""
        sm = ui_state.sm

        if not ui_state.started:
            self._data.valid = False
            return

        try:
            if sm.valid.get('carState', False):
                self._data.actual_steer_angle = sm['carState'].steeringAngleDeg

            if sm.valid.get('carControl', False):
                actuators = sm['carControl'].actuators
                self._data.desired_steer_angle = actuators.steeringAngleDeg

            if sm.valid.get('carParams', False):
                self._data.steer_actuator_delay = sm['carParams'].steerActuatorDelay

            if sm.valid.get('controlsState', False):
                controls = sm['controlsState']
                self._data.actual_curvature = controls.curvature
                self._data.desired_curvature = controls.desiredCurvature

            # Add data point to graph
            self._steer_graph.add_data_point(
                self._data.desired_steer_angle,
                self._data.actual_steer_angle
            )

            self._data.valid = True

        except (AttributeError, KeyError):
            pass

    def _draw_background(self, rect: rl.Rectangle):
        """Draw metallic card background"""
        # Shadow
        shadow_rect = rl.Rectangle(rect.x + 3, rect.y + 3, rect.width, rect.height)
        rl.draw_rectangle_rounded(shadow_rect, 0.05, 10, rl.Color(0, 0, 0, 60))

        # Main background gradient
        rl.draw_rectangle_rounded(rect, 0.05, 10, self.COLOR_BG)

        # Inner panel (slightly darker)
        inner_rect = rl.Rectangle(rect.x + 5, rect.y + 5, rect.width - 10, rect.height - 10)
        rl.draw_rectangle_rounded(inner_rect, 0.05, 10, self.COLOR_BG_DARK)

        # Top highlight
        highlight_rect = rl.Rectangle(rect.x + 8, rect.y + 8, rect.width - 16, 40)
        rl.draw_rectangle_rounded(highlight_rect, 0.1, 10, rl.Color(255, 255, 255, 15))

        # Border
        rl.draw_rectangle_rounded_lines_ex(rect, 0.05, 10, 2, self.COLOR_BORDER)

        # Top accent line
        accent_x = rect.x + 10
        accent_width = rect.width - 20
        for i in range(int(accent_width)):
            alpha = int(200 * (1 - abs(i / accent_width - 0.5) * 2))
            rl.draw_rectangle(int(accent_x + i), int(rect.y + 5), 1, 3,
                             rl.Color(24, 144, 255, alpha))

    def _draw_title(self, rect: rl.Rectangle):
        """Draw panel title"""
        title = "Lateral Control"
        title_size = measure_text_cached(self._font_bold, title, 34)
        x = rect.x + (rect.width - title_size.x) / 2
        y = rect.y + 18

        # Shadow
        rl.draw_text_ex(self._font_bold, title,
                        rl.Vector2(x + 2, y + 2), 34, 0,
                        rl.Color(0, 0, 0, 100))
        # Text
        rl.draw_text_ex(self._font_bold, title,
                        rl.Vector2(x, y), 34, 0, self.COLOR_TEXT)

    def _draw_metrics(self, rect: rl.Rectangle):
        """Draw control metrics below graph"""
        y = rect.y + rect.height - 90
        font_size = 22
        col_width = (rect.width - 30) / 2

        # Row 1: Curvature
        self._draw_metric_pair(
            rect.x + 15, y,
            "Desired Curv:", f"{self._data.desired_curvature:.5f}",
            rect.x + 15 + col_width, y,
            "Actual Curv:", f"{self._data.actual_curvature:.5f}",
            font_size
        )

        # Row 2: Delay
        y += 30
        self._draw_metric(
            rect.x + 15, y,
            "Steer Delay:", f"{self._data.steer_actuator_delay:.3f}s",
            font_size
        )

    def _draw_metric(self, x: float, y: float, label: str, value: str, font_size: int):
        """Draw a single metric"""
        rl.draw_text_ex(self._font, label,
                        rl.Vector2(x, y), font_size, 0, self.COLOR_TEXT_DIM)
        label_width = measure_text_cached(self._font, label, font_size).x
        rl.draw_text_ex(self._font_bold, value,
                        rl.Vector2(x + label_width + 8, y), font_size, 0, self.COLOR_TEXT)

    def _draw_metric_pair(self, x1: float, y1: float, label1: str, value1: str,
                          x2: float, y2: float, label2: str, value2: str,
                          font_size: int):
        """Draw two metrics side by side"""
        self._draw_metric(x1, y1, label1, value1, font_size)
        self._draw_metric(x2, y2, label2, value2, font_size)
