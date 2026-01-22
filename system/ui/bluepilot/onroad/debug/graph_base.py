"""
BluePilot Graph Base Widget
Base class for debug graph widgets with line plotting
"""

import pyray as rl
from collections import deque
from dataclasses import dataclass, field
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget


MAX_DATA_POINTS = 100


@dataclass
class GraphConfig:
    """Configuration for graph appearance"""
    title: str = ""
    y_label: str = ""
    line1_color: rl.Color = field(default_factory=lambda: rl.Color(46, 204, 113, 255))  # Green
    line2_color: rl.Color = field(default_factory=lambda: rl.Color(52, 152, 219, 255))  # Blue
    line1_label: str = "Desired"
    line2_label: str = "Actual"
    grid_color: rl.Color = field(default_factory=lambda: rl.Color(100, 100, 100, 100))
    bg_color: rl.Color = field(default_factory=lambda: rl.Color(20, 20, 25, 200))
    show_legend: bool = True
    show_values: bool = True


class GraphWidget(Widget):
    """
    Base graph widget for plotting time-series data.

    Features:
    - Dual-line plot (desired vs actual)
    - Auto-scaling Y-axis
    - Grid lines
    - Legend and current values display
    """

    def __init__(self, config: GraphConfig | None = None):
        super().__init__()
        self._config = config or GraphConfig()
        self._font = gui_app.font(FontWeight.MEDIUM)
        self._font_bold = gui_app.font(FontWeight.BOLD)

        # Data storage (pairs of desired, actual)
        self._data: deque = deque(maxlen=MAX_DATA_POINTS)
        self._max_value = 1.0
        self._current_desired = 0.0
        self._current_actual = 0.0

    def add_data_point(self, desired: float, actual: float):
        """Add a new data point to the graph"""
        self._data.appendleft((desired, actual))
        self._current_desired = desired
        self._current_actual = actual

        # Auto-scale max value
        for d, a in self._data:
            max_abs = max(abs(d), abs(a))
            if max_abs > self._max_value:
                self._max_value = max_abs * 1.2

        # Gradually reduce max if data is smaller
        if len(self._data) > 10:
            current_max = max(max(abs(d), abs(a)) for d, a in self._data)
            if current_max < self._max_value * 0.7:
                self._max_value *= 0.99

    def set_max_value(self, max_val: float):
        """Manually set the maximum Y value"""
        self._max_value = max(max_val, 0.1)

    def _render(self, rect: rl.Rectangle):
        """Render the graph"""
        # Draw background
        rl.draw_rectangle_rounded(rect, 0.05, 10, self._config.bg_color)

        # Calculate graph area (with padding for labels)
        padding = 50
        graph_rect = rl.Rectangle(
            rect.x + padding,
            rect.y + 30,
            rect.width - padding - 20,
            rect.height - 60
        )

        # Draw grid
        self._draw_grid(graph_rect)

        # Draw lines
        if len(self._data) >= 2:
            self._draw_line(graph_rect, 0, self._config.line1_color)  # Desired
            self._draw_line(graph_rect, 1, self._config.line2_color)  # Actual

        # Draw title
        if self._config.title:
            title_size = measure_text_cached(self._font_bold, self._config.title, 24)
            rl.draw_text_ex(
                self._font_bold, self._config.title,
                rl.Vector2(rect.x + (rect.width - title_size.x) / 2, rect.y + 5),
                24, 0, rl.WHITE
            )

        # Draw Y-axis labels
        self._draw_y_labels(graph_rect)

        # Draw legend and current values
        if self._config.show_legend:
            self._draw_legend(rect, graph_rect)

    def _draw_grid(self, rect: rl.Rectangle):
        """Draw grid lines"""
        # Horizontal grid lines (5 lines)
        for i in range(5):
            y = rect.y + rect.height * i / 4
            rl.draw_line_ex(
                rl.Vector2(rect.x, y),
                rl.Vector2(rect.x + rect.width, y),
                1, self._config.grid_color
            )

        # Vertical grid lines (10 lines)
        for i in range(11):
            x = rect.x + rect.width * i / 10
            rl.draw_line_ex(
                rl.Vector2(x, rect.y),
                rl.Vector2(x, rect.y + rect.height),
                1, self._config.grid_color
            )

        # Center line (zero line) - slightly brighter
        center_y = rect.y + rect.height / 2
        rl.draw_line_ex(
            rl.Vector2(rect.x, center_y),
            rl.Vector2(rect.x + rect.width, center_y),
            2, rl.Color(150, 150, 150, 150)
        )

    def _draw_line(self, rect: rl.Rectangle, data_idx: int, color: rl.Color):
        """Draw a data line"""
        if len(self._data) < 2:
            return

        points = []
        for i, data_point in enumerate(self._data):
            value = data_point[data_idx]
            x = rect.x + rect.width - (i / MAX_DATA_POINTS) * rect.width
            # Normalize value to graph height (centered at zero)
            normalized = value / self._max_value if self._max_value > 0 else 0
            y = rect.y + rect.height / 2 - (normalized * rect.height / 2)
            y = max(rect.y, min(rect.y + rect.height, y))
            points.append((x, y))

        # Draw line segments
        for i in range(len(points) - 1):
            rl.draw_line_ex(
                rl.Vector2(points[i][0], points[i][1]),
                rl.Vector2(points[i + 1][0], points[i + 1][1]),
                2, color
            )

    def _draw_y_labels(self, rect: rl.Rectangle):
        """Draw Y-axis value labels"""
        font_size = 16
        # Top value
        top_text = f"{self._max_value:.1f}"
        rl.draw_text_ex(
            self._font, top_text,
            rl.Vector2(rect.x - 45, rect.y - 8),
            font_size, 0, rl.Color(200, 200, 200, 255)
        )

        # Center (zero)
        rl.draw_text_ex(
            self._font, "0",
            rl.Vector2(rect.x - 20, rect.y + rect.height / 2 - 8),
            font_size, 0, rl.Color(200, 200, 200, 255)
        )

        # Bottom value
        bottom_text = f"-{self._max_value:.1f}"
        rl.draw_text_ex(
            self._font, bottom_text,
            rl.Vector2(rect.x - 45, rect.y + rect.height - 8),
            font_size, 0, rl.Color(200, 200, 200, 255)
        )

    def _draw_legend(self, outer_rect: rl.Rectangle, graph_rect: rl.Rectangle):
        """Draw legend with current values"""
        font_size = 18
        y = outer_rect.y + outer_rect.height - 25

        # Desired
        rl.draw_rectangle(int(graph_rect.x), int(y), 12, 12, self._config.line1_color)
        value_text = f"{self._config.line1_label}: {self._current_desired:.2f}"
        rl.draw_text_ex(
            self._font, value_text,
            rl.Vector2(graph_rect.x + 18, y - 2),
            font_size, 0, rl.WHITE
        )

        # Actual
        x_offset = graph_rect.width / 2
        rl.draw_rectangle(int(graph_rect.x + x_offset), int(y), 12, 12, self._config.line2_color)
        value_text = f"{self._config.line2_label}: {self._current_actual:.2f}"
        rl.draw_text_ex(
            self._font, value_text,
            rl.Vector2(graph_rect.x + x_offset + 18, y - 2),
            font_size, 0, rl.WHITE
        )
