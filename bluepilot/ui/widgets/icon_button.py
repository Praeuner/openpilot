"""
BluePilot Icon Button Widget
Reusable button component with icon and optional label
"""

import pyray as rl
from collections.abc import Callable
from openpilot.system.ui.lib.application import gui_app, MousePos
from openpilot.system.ui.widgets import Widget
from bluepilot.ui.lib.colors import BPColors
from bluepilot.ui.lib.constants import BPConstants


class IconButton(Widget):
  """
  A button with an icon, styled to match BluePilot sidebar.
  """

  def __init__(self, icon_path: str = None, size: int = None):
    super().__init__()
    self._icon_path = icon_path
    self._icon_texture = None
    self._size = size if size else BPConstants.BUTTON_SIZE
    self._on_click: Callable | None = None
    self._scale = 0.55  # Icon scale factor

    if icon_path:
      self._load_icon()

  def _load_icon(self):
    """Load the icon texture"""
    if self._icon_path:
      try:
        self._icon_texture = gui_app.texture(self._icon_path, 120, 120)
      except Exception:
        self._icon_texture = None

  def set_icon(self, icon_path: str):
    """Set or update the icon"""
    self._icon_path = icon_path
    self._load_icon()

  def set_on_click(self, callback: Callable):
    """Set click callback"""
    self._on_click = callback

  def set_scale(self, scale: float):
    """Set icon scale factor"""
    self._scale = scale

  def _handle_mouse_release(self, mouse_pos: MousePos) -> bool:
    if self._on_click and self.enabled:
      self._on_click()
      return True
    return False

  def _render(self, rect: rl.Rectangle) -> None:
    # Determine colors based on state
    bg_color = BPColors.BUTTON_BG_PRESSED if self.is_pressed else BPColors.BUTTON_BG
    border_color = BPColors.with_alpha(BPColors.WHITE, 80)

    # Draw button background
    rl.draw_rectangle_rounded(rect, 0.2, 10, bg_color)

    # Draw border
    rl.draw_rectangle_rounded_lines_ex(rect, 0.2, 10, 2, border_color)

    # Draw icon if available
    if self._icon_texture:
      opacity = 0.65 if self.is_pressed else 1.0

      # Calculate scaled icon dimensions
      scaled_width = int(self._icon_texture.width * self._scale)
      scaled_height = int(self._icon_texture.height * self._scale)

      # Center icon in button
      icon_x = int(rect.x + (rect.width - scaled_width) / 2)
      icon_y = int(rect.y + (rect.height - scaled_height) / 2)

      # Draw with tint for opacity effect
      tint = rl.Color(255, 255, 255, int(255 * opacity))

      # Draw scaled texture
      source_rect = rl.Rectangle(0, 0, self._icon_texture.width, self._icon_texture.height)
      dest_rect = rl.Rectangle(icon_x, icon_y, scaled_width, scaled_height)
      rl.draw_texture_pro(self._icon_texture, source_rect, dest_rect, rl.Vector2(0, 0), 0, tint)


class FanWidget(Widget):
  """
  Animated fan widget with rotation and percentage display.
  Like Qt version, spins briefly when sidebar appears then stops.
  """

  # Animation: spin for ~1.5 rotations (540 degrees) then stop
  SPIN_DURATION_DEGREES = 540.0
  SPIN_SPEED = 6.0  # degrees per frame

  def __init__(self):
    super().__init__()
    self._fan_texture = None
    self._rotation = 0.0
    self._demand_percent = "0%"
    self._is_animating = False
    self._spin_remaining = 0.0  # Degrees left to spin
    self._font = gui_app.font()

    self._load_fan_icon()

  def _load_fan_icon(self):
    """Load the fan texture"""
    try:
      self._fan_texture = gui_app.texture("images/button_fan.png", BPConstants.FAN_SIZE, BPConstants.FAN_SIZE)
    except Exception:
      self._fan_texture = None

  def set_demand(self, demand_percent: str):
    """Update fan demand percentage"""
    self._demand_percent = demand_percent

  def set_rotation(self, angle: float):
    """Set fan rotation angle"""
    self._rotation = angle % 360.0

  def animate_step(self, delta: float = None):
    """Advance animation by delta degrees (uses SPIN_SPEED if not specified)"""
    if not self._is_animating:
      return

    if delta is None:
      delta = self.SPIN_SPEED

    if self._spin_remaining > 0:
      self._rotation = (self._rotation + delta) % 360.0
      self._spin_remaining -= delta
      if self._spin_remaining <= 0:
        self._is_animating = False
        self._spin_remaining = 0

  def start_animation(self):
    """Start brief fan animation (like Qt version - spins briefly then stops)"""
    self._is_animating = True
    self._spin_remaining = self.SPIN_DURATION_DEGREES

  def stop_animation(self):
    """Stop fan animation immediately"""
    self._is_animating = False
    self._spin_remaining = 0

  def _render(self, rect: rl.Rectangle) -> None:
    if not self._fan_texture:
      return

    # Draw rotating fan
    center_x = rect.x + rect.width / 2
    center_y = rect.y + rect.height / 2

    # Source and destination rectangles for rotation
    source = rl.Rectangle(0, 0, self._fan_texture.width, self._fan_texture.height)
    dest = rl.Rectangle(center_x, center_y, rect.width, rect.height)
    origin = rl.Vector2(rect.width / 2, rect.height / 2)

    rl.draw_texture_pro(self._fan_texture, source, dest, origin, self._rotation, BPColors.WHITE)

    # Draw percentage text below fan
    from openpilot.system.ui.lib.text_measure import measure_text_cached
    text_size = measure_text_cached(self._font, self._demand_percent, 28)
    text_x = rect.x + (rect.width - text_size.x) / 2
    text_y = rect.y + rect.height + 5

    rl.draw_text_ex(self._font, self._demand_percent, rl.Vector2(text_x, text_y),
                    28, 0, BPColors.WHITE)
