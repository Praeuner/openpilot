"""
BluePilot Drive Stats Widget
Shows cumulative driving statistics (all time and past week)
Port of Qt DriveStats widget - exact styling match
"""

import json
import pyray as rl
from openpilot.common.params import Params
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget


# Qt styling constants
MILE_TO_KM = 1.609344

# Colors from Qt stylesheet
COLOR_BG_TOP = rl.Color(44, 44, 44, 255)  # #2c2c2c
COLOR_BG_BOTTOM = rl.Color(26, 26, 26, 255)  # #1a1a1a
COLOR_BORDER = rl.Color(255, 255, 255, 26)  # rgba(255, 255, 255, 0.1)
COLOR_TITLE = rl.Color(255, 255, 255, 255)  # white
COLOR_NUMBER = rl.Color(24, 180, 255, 255)  # #18b4ff
COLOR_UNIT = rl.Color(176, 176, 176, 255)  # #b0b0b0
COLOR_CONTAINER_BG = rl.Color(255, 255, 255, 13)  # rgba(255, 255, 255, 0.05)

# Qt base font sizes
BASE_TITLE_SIZE = 48
BASE_NUMBER_SIZE = 66
BASE_UNIT_SIZE = 42
BASE_HEIGHT = 550.0


class DriveStats(Widget):
  """
  Displays cumulative driving statistics in two sections: ALL TIME and PAST WEEK.
  Exactly matches Qt DriveStats widget styling.
  """

  def __init__(self):
    super().__init__()
    self.params = Params()

    # Stats data
    self._all_time = {"routes": 0, "distance": 0.0, "hours": 0.0}
    self._week = {"routes": 0, "distance": 0.0, "hours": 0.0}

    # Metric setting
    self._is_metric = False

    # Fonts - matching Qt font weights
    self._font_title = gui_app.font(FontWeight.SEMI_BOLD)  # 600
    self._font_number = gui_app.font(FontWeight.BOLD)  # 700
    self._font_unit = gui_app.font(FontWeight.NORMAL)  # 400

    self._load_stats()

  def _load_stats(self):
    """Load stats from params"""
    try:
      self._is_metric = self.params.get_bool("IsMetric")
    except Exception:
      self._is_metric = False

    # Try to load cached API stats
    try:
      stats_json = self.params.get("ApiCache_DriveStats")
      if stats_json:
        data = json.loads(stats_json)
        if "all" in data:
          self._all_time = {
            "routes": int(data["all"].get("routes", 0)),
            "distance": data["all"].get("distance", 0.0),
            "hours": data["all"].get("minutes", 0.0) / 60.0
          }
        if "week" in data:
          self._week = {
            "routes": int(data["week"].get("routes", 0)),
            "distance": data["week"].get("distance", 0.0),
            "hours": data["week"].get("minutes", 0.0) / 60.0
          }
    except Exception:
      pass

  def refresh(self):
    """Refresh stats from params"""
    self._load_stats()

  def _format_distance(self, distance_miles: float) -> str:
    """Format distance in appropriate units - matches Qt exactly"""
    if self._is_metric:
      km = distance_miles * MILE_TO_KM
      return str(int(km))
    else:
      return str(int(distance_miles))

  def _format_hours(self, hours: float) -> str:
    """Format hours as integer like Qt version"""
    return str(int(hours))

  def _render(self, rect: rl.Rectangle):
    # Calculate scale factor based on height (Qt: height / 550.0)
    scale = rect.height / BASE_HEIGHT
    scale = max(0.35, min(1.0, scale))

    # Calculate scaled sizes with min/max clamping like Qt
    title_size = max(20, min(BASE_TITLE_SIZE, int(BASE_TITLE_SIZE * scale)))
    number_size = max(26, min(BASE_NUMBER_SIZE, int(BASE_NUMBER_SIZE * scale)))
    unit_size = max(18, min(BASE_UNIT_SIZE, int(BASE_UNIT_SIZE * scale)))

    # Scaled margins/spacing (Qt: 20, 30, 20, 30 margins, 10 spacing)
    top_bottom_margin = max(10, min(30, int(30 * scale)))
    side_margin = max(10, min(20, int(20 * scale)))
    main_spacing = max(3, min(10, int(10 * scale)))

    # Draw background with gradient and border
    self._draw_background(rect)

    # Content area with margins
    content_x = rect.x + side_margin
    content_y = rect.y + top_bottom_margin
    content_width = rect.width - 2 * side_margin
    content_height = rect.height - 2 * top_bottom_margin

    # Two sections: ALL TIME and PAST WEEK
    section_height = (content_height - main_spacing - 5) / 2  # 5 is extra spacing between sections

    # ALL TIME section
    all_time_rect = rl.Rectangle(content_x, content_y, content_width, section_height)
    self._render_section("ALL TIME", self._all_time, all_time_rect, title_size, number_size, unit_size, main_spacing)

    # PAST WEEK section
    week_y = content_y + section_height + main_spacing + 5
    week_rect = rl.Rectangle(content_x, week_y, content_width, section_height)
    self._render_section("PAST WEEK", self._week, week_rect, title_size, number_size, unit_size, main_spacing)

  def _draw_background(self, rect: rl.Rectangle):
    """Draw gradient background matching Qt stylesheet"""
    # Qt uses: qlineargradient from #2c2c2c to #1a1a1a
    # Draw base rounded rectangle
    rl.draw_rectangle_rounded(rect, 0.05, 10, COLOR_BG_TOP)

    # Draw border (1px solid rgba(255, 255, 255, 0.1))
    rl.draw_rectangle_rounded_lines_ex(rect, 0.05, 10, 1, COLOR_BORDER)

  def _render_section(self, title: str, stats: dict, rect: rl.Rectangle,
                      title_size: int, number_size: int, unit_size: int, spacing: int):
    """Render a stats section (ALL TIME or PAST WEEK) matching Qt layout"""
    # Title - white, semi-bold
    title_pos = rl.Vector2(rect.x, rect.y)
    rl.draw_text_ex(self._font_title, title, title_pos, title_size, 0, COLOR_TITLE)

    # Stats container background - below title
    container_y = rect.y + title_size + spacing
    container_height = rect.height - title_size - spacing
    container_rect = rl.Rectangle(rect.x, container_y, rect.width, container_height)

    # Draw container with background and border (Qt: rgba(255,255,255,0.05), 12px radius)
    rl.draw_rectangle_rounded(container_rect, 0.12, 10, COLOR_CONTAINER_BG)
    rl.draw_rectangle_rounded_lines_ex(container_rect, 0.12, 10, 1, COLOR_BORDER)

    # Three columns: Drives, Distance, Hours - centered in container
    col_width = rect.width / 3
    col_center_y = container_y + container_height / 2

    # Drives column
    self._render_stat_column(
      rect.x + col_width * 0.5,
      col_center_y,
      str(stats["routes"]),
      "Drives",
      number_size, unit_size
    )

    # Distance column
    distance_text = self._format_distance(stats["distance"])
    distance_unit = "KM" if self._is_metric else "Miles"
    self._render_stat_column(
      rect.x + col_width * 1.5,
      col_center_y,
      distance_text,
      distance_unit,
      number_size, unit_size
    )

    # Hours column
    self._render_stat_column(
      rect.x + col_width * 2.5,
      col_center_y,
      self._format_hours(stats["hours"]),
      "Hours",
      number_size, unit_size
    )

  def _render_stat_column(self, center_x: float, center_y: float, value: str, unit: str,
                          number_size: int, unit_size: int):
    """Render a single stat column (centered) matching Qt layout"""
    spacing = 5  # Qt spacing between number and unit

    # Calculate total height of number + spacing + unit
    total_height = number_size + spacing + unit_size

    # Value (number) - centered, cyan, bold
    value_measure = measure_text_cached(self._font_number, value, number_size)
    value_x = center_x - value_measure.x / 2
    value_y = center_y - total_height / 2
    rl.draw_text_ex(self._font_number, value, rl.Vector2(value_x, value_y),
                    number_size, 0, COLOR_NUMBER)

    # Unit label - centered below value, gray
    unit_measure = measure_text_cached(self._font_unit, unit, unit_size)
    unit_x = center_x - unit_measure.x / 2
    unit_y = value_y + number_size + spacing
    rl.draw_text_ex(self._font_unit, unit, rl.Vector2(unit_x, unit_y),
                    unit_size, 0, COLOR_UNIT)
