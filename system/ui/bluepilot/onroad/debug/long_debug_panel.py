"""
BluePilot Longitudinal Debug Panel
Displays acceleration graph and longitudinal control metrics
"""

import pyray as rl
from dataclasses import dataclass
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget
from system.ui.bluepilot.onroad.debug.graph_base import GraphWidget, GraphConfig


@dataclass
class LongData:
    """Cached longitudinal control data"""
    actual_accel: float = 0.0
    desired_accel: float = 0.0
    gas_signal: float = 0.0
    brake_signal: float = 0.0
    current_speed: float = 0.0
    target_speed: float = 0.0
    longitudinal_actuator_delay: float = 0.0
    should_stop: bool = False
    allow_throttle: bool = True
    allow_brake: bool = True
    valid: bool = False


class LongDebugPanel(Widget):
    """
    Longitudinal control debug panel with:
    - Acceleration graph (desired vs actual)
    - Gas/brake signal bars
    - Speed and target display
    - Control flags display
    """

    # Panel dimensions
    PANEL_WIDTH = 400
    PANEL_HEIGHT = 450

    # Colors
    COLOR_BG = rl.Color(44, 62, 80, 240)
    COLOR_BG_DARK = rl.Color(32, 33, 35, 240)
    COLOR_BORDER_TOP = rl.Color(46, 204, 113, 180)  # Green
    COLOR_BORDER_MID = rl.Color(241, 196, 15, 180)  # Yellow
    COLOR_BORDER_BOT = rl.Color(231, 76, 60, 180)   # Red
    COLOR_ACCENT = rl.Color(46, 204, 113, 200)
    COLOR_TEXT = rl.Color(236, 240, 241, 255)
    COLOR_TEXT_DIM = rl.Color(180, 180, 180, 255)
    COLOR_GAS = rl.Color(46, 204, 113, 255)         # Green
    COLOR_BRAKE = rl.Color(231, 76, 60, 255)        # Red

    def __init__(self):
        super().__init__()
        self._font = gui_app.font(FontWeight.MEDIUM)
        self._font_bold = gui_app.font(FontWeight.BOLD)

        # Create acceleration graph
        graph_config = GraphConfig(
            title="Acceleration",
            y_label="m/s^2",
            line1_color=rl.Color(46, 204, 113, 255),   # Green (desired)
            line2_color=rl.Color(52, 152, 219, 255),   # Blue (actual)
            line1_label="Desired",
            line2_label="Actual"
        )
        self._accel_graph = GraphWidget(graph_config)
        self._accel_graph.set_max_value(2.0)

        # Data cache
        self._data = LongData()

    def _render(self, rect: rl.Rectangle):
        """Render the longitudinal debug panel"""
        # Update data
        self._update_data()

        if not self._data.valid:
            return

        # Draw panel background with metallic styling
        self._draw_background(rect)

        # Draw title
        self._draw_title(rect)

        # Draw acceleration graph
        graph_rect = rl.Rectangle(
            rect.x + 15,
            rect.y + 60,
            rect.width - 30,
            rect.height - 250
        )
        self._accel_graph.render(graph_rect)

        # Draw gas/brake bars
        self._draw_control_bars(rect, graph_rect.y + graph_rect.height + 20)

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
                car = sm['carState']
                self._data.actual_accel = car.aEgo
                self._data.current_speed = car.vEgo

            if sm.valid.get('carControl', False):
                control = sm['carControl']
                self._data.gas_signal = control.actuators.gas
                self._data.brake_signal = control.actuators.brake
                self._data.target_speed = control.hudControl.setSpeed

            if sm.valid.get('longitudinalPlan', False):
                plan = sm['longitudinalPlan']
                accels = plan.accels
                if len(accels) > 0:
                    self._data.desired_accel = accels[0]
                self._data.should_stop = plan.shouldStop
                self._data.allow_throttle = plan.allowThrottle
                self._data.allow_brake = plan.allowBrake

            if sm.valid.get('carParams', False):
                self._data.longitudinal_actuator_delay = sm['carParams'].longitudinalActuatorDelay

            # Add data point to graph
            self._accel_graph.add_data_point(
                self._data.desired_accel,
                self._data.actual_accel
            )

            self._data.valid = True

        except (AttributeError, KeyError):
            pass

    def _draw_background(self, rect: rl.Rectangle):
        """Draw metallic card background with gradient border"""
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

        # Draw gradient border (green -> yellow -> red)
        # Top segment (green)
        rl.draw_line_ex(
            rl.Vector2(rect.x, rect.y + 10),
            rl.Vector2(rect.x, rect.y + rect.height / 3),
            2, self.COLOR_BORDER_TOP
        )
        rl.draw_line_ex(
            rl.Vector2(rect.x + rect.width, rect.y + 10),
            rl.Vector2(rect.x + rect.width, rect.y + rect.height / 3),
            2, self.COLOR_BORDER_TOP
        )

        # Middle segment (yellow)
        rl.draw_line_ex(
            rl.Vector2(rect.x, rect.y + rect.height / 3),
            rl.Vector2(rect.x, rect.y + 2 * rect.height / 3),
            2, self.COLOR_BORDER_MID
        )
        rl.draw_line_ex(
            rl.Vector2(rect.x + rect.width, rect.y + rect.height / 3),
            rl.Vector2(rect.x + rect.width, rect.y + 2 * rect.height / 3),
            2, self.COLOR_BORDER_MID
        )

        # Bottom segment (red)
        rl.draw_line_ex(
            rl.Vector2(rect.x, rect.y + 2 * rect.height / 3),
            rl.Vector2(rect.x, rect.y + rect.height - 10),
            2, self.COLOR_BORDER_BOT
        )
        rl.draw_line_ex(
            rl.Vector2(rect.x + rect.width, rect.y + 2 * rect.height / 3),
            rl.Vector2(rect.x + rect.width, rect.y + rect.height - 10),
            2, self.COLOR_BORDER_BOT
        )

        # Top accent line (green)
        accent_x = rect.x + 10
        accent_width = rect.width - 20
        for i in range(int(accent_width)):
            alpha = int(200 * (1 - abs(i / accent_width - 0.5) * 2))
            rl.draw_rectangle(int(accent_x + i), int(rect.y + 5), 1, 3,
                             rl.Color(46, 204, 113, alpha))

    def _draw_title(self, rect: rl.Rectangle):
        """Draw panel title"""
        title = "Longitudinal Control"
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

    def _draw_control_bars(self, rect: rl.Rectangle, y: float):
        """Draw gas and brake signal bars"""
        bar_height = 25
        bar_width = (rect.width - 50) / 2
        x_start = rect.x + 15

        # Gas bar
        rl.draw_text_ex(self._font, "Gas",
                        rl.Vector2(x_start, y), 18, 0, self.COLOR_TEXT_DIM)

        gas_bg = rl.Rectangle(x_start, y + 22, bar_width, bar_height)
        rl.draw_rectangle_rounded(gas_bg, 0.3, 10, rl.Color(30, 30, 35, 200))

        gas_fill_width = bar_width * min(1.0, max(0.0, self._data.gas_signal))
        if gas_fill_width > 0:
            gas_fill = rl.Rectangle(x_start, y + 22, gas_fill_width, bar_height)
            rl.draw_rectangle_rounded(gas_fill, 0.3, 10, self.COLOR_GAS)

        gas_text = f"{self._data.gas_signal:.2f}"
        rl.draw_text_ex(self._font_bold, gas_text,
                        rl.Vector2(x_start + bar_width / 2 - 20, y + 24), 20, 0, rl.WHITE)

        # Brake bar
        x_brake = x_start + bar_width + 20
        rl.draw_text_ex(self._font, "Brake",
                        rl.Vector2(x_brake, y), 18, 0, self.COLOR_TEXT_DIM)

        brake_bg = rl.Rectangle(x_brake, y + 22, bar_width, bar_height)
        rl.draw_rectangle_rounded(brake_bg, 0.3, 10, rl.Color(30, 30, 35, 200))

        brake_fill_width = bar_width * min(1.0, max(0.0, self._data.brake_signal))
        if brake_fill_width > 0:
            brake_fill = rl.Rectangle(x_brake, y + 22, brake_fill_width, bar_height)
            rl.draw_rectangle_rounded(brake_fill, 0.3, 10, self.COLOR_BRAKE)

        brake_text = f"{self._data.brake_signal:.2f}"
        rl.draw_text_ex(self._font_bold, brake_text,
                        rl.Vector2(x_brake + bar_width / 2 - 20, y + 24), 20, 0, rl.WHITE)

    def _draw_metrics(self, rect: rl.Rectangle):
        """Draw control metrics at bottom"""
        y = rect.y + rect.height - 90
        font_size = 20
        col_width = (rect.width - 30) / 2

        # Row 1: Speed
        self._draw_metric(
            rect.x + 15, y,
            "Speed:", f"{self._data.current_speed * 3.6:.1f} km/h",
            font_size
        )
        self._draw_metric(
            rect.x + 15 + col_width, y,
            "Target:", f"{self._data.target_speed:.1f} km/h",
            font_size
        )

        # Row 2: Flags
        y += 28
        flags = []
        if self._data.should_stop:
            flags.append("STOP")
        if not self._data.allow_throttle:
            flags.append("NO GAS")
        if not self._data.allow_brake:
            flags.append("NO BRAKE")

        flag_text = " | ".join(flags) if flags else "Normal"
        flag_color = self.COLOR_BRAKE if flags else self.COLOR_GAS

        rl.draw_text_ex(self._font, "Status:",
                        rl.Vector2(rect.x + 15, y), font_size, 0, self.COLOR_TEXT_DIM)
        rl.draw_text_ex(self._font_bold, flag_text,
                        rl.Vector2(rect.x + 85, y), font_size, 0, flag_color)

        # Row 3: Delay
        y += 28
        self._draw_metric(
            rect.x + 15, y,
            "Long Delay:", f"{self._data.longitudinal_actuator_delay:.3f}s",
            font_size
        )

    def _draw_metric(self, x: float, y: float, label: str, value: str, font_size: int):
        """Draw a single metric"""
        rl.draw_text_ex(self._font, label,
                        rl.Vector2(x, y), font_size, 0, self.COLOR_TEXT_DIM)
        label_width = measure_text_cached(self._font, label, font_size).x
        rl.draw_text_ex(self._font_bold, value,
                        rl.Vector2(x + label_width + 8, y), font_size, 0, self.COLOR_TEXT)
