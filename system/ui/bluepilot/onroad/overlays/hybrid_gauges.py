"""
BluePilot Hybrid Gauges Overlay
Ford hybrid vehicle power and battery display
"""

import time
from dataclasses import dataclass
import pyray as rl
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached


@dataclass
class HybridState:
    """State for hybrid vehicle data"""
    hybrid_available: bool = False
    battery_available: bool = False
    throttle_demand: float = 0.0
    throttle_threshold: float = 0.0
    power_mode: str = ""
    engine_reason: str = ""
    batt_soc_actual: float = 0.0
    batt_soc_min: float = 0.0
    batt_soc_max: float = 100.0
    batt_volt_actual: float = 0.0
    batt_volt_low: float = 0.0
    batt_volt_high: float = 0.0
    batt_amps_actual: float = 0.0


class HybridGaugesOverlay:
    """
    Renders Ford hybrid vehicle power and battery gauges.

    Features:
    - Hybrid Drive Gauge: Shows throttle demand with EV threshold brackets
    - Hybrid Battery Gauge: Shows battery SOC, voltage, and current
    - Automotive-style metallic gradients and styling
    """

    # Constants
    BAR_ROUND_RADIUS = 6.0
    BORDER_WIDTH = 3
    AMPS_UPDATE_INTERVAL = 0.5

    # Colors
    COLOR_BG_CENTER = rl.Color(44, 62, 80, 230)
    COLOR_BG_EDGE = rl.Color(26, 37, 47, 230)
    COLOR_TEXT = rl.Color(236, 240, 241, 230)
    COLOR_TEXT_SHADOW = rl.Color(0, 0, 0, 100)
    COLOR_ELECTRIC_BLUE = rl.Color(30, 144, 255, 255)
    COLOR_CORNFLOWER_BLUE = rl.Color(100, 149, 237, 255)
    COLOR_STEEL_BLUE = rl.Color(70, 130, 180, 255)
    COLOR_GREEN = rl.Color(46, 204, 113, 255)
    COLOR_DARK_GREEN = rl.Color(39, 174, 96, 255)
    COLOR_YELLOW = rl.Color(241, 196, 15, 255)
    COLOR_ORANGE = rl.Color(243, 156, 18, 255)
    COLOR_RED = rl.Color(231, 76, 60, 255)
    COLOR_DARK_RED = rl.Color(192, 57, 43, 255)
    COLOR_REGEN_GREEN = rl.Color(0, 255, 127, 255)
    COLOR_AMBER = rl.Color(243, 156, 18, 200)

    def __init__(self):
        self._font_bold = gui_app.font(FontWeight.BOLD)
        self._font_semi_bold = gui_app.font(FontWeight.SEMI_BOLD)
        self._hybrid_state = HybridState()

        # State for amp display smoothing
        self._last_displayed_amps: float = 0.0
        self._last_amps_update_time: float = 0.0

        # Bracket animation state (simplified - no Qt animation)
        self._bracket_scale: float = 1.0
        self._was_near_bracket: bool = False

    def render(self, rect: rl.Rectangle, bp_state) -> None:
        """Render hybrid gauges overlay"""
        # Update hybrid state from car state
        self._update_hybrid_state()

        # Check if we should render
        if not bp_state.show_hybrid_drive_overlay or not self._hybrid_state.hybrid_available:
            return

        # Calculate gauge dimensions based on scale
        gauge_scale = bp_state.hybrid_drive_gauge_size if hasattr(bp_state, 'hybrid_drive_gauge_size') else 2
        sidebar_visible = bp_state.sidebar_visible if hasattr(bp_state, 'sidebar_visible') else False

        gauge_width, gauge_height = self._calculate_gauge_size(rect, gauge_scale, sidebar_visible)

        # Position at bottom center
        bottom_margin = 30
        y_position = rect.height - gauge_height - bottom_margin
        gauge_center_x = rect.width / 2

        gauge_rect = rl.Rectangle(
            rect.x + gauge_center_x - gauge_width / 2,
            rect.y + y_position,
            gauge_width,
            gauge_height
        )

        # Draw hybrid drive gauge
        self._draw_hybrid_drive_gauge(
            gauge_rect,
            self._hybrid_state.throttle_demand,
            self._hybrid_state.throttle_threshold,
            self._hybrid_state.power_mode,
            self._hybrid_state.engine_reason
        )

        # Draw battery gauge if enabled and available
        if bp_state.show_hybrid_battery_overlay and self._hybrid_state.battery_available:
            batt_width_ratio = 0.25
            if gauge_scale == 1:
                batt_width_ratio = 0.28 if sidebar_visible else 0.26
            elif gauge_scale == 2:
                batt_width_ratio = 0.27 if sidebar_visible else 0.25
            elif gauge_scale == 3:
                batt_width_ratio = 0.26 if sidebar_visible else 0.24

            batt_width = int(gauge_width * batt_width_ratio)
            gauge_spacing = 8 if sidebar_visible else 10

            battery_rect = rl.Rectangle(
                gauge_rect.x + gauge_rect.width + gauge_spacing,
                gauge_rect.y,
                batt_width,
                gauge_height
            )

            self._draw_hybrid_battery_gauge(battery_rect, self._hybrid_state)

    def _update_hybrid_state(self) -> None:
        """Update hybrid state from car state messages"""
        sm = ui_state.sm

        if not sm.valid.get('carStateBP', False):
            return

        try:
            car_state_bp = sm['carStateBP']

            # Hybrid drive data
            hybrid_drive = car_state_bp.hybridDrive
            self._hybrid_state.hybrid_available = hybrid_drive.dataAvailable
            if self._hybrid_state.hybrid_available:
                self._hybrid_state.throttle_demand = hybrid_drive.throttleDemandPercent
                self._hybrid_state.throttle_threshold = hybrid_drive.throttleThresholdPercent
                self._hybrid_state.power_mode = hybrid_drive.powerFlowMode
                self._hybrid_state.engine_reason = hybrid_drive.engineOnReason

            # Hybrid battery data
            hybrid_battery = car_state_bp.hybridBattery
            self._hybrid_state.battery_available = hybrid_battery.dataAvailable
            if self._hybrid_state.battery_available:
                self._hybrid_state.batt_soc_actual = hybrid_battery.socActual
                self._hybrid_state.batt_soc_min = hybrid_battery.socMinPerc
                self._hybrid_state.batt_soc_max = hybrid_battery.socMaxPerc
                self._hybrid_state.batt_volt_actual = hybrid_battery.voltActual
                self._hybrid_state.batt_volt_low = hybrid_battery.voltLowLimit
                self._hybrid_state.batt_volt_high = hybrid_battery.voltHighLimit
                self._hybrid_state.batt_amps_actual = hybrid_battery.ampsActual

        except (AttributeError, KeyError):
            # Message structure may vary
            pass

    def _calculate_gauge_size(self, rect: rl.Rectangle, gauge_scale: int, sidebar_visible: bool) -> tuple[int, int]:
        """Calculate gauge dimensions based on scale and sidebar visibility"""
        if gauge_scale == 1:
            gauge_height = 100
            gauge_width = int(rect.width * (0.28 if sidebar_visible else 0.30))
        elif gauge_scale == 2:
            gauge_height = 120
            gauge_width = int(rect.width * (0.34 if sidebar_visible else 0.37))
        elif gauge_scale == 3:
            gauge_height = 155
            gauge_width = int(rect.width * (0.42 if sidebar_visible else 0.48))
        else:
            gauge_height = 100
            gauge_width = int(rect.width * (0.28 if sidebar_visible else 0.30))

        return gauge_width, gauge_height

    def _draw_hybrid_drive_gauge(self, rect: rl.Rectangle, throttle_demand: float,
                                  throttle_threshold: float, power_mode: str, engine_reason: str) -> None:
        """Draw the hybrid drive power gauge"""
        # Layout: split into power bar (55%) and text area (45%)
        power_bar_ratio = 0.55
        power_bar_height = int(rect.height * power_bar_ratio)
        text_height = int(rect.height - power_bar_height)

        # Draw metallic background
        self._draw_metallic_background(rect, power_mode)

        # Draw border based on mode
        border_color = self._get_border_color(throttle_demand, power_mode)
        rl.draw_rectangle_rounded_lines_ex(rect, 0.1, 10, self.BORDER_WIDTH, border_color)

        # Draw power bar
        bar_margin = 8
        bar_rect = rl.Rectangle(
            rect.x + bar_margin,
            rect.y + bar_margin,
            rect.width - 2 * bar_margin,
            power_bar_height - 2 * bar_margin
        )
        self._draw_power_bar(bar_rect, throttle_demand, throttle_threshold, power_mode)

        # Draw text area
        text_rect = rl.Rectangle(
            rect.x + self.BORDER_WIDTH,
            rect.y + power_bar_height,
            rect.width - 2 * self.BORDER_WIDTH,
            text_height - self.BORDER_WIDTH
        )

        # Draw text background
        rl.draw_rectangle_rounded(text_rect, 0.1, 10, rl.Color(44, 62, 80, 255))

        # Prepare combined text
        combined_text = power_mode
        if engine_reason:
            combined_text = f"{power_mode} | {engine_reason}" if power_mode else engine_reason

        # Calculate font size to fit
        font_size = int(rect.width * 0.05)
        font_size = max(min(font_size, 24), 10)

        # Measure and draw text centered
        text_size = measure_text_cached(self._font_bold, combined_text, font_size)
        text_x = text_rect.x + (text_rect.width - text_size.x) / 2
        text_y = text_rect.y + (text_rect.height - text_size.y) / 2

        # Text shadow
        rl.draw_text_ex(self._font_bold, combined_text,
                        rl.Vector2(text_x + 1, text_y + 1), font_size, 0, self.COLOR_TEXT_SHADOW)
        # Main text
        rl.draw_text_ex(self._font_bold, combined_text,
                        rl.Vector2(text_x, text_y), font_size, 0, self.COLOR_TEXT)

    def _draw_metallic_background(self, rect: rl.Rectangle, power_mode: str) -> None:
        """Draw automotive-style metallic background"""
        # Draw main background with gradient (simulated radial with vertical gradient)
        rl.draw_rectangle_rounded(rect, 0.1, 10, self.COLOR_BG_CENTER)

        # Add highlight at top for metallic effect
        highlight_rect = rl.Rectangle(
            rect.x + self.BORDER_WIDTH,
            rect.y + self.BORDER_WIDTH,
            rect.width - 2 * self.BORDER_WIDTH,
            rect.height * 0.3
        )
        rl.draw_rectangle_rounded(highlight_rect, 0.1, 10, rl.Color(255, 255, 255, 20))

    def _get_border_color(self, value: float, mode: str) -> rl.Color:
        """Get border color based on power mode"""
        is_ev_mode = "electric" in mode.lower() if mode else False
        is_hybrid_mode = "hybrid" in mode.lower() if mode else False
        is_regen = value < 0

        if is_regen:
            return self.COLOR_REGEN_GREEN
        elif is_ev_mode:
            return self.COLOR_ELECTRIC_BLUE
        elif is_hybrid_mode:
            return self.COLOR_CORNFLOWER_BLUE
        else:
            return self.COLOR_STEEL_BLUE

    def _draw_power_bar(self, rect: rl.Rectangle, value: float, threshold: float, mode: str) -> None:
        """Draw the power bar with threshold markers"""
        center_x = rect.x + rect.width / 2
        is_ev_mode = "electric" in mode.lower() if mode else False

        # Draw bar background
        bg_color = rl.Color(52, 73, 94, 255)
        if is_ev_mode:
            # In EV mode, only show background for EV range
            ev_width = rect.width * (threshold / 100.0)
            ev_rect = rl.Rectangle(center_x - ev_width / 2, rect.y, ev_width, rect.height)
            rl.draw_rectangle_rounded(ev_rect, 0.15, 10, bg_color)
        else:
            rl.draw_rectangle_rounded(rect, 0.15, 10, bg_color)

        # Draw active bar if value is non-zero
        if abs(value) > 0.1:
            bar_width = rect.width * (abs(value) / 100.0)
            bar_rect = rl.Rectangle(
                center_x - bar_width / 2,
                rect.y,
                bar_width,
                rect.height
            )

            # Get bar color based on mode and value
            bar_color = self._get_power_bar_color(value, threshold, mode)
            rl.draw_rectangle_rounded(bar_rect, 0.15, 10, bar_color)

            # Add highlight for 3D effect
            highlight_rect = rl.Rectangle(
                bar_rect.x + 2,
                bar_rect.y + 2,
                bar_rect.width - 4,
                bar_rect.height * 0.4
            )
            rl.draw_rectangle_rounded(highlight_rect, 0.15, 10, rl.Color(255, 255, 255, 30))

        # Draw threshold brackets in EV mode
        if is_ev_mode:
            self._draw_threshold_brackets(rect, threshold, value)

        # Draw center line
        rl.draw_line_ex(
            rl.Vector2(center_x, rect.y),
            rl.Vector2(center_x, rect.y + rect.height),
            2, rl.Color(236, 240, 241, 200)
        )

        # Draw scale markers (not in EV mode)
        if not is_ev_mode:
            for i in range(-75, 76, 25):
                x = center_x + (rect.width * i / 200)
                marker_height = 10 if i % 50 == 0 else 5
                marker_color = rl.Color(236, 240, 241, 200) if i % 50 == 0 else rl.Color(189, 195, 199, 150)

                rl.draw_line_ex(
                    rl.Vector2(x, rect.y),
                    rl.Vector2(x, rect.y + marker_height),
                    1, marker_color
                )
                rl.draw_line_ex(
                    rl.Vector2(x, rect.y + rect.height - marker_height),
                    rl.Vector2(x, rect.y + rect.height),
                    1, marker_color
                )

    def _get_power_bar_color(self, value: float, threshold: float, mode: str) -> rl.Color:
        """Get power bar color based on mode and value"""
        is_ev_mode = "electric" in mode.lower() if mode else False

        if value < 0:
            # Regen - green
            return rl.Color(0, 220, 100, 255)
        elif is_ev_mode:
            # Electric - blue
            return rl.Color(0, 150, 255, 255)
        else:
            # Engine on - blend from gray to orange based on demand
            normalized = min(max(value / 100.0, 0.0), 1.0)

            if normalized <= 0.33:
                # Light gray for low demand
                return rl.Color(200, 200, 200, 255)
            else:
                # Blend from gray to orange
                blend = (normalized - 0.33) / 0.67
                r = int(200 + (255 - 200) * blend)
                g = int(200 + (140 - 200) * blend)
                b = int(200 * (1 - blend))
                return rl.Color(r, g, b, 255)

    def _draw_threshold_brackets(self, rect: rl.Rectangle, threshold: float, current_value: float) -> None:
        """Draw EV threshold brackets"""
        center_x = rect.x + rect.width / 2
        half_threshold = threshold / 2.0

        # Calculate bracket color based on proximity
        proximity_percent = (abs(current_value) / threshold) * 100.0 if threshold > 0 else 0

        if proximity_percent < 80.0:
            bracket_color = self.COLOR_AMBER
        else:
            # Transition to red
            t = (proximity_percent - 80.0) / 20.0
            r = int(243 - t * 43)
            g = int(156 - t * 156)
            bracket_color = rl.Color(r, g, 18, int(200 + t * 55))

        # Draw left and right brackets
        for side in [-1, 1]:
            x = center_x + side * (rect.width * half_threshold / 100.0)

            bracket_width = 12
            bracket_depth = 8

            # Top bracket
            rl.draw_line_ex(
                rl.Vector2(x + side * bracket_width, rect.y),
                rl.Vector2(x, rect.y),
                4, bracket_color
            )
            rl.draw_line_ex(
                rl.Vector2(x, rect.y),
                rl.Vector2(x, rect.y + bracket_depth),
                4, bracket_color
            )

            # Bottom bracket
            rl.draw_line_ex(
                rl.Vector2(x + side * bracket_width, rect.y + rect.height),
                rl.Vector2(x, rect.y + rect.height),
                4, bracket_color
            )
            rl.draw_line_ex(
                rl.Vector2(x, rect.y + rect.height),
                rl.Vector2(x, rect.y + rect.height - bracket_depth),
                4, bracket_color
            )

    def _draw_hybrid_battery_gauge(self, rect: rl.Rectangle, state: HybridState) -> None:
        """Draw the hybrid battery gauge"""
        # Smooth amps display
        current_time = time.monotonic()
        display_amps = state.batt_amps_actual

        if (current_time - self._last_amps_update_time < self.AMPS_UPDATE_INTERVAL and
            abs(state.batt_amps_actual - self._last_displayed_amps) < 1.0):
            display_amps = self._last_displayed_amps
        else:
            self._last_displayed_amps = state.batt_amps_actual
            self._last_amps_update_time = current_time

        # Widen the gauge slightly
        rect = rl.Rectangle(rect.x, rect.y, rect.width * 1.45, rect.height)

        # Calculate battery percentage
        soc_range = max(state.batt_soc_max - state.batt_soc_min, 1.0)
        fill_perc = (state.batt_soc_actual - state.batt_soc_min) / soc_range
        fill_perc = max(0.0, min(1.0, fill_perc))
        battery_percent = fill_perc * 100

        # Draw background
        rl.draw_rectangle_rounded(rect, 0.1, 10, self.COLOR_BG_CENTER)

        # Draw border colored by battery state
        if battery_percent > 50:
            border_color = self.COLOR_GREEN
        elif battery_percent > 25:
            border_color = self.COLOR_YELLOW
        else:
            border_color = self.COLOR_RED

        rl.draw_rectangle_rounded_lines_ex(rect, 0.1, 10, 3, border_color)

        # Layout: 55% top for battery icon, 45% bottom for text
        power_bar_ratio = 0.55
        main_height = int(rect.height * power_bar_ratio)

        # Draw battery icon
        battery_width = int(rect.width * 0.45)
        battery_height = int(main_height * 0.6)
        battery_x = int(rect.x + 20)
        battery_y = int(rect.y + (main_height - battery_height) / 2)

        battery_rect = rl.Rectangle(battery_x, battery_y, battery_width, battery_height)
        self._draw_battery_icon(battery_rect, fill_perc)

        # Draw percentage text
        percent_text = f"{int(battery_percent)}%"
        font_size = int(rect.height * 0.24)
        text_size = measure_text_cached(self._font_bold, percent_text, font_size)
        text_x = battery_rect.x + battery_rect.width + 15
        text_y = rect.y + (main_height - text_size.y) / 2

        # Text shadow
        rl.draw_text_ex(self._font_bold, percent_text,
                        rl.Vector2(text_x + 1, text_y + 1), font_size, 0, self.COLOR_TEXT_SHADOW)
        rl.draw_text_ex(self._font_bold, percent_text,
                        rl.Vector2(text_x, text_y), font_size, 0, self.COLOR_TEXT)

        # Draw bottom text area (voltage and amps)
        text_area_height = rect.height - main_height
        text_rect = rl.Rectangle(
            rect.x + 3,
            rect.y + main_height,
            rect.width - 6,
            text_area_height - 3
        )
        rl.draw_rectangle_rounded(text_rect, 0.1, 10, rl.Color(44, 62, 80, 255))

        # Voltage text
        volt_text = f"{int(state.batt_volt_actual)}V"
        volt_color = self._get_voltage_color(state.batt_volt_actual, state.batt_volt_low, state.batt_volt_high)

        # Amps text (+ for charging/regen, - for discharging)
        amp_sign = "+" if display_amps < 0 else "-"
        amp_text = f"{amp_sign}{abs(display_amps):.1f}A"
        amp_color = self.COLOR_GREEN if display_amps < 0 else self.COLOR_TEXT

        font_size_small = int(rect.height * 0.20)

        # Draw voltage (left)
        volt_size = measure_text_cached(self._font_bold, volt_text, font_size_small)
        volt_x = text_rect.x + 8
        volt_y = text_rect.y + (text_rect.height - volt_size.y) / 2
        rl.draw_text_ex(self._font_bold, volt_text,
                        rl.Vector2(volt_x, volt_y), font_size_small, 0, volt_color)

        # Draw amps (right)
        amp_size = measure_text_cached(self._font_bold, amp_text, font_size_small)
        amp_x = text_rect.x + text_rect.width - amp_size.x - 8
        amp_y = text_rect.y + (text_rect.height - amp_size.y) / 2
        rl.draw_text_ex(self._font_bold, amp_text,
                        rl.Vector2(amp_x, amp_y), font_size_small, 0, amp_color)

    def _draw_battery_icon(self, rect: rl.Rectangle, fill_perc: float) -> None:
        """Draw automotive-style battery icon"""
        # Battery outline
        outline_color = rl.Color(236, 240, 241, 255)
        rl.draw_rectangle_lines_ex(rect, 2, outline_color)

        # Positive terminal (right side)
        tab_width = int(rect.height * 0.2)
        tab_height = int(rect.height * 0.4)
        tab_x = int(rect.x + rect.width)
        tab_y = int(rect.y + (rect.height - tab_height) / 2)
        rl.draw_rectangle(tab_x, tab_y, tab_width, tab_height, outline_color)

        # Battery background
        bg_rect = rl.Rectangle(rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4)
        rl.draw_rectangle_rec(bg_rect, rl.Color(52, 73, 94, 255))

        # Battery fill
        if fill_perc > 0:
            fill_width = (rect.width - 4) * fill_perc
            fill_rect = rl.Rectangle(rect.x + 2, rect.y + 2, fill_width, rect.height - 4)

            # Fill color based on level
            if fill_perc > 0.5:
                fill_color = self.COLOR_GREEN
            elif fill_perc > 0.25:
                fill_color = self.COLOR_YELLOW
            else:
                fill_color = self.COLOR_RED

            rl.draw_rectangle_rec(fill_rect, fill_color)

            # Highlight for 3D effect
            highlight_rect = rl.Rectangle(fill_rect.x, fill_rect.y, fill_rect.width, fill_rect.height * 0.4)
            rl.draw_rectangle_rec(highlight_rect, rl.Color(255, 255, 255, 40))

    def _get_voltage_color(self, voltage: float, low_limit: float, high_limit: float) -> rl.Color:
        """Get voltage display color"""
        if voltage <= low_limit + 10:
            return self.COLOR_RED
        elif voltage >= high_limit - 10:
            return self.COLOR_YELLOW
        else:
            return self.COLOR_TEXT
