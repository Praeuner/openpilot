"""
BluePilot Alert Renderer
Pill-style alerts with pulsing animations for warnings
"""

import time
import pyray as rl
from dataclasses import dataclass
from enum import IntEnum
from cereal import messaging, log
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.hardware import TICI
from openpilot.system.ui.lib.application import gui_app, FontWeight, DEFAULT_FPS
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget

AlertSize = log.SelfdriveState.AlertSize
AlertStatus = log.SelfdriveState.AlertStatus


class PillAlertSize(IntEnum):
    """Pill size variants"""
    SMALL = 0   # Single line: 74pt font
    MEDIUM = 1  # Two lines: 88pt/66pt fonts


@dataclass
class PillDimensions:
    """Calculated pill dimensions with scaled fonts"""
    width: int = 0
    height: int = 0
    font_size1: int = 74
    font_size2: int = 66


@dataclass
class Alert:
    """Alert data structure"""
    text1: str = ""
    text2: str = ""
    size: int = 0
    status: int = 0


# Pre-defined alert instances
ALERT_STARTUP_PENDING = Alert(
    text1="BluePilot Unavailable",
    text2="Waiting to start",
    size=AlertSize.mid,
    status=AlertStatus.normal,
)

ALERT_CRITICAL_TIMEOUT = Alert(
    text1="TAKE CONTROL IMMEDIATELY",
    text2="System Unresponsive",
    size=AlertSize.full,
    status=AlertStatus.critical,
)

ALERT_CRITICAL_REBOOT = Alert(
    text1="System Unresponsive",
    text2="Reboot Device",
    size=AlertSize.full,
    status=AlertStatus.critical,
)

# Constants
SELFDRIVE_STATE_TIMEOUT = 5  # Seconds
SELFDRIVE_UNRESPONSIVE_TIMEOUT = 10  # Seconds

# Fullscreen alert colors
ALERT_COLORS = {
    AlertStatus.normal: rl.Color(45, 46, 48, 255),        # Dark neutral
    AlertStatus.userPrompt: rl.Color(220, 100, 20, 255),  # Orange warning
    AlertStatus.critical: rl.Color(201, 34, 49, 255),     # Red critical
}


class AlertRendererBP(Widget):
    """
    BluePilot Alert Renderer with pill-style alerts.

    Features:
    - Pill mode for non-critical alerts (bottom-centered rounded pills)
    - Fullscreen mode for critical alerts
    - Pulsing animation for warnings
    - Drop shadows for depth
    - Dynamic font scaling for long text
    """

    # Pill colors
    COLOR_PILL_BG_NORMAL = rl.Color(45, 46, 48, 255)         # Dark neutral
    COLOR_PILL_BG_WARNING = rl.Color(220, 100, 20, 255)      # Orange
    COLOR_PILL_BORDER_NORMAL = rl.Color(80, 82, 85, 200)     # Subtle
    COLOR_PILL_BORDER_WARNING = rl.Color(255, 140, 60, 200)  # Warm glow

    # Pulse animation
    PULSE_SPEED = 0.035
    PULSE_MIN = 0.7
    PULSE_MAX = 1.0

    def __init__(self):
        super().__init__()
        self._font_regular = gui_app.font(FontWeight.NORMAL)
        self._font_semi_bold = gui_app.font(FontWeight.SEMI_BOLD)
        self._font_bold = gui_app.font(FontWeight.BOLD)

        # Pulse state
        self._pulse_opacity = 1.0
        self._pulse_increasing = False
        self._last_pulse_time = time.monotonic()

    def get_alert(self, sm: messaging.SubMaster) -> Alert | None:
        """Generate the current alert based on selfdrive state."""
        ss = sm['selfdriveState']

        # Check if selfdriveState messages have stopped arriving
        if not sm.updated['selfdriveState']:
            recv_frame = sm.recv_frame['selfdriveState']
            time_since_onroad = (sm.frame - ui_state.started_frame) / DEFAULT_FPS

            # 1. Never received selfdriveState since going onroad
            waiting_for_startup = recv_frame < ui_state.started_frame
            if waiting_for_startup and time_since_onroad > 5:
                return ALERT_STARTUP_PENDING

            # 2. Lost communication with selfdriveState after receiving it
            if TICI and not waiting_for_startup:
                ss_missing = time.monotonic() - sm.recv_time['selfdriveState']
                if ss_missing > SELFDRIVE_STATE_TIMEOUT:
                    if ss.enabled and (ss_missing - SELFDRIVE_STATE_TIMEOUT) < SELFDRIVE_UNRESPONSIVE_TIMEOUT:
                        return ALERT_CRITICAL_TIMEOUT
                    return ALERT_CRITICAL_REBOOT

        # No alert if size is none
        if ss.alertSize == 0:
            return None

        # Return current alert
        return Alert(
            text1=ss.alertText1,
            text2=ss.alertText2,
            size=ss.alertSize.raw,
            status=ss.alertStatus.raw
        )

    def _render(self, rect: rl.Rectangle) -> bool:
        """Render alert if active."""
        alert = self.get_alert(ui_state.sm)
        if not alert:
            return False

        # Update pulse animation for warnings
        self._update_pulse(alert)

        # Render based on size
        if alert.size == AlertSize.full:
            self._draw_fullscreen_alert(rect, alert)
        else:
            self._draw_pill_alert(rect, alert)

        return True

    def _update_pulse(self, alert: Alert) -> None:
        """Update pulse animation for warning alerts."""
        should_pulse = (
            alert.size != AlertSize.full and
            alert.status == AlertStatus.userPrompt
        )

        if not should_pulse:
            self._pulse_opacity = 1.0
            return

        # Update pulse at ~60fps
        current_time = time.monotonic()
        dt = current_time - self._last_pulse_time
        if dt < 0.016:  # ~60fps
            return
        self._last_pulse_time = current_time

        if self._pulse_increasing:
            self._pulse_opacity += self.PULSE_SPEED
            if self._pulse_opacity >= self.PULSE_MAX:
                self._pulse_opacity = self.PULSE_MAX
                self._pulse_increasing = False
        else:
            self._pulse_opacity -= self.PULSE_SPEED
            if self._pulse_opacity <= self.PULSE_MIN:
                self._pulse_opacity = self.PULSE_MIN
                self._pulse_increasing = True

    # -------------------------------------------------------------------------
    # Pill Alert Rendering
    # -------------------------------------------------------------------------

    def _draw_pill_alert(self, rect: rl.Rectangle, alert: Alert) -> None:
        """Draw pill-style alert at bottom center."""
        # Calculate pill dimensions
        pill_size = PillAlertSize.SMALL if not alert.text2 else PillAlertSize.MEDIUM
        dims = self._calculate_pill_dimensions(rect, alert.text1, alert.text2, pill_size)
        pill_rect = self._calculate_pill_rect(rect, dims.width, dims.height)

        # Draw shadows for depth
        self._draw_pill_shadows(pill_rect)

        # Draw background
        bg_color = self._get_pill_bg_color(alert.status)
        if alert.status == AlertStatus.userPrompt:
            bg_color.a = int(bg_color.a * self._pulse_opacity)

        rl.draw_rectangle_rounded(pill_rect, 1.0, 16, bg_color)

        # Draw border
        border_color = self._get_pill_border_color(alert.status)
        if alert.status == AlertStatus.userPrompt:
            border_color.a = int(border_color.a * self._pulse_opacity)

        rl.draw_rectangle_rounded_lines_ex(pill_rect, 1.0, 16, 2, border_color)

        # Draw text
        self._draw_pill_text(pill_rect, alert, dims)

    def _calculate_pill_dimensions(self, rect: rl.Rectangle, text1: str, text2: str,
                                   pill_size: PillAlertSize) -> PillDimensions:
        """Calculate pill dimensions with dynamic font scaling."""
        max_width = int(rect.width - 40)  # 20px margin per side
        h_padding = 140 if pill_size == PillAlertSize.SMALL else 160
        max_text_width = max_width - h_padding
        v_padding = 24 if pill_size == PillAlertSize.SMALL else 40

        dims = PillDimensions()

        if pill_size == PillAlertSize.SMALL:
            # Single line - 74pt DemiBold
            base_size = 74
            text_size = measure_text_cached(self._font_semi_bold, text1, base_size)

            if text_size.x > max_text_width:
                scale = max_text_width / text_size.x
                dims.font_size1 = int(base_size * scale)
                text_size = measure_text_cached(self._font_semi_bold, text1, dims.font_size1)
            else:
                dims.font_size1 = base_size

            dims.width = int(text_size.x + h_padding)
            dims.height = int(text_size.y + v_padding)
            dims.font_size2 = 0

        else:
            # Two lines - 88pt Bold / 66pt Regular
            base_size1 = 88
            base_size2 = 66

            text_size1 = measure_text_cached(self._font_bold, text1, base_size1)
            text_size2 = measure_text_cached(self._font_regular, text2, base_size2)
            max_needed = max(text_size1.x, text_size2.x)

            if max_needed > max_text_width:
                scale = max_text_width / max_needed
                dims.font_size1 = int(base_size1 * scale)
                dims.font_size2 = int(base_size2 * scale)
                text_size1 = measure_text_cached(self._font_bold, text1, dims.font_size1)
                text_size2 = measure_text_cached(self._font_regular, text2, dims.font_size2)
            else:
                dims.font_size1 = base_size1
                dims.font_size2 = base_size2

            line_spacing = 8
            dims.width = int(max(text_size1.x, text_size2.x) + h_padding)
            dims.height = int(text_size1.y + text_size2.y + line_spacing + v_padding)

        return dims

    def _calculate_pill_rect(self, rect: rl.Rectangle, width: int, height: int) -> rl.Rectangle:
        """Calculate pill position (bottom-centered)."""
        x = rect.x + (rect.width - width) / 2
        y = rect.y + rect.height - height - 50  # 50px from bottom
        return rl.Rectangle(x, y, width, height)

    def _draw_pill_shadows(self, pill_rect: rl.Rectangle) -> None:
        """Draw drop shadow layers for depth."""
        # Shadow 1 (furthest)
        shadow1 = rl.Rectangle(pill_rect.x, pill_rect.y + 6, pill_rect.width, pill_rect.height)
        rl.draw_rectangle_rounded(shadow1, 1.0, 16, rl.Color(0, 0, 0, 100))

        # Shadow 2
        shadow2 = rl.Rectangle(pill_rect.x, pill_rect.y + 4, pill_rect.width, pill_rect.height)
        rl.draw_rectangle_rounded(shadow2, 1.0, 16, rl.Color(0, 0, 0, 60))

        # Shadow 3 (closest)
        shadow3 = rl.Rectangle(pill_rect.x, pill_rect.y + 2, pill_rect.width, pill_rect.height)
        rl.draw_rectangle_rounded(shadow3, 1.0, 16, rl.Color(0, 0, 0, 30))

    def _get_pill_bg_color(self, status: int) -> rl.Color:
        """Get pill background color based on status."""
        if status == AlertStatus.userPrompt:
            return rl.Color(220, 100, 20, 255)
        return rl.Color(45, 46, 48, 255)

    def _get_pill_border_color(self, status: int) -> rl.Color:
        """Get pill border color based on status."""
        if status == AlertStatus.userPrompt:
            return rl.Color(255, 140, 60, 200)
        return rl.Color(80, 82, 85, 200)

    def _draw_pill_text(self, pill_rect: rl.Rectangle, alert: Alert, dims: PillDimensions) -> None:
        """Draw pill text with shadow."""
        v_padding = 12 if not alert.text2 else 20
        text_rect = rl.Rectangle(
            pill_rect.x,
            pill_rect.y + v_padding,
            pill_rect.width,
            pill_rect.height - v_padding * 2
        )

        if not alert.text2:
            # Single line - centered
            text_size = measure_text_cached(self._font_semi_bold, alert.text1, dims.font_size1)
            x = text_rect.x + (text_rect.width - text_size.x) / 2
            y = text_rect.y + (text_rect.height - text_size.y) / 2

            # Shadow
            rl.draw_text_ex(self._font_semi_bold, alert.text1,
                            rl.Vector2(x + 2, y + 2), dims.font_size1, 0,
                            rl.Color(0, 0, 0, 100))
            # Main text
            rl.draw_text_ex(self._font_semi_bold, alert.text1,
                            rl.Vector2(x, y), dims.font_size1, 0, rl.WHITE)
        else:
            # Two lines
            # Line 1 (bold, upper portion)
            text_size1 = measure_text_cached(self._font_bold, alert.text1, dims.font_size1)
            x1 = text_rect.x + (text_rect.width - text_size1.x) / 2
            y1 = text_rect.y + text_rect.height * 0.55 - text_size1.y

            # Shadow
            rl.draw_text_ex(self._font_bold, alert.text1,
                            rl.Vector2(x1 + 2, y1 + 2), dims.font_size1, 0,
                            rl.Color(0, 0, 0, 100))
            # Main text
            rl.draw_text_ex(self._font_bold, alert.text1,
                            rl.Vector2(x1, y1), dims.font_size1, 0, rl.WHITE)

            # Line 2 (regular, lower portion)
            text_size2 = measure_text_cached(self._font_regular, alert.text2, dims.font_size2)
            x2 = text_rect.x + (text_rect.width - text_size2.x) / 2
            y2 = text_rect.y + text_rect.height * 0.55

            rl.draw_text_ex(self._font_regular, alert.text2,
                            rl.Vector2(x2, y2), dims.font_size2, 0,
                            rl.Color(220, 220, 220, 255))

    # -------------------------------------------------------------------------
    # Fullscreen Alert Rendering
    # -------------------------------------------------------------------------

    def _draw_fullscreen_alert(self, rect: rl.Rectangle, alert: Alert) -> None:
        """Draw fullscreen critical alert."""
        # Determine height based on size
        if alert.size == AlertSize.small:
            h = 271
            margin = 40
            radius = 30
        elif alert.size == AlertSize.mid:
            h = 420
            margin = 40
            radius = 30
        else:  # full
            h = int(rect.height)
            margin = 0
            radius = 0

        alert_rect = rl.Rectangle(
            rect.x + margin,
            rect.y + rect.height - h + margin,
            rect.width - margin * 2,
            h - margin * 2
        )

        # Draw shadows for non-fullscreen
        if margin > 0:
            self._draw_fullscreen_shadows(alert_rect, radius)

        # Draw background
        bg_color = ALERT_COLORS.get(alert.status, ALERT_COLORS[AlertStatus.normal])
        roundness = radius / (min(alert_rect.width, alert_rect.height) / 2) if radius > 0 else 0
        rl.draw_rectangle_rounded(alert_rect, roundness, 16, bg_color)

        # Draw border for non-fullscreen
        if margin > 0:
            # Lighter border
            border_color = rl.Color(
                min(255, bg_color.r + 30),
                min(255, bg_color.g + 30),
                min(255, bg_color.b + 30),
                150
            )
            rl.draw_rectangle_rounded_lines_ex(alert_rect, roundness, 16, 2, border_color)

        # Draw text
        self._draw_fullscreen_text(alert_rect, alert)

    def _draw_fullscreen_shadows(self, alert_rect: rl.Rectangle, radius: int) -> None:
        """Draw shadow layers for fullscreen alert."""
        roundness = radius / (min(alert_rect.width, alert_rect.height) / 2) if radius > 0 else 0

        shadow1 = rl.Rectangle(alert_rect.x, alert_rect.y + 6, alert_rect.width, alert_rect.height)
        rl.draw_rectangle_rounded(shadow1, roundness, 16, rl.Color(0, 0, 0, 100))

        shadow2 = rl.Rectangle(alert_rect.x, alert_rect.y + 4, alert_rect.width, alert_rect.height)
        rl.draw_rectangle_rounded(shadow2, roundness, 16, rl.Color(0, 0, 0, 60))

        shadow3 = rl.Rectangle(alert_rect.x, alert_rect.y + 2, alert_rect.width, alert_rect.height)
        rl.draw_rectangle_rounded(shadow3, roundness, 16, rl.Color(0, 0, 0, 30))

    def _draw_fullscreen_text(self, alert_rect: rl.Rectangle, alert: Alert) -> None:
        """Draw fullscreen alert text."""
        center_x = alert_rect.x + alert_rect.width / 2
        center_y = alert_rect.y + alert_rect.height / 2

        if alert.size == AlertSize.small:
            # Single line centered
            font_size = 74
            text_size = measure_text_cached(self._font_semi_bold, alert.text1, font_size)
            x = center_x - text_size.x / 2
            y = center_y - text_size.y / 2

            # Shadow
            rl.draw_text_ex(self._font_semi_bold, alert.text1,
                            rl.Vector2(x + 2, y + 2), font_size, 0,
                            rl.Color(0, 0, 0, 100))
            rl.draw_text_ex(self._font_semi_bold, alert.text1,
                            rl.Vector2(x, y), font_size, 0, rl.WHITE)

        elif alert.size == AlertSize.mid:
            # Two lines
            # Line 1
            font_size1 = 88
            text_size1 = measure_text_cached(self._font_bold, alert.text1, font_size1)
            x1 = center_x - text_size1.x / 2
            y1 = center_y - 125

            rl.draw_text_ex(self._font_bold, alert.text1,
                            rl.Vector2(x1 + 2, y1 + 2), font_size1, 0,
                            rl.Color(0, 0, 0, 100))
            rl.draw_text_ex(self._font_bold, alert.text1,
                            rl.Vector2(x1, y1), font_size1, 0, rl.WHITE)

            # Line 2
            font_size2 = 66
            text_size2 = measure_text_cached(self._font_regular, alert.text2, font_size2)
            x2 = center_x - text_size2.x / 2
            y2 = center_y + 21

            rl.draw_text_ex(self._font_regular, alert.text2,
                            rl.Vector2(x2, y2), font_size2, 0,
                            rl.Color(220, 220, 220, 255))

        else:  # full
            is_long = len(alert.text1) > 15
            font_size1 = 132 if is_long else 177

            # Line 1
            text_size1 = measure_text_cached(self._font_bold, alert.text1, font_size1)
            x1 = center_x - text_size1.x / 2
            y1 = alert_rect.y + (240 if is_long else 270)

            rl.draw_text_ex(self._font_bold, alert.text1,
                            rl.Vector2(x1 + 3, y1 + 3), font_size1, 0,
                            rl.Color(0, 0, 0, 120))
            rl.draw_text_ex(self._font_bold, alert.text1,
                            rl.Vector2(x1, y1), font_size1, 0, rl.WHITE)

            # Line 2
            font_size2 = 88
            text_size2 = measure_text_cached(self._font_regular, alert.text2, font_size2)
            x2 = center_x - text_size2.x / 2
            y2 = alert_rect.y + alert_rect.height - (361 if is_long else 420)

            rl.draw_text_ex(self._font_regular, alert.text2,
                            rl.Vector2(x2, y2), font_size2, 0,
                            rl.Color(220, 220, 220, 255))
