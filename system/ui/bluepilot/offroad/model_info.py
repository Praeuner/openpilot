"""
BluePilot Model Info Widget
Shows currently active driving model name
Port of Qt ModelInfoWidget - exact styling match
"""

import re
import pyray as rl
from collections.abc import Callable
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget


# Colors from Qt stylesheet
COLOR_BG_TOP = rl.Color(44, 44, 44, 255)  # #2c2c2c
COLOR_BG_BOTTOM = rl.Color(26, 26, 26, 255)  # #1a1a1a
COLOR_HOVER_BG_TOP = rl.Color(50, 50, 50, 255)  # #323232
COLOR_HOVER_BG_BOTTOM = rl.Color(31, 31, 31, 255)  # #1f1f1f
COLOR_BORDER = rl.Color(255, 255, 255, 26)  # rgba(255, 255, 255, 0.1)
COLOR_HOVER_BORDER = rl.Color(255, 255, 255, 38)  # rgba(255, 255, 255, 0.15)
COLOR_TITLE = rl.Color(255, 255, 255, 255)  # white
COLOR_MODEL_NAME = rl.Color(24, 180, 255, 255)  # #18b4ff
COLOR_CONTAINER_BG = rl.Color(255, 255, 255, 13)  # rgba(255, 255, 255, 0.05)

# Qt base font sizes
BASE_TITLE_SIZE = 48
BASE_MODEL_SIZE = 38
MIN_MODEL_SIZE = 24

# Default model name from common/model.h
DEFAULT_MODEL = "bd6c60a5-5a36-4907-9542-d00cfc174d78"


class ModelInfoWidget(Widget):
  """
  Displays currently active driving model name.
  Exactly matches Qt ModelInfoWidget styling.
  """

  def __init__(self):
    super().__init__()

    self._model_name = DEFAULT_MODEL
    self._on_click: Callable | None = None
    self._is_hovered = False

    # Fonts - matching Qt font weights
    self._font_title = gui_app.font(FontWeight.SEMI_BOLD)  # 600
    self._font_model = gui_app.font(FontWeight.MEDIUM)  # 500

    self._update_model_name()

  def set_on_click(self, callback: Callable):
    """Set callback for when widget is clicked (opens model settings)"""
    self._on_click = callback

  def _update_model_name(self):
    """Update model name from SubMaster"""
    try:
      sm = ui_state.sm
      if sm.valid.get("modelManagerSP", False):
        model_manager = sm["modelManagerSP"]
        if hasattr(model_manager, 'activeBundle') and model_manager.activeBundle:
          bundle = model_manager.activeBundle
          if hasattr(bundle, 'displayName') and bundle.displayName:
            self._model_name = bundle.displayName
          else:
            self._model_name = DEFAULT_MODEL
        else:
          self._model_name = DEFAULT_MODEL
      else:
        self._model_name = DEFAULT_MODEL
    except Exception:
      self._model_name = DEFAULT_MODEL

  def refresh(self):
    """Refresh model name"""
    self._update_model_name()

  def _format_model_name(self, name: str) -> str:
    """Format model name, inserting line breaks before date patterns (like Qt version)"""
    # Qt uses regex: (\([A-Za-z]+\s+\d{1,2},\s+\d{4}\))
    # Matches patterns like: (October 03, 2023), (November 07, 2023)
    pattern = r'\s+(\([A-Za-z]+\s+\d{1,2},\s+\d{4}\))'
    return re.sub(pattern, r'\n\1', name)

  def _render(self, rect: rl.Rectangle):
    # Update model name periodically
    self._update_model_name()

    # Check hover state
    mouse_pos = rl.get_mouse_position()
    self._is_hovered = rl.check_collision_point_rec(mouse_pos, rect)

    # Handle click
    if self._is_hovered and rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT):
      if self._on_click:
        self._on_click()

    # Draw background with gradient
    self._draw_background(rect)

    # Qt margins: 20, 30, 20, 30 (left, top, right, bottom)
    side_margin = 20
    top_bottom_margin = 30

    # Content area with margins
    content_x = rect.x + side_margin
    content_y = rect.y + top_bottom_margin
    content_width = rect.width - 2 * side_margin
    content_height = rect.height - 2 * top_bottom_margin

    # Title - "Driving Model", white, 48px, semi-bold
    title = "Driving Model"
    title_pos = rl.Vector2(content_x, content_y)
    rl.draw_text_ex(self._font_title, title, title_pos, BASE_TITLE_SIZE, 0, COLOR_TITLE)

    # Model name container - below title with 15px spacing
    spacing = 15
    container_y = content_y + BASE_TITLE_SIZE + spacing
    container_height = content_height - BASE_TITLE_SIZE - spacing
    container_rect = rl.Rectangle(content_x, container_y, content_width, container_height)

    # Draw container background (Qt: rgba(255,255,255,0.05), 12px radius)
    rl.draw_rectangle_rounded(container_rect, 0.12, 10, COLOR_CONTAINER_BG)
    rl.draw_rectangle_rounded_lines_ex(container_rect, 0.12, 10, 1, COLOR_BORDER)

    # Model name - centered, cyan, 38px (scales down to fit)
    formatted_name = self._format_model_name(self._model_name)
    model_size = BASE_MODEL_SIZE

    # Measure and scale down if needed (Qt: container_width - 80)
    container_inner_width = container_rect.width - 40  # Account for padding
    name_measure = measure_text_cached(self._font_model, formatted_name, model_size)
    while name_measure.x > container_inner_width and model_size > MIN_MODEL_SIZE:
      model_size -= 1
      name_measure = measure_text_cached(self._font_model, formatted_name, model_size)

    # Center the model name in the container
    name_x = container_rect.x + (container_rect.width - name_measure.x) / 2
    name_y = container_rect.y + (container_rect.height - name_measure.y) / 2
    rl.draw_text_ex(self._font_model, formatted_name, rl.Vector2(name_x, name_y),
                    model_size, 0, COLOR_MODEL_NAME)

  def _draw_background(self, rect: rl.Rectangle):
    """Draw widget background with gradient (matches Qt stylesheet)"""
    if self._is_hovered:
      bg_color = COLOR_HOVER_BG_TOP
      border_color = COLOR_HOVER_BORDER
    else:
      bg_color = COLOR_BG_TOP
      border_color = COLOR_BORDER

    # Draw rounded rectangle background
    rl.draw_rectangle_rounded(rect, 0.05, 10, bg_color)

    # Draw border (1px solid)
    rl.draw_rectangle_rounded_lines_ex(rect, 0.05, 10, 1, border_color)
