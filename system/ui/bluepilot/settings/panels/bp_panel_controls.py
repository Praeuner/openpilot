"""
BluePilot Panel Controls
Individual control widgets for BP panels
Ported from Qt bp_panel_controls.h/cc
"""

import math
import re
import pyray as rl
from dataclasses import dataclass, field
from typing import Callable, Optional
from openpilot.common.params import Params
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget

from system.ui.bluepilot.settings.panels.bp_panel_styles import (
  COLOR_BG, COLOR_TITLE, COLOR_TITLE_DISABLED, COLOR_DESC, COLOR_DESC_DISABLED,
  COLOR_BUTTON_BG, COLOR_BUTTON_PRESSED, COLOR_BUTTON_TEXT,
  COLOR_BUTTON_DISABLED_BG, COLOR_BUTTON_DISABLED_TEXT,
  COLOR_SEGMENT_SELECTED, COLOR_SEGMENT_BG, COLOR_SEGMENT_HOVER, COLOR_SEGMENT_DISABLED,
  COLOR_PRIMARY, COLOR_DANGER, COLOR_SUCCESS, COLOR_WARNING,
  TITLE_SIZE, DESC_SIZE, BUTTON_TEXT_SIZE, REASON_LABEL_SIZE,
  SEGMENTED_BUTTON_SIZE, VALUE_DISPLAY_SIZE,
  CONTROL_MIN_HEIGHT, CONTROL_MARGINS, CONTROL_SPACING, TEXT_SPACING,
  TOGGLE_WIDTH, TOGGLE_HEIGHT, BUTTON_WIDTH, BUTTON_HEIGHT,
  parse_hex_color
)


def parse_html_text(text: str) -> str:
  """
  Convert HTML tags to plain text for raylib rendering.
  - <br> and <br/> become newlines
  - <b>, </b>, <i>, </i> etc. are stripped (raylib doesn't support rich text)
  - HTML entities like &nbsp; are converted
  """
  if not text:
    return text

  # Convert <br> and <br/> to newlines
  text = re.sub(r'<br\s*/?>', '\n', text, flags=re.IGNORECASE)

  # Strip common HTML tags (keep content)
  text = re.sub(r'</?(?:b|i|u|strong|em|span|div|p)(?:\s[^>]*)?\s*/?>', '', text, flags=re.IGNORECASE)

  # Convert common HTML entities
  html_entities = {
    '&nbsp;': ' ',
    '&amp;': '&',
    '&lt;': '<',
    '&gt;': '>',
    '&quot;': '"',
    '&#39;': "'",
    '&apos;': "'",
  }
  for entity, char in html_entities.items():
    text = text.replace(entity, char)

  return text


@dataclass
class ControlConfig:
  """Configuration for a single control - matches Qt JSON schema"""
  type: str
  param: str = ""
  title: str = ""
  desc: str = ""
  icon: str = ""
  options: list = field(default_factory=list)
  min_val: float = 0.0
  max_val: float = 1.0
  increment: float = 0.1
  division: float = 1.0
  hidden: bool = False
  visible_conditions: dict = field(default_factory=dict)
  enable_conditions: dict = field(default_factory=dict)

  # Dynamic descriptions (from Qt descriptionConditions)
  descriptions: dict = field(default_factory=dict)
  description_conditions: dict = field(default_factory=dict)
  default_desc: str = ""

  # Dynamic title (Qt enableDynamicTitle)
  dynamic_title: bool = False
  title_when_enabled: str = ""
  title_when_disabled: str = ""

  # Dynamic styling (Qt enableDynamicStyling)
  dynamic_styling: bool = False
  styles: dict = field(default_factory=dict)
  bg_color_when_enabled: str = "#2196F3"
  bg_color_when_disabled: str = "#808080"
  bg_color_when_enabled_pressed: str = ""
  bg_color_when_disabled_pressed: str = ""

  # Dynamic button text (Qt enableDynamicButtonText)
  dynamic_button_text: bool = False
  button_text_when_enabled: str = ""
  button_text_when_disabled: str = ""

  # Confirmation (Qt setConfirmationTexts)
  confirm: bool = False
  confirm_text: str = ""
  confirm_text_on: str = ""
  confirm_text_off: str = ""
  confirm_yes_text: str = "Confirm"
  confirm_no_text: str = "Cancel"

  # Disabled reasons (Qt setDisabledReasons)
  disabled_reasons: list = field(default_factory=list)

  # Command button specific
  command: str = ""
  action: str = ""
  button_text: str = ""
  working_dir: str = ""

  # Nested controls
  groups: list = field(default_factory=list)
  panel_title: str = ""

  # File/path
  path: str = ""
  file: str = ""
  header: str = ""

  # Mutual exclusion
  mutually_exclusive: list = field(default_factory=list)
  needs_restart: bool = False

  # Value display
  value_processor: str = ""
  prefix: str = ""
  suffix: str = ""
  value_color: str = "#0086E9"

  # Default value (for numeric controls when param is empty)
  default: float = 0.0

  # Segmented control specific
  option_descriptions: list = field(default_factory=list)
  show_desc_bottom: bool = False
  per_option_conditions: list = field(default_factory=list)

  # Selection control specific
  hide_description: bool = False

  # Unit handling (for {unit} placeholder substitution)
  unit: str = ""
  unit_metric: str = ""

  # Button styling (from JSON button_style)
  button_style: dict = field(default_factory=dict)


class BPControlBase(Widget):
  """Base class for all BP controls - matches Qt BPControlBase pattern"""

  def __init__(self, config: ControlConfig):
    super().__init__()
    self.config = config
    self._params = Params()
    self._font_title = gui_app.font(FontWeight.MEDIUM)
    self._font_normal = gui_app.font(FontWeight.NORMAL)
    self._enabled = True
    self._visible = True
    self._desc = parse_html_text(config.desc)
    self._disabled_reasons: list = []
    self._show_disabled_reasons = False

  def set_enabled(self, enabled: bool):
    self._enabled = enabled

  def set_visible(self, visible: bool):
    self._visible = visible

  def set_description(self, desc: str):
    self._desc = parse_html_text(desc)

  def set_disabled_reasons(self, reasons: list):
    """Set disabled reasons to show why control is disabled (Qt setDisabledReasons)"""
    self._disabled_reasons = reasons
    self._show_disabled_reasons = bool(reasons)

  def _get_param_bool(self, param: str) -> bool:
    try:
      return self._params.get_bool(param)
    except Exception:
      return False

  def _set_param_bool(self, param: str, value: bool):
    try:
      self._params.put_bool(param, value)
    except Exception as e:
      print(f"BPControl: Failed to set {param}: {e}")

  def _get_param_value(self, param: str) -> str:
    try:
      val = self._params.get(param)
      return val.decode('utf-8') if val else ""
    except Exception:
      return ""

  def _set_param_value(self, param: str, value: str):
    try:
      self._params.put(param, value)
    except Exception as e:
      print(f"BPControl: Failed to set {param}: {e}")

  def _truncate_text(self, text: str, max_width: float, font_size: int) -> str:
    """Truncate text to fit within max_width"""
    text = text.split('\n')[0]
    measure = measure_text_cached(self._font_normal, text, font_size)
    if measure.x <= max_width:
      return text
    while len(text) > 0 and measure.x > max_width:
      text = text[:-1]
      measure = measure_text_cached(self._font_normal, text + "...", font_size)
    return text + "..." if text else ""

  def _get_unit(self) -> str:
    """Get the appropriate unit based on IsMetric param"""
    is_metric = self._get_param_bool("IsMetric")
    if is_metric and self.config.unit_metric:
      return self.config.unit_metric
    return self.config.unit

  def _apply_unit_substitution(self, text: str) -> str:
    """Replace {unit} placeholder with appropriate unit value"""
    if not text or "{unit}" not in text:
      return text
    unit = self._get_unit()
    return text.replace("{unit}", unit)

  def _render_disabled_reasons(self, rect: rl.Rectangle, y_offset: float) -> float:
    """Render disabled reasons UI (Qt style) - returns height used"""
    if not self._show_disabled_reasons or not self._disabled_reasons:
      return 0

    x = rect.x + rect.width - CONTROL_MARGINS - 400
    y = y_offset

    # Draw warning-colored background with border
    reason_text = "• " + "\n• ".join(self._disabled_reasons)
    lines = reason_text.count('\n') + 1
    bg_height = lines * (REASON_LABEL_SIZE + 5) + 20

    bg_rect = rl.Rectangle(x, y, 380, bg_height)
    rl.draw_rectangle_rounded(bg_rect, 0.1, 10, rl.Color(255, 152, 0, 40))
    rl.draw_rectangle_rounded_lines_ex(bg_rect, 0.1, 10, 1, rl.Color(255, 152, 0, 80))

    # Draw reason text
    text_y = y + 10
    for reason in self._disabled_reasons:
      line = f"• {reason}"
      rl.draw_text_ex(self._font_normal, line,
                      rl.Vector2(x + 15, text_y), REASON_LABEL_SIZE, 0, COLOR_WARNING)
      text_y += REASON_LABEL_SIZE + 5

    return bg_height


class BPToggleControl(BPControlBase):
  """
  Toggle control with Qt BPToggleControl styling.
  Layout: [Toggle] ---- [Title + Description]
  Features: dynamic title, dynamic styling, state colors, disabled reasons, compact toggle
  """

  # Compact toggle colors (Qt style)
  TOGGLE_ON_COLOR = rl.Color(51, 171, 76, 255)  # #33ab4c
  TOGGLE_OFF_COLOR = rl.Color(57, 57, 57, 255)  # #393939
  TOGGLE_DISABLED_ON = rl.Color(34, 119, 34, 255)  # #227722
  TOGGLE_DISABLED_OFF = rl.Color(57, 57, 57, 255)
  TOGGLE_KNOB_COLOR = rl.Color(250, 250, 250, 255)  # #fafafa
  TOGGLE_KNOB_DISABLED = rl.Color(136, 136, 136, 255)  # #888888

  def __init__(self, config: ControlConfig):
    super().__init__(config)
    self._state = self._get_param_bool(config.param)
    self._animation_progress = 1.0 if self._state else 0.0
    self.on_toggled: Optional[Callable[[bool], None]] = None

    # Dynamic title support (Qt enableDynamicTitle)
    self._dynamic_title_enabled = config.dynamic_title
    self._title_when_enabled = config.title_when_enabled
    self._title_when_disabled = config.title_when_disabled

    # Dynamic styling support (Qt enableDynamicStyling)
    self._dynamic_styling_enabled = config.dynamic_styling
    self._bg_color_when_enabled = config.bg_color_when_enabled
    self._bg_color_when_disabled = config.bg_color_when_disabled

  def get_state(self) -> bool:
    return self._state

  def set_state(self, state: bool):
    self._state = state
    self._animation_progress = 1.0 if state else 0.0

  def refresh(self):
    """Refresh toggle state from param"""
    self._state = self._get_param_bool(self.config.param)
    self._animation_progress = 1.0 if self._state else 0.0

  def enable_dynamic_title(self, enabled_title: str, disabled_title: str):
    """Enable dynamic title updates based on parameter state (Qt API)"""
    self._dynamic_title_enabled = True
    self._title_when_enabled = enabled_title
    self._title_when_disabled = disabled_title

  def enable_dynamic_styling(self, bg_enabled: str, bg_disabled: str):
    """Enable dynamic styling based on parameter state (Qt API)"""
    self._dynamic_styling_enabled = True
    self._bg_color_when_enabled = bg_enabled
    self._bg_color_when_disabled = bg_disabled

  def _get_current_title(self) -> str:
    """Get title based on dynamic state"""
    if self._dynamic_title_enabled:
      param_value = self._get_param_bool(self.config.param)
      return self._title_when_enabled if param_value else self._title_when_disabled
    return self.config.title

  def _blend_color(self, c1: rl.Color, c2: rl.Color, t: float) -> rl.Color:
    """Blend two colors based on progress"""
    return rl.Color(
      int(c1.r + (c2.r - c1.r) * t),
      int(c1.g + (c2.g - c1.g) * t),
      int(c1.b + (c2.b - c1.b) * t),
      255
    )

  def _render_compact_toggle(self, x: float, y: float, width: float, height: float):
    """Render a compact toggle switch (Qt style, smaller than openpilot default)"""
    # Update animation
    target = 1.0 if self._state else 0.0
    if abs(self._animation_progress - target) > 0.01:
      delta = rl.get_frame_time() * 8.0
      self._animation_progress += delta if self._animation_progress < target else -delta
      self._animation_progress = max(0.0, min(1.0, self._animation_progress))

    # Colors based on enabled state
    if self._enabled:
      bg_color = self._blend_color(self.TOGGLE_OFF_COLOR, self.TOGGLE_ON_COLOR, self._animation_progress)
      knob_color = self.TOGGLE_KNOB_COLOR
    else:
      bg_color = self._blend_color(self.TOGGLE_DISABLED_OFF, self.TOGGLE_DISABLED_ON, self._animation_progress)
      knob_color = self.TOGGLE_KNOB_DISABLED

    # Background track (smaller than default)
    bg_height = height * 0.65
    bg_y = y + (height - bg_height) / 2
    bg_rect = rl.Rectangle(x, bg_y, width, bg_height)
    rl.draw_rectangle_rounded(bg_rect, 1.0, 10, bg_color)

    # Knob
    knob_radius = height / 2
    knob_x = x + knob_radius + (width - height) * self._animation_progress
    knob_y = y + height / 2
    rl.draw_circle(int(knob_x), int(knob_y), knob_radius, knob_color)

  def _render(self, rect: rl.Rectangle):
    height = CONTROL_MIN_HEIGHT
    x, y, width = rect.x, rect.y, rect.width

    # Compact toggle on LEFT side (smaller than openpilot default)
    toggle_x = x + CONTROL_MARGINS
    toggle_y = y + (height - TOGGLE_HEIGHT) // 2
    toggle_rect = rl.Rectangle(toggle_x, toggle_y, TOGGLE_WIDTH, TOGGLE_HEIGHT)

    # Render our own compact toggle
    self._render_compact_toggle(toggle_x, toggle_y, TOGGLE_WIDTH, TOGGLE_HEIGHT)

    # Handle click on toggle area
    mouse_pos = rl.get_mouse_position()
    if self._enabled and rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT):
      if rl.check_collision_point_rec(mouse_pos, toggle_rect):
        self._state = not self._state
        # Handle mutual exclusion
        if self._state and self.config.mutually_exclusive:
          for excluded in self.config.mutually_exclusive:
            self._set_param_bool(excluded, False)
        self._set_param_bool(self.config.param, self._state)
        if self.on_toggled:
          self.on_toggled(self._state)

    # Text on RIGHT side
    text_x = x + CONTROL_MARGINS + TOGGLE_WIDTH + CONTROL_SPACING
    text_width = width - CONTROL_MARGINS * 2 - TOGGLE_WIDTH - CONTROL_SPACING
    text_y = y + CONTROL_MARGINS

    # Title (with dynamic title support)
    title = self._get_current_title()
    title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
    rl.draw_text_ex(self._font_title, title,
                    rl.Vector2(text_x, text_y), TITLE_SIZE, 0, title_color)

    # Description
    if self._desc:
      desc_y = text_y + TITLE_SIZE + TEXT_SPACING
      desc_color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
      desc_text = self._truncate_text(self._desc, text_width, DESC_SIZE)
      rl.draw_text_ex(self._font_normal, desc_text,
                      rl.Vector2(text_x, desc_y), DESC_SIZE, 0, desc_color)

    # Disabled reasons
    if self._show_disabled_reasons:
      reasons_y = y + height - 50
      self._render_disabled_reasons(rect, reasons_y)


class BPSegmentedControl(BPControlBase):
  """
  Segmented control with Qt BPSegmentedControl styling.
  Layout: [Segmented buttons on LEFT] [Title + Description on RIGHT]
  Or with showDescBottom: [Buttons at top] -> [Title] -> [Description list]
  Features: per-button states, description list, disabled reasons, pill-shaped buttons
  """

  def __init__(self, config: ControlConfig):
    super().__init__(config)
    self.on_value_changed: Optional[Callable[[str], None]] = None

    # Per-option enabled states (Qt setButtonEnabled/updateButtonStates)
    self._button_enabled_states: list = [True] * len(config.options)

    # Description list (Qt optionDescriptions)
    self._option_descriptions = config.option_descriptions
    self._use_description_list = bool(self._option_descriptions)
    self._use_bottom_desc_layout = config.show_desc_bottom

  def refresh(self):
    """Refresh value from param"""
    pass  # Value is read dynamically

  def set_button_enabled(self, index: int, enabled: bool):
    """Set enabled state for specific button (Qt API)"""
    if 0 <= index < len(self._button_enabled_states):
      self._button_enabled_states[index] = enabled

  def update_button_states(self, states: list):
    """Update enabled states for all buttons (Qt API)"""
    self._button_enabled_states = states[:len(self.config.options)]

  def _get_formatted_descriptions(self, selected_index: int) -> str:
    """Get formatted description list with selected item highlighted"""
    if not self._use_description_list or not self._option_descriptions:
      return ""

    lines = []
    for i, desc in enumerate(self._option_descriptions):
      if i == selected_index:
        lines.append(f"● {desc}")  # Highlighted
      else:
        lines.append(f"○ {desc}")  # Not highlighted
    return "\n".join(lines)

  def _render(self, rect: rl.Rectangle):
    height = CONTROL_MIN_HEIGHT
    x, y, width = rect.x, rect.y, rect.width

    # Qt style dimensions
    button_height = 80  # Qt: setFixedHeight(80)
    button_min_width = 180  # Qt: setMinimumWidth(180)
    button_padding = 30  # Horizontal padding inside button
    button_radius = 35  # Qt: border-radius 35px for pill shape (half of 80)

    current_value = self._get_param_value(self.config.param)
    mouse_pos = rl.get_mouse_position()
    mouse_clicked = rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT)
    selected_index = 0

    # Calculate button widths based on text content (min 180px per Qt)
    button_widths = []
    for opt in self.config.options:
      opt_name = opt.get("name", "")
      text_measure = measure_text_cached(self._font_title, opt_name, SEGMENTED_BUTTON_SIZE)
      btn_width = max(button_min_width, text_measure.x + button_padding * 2)
      button_widths.append(btn_width)

    total_buttons_width = sum(button_widths)

    if self._use_bottom_desc_layout:
      # Vertical layout: buttons at top, title below, then description list
      btn_x = x + CONTROL_MARGINS
      btn_y = y + CONTROL_MARGINS

      # Draw segmented buttons
      for i, opt in enumerate(self.config.options):
        btn_width = button_widths[i]
        btn_rect = rl.Rectangle(btn_x, btn_y, btn_width, button_height)

        opt_value = str(opt.get("value", i))
        is_selected = (current_value == opt_value) or (not current_value and opt.get("default", False))
        if is_selected:
          selected_index = i

        button_enabled = self._enabled and (i < len(self._button_enabled_states) and self._button_enabled_states[i])
        is_hovered = button_enabled and rl.check_collision_point_rec(mouse_pos, btn_rect)

        # Button color
        if is_selected:
          btn_color = COLOR_SEGMENT_SELECTED
        elif is_hovered:
          btn_color = COLOR_SEGMENT_HOVER
        else:
          btn_color = COLOR_SEGMENT_BG
        if not button_enabled:
          btn_color = COLOR_SEGMENT_DISABLED

        # Draw pill-shaped button (Qt style with 35px radius)
        self._draw_pill_button(btn_x, btn_y, btn_width, button_height, button_radius, i, len(self.config.options), btn_color)

        # Button text
        opt_name = opt.get("name", str(i))
        text_measure = measure_text_cached(self._font_title, opt_name, SEGMENTED_BUTTON_SIZE)
        btn_text_x = btn_x + (btn_width - text_measure.x) / 2
        btn_text_y = btn_y + (button_height - SEGMENTED_BUTTON_SIZE) / 2
        text_color = rl.WHITE if is_selected or button_enabled else COLOR_BUTTON_DISABLED_TEXT
        rl.draw_text_ex(self._font_title, opt_name,
                        rl.Vector2(btn_text_x, btn_text_y), SEGMENTED_BUTTON_SIZE, 0, text_color)

        # Handle click
        if button_enabled and mouse_clicked and is_hovered:
          self._set_param_value(self.config.param, opt_value)
          if self.on_value_changed:
            self.on_value_changed(opt_value)

        btn_x += btn_width  # Flush together

      # Title below buttons
      text_y = btn_y + button_height + 20
      title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
      rl.draw_text_ex(self._font_title, self.config.title,
                      rl.Vector2(x + CONTROL_MARGINS, text_y), TITLE_SIZE, 0, title_color)

      # Description list below title
      list_y = text_y + TITLE_SIZE + 10
      if self._use_description_list:
        for i, desc in enumerate(self._option_descriptions):
          if i == selected_index:
            marker = "●"
            color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
          else:
            marker = "○"
            color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
          line = f"{marker} {desc}"
          rl.draw_text_ex(self._font_normal, line,
                          rl.Vector2(x + CONTROL_MARGINS, list_y), DESC_SIZE, 0, color)
          list_y += DESC_SIZE + 5
      elif self._desc:
        desc_color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
        rl.draw_text_ex(self._font_normal, self._desc,
                        rl.Vector2(x + CONTROL_MARGINS, list_y), DESC_SIZE, 0, desc_color)

    else:
      # Vertical layout: Title at top, buttons below, description at bottom
      text_x = x + CONTROL_MARGINS
      text_y = y + CONTROL_MARGINS
      text_width = width - CONTROL_MARGINS * 2

      # Title at top
      title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
      rl.draw_text_ex(self._font_title, self.config.title,
                      rl.Vector2(text_x, text_y), TITLE_SIZE, 0, title_color)

      # Buttons below title
      btn_x = x + CONTROL_MARGINS
      btn_y = text_y + TITLE_SIZE + 15  # Space between title and buttons

      for i, opt in enumerate(self.config.options):
        btn_width = button_widths[i]
        btn_rect = rl.Rectangle(btn_x, btn_y, btn_width, button_height)

        opt_value = str(opt.get("value", i))
        is_selected = (current_value == opt_value) or (not current_value and opt.get("default", False))
        if is_selected:
          selected_index = i

        button_enabled = self._enabled and (i < len(self._button_enabled_states) and self._button_enabled_states[i])
        is_hovered = button_enabled and rl.check_collision_point_rec(mouse_pos, btn_rect)

        # Button color
        if is_selected:
          btn_color = COLOR_SEGMENT_SELECTED
        elif is_hovered:
          btn_color = COLOR_SEGMENT_HOVER
        else:
          btn_color = COLOR_SEGMENT_BG
        if not button_enabled:
          btn_color = COLOR_SEGMENT_DISABLED

        # Draw pill-shaped button
        self._draw_pill_button(btn_x, btn_y, btn_width, button_height, button_radius, i, len(self.config.options), btn_color)

        # Button text
        opt_name = opt.get("name", str(i))
        text_measure = measure_text_cached(self._font_title, opt_name, SEGMENTED_BUTTON_SIZE)
        btn_text_x = btn_x + (btn_width - text_measure.x) / 2
        btn_text_y = btn_y + (button_height - SEGMENTED_BUTTON_SIZE) / 2
        text_color = rl.WHITE if is_selected or button_enabled else COLOR_BUTTON_DISABLED_TEXT
        rl.draw_text_ex(self._font_title, opt_name,
                        rl.Vector2(btn_text_x, btn_text_y), SEGMENTED_BUTTON_SIZE, 0, text_color)

        # Handle click
        if button_enabled and mouse_clicked and is_hovered:
          self._set_param_value(self.config.param, opt_value)
          if self.on_value_changed:
            self.on_value_changed(opt_value)

        btn_x += btn_width  # Flush together

      # Description below buttons
      if self._desc:
        desc_y = btn_y + button_height + 10
        desc_color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
        desc_text = self._truncate_text(self._desc, text_width, DESC_SIZE)
        rl.draw_text_ex(self._font_normal, desc_text,
                        rl.Vector2(text_x, desc_y), DESC_SIZE, 0, desc_color)

    # Disabled reasons
    if self._show_disabled_reasons:
      self._render_disabled_reasons(rect, y + height - 50)

  def _draw_pill_button(self, x: float, y: float, width: float, height: float,
                        radius: float, index: int, total: int, color: rl.Color):
    """Draw a pill-shaped button segment (Qt style with proper corner radii)"""
    btn_rect = rl.Rectangle(x, y, width, height)

    if total == 1:
      # Single button - full pill
      rl.draw_rectangle_rounded(btn_rect, 0.5, 10, color)
    elif index == 0:
      # First button - left pill end (rounded left, square right)
      # Draw full rounded, then cover right side with square
      rl.draw_rectangle_rounded(btn_rect, radius / height, 10, color)
      rl.draw_rectangle(int(x + width - radius), int(y), int(radius), int(height), color)
    elif index == total - 1:
      # Last button - right pill end (square left, rounded right)
      rl.draw_rectangle_rounded(btn_rect, radius / height, 10, color)
      rl.draw_rectangle(int(x), int(y), int(radius), int(height), color)
    else:
      # Middle button - square
      rl.draw_rectangle(int(x), int(y), int(width), int(height), color)


class BPSelectionControl(BPControlBase):
  """
  Selection control (dropdown-style) with Qt BPButton styling.
  Layout: [Rounded SELECT Button on LEFT] [Title + Selected Value on RIGHT]
  Features: options mapping, selected value display, disabled reasons
  """

  def __init__(self, config: ControlConfig):
    super().__init__(config)
    self.on_click: Optional[Callable[[], None]] = None

    # Options mapping (value -> display name)
    self._options_map: dict = {}
    for opt in config.options:
      value = str(opt.get("value", ""))
      name = opt.get("name", value)
      self._options_map[value] = name

    self._hide_description = config.hide_description

  def set_options(self, options: list):
    """Set options mapping (Qt API)"""
    self._options_map = {}
    for opt in options:
      if isinstance(opt, tuple) and len(opt) == 2:
        self._options_map[opt[1]] = opt[0]  # (display, value)
      elif isinstance(opt, dict):
        value = str(opt.get("value", ""))
        name = opt.get("name", value)
        self._options_map[value] = name

  def _render(self, rect: rl.Rectangle):
    height = CONTROL_MIN_HEIGHT
    x, y, width = rect.x, rect.y, rect.width

    # Qt style button dimensions (matching BPButton)
    btn_height = 80  # Qt: setMinimumHeight(80)
    btn_min_width = 200  # Qt: setMinimumWidth(200)
    btn_padding = 60  # Qt: padding: 15px 30px
    btn_radius = 40  # Qt: border-radius: 40px (fully rounded)
    btn_font_size = 32  # Qt: font-size: 32px

    btn_text = "SELECT"
    text_measure = measure_text_cached(self._font_title, btn_text, btn_font_size)
    btn_width = max(btn_min_width, text_measure.x + btn_padding)

    # SELECT button on LEFT (Qt BPButton style)
    btn_x = x + CONTROL_MARGINS
    btn_y = y + (height - btn_height) // 2
    btn_rect = rl.Rectangle(btn_x, btn_y, btn_width, btn_height)

    mouse_pos = rl.get_mouse_position()
    mouse_clicked = rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT)
    is_hovered = self._enabled and rl.check_collision_point_rec(mouse_pos, btn_rect)

    # Qt button colors: #363636 normal, #404040 hover, #505050 pressed
    if not self._enabled:
      btn_color = rl.Color(32, 32, 32, 255)  # #202020
    elif is_hovered:
      btn_color = rl.Color(64, 64, 64, 255)  # #404040 hover
    else:
      btn_color = rl.Color(54, 54, 54, 255)  # #363636 normal

    # Fully rounded button (Qt: border-radius: 40px)
    rl.draw_rectangle_rounded(btn_rect, btn_radius / btn_height, 10, btn_color)

    # Button text
    text_measure = measure_text_cached(self._font_title, btn_text, btn_font_size)
    btn_text_x = btn_x + (btn_width - text_measure.x) / 2
    btn_text_y = btn_y + (btn_height - btn_font_size) / 2
    text_color = rl.WHITE if self._enabled else rl.Color(102, 102, 102, 255)
    rl.draw_text_ex(self._font_title, btn_text,
                    rl.Vector2(btn_text_x, btn_text_y), btn_font_size, 0, text_color)

    # Title and selected value on RIGHT
    text_x = btn_x + btn_width + CONTROL_SPACING
    text_y = y + CONTROL_MARGINS
    text_width = width - CONTROL_MARGINS * 2 - btn_width - CONTROL_SPACING

    title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
    rl.draw_text_ex(self._font_title, self.config.title,
                    rl.Vector2(text_x, text_y), TITLE_SIZE, 0, title_color)

    # Selected value in blue (Qt style)
    current_value = self._get_param_value(self.config.param)

    # If no value set, find option marked as default
    if not current_value:
      for opt in self.config.options:
        if opt.get("default", False):
          current_value = str(opt.get("value", ""))
          break

    display_name = self._options_map.get(current_value, current_value)
    # Apply {unit} placeholder substitution
    display_name = self._apply_unit_substitution(display_name)

    if display_name:
      value_y = text_y + TITLE_SIZE + TEXT_SPACING
      value_color = COLOR_PRIMARY if self._enabled else COLOR_TITLE_DISABLED
      rl.draw_text_ex(self._font_title, display_name,
                      rl.Vector2(text_x, value_y), DESC_SIZE + 4, 0, value_color)

      # Description below selected value (if not hidden)
      if self._desc and not self._hide_description:
        desc_y = value_y + DESC_SIZE + TEXT_SPACING
        desc_color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
        desc_text = self._truncate_text(self._desc, text_width, DESC_SIZE)
        rl.draw_text_ex(self._font_normal, desc_text,
                        rl.Vector2(text_x, desc_y), DESC_SIZE, 0, desc_color)
    elif self._desc:
      # No value selected - show description as placeholder
      desc_y = text_y + TITLE_SIZE + TEXT_SPACING
      desc_color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
      rl.draw_text_ex(self._font_normal, self._desc,
                      rl.Vector2(text_x, desc_y), DESC_SIZE, 0, desc_color)

    # Handle click
    if self._enabled and mouse_clicked and is_hovered and self.on_click:
      self.on_click()

    # Disabled reasons
    if self._show_disabled_reasons:
      reasons_y = y + height - 50
      self._render_disabled_reasons(rect, reasons_y)


class BPNumericControl(BPControlBase):
  """
  Numeric control with +/- buttons (Qt BPNumericControl style).
  Features: 3D container, min/max labels, division factor for decimal formatting
  """

  def __init__(self, config: ControlConfig):
    super().__init__(config)
    self.on_value_changed: Optional[Callable[[float], None]] = None
    self._is_float = config.type == "float"
    # Calculate decimal places from division factor (Qt: decimals = log10(div))
    self._decimals = int(math.log10(config.division)) if config.division > 1.0 else 0
    # Initialize param if needed
    self._initialize_param()

  def _initialize_param(self):
    """Initialize param with default if empty, and clamp to range"""
    config = self.config
    raw_value = self._get_param_value(config.param)

    if not raw_value:
      # Param is empty - initialize with default
      default_val = max(config.min_val, min(config.max_val, config.default))
      if self._is_float:
        self._set_param_value(config.param, str(default_val))
      else:
        self._set_param_value(config.param, str(int(default_val)))
    else:
      # Param exists - validate it's within range
      try:
        current = float(raw_value)
        clamped = max(config.min_val, min(config.max_val, current))
        if clamped != current:
          # Value was out of range - clamp it
          if self._is_float:
            self._set_param_value(config.param, str(clamped))
          else:
            self._set_param_value(config.param, str(int(clamped)))
      except ValueError:
        # Invalid value - reset to default
        default_val = max(config.min_val, min(config.max_val, config.default))
        if self._is_float:
          self._set_param_value(config.param, str(default_val))
        else:
          self._set_param_value(config.param, str(int(default_val)))

  def refresh(self):
    """Refresh value from param"""
    pass  # Value is read dynamically

  def _render(self, rect: rl.Rectangle):
    height = CONTROL_MIN_HEIGHT + 40  # Taller for numeric controls
    x, y, width = rect.x, rect.y, rect.width
    config = self.config

    # Left side: 3D numeric container (Qt style)
    container_width = 280
    container_height = 140
    container_x = x + CONTROL_MARGINS
    container_y = y + (height - container_height) // 2

    # Container background with shadow effect
    shadow_rect = rl.Rectangle(container_x + 2, container_y + 4, container_width, container_height)
    rl.draw_rectangle_rounded(shadow_rect, 0.15, 10, rl.Color(0, 0, 0, 40))

    container_rect = rl.Rectangle(container_x, container_y, container_width, container_height)
    container_color = rl.Color(54, 54, 54, 255) if self._enabled else rl.Color(32, 32, 32, 255)
    rl.draw_rectangle_rounded(container_rect, 0.15, 10, container_color)
    rl.draw_rectangle_rounded_lines_ex(container_rect, 0.15, 10, 1, rl.Color(64, 64, 64, 255))

    # Get current value (use default from config if param is empty)
    try:
      raw_value = self._get_param_value(config.param)
      if raw_value:
        current_value = float(raw_value)
      else:
        current_value = config.default
    except ValueError:
      current_value = config.default

    btn_size = 60
    value_width = 100

    # Buttons inside container
    inner_y = container_y + 20
    minus_x = container_x + 20

    mouse_pos = rl.get_mouse_position()
    mouse_clicked = rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT)

    # Minus button
    minus_rect = rl.Rectangle(minus_x, inner_y, btn_size, btn_size)
    minus_hover = self._enabled and rl.check_collision_point_rec(mouse_pos, minus_rect)
    minus_color = rl.Color(80, 80, 80, 255) if minus_hover else rl.Color(64, 64, 64, 255)
    if not self._enabled:
      minus_color = rl.Color(37, 37, 37, 255)
    rl.draw_rectangle_rounded(minus_rect, 0.5, 10, minus_color)
    rl.draw_text_ex(self._font_title, "-",
                    rl.Vector2(minus_rect.x + 20, minus_rect.y + 8), 40, 0,
                    rl.WHITE if self._enabled else rl.Color(102, 102, 102, 255))

    # Value display
    value_x = minus_x + btn_size + 15
    display_value = current_value
    if self._is_float:
      # Use division-based decimals (Qt: decimals = log10(div))
      value_text = f"{display_value:.{self._decimals}f}"
    else:
      value_text = str(int(display_value))

    text_measure = measure_text_cached(self._font_title, value_text, 40)
    txt_x = value_x + (value_width - text_measure.x) / 2
    value_color = COLOR_PRIMARY if self._enabled else rl.Color(102, 102, 102, 255)
    rl.draw_text_ex(self._font_title, value_text,
                    rl.Vector2(txt_x, inner_y + 10), 40, 0, value_color)

    # Plus button
    plus_x = value_x + value_width + 15
    plus_rect = rl.Rectangle(plus_x, inner_y, btn_size, btn_size)
    plus_hover = self._enabled and rl.check_collision_point_rec(mouse_pos, plus_rect)
    plus_color = rl.Color(80, 80, 80, 255) if plus_hover else rl.Color(64, 64, 64, 255)
    if not self._enabled:
      plus_color = rl.Color(37, 37, 37, 255)
    rl.draw_rectangle_rounded(plus_rect, 0.5, 10, plus_color)
    rl.draw_text_ex(self._font_title, "+",
                    rl.Vector2(plus_rect.x + 16, plus_rect.y + 8), 40, 0,
                    rl.WHITE if self._enabled else rl.Color(102, 102, 102, 255))

    # Min/Max labels
    labels_y = inner_y + btn_size + 10
    min_color = rl.Color(255, 124, 48, 255) if self._enabled else rl.Color(102, 102, 102, 255)  # Orange
    max_color = rl.Color(80, 211, 50, 255) if self._enabled else rl.Color(102, 102, 102, 255)   # Green

    # Format min/max with proper decimals based on division factor
    if self._is_float and self._decimals > 0:
      min_text = f"Min: {config.min_val:.{self._decimals}f}"
      max_text = f"Max: {config.max_val:.{self._decimals}f}"
    else:
      min_text = f"Min: {int(config.min_val)}"
      max_text = f"Max: {int(config.max_val)}"

    rl.draw_text_ex(self._font_normal, min_text,
                    rl.Vector2(container_x + 20, labels_y), 28, 0, min_color)
    max_measure = measure_text_cached(self._font_normal, max_text, 28)
    rl.draw_text_ex(self._font_normal, max_text,
                    rl.Vector2(container_x + container_width - 20 - max_measure.x, labels_y), 28, 0, max_color)

    # Right side: title and description
    text_x = container_x + container_width + CONTROL_SPACING
    text_width = width - CONTROL_MARGINS - text_x + x
    text_y = y + CONTROL_MARGINS + 20

    title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
    rl.draw_text_ex(self._font_title, config.title,
                    rl.Vector2(text_x, text_y), TITLE_SIZE, 0, title_color)

    if self._desc:
      desc_y = text_y + TITLE_SIZE + TEXT_SPACING
      desc_color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
      desc_text = self._truncate_text(self._desc, text_width, DESC_SIZE)
      rl.draw_text_ex(self._font_normal, desc_text,
                      rl.Vector2(text_x, desc_y), DESC_SIZE, 0, desc_color)

    # Handle clicks
    if self._enabled and mouse_clicked:
      step = config.increment
      min_val = config.min_val
      max_val = config.max_val

      new_value = None
      if rl.check_collision_point_rec(mouse_pos, minus_rect):
        new_value = max(min_val, current_value - step)
      elif rl.check_collision_point_rec(mouse_pos, plus_rect):
        new_value = min(max_val, current_value + step)

      if new_value is not None:
        if self._is_float:
          self._set_param_value(config.param, str(new_value))
        else:
          self._set_param_value(config.param, str(int(new_value)))
        if self.on_value_changed:
          self.on_value_changed(new_value)

    # Disabled reasons
    if self._show_disabled_reasons:
      reasons_y = y + height - 50
      self._render_disabled_reasons(rect, reasons_y)


class BPButtonControl(BPControlBase):
  """
  Generic button control with Qt BPButton styling.
  Layout: [Rounded Button on LEFT] [Title + Description on RIGHT]
  Features: Fully rounded buttons (border-radius: 40px), hover states, custom colors
  """

  def __init__(self, config: ControlConfig, button_text: str = ""):
    super().__init__(config)
    self._button_text = button_text or config.button_text or "OPEN"
    self.on_click: Optional[Callable[[], None]] = None

    # Parse button_style colors
    style = config.button_style
    self._custom_bg_color: Optional[rl.Color] = None
    self._custom_bg_pressed: Optional[rl.Color] = None
    self._custom_text_color: Optional[rl.Color] = None

    if style:
      if "background_color" in style:
        self._custom_bg_color = parse_hex_color(style["background_color"], None)
      if "background_color_pressed" in style:
        self._custom_bg_pressed = parse_hex_color(style["background_color_pressed"], None)
      if "text_color" in style:
        self._custom_text_color = parse_hex_color(style["text_color"], None)

  def set_button_text(self, text: str):
    self._button_text = text

  def _render(self, rect: rl.Rectangle):
    height = CONTROL_MIN_HEIGHT
    x, y, width = rect.x, rect.y, rect.width

    # Qt style button dimensions
    btn_height = 80  # Qt: setMinimumHeight(80)
    btn_min_width = 200  # Qt: setMinimumWidth(200)
    btn_padding = 60  # Qt: padding: 15px 30px (30px * 2)
    btn_radius = 40  # Qt: border-radius: 40px (fully rounded)

    # Calculate button width based on text
    text_measure = measure_text_cached(self._font_title, self._button_text, 32)  # Qt: font-size: 32px
    btn_width = max(btn_min_width, text_measure.x + btn_padding)

    # Button on LEFT (Qt style)
    btn_x = x + CONTROL_MARGINS
    btn_y = y + (height - btn_height) // 2
    btn_rect = rl.Rectangle(btn_x, btn_y, btn_width, btn_height)

    mouse_pos = rl.get_mouse_position()
    mouse_clicked = rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT)
    is_hovered = self._enabled and rl.check_collision_point_rec(mouse_pos, btn_rect)

    # Button colors with custom styling support
    if not self._enabled:
      btn_color = rl.Color(32, 32, 32, 255)  # #202020 disabled
    elif is_hovered:
      if self._custom_bg_pressed:
        btn_color = self._custom_bg_pressed
      elif self._custom_bg_color:
        # Lighten custom color for hover
        btn_color = rl.Color(
          min(255, self._custom_bg_color.r + 20),
          min(255, self._custom_bg_color.g + 20),
          min(255, self._custom_bg_color.b + 20),
          self._custom_bg_color.a
        )
      else:
        btn_color = rl.Color(64, 64, 64, 255)  # #404040 hover
    else:
      btn_color = self._custom_bg_color if self._custom_bg_color else rl.Color(54, 54, 54, 255)

    # Fully rounded button (Qt: border-radius: 40px)
    rl.draw_rectangle_rounded(btn_rect, btn_radius / btn_height, 10, btn_color)

    # Button text (Qt: font-size: 32px)
    btn_font_size = 32
    text_measure = measure_text_cached(self._font_title, self._button_text, btn_font_size)
    btn_text_x = btn_x + (btn_width - text_measure.x) / 2
    btn_text_y = btn_y + (btn_height - btn_font_size) / 2
    if not self._enabled:
      text_color = rl.Color(102, 102, 102, 255)  # #666666 disabled
    elif self._custom_text_color:
      text_color = self._custom_text_color
    else:
      text_color = rl.WHITE
    rl.draw_text_ex(self._font_title, self._button_text,
                    rl.Vector2(btn_text_x, btn_text_y), btn_font_size, 0, text_color)

    # Title and description on RIGHT
    text_x = btn_x + btn_width + CONTROL_SPACING
    text_y = y + CONTROL_MARGINS
    text_width = width - CONTROL_MARGINS * 2 - btn_width - CONTROL_SPACING

    title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
    rl.draw_text_ex(self._font_title, self.config.title,
                    rl.Vector2(text_x, text_y), TITLE_SIZE, 0, title_color)

    if self._desc:
      desc_y = text_y + TITLE_SIZE + TEXT_SPACING
      desc_color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
      desc_text = self._truncate_text(self._desc, text_width, DESC_SIZE)
      rl.draw_text_ex(self._font_normal, desc_text,
                      rl.Vector2(text_x, desc_y), DESC_SIZE, 0, desc_color)

    # Handle click
    if self._enabled and mouse_clicked and is_hovered and self.on_click:
      self.on_click()


class BPStaticParamDisplay(BPControlBase):
  """Static display showing a parameter value"""

  def __init__(self, config: ControlConfig):
    super().__init__(config)

  def _render(self, rect: rl.Rectangle):
    height = CONTROL_MIN_HEIGHT
    x, y, width = rect.x, rect.y, rect.width

    # Get value first to calculate proper right-aligned position
    value = self._get_param_value(self.config.param)
    if not value:
      value = "N/A"  # Show N/A for empty/None params (e.g., DongleId on PC)
    if self.config.prefix:
      value = self.config.prefix + value
    if self.config.suffix:
      value = value + self.config.suffix

    # Measure value text for right alignment
    value_measure = measure_text_cached(self._font_title, value, TITLE_SIZE)
    value_color = parse_hex_color(self.config.value_color)
    value_x = x + width - CONTROL_MARGINS - value_measure.x

    # For controls without description, vertically center both title and value
    if not self._desc:
      center_y = y + (height - TITLE_SIZE) // 2

      # Title on left, centered
      title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
      rl.draw_text_ex(self._font_title, self.config.title,
                      rl.Vector2(x + CONTROL_MARGINS, center_y), TITLE_SIZE, 0, title_color)

      # Value on right, centered
      rl.draw_text_ex(self._font_title, value,
                      rl.Vector2(value_x, center_y), TITLE_SIZE, 0, value_color)
    else:
      # With description: title at top, description below
      text_x = x + CONTROL_MARGINS
      text_y = y + CONTROL_MARGINS
      title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
      rl.draw_text_ex(self._font_title, self.config.title,
                      rl.Vector2(text_x, text_y), TITLE_SIZE, 0, title_color)

      desc_y = text_y + TITLE_SIZE + TEXT_SPACING
      desc_color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
      max_desc_width = width - CONTROL_MARGINS * 2 - value_measure.x - 50
      desc_text = self._truncate_text(self._desc, max_desc_width, DESC_SIZE)
      rl.draw_text_ex(self._font_normal, desc_text,
                      rl.Vector2(text_x, desc_y), DESC_SIZE, 0, desc_color)

      # Value on right, centered
      value_y = y + (height - TITLE_SIZE) // 2
      rl.draw_text_ex(self._font_title, value,
                      rl.Vector2(value_x, value_y), TITLE_SIZE, 0, value_color)


class BPFileParamDisplay(BPControlBase):
  """Display content from a file (like version files)"""

  def __init__(self, config: ControlConfig):
    super().__init__(config)
    self._file_content = ""
    self._load_file_content()

  def _load_file_content(self):
    """Load content from file"""
    import os
    file_path = self.config.file
    if not file_path:
      return

    # Get project root (5 levels up from this file: panels -> settings -> bluepilot -> ui -> system -> root)
    this_dir = os.path.dirname(__file__)
    project_root = os.path.abspath(os.path.join(this_dir, "..", "..", "..", "..", ".."))

    # Normalize the file path - handle relative paths like ../../BPVERSION
    # Strip leading ../ since we're resolving from project root
    clean_path = file_path
    while clean_path.startswith("../"):
      clean_path = clean_path[3:]
    while clean_path.startswith("./"):
      clean_path = clean_path[2:]

    # Try multiple base paths
    base_paths = [
      "/data/openpilot",
      project_root,
    ]

    for base in base_paths:
      full_path = os.path.join(base, clean_path.lstrip('/'))
      if os.path.exists(full_path):
        try:
          with open(full_path, 'r') as f:
            self._file_content = f.read().strip()
          return
        except Exception:
          pass

    # Fallback: show N/A if file not found
    self._file_content = ""

  def _render(self, rect: rl.Rectangle):
    height = CONTROL_MIN_HEIGHT
    x, y, width = rect.x, rect.y, rect.width

    # Get value first (with prefix/suffix)
    value = self._file_content
    if not value:
      value = "N/A"  # Show N/A if file not found
    if self.config.prefix and self._file_content:  # Only add prefix if we have real content
      value = self.config.prefix + value
    if self.config.suffix and self._file_content:  # Only add suffix if we have real content
      value = value + self.config.suffix

    # Measure value text for right alignment
    value_measure = measure_text_cached(self._font_title, value, TITLE_SIZE)
    value_color = parse_hex_color(self.config.value_color)
    value_x = x + width - CONTROL_MARGINS - value_measure.x

    # For controls without description, vertically center both title and value
    if not self._desc:
      center_y = y + (height - TITLE_SIZE) // 2

      # Title on left, centered
      title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
      rl.draw_text_ex(self._font_title, self.config.title,
                      rl.Vector2(x + CONTROL_MARGINS, center_y), TITLE_SIZE, 0, title_color)

      # Value on right, centered
      rl.draw_text_ex(self._font_title, value,
                      rl.Vector2(value_x, center_y), TITLE_SIZE, 0, value_color)
    else:
      # With description: title at top, description below
      text_x = x + CONTROL_MARGINS
      text_y = y + CONTROL_MARGINS
      title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
      rl.draw_text_ex(self._font_title, self.config.title,
                      rl.Vector2(text_x, text_y), TITLE_SIZE, 0, title_color)

      desc_y = text_y + TITLE_SIZE + TEXT_SPACING
      desc_color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
      max_desc_width = width - CONTROL_MARGINS * 2 - value_measure.x - 50
      desc_text = self._truncate_text(self._desc, max_desc_width, DESC_SIZE)
      rl.draw_text_ex(self._font_normal, desc_text,
                      rl.Vector2(text_x, desc_y), DESC_SIZE, 0, desc_color)

      # Value on right, centered
      value_y = y + (height - TITLE_SIZE) // 2
      rl.draw_text_ex(self._font_title, value,
                      rl.Vector2(value_x, value_y), TITLE_SIZE, 0, value_color)


class BPStaticText(BPControlBase):
  """Static text display (informational)"""

  def __init__(self, config: ControlConfig):
    super().__init__(config)

  def _render(self, rect: rl.Rectangle):
    x, y, width = rect.x, rect.y, rect.width

    text_x = x + CONTROL_MARGINS
    text_y = y + CONTROL_MARGINS

    # Title
    title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
    rl.draw_text_ex(self._font_title, self.config.title,
                    rl.Vector2(text_x, text_y), TITLE_SIZE, 0, title_color)

    # Description
    if self._desc:
      desc_y = text_y + TITLE_SIZE + TEXT_SPACING
      desc_color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
      desc_text = self._truncate_text(self._desc, width - CONTROL_MARGINS * 2, DESC_SIZE)
      rl.draw_text_ex(self._font_normal, desc_text,
                      rl.Vector2(text_x, desc_y), DESC_SIZE, 0, desc_color)


class BPParamToggleButton(BPControlBase):
  """
  Button that toggles a boolean parameter (Qt BPParamToggleButton).
  Layout: Vertical - [Title at top] [Description] [Full-width button at bottom]
  Features: dynamic button text, dynamic styling, confirmation texts
  """

  def __init__(self, config: ControlConfig):
    super().__init__(config)
    self.on_toggled: Optional[Callable[[bool], None]] = None

    # Dynamic button text (Qt enableDynamicButtonText)
    self._dynamic_text_enabled = config.dynamic_button_text
    self._button_text_when_enabled = config.button_text_when_enabled
    self._button_text_when_disabled = config.button_text_when_disabled

    # Dynamic styling (Qt enableDynamicStyling)
    self._dynamic_styling_enabled = config.dynamic_styling
    self._bg_color_when_enabled = parse_hex_color(config.bg_color_when_enabled)
    self._bg_color_when_disabled = parse_hex_color(config.bg_color_when_disabled)

    # Confirmation texts (Qt setConfirmationTexts)
    self._require_confirmation = config.confirm
    self._confirm_text_on = config.confirm_text_on or config.confirm_text
    self._confirm_text_off = config.confirm_text_off or config.confirm_text
    self._confirm_yes_text = config.confirm_yes_text
    self._confirm_no_text = config.confirm_no_text

    # Pending confirmation
    self._pending_confirmation = False
    self._confirm_target_state = False

  def enable_dynamic_button_text(self, enabled_text: str, disabled_text: str):
    """Enable dynamic button text based on parameter state (Qt API)"""
    self._dynamic_text_enabled = True
    self._button_text_when_enabled = enabled_text
    self._button_text_when_disabled = disabled_text

  def enable_dynamic_styling(self, bg_enabled: str, bg_disabled: str):
    """Enable dynamic styling based on parameter state (Qt API)"""
    self._dynamic_styling_enabled = True
    self._bg_color_when_enabled = parse_hex_color(bg_enabled)
    self._bg_color_when_disabled = parse_hex_color(bg_disabled)

  def set_confirmation_texts(self, confirm_on: str, confirm_off: str,
                              yes_text: str = "Confirm", no_text: str = "Cancel"):
    """Set different confirmation texts for on/off (Qt API)"""
    self._require_confirmation = True
    self._confirm_text_on = confirm_on
    self._confirm_text_off = confirm_off
    self._confirm_yes_text = yes_text
    self._confirm_no_text = no_text

  def _get_button_text(self) -> str:
    """Get button text based on dynamic state"""
    current_state = self._get_param_bool(self.config.param)
    if self._dynamic_text_enabled:
      return self._button_text_when_enabled if current_state else self._button_text_when_disabled
    return self.config.button_text or ("ON" if current_state else "OFF")

  def _get_button_color(self, is_hovered: bool) -> rl.Color:
    """Get button color based on dynamic state"""
    current_state = self._get_param_bool(self.config.param)
    if self._dynamic_styling_enabled:
      base_color = self._bg_color_when_enabled if current_state else self._bg_color_when_disabled
    else:
      base_color = COLOR_SEGMENT_SELECTED if current_state else COLOR_BUTTON_BG

    if is_hovered:
      # Lighten for hover
      return rl.Color(
        min(255, base_color.r + 20),
        min(255, base_color.g + 20),
        min(255, base_color.b + 20),
        base_color.a
      )
    return base_color

  def _render(self, rect: rl.Rectangle):
    x, y, width = rect.x, rect.y, rect.width

    # Qt style: Vertical layout - title, description, then full-width button
    text_x = x + CONTROL_MARGINS
    text_y = y + CONTROL_MARGINS
    content_width = width - CONTROL_MARGINS * 2

    # Title
    title_color = COLOR_TITLE if self._enabled else COLOR_TITLE_DISABLED
    rl.draw_text_ex(self._font_title, self.config.title,
                    rl.Vector2(text_x, text_y), TITLE_SIZE, 0, title_color)

    # Description
    desc_y = text_y + TITLE_SIZE + TEXT_SPACING
    if self._desc:
      desc_color = COLOR_DESC if self._enabled else COLOR_DESC_DISABLED
      rl.draw_text_ex(self._font_normal, self._desc,
                      rl.Vector2(text_x, desc_y), DESC_SIZE, 0, desc_color)
      desc_y += DESC_SIZE + 15
    else:
      desc_y += 10

    # Full-width button below (Qt style)
    button_text = self._get_button_text()
    btn_height = 150  # Qt: setMinimumHeight(150)
    btn_width = content_width
    btn_x = text_x
    btn_y = desc_y
    btn_rect = rl.Rectangle(btn_x, btn_y, btn_width, btn_height)

    mouse_pos = rl.get_mouse_position()
    mouse_clicked = rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT)
    is_hovered = self._enabled and rl.check_collision_point_rec(mouse_pos, btn_rect)

    btn_bg = self._get_button_color(is_hovered) if self._enabled else COLOR_BUTTON_DISABLED_BG

    # Qt: border-radius: 20px
    rl.draw_rectangle_rounded(btn_rect, 20 / btn_height, 10, btn_bg)

    # Button text (Qt: font-size from buttonTextSize)
    text_measure = measure_text_cached(self._font_title, button_text, BUTTON_TEXT_SIZE)
    btn_text_x = btn_x + (btn_width - text_measure.x) / 2
    btn_text_y = btn_y + (btn_height - BUTTON_TEXT_SIZE) / 2
    rl.draw_text_ex(self._font_title, button_text,
                    rl.Vector2(btn_text_x, btn_text_y), BUTTON_TEXT_SIZE, 0, COLOR_BUTTON_TEXT)

    # Handle click
    if self._enabled and mouse_clicked and is_hovered:
      current_state = self._get_param_bool(self.config.param)
      new_state = not current_state

      # TODO: Add confirmation dialog support when modal manager is available
      # For now, toggle directly
      self._set_param_bool(self.config.param, new_state)
      if self.on_toggled:
        self.on_toggled(new_state)

    # Disabled reasons
    if self._show_disabled_reasons:
      reasons_y = btn_y + btn_height + 10
      self._render_disabled_reasons(rect, reasons_y)


# Factory function to create controls from config
def create_control(config: ControlConfig) -> Optional[BPControlBase]:
  """Create a control widget from configuration"""
  control_map = {
    "toggle": BPToggleControl,
    "segmented_control": BPSegmentedControl,
    "selection": BPSelectionControl,
    "integer": BPNumericControl,
    "float": BPNumericControl,
    "command_button": lambda c: BPButtonControl(c, c.button_text or "EXECUTE"),
    "nested_controls_button": lambda c: BPButtonControl(c, c.button_text or "OPEN"),
    "param_viewer": lambda c: BPButtonControl(c, "VIEW"),
    "param_list_viewer": lambda c: BPButtonControl(c, "VIEW ALL"),
    "file_viewer": lambda c: BPButtonControl(c, "VIEW"),
    "file_param_display": BPFileParamDisplay,  # For file-based param displays
    "html_viewer": lambda c: BPButtonControl(c, "VIEW"),
    "recent_changes": lambda c: BPButtonControl(c, "VIEW"),
    "restart_ui": lambda c: BPButtonControl(c, c.button_text or "RESTART UI"),
    "text_input": lambda c: BPButtonControl(c, c.button_text or "EDIT"),
    "static_param_display": BPStaticParamDisplay,
    "static_text": BPStaticText,
    "param_toggle_button": BPParamToggleButton,
  }

  creator = control_map.get(config.type)
  if creator:
    if callable(creator):
      return creator(config)
  return None
