"""
BluePilot Panel Dialogs
Modal dialog widgets for BP panels
Ported from Qt bp_panel_dialogs.h/cc
"""

import re
import pyray as rl
from typing import Callable, Optional
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.scroll_panel import GuiScrollPanel
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget, DialogResult

from system.ui.bluepilot.settings.panels.bp_panel_styles import (
  COLOR_BG, COLOR_TITLE, COLOR_DESC, COLOR_PRIMARY,
  COLOR_BUTTON_TEXT, TITLE_SIZE, DESC_SIZE,
  DIALOG_TITLE_SIZE, DIALOG_OPTION_SIZE, DIALOG_BUTTON_SIZE,
  parse_hex_color
)


def parse_html_text(text: str) -> str:
  """Convert HTML tags to plain text for raylib rendering."""
  if not text:
    return text
  # Convert <br> and <br/> to newlines
  text = re.sub(r'<br\s*/?>', '\n', text, flags=re.IGNORECASE)
  # Strip common HTML tags
  text = re.sub(r'</?(?:b|i|u|strong|em|span|div|p)(?:\s[^>]*)?\s*/?>', '', text, flags=re.IGNORECASE)
  # Convert common HTML entities
  for entity, char in {'&nbsp;': ' ', '&amp;': '&', '&lt;': '<', '&gt;': '>', '&quot;': '"'}.items():
    text = text.replace(entity, char)
  return text


# Dialog constants (from Qt bp_panel_dialogs.cc)
DIALOG_BG_COLOR = rl.Color(36, 36, 36, 255)  # #242424 - matches Qt container
DIALOG_OVERLAY_COLOR = rl.Color(0, 0, 0, 190)  # rgba(0, 0, 0, 0.75)
DIALOG_CONTAINER_WIDTH = 1800  # Qt: container->setFixedWidth(1800)
DIALOG_CONTAINER_RADIUS = 20  # Qt: border-radius: 20px
DIALOG_PADDING = 40  # Qt: containerLayout margins (40, 40, 40, 40)
DIALOG_SPACING = 30  # Qt: containerLayout->setSpacing(30)

# Option button styling (from Qt)
OPTION_BG_COLOR = rl.Color(54, 54, 54, 255)  # #363636
OPTION_SELECTED_COLOR = rl.Color(33, 150, 243, 255)  # #2196F3
OPTION_PRESSED_COLOR = rl.Color(25, 118, 210, 255)  # #1976D2
OPTION_HEIGHT = 100  # Qt: padding: 30px on each side
OPTION_SPACING = 20  # Qt: buttonLayout->setSpacing(20)
OPTION_RADIUS = 15  # Qt: border-radius: 15px

# Action button styling
ACTION_BUTTON_HEIGHT = 120  # Qt: padding: 30px 60px
ACTION_BUTTON_MIN_WIDTH = 300  # Qt: min-width: 300px
ACTION_BUTTON_SPACING = 20  # Qt: actionButtonLayout->setSpacing(20)
ACTION_CANCEL_COLOR = rl.Color(64, 64, 64, 255)  # #404040
ACTION_CANCEL_HOVER = rl.Color(80, 80, 80, 255)  # #505050


class BPConfirmationDialog(Widget):
  """
  Confirmation dialog with title, message, and confirm/cancel buttons.
  Matches Qt BPConfirmationDialog styling - scaled up 25%.
  """

  def __init__(self, title: str, message: str, confirm_text: str = "Yes",
               cancel_text: str = "No", confirm_color: str = "#2196F3"):
    super().__init__()
    self.title = title
    self.message = parse_html_text(message)
    self.confirm_text = confirm_text
    self.cancel_text = cancel_text
    self.confirm_color = parse_hex_color(confirm_color, COLOR_PRIMARY)
    self.result: Optional[DialogResult] = None
    self._font_title = gui_app.font(FontWeight.BOLD)
    self._font_normal = gui_app.font(FontWeight.NORMAL)

  def _render(self, rect: rl.Rectangle):
    # Qt scaled sizes: container width 1750px (was 1400 * 1.25)
    container_width = 1750
    container_height = 500  # Auto-size based on content

    # Center the container
    container_x = rect.x + (rect.width - container_width) / 2
    container_y = rect.y + (rect.height - container_height) / 2
    container_rect = rl.Rectangle(container_x, container_y, container_width, container_height)

    # Draw container background (Qt: border-radius: 25px)
    rl.draw_rectangle_rounded(container_rect, 0.03, 20, DIALOG_BG_COLOR)

    content_x = container_x + 75  # Qt scaled: was 60
    content_y = container_y + 75
    content_width = container_width - 150

    # Title (Qt: font-size: 75px, font-weight: 600)
    rl.draw_text_ex(self._font_title, self.title,
                    rl.Vector2(content_x + (content_width - measure_text_cached(self._font_title, self.title, 75).x) / 2, content_y),
                    75, 0, COLOR_TITLE)

    # Message (Qt: font-size: 63px, padding: 25px)
    message_y = content_y + 75 + 50
    msg_measure = measure_text_cached(self._font_normal, self.message, 63)
    rl.draw_text_ex(self._font_normal, self.message,
                    rl.Vector2(content_x + (content_width - msg_measure.x) / 2, message_y),
                    63, 0, COLOR_DESC)

    # Buttons (Qt scaled: padding: 38px 75px, font-size: 63px, min-width: 375px)
    button_y = container_y + container_height - 75 - ACTION_BUTTON_HEIGHT
    button_width = max(375, (content_width - ACTION_BUTTON_SPACING) / 2)

    mouse_pos = rl.get_mouse_position()
    mouse_clicked = rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT)

    # Cancel button
    cancel_rect = rl.Rectangle(content_x, button_y, button_width, ACTION_BUTTON_HEIGHT)
    cancel_hover = rl.check_collision_point_rec(mouse_pos, cancel_rect)
    cancel_color = ACTION_CANCEL_HOVER if cancel_hover else ACTION_CANCEL_COLOR
    rl.draw_rectangle_rounded(cancel_rect, 0.15, 10, cancel_color)

    cancel_measure = measure_text_cached(self._font_title, self.cancel_text, 63)
    rl.draw_text_ex(self._font_title, self.cancel_text,
                    rl.Vector2(cancel_rect.x + (button_width - cancel_measure.x) / 2,
                               button_y + (ACTION_BUTTON_HEIGHT - 63) / 2),
                    63, 0, COLOR_BUTTON_TEXT)

    # Confirm button
    confirm_rect = rl.Rectangle(content_x + button_width + ACTION_BUTTON_SPACING, button_y, button_width, ACTION_BUTTON_HEIGHT)
    confirm_hover = rl.check_collision_point_rec(mouse_pos, confirm_rect)
    confirm_bg = OPTION_PRESSED_COLOR if confirm_hover else self.confirm_color
    rl.draw_rectangle_rounded(confirm_rect, 0.15, 10, confirm_bg)

    confirm_measure = measure_text_cached(self._font_title, self.confirm_text, 63)
    rl.draw_text_ex(self._font_title, self.confirm_text,
                    rl.Vector2(confirm_rect.x + (button_width - confirm_measure.x) / 2,
                               button_y + (ACTION_BUTTON_HEIGHT - 63) / 2),
                    63, 0, COLOR_BUTTON_TEXT)

    # Handle keyboard
    if rl.is_key_pressed(rl.KeyboardKey.KEY_ENTER):
      self.result = DialogResult.CONFIRM
    elif rl.is_key_pressed(rl.KeyboardKey.KEY_ESCAPE):
      self.result = DialogResult.CANCEL

    # Handle mouse clicks
    if mouse_clicked:
      if cancel_hover:
        self.result = DialogResult.CANCEL
      elif confirm_hover:
        self.result = DialogResult.CONFIRM

    return self.result


class BPSelectionDialog(Widget):
  """
  Selection dialog with scrollable list of options.
  Matches Qt BPSelectionDialog styling - full width container (1800px).
  """

  def __init__(self, title: str, options: list, current_value: str = ""):
    super().__init__()
    self.title = title
    self.options = options
    self.current_value = current_value
    self.selected_value = current_value
    self.result: Optional[int] = None  # None = pending, 0 = cancel, 1 = select
    self._scroll = GuiScrollPanel()
    self._font_title = gui_app.font(FontWeight.BOLD)
    self._font_normal = gui_app.font(FontWeight.MEDIUM)

  def _render(self, rect: rl.Rectangle):
    # Qt: container->setFixedWidth(1800), centered in full screen
    container_width = DIALOG_CONTAINER_WIDTH
    # Calculate a reasonable height (90% of screen height, max 900px)
    max_container_height = min(rect.height * 0.9, 900)
    container_height = max_container_height

    # Center both horizontally and vertically
    container_x = rect.x + (rect.width - container_width) / 2
    container_y = rect.y + (rect.height - container_height) / 2
    container_rect = rl.Rectangle(container_x, container_y, container_width, container_height)

    # Draw container background (Qt: background-color: #242424, border-radius: 20px)
    rl.draw_rectangle_rounded(container_rect, 0.01, 20, DIALOG_BG_COLOR)

    content_x = container_x + DIALOG_PADDING
    content_y = container_y + DIALOG_PADDING
    content_width = container_width - DIALOG_PADDING * 2

    # Title (Qt: font-size: 60px, font-weight: 600, centered)
    title_measure = measure_text_cached(self._font_title, self.title, DIALOG_TITLE_SIZE)
    rl.draw_text_ex(self._font_title, self.title,
                    rl.Vector2(content_x + (content_width - title_measure.x) / 2, content_y),
                    DIALOG_TITLE_SIZE, 0, COLOR_TITLE)

    # Options area (with scroll)
    options_y = content_y + DIALOG_TITLE_SIZE + DIALOG_SPACING
    options_height = container_height - DIALOG_TITLE_SIZE - ACTION_BUTTON_HEIGHT - DIALOG_PADDING * 4 - DIALOG_SPACING * 2
    view_rect = rl.Rectangle(content_x, options_y, content_width, options_height)

    # Calculate content height
    content_h = len(self.options) * (OPTION_HEIGHT + OPTION_SPACING)
    list_content_rect = rl.Rectangle(content_x, options_y, content_width, content_h)

    # Handle scrolling
    offset = self._scroll.handle_scroll(view_rect, list_content_rect)
    valid_click = self._scroll.is_touch_valid() and rl.is_mouse_button_released(rl.MouseButton.MOUSE_BUTTON_LEFT)
    mouse_pos = rl.get_mouse_position()

    # Draw options
    rl.begin_scissor_mode(int(view_rect.x), int(options_y), int(view_rect.width), int(options_height))

    for i, option in enumerate(self.options):
      item_y = options_y + i * (OPTION_HEIGHT + OPTION_SPACING) + offset.y
      # Qt: margin: 8px on each side
      item_rect = rl.Rectangle(view_rect.x + 8, item_y, view_rect.width - 16, OPTION_HEIGHT)

      if item_y + OPTION_HEIGHT > options_y and item_y < options_y + options_height:
        is_selected = (option == self.selected_value)
        is_hovered = rl.check_collision_point_rec(mouse_pos, item_rect) and rl.check_collision_point_rec(mouse_pos, view_rect)

        # Item background (Qt: #363636 normal, #2196F3 checked, #1976D2 pressed)
        if is_selected:
          item_color = OPTION_SELECTED_COLOR
        elif is_hovered:
          item_color = OPTION_PRESSED_COLOR
        else:
          item_color = OPTION_BG_COLOR

        rl.draw_rectangle_rounded(item_rect, 0.15, 10, item_color)

        # Item text (Qt: font-size: 50px, text-align: left, padding: 30px)
        text_y = item_y + (OPTION_HEIGHT - DIALOG_OPTION_SIZE) / 2
        rl.draw_text_ex(self._font_normal, option,
                        rl.Vector2(item_rect.x + 30, text_y),
                        DIALOG_OPTION_SIZE, 0, COLOR_BUTTON_TEXT)

        # Handle selection
        if valid_click and is_hovered:
          self.selected_value = option

    rl.end_scissor_mode()

    # Action buttons (Qt: padding: 30px 60px, font-size: 50px)
    button_y = container_y + container_height - ACTION_BUTTON_HEIGHT - DIALOG_PADDING
    button_width = (content_width - ACTION_BUTTON_SPACING) / 2

    mouse_clicked = rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT)

    # Cancel button
    cancel_rect = rl.Rectangle(content_x, button_y, button_width, ACTION_BUTTON_HEIGHT)
    cancel_hover = rl.check_collision_point_rec(mouse_pos, cancel_rect)
    cancel_color = ACTION_CANCEL_HOVER if cancel_hover else ACTION_CANCEL_COLOR
    rl.draw_rectangle_rounded(cancel_rect, 0.15, 10, cancel_color)

    cancel_text = "Cancel"
    cancel_measure = measure_text_cached(self._font_title, cancel_text, DIALOG_BUTTON_SIZE)
    rl.draw_text_ex(self._font_title, cancel_text,
                    rl.Vector2(cancel_rect.x + (button_width - cancel_measure.x) / 2,
                               button_y + (ACTION_BUTTON_HEIGHT - DIALOG_BUTTON_SIZE) / 2),
                    DIALOG_BUTTON_SIZE, 0, COLOR_BUTTON_TEXT)

    # Select button (enabled when selection differs from current)
    can_select = bool(self.selected_value)  # Enable if anything is selected
    select_rect = rl.Rectangle(content_x + button_width + ACTION_BUTTON_SPACING, button_y, button_width, ACTION_BUTTON_HEIGHT)
    select_hover = can_select and rl.check_collision_point_rec(mouse_pos, select_rect)

    if can_select:
      select_color = OPTION_PRESSED_COLOR if select_hover else OPTION_SELECTED_COLOR
      text_color = COLOR_BUTTON_TEXT
    else:
      select_color = rl.Color(64, 64, 64, 255)  # #404040 disabled
      text_color = rl.Color(136, 136, 136, 255)  # #888888 disabled text

    rl.draw_rectangle_rounded(select_rect, 0.15, 10, select_color)

    select_text = "Select"
    select_measure = measure_text_cached(self._font_title, select_text, DIALOG_BUTTON_SIZE)
    rl.draw_text_ex(self._font_title, select_text,
                    rl.Vector2(select_rect.x + (button_width - select_measure.x) / 2,
                               button_y + (ACTION_BUTTON_HEIGHT - DIALOG_BUTTON_SIZE) / 2),
                    DIALOG_BUTTON_SIZE, 0, text_color)

    # Handle keyboard
    if rl.is_key_pressed(rl.KeyboardKey.KEY_ESCAPE):
      self.result = 0
    elif rl.is_key_pressed(rl.KeyboardKey.KEY_ENTER) and can_select:
      self.result = 1

    # Handle mouse clicks
    if mouse_clicked:
      if cancel_hover:
        self.result = 0
      elif select_hover and can_select:
        self.result = 1

    return self.result if self.result is not None else -1


class BPAlertDialog(Widget):
  """Simple alert dialog with single button (Qt: single button mode)"""

  def __init__(self, title: str, message: str, button_text: str = "OK"):
    super().__init__()
    self.title = title
    self.message = message
    self.button_text = button_text
    self.result: Optional[DialogResult] = None
    self._font_title = gui_app.font(FontWeight.BOLD)
    self._font_normal = gui_app.font(FontWeight.NORMAL)

  def _render(self, rect: rl.Rectangle):
    # Similar to confirmation but single button
    container_width = 1400
    container_height = 400

    container_x = rect.x + (rect.width - container_width) / 2
    container_y = rect.y + (rect.height - container_height) / 2
    container_rect = rl.Rectangle(container_x, container_y, container_width, container_height)

    rl.draw_rectangle_rounded(container_rect, 0.03, 20, DIALOG_BG_COLOR)

    content_x = container_x + 60
    content_y = container_y + 60
    content_width = container_width - 120

    # Title
    title_measure = measure_text_cached(self._font_title, self.title, 75)
    rl.draw_text_ex(self._font_title, self.title,
                    rl.Vector2(content_x + (content_width - title_measure.x) / 2, content_y),
                    75, 0, COLOR_TITLE)

    # Message
    message_y = content_y + 75 + 40
    msg_measure = measure_text_cached(self._font_normal, self.message, 50)
    rl.draw_text_ex(self._font_normal, self.message,
                    rl.Vector2(content_x + (content_width - msg_measure.x) / 2, message_y),
                    50, 0, COLOR_DESC)

    # Single centered button
    button_y = container_y + container_height - 60 - ACTION_BUTTON_HEIGHT
    button_width = 375

    mouse_pos = rl.get_mouse_position()
    mouse_clicked = rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT)

    button_rect = rl.Rectangle(container_x + (container_width - button_width) / 2, button_y, button_width, ACTION_BUTTON_HEIGHT)
    button_hover = rl.check_collision_point_rec(mouse_pos, button_rect)
    button_color = OPTION_PRESSED_COLOR if button_hover else OPTION_SELECTED_COLOR
    rl.draw_rectangle_rounded(button_rect, 0.15, 10, button_color)

    btn_measure = measure_text_cached(self._font_title, self.button_text, 63)
    rl.draw_text_ex(self._font_title, self.button_text,
                    rl.Vector2(button_rect.x + (button_width - btn_measure.x) / 2,
                               button_y + (ACTION_BUTTON_HEIGHT - 63) / 2),
                    63, 0, COLOR_BUTTON_TEXT)

    # Handle input
    if rl.is_key_pressed(rl.KeyboardKey.KEY_ENTER) or rl.is_key_pressed(rl.KeyboardKey.KEY_ESCAPE):
      self.result = DialogResult.CONFIRM
    elif mouse_clicked and button_hover:
      self.result = DialogResult.CONFIRM

    return self.result


class ModalManager:
  """Manages modal dialog state for a panel"""

  def __init__(self):
    self.active_dialog: Optional[Widget] = None
    self.pending_callback: Optional[Callable] = None
    self.dialog_type: str = ""

  def show_confirm(self, title: str, message: str, confirm_text: str = "Yes",
                   cancel_text: str = "No", on_confirm: Callable = None):
    """Show a confirmation dialog"""
    self.active_dialog = BPConfirmationDialog(title, message, confirm_text, cancel_text)
    self.pending_callback = on_confirm
    self.dialog_type = "confirm"

  def show_selection(self, title: str, options: list, current: str = "",
                     on_select: Callable[[str], None] = None):
    """Show a selection dialog"""
    self.active_dialog = BPSelectionDialog(title, options, current)
    self.pending_callback = on_select
    self.dialog_type = "selection"

  def show_alert(self, title: str, message: str, button_text: str = "OK",
                 on_dismiss: Callable = None):
    """Show an alert dialog"""
    self.active_dialog = BPAlertDialog(title, message, button_text)
    self.pending_callback = on_dismiss
    self.dialog_type = "alert"

  def close(self):
    """Close the active dialog"""
    self.active_dialog = None
    self.pending_callback = None
    self.dialog_type = ""

  def is_active(self) -> bool:
    """Check if a dialog is active"""
    return self.active_dialog is not None

  def render(self, rect: rl.Rectangle) -> bool:
    """
    Render the active dialog.
    Returns True if dialog was closed this frame.
    """
    if not self.active_dialog:
      return False

    # Draw overlay (Qt: rgba(0, 0, 0, 0.75))
    rl.draw_rectangle(0, 0, gui_app.width, gui_app.height, DIALOG_OVERLAY_COLOR)

    # Render dialog centered on full screen (not just panel rect)
    full_screen_rect = rl.Rectangle(0, 0, gui_app.width, gui_app.height)
    result = self.active_dialog.render(full_screen_rect)

    # Handle result
    if self.dialog_type == "confirm":
      if result == DialogResult.CONFIRM:
        if self.pending_callback:
          self.pending_callback()
        self.close()
        return True
      elif result == DialogResult.CANCEL:
        self.close()
        return True

    elif self.dialog_type == "selection":
      if result == 0:  # Cancel
        self.close()
        return True
      elif result == 1:  # Select
        if self.pending_callback and hasattr(self.active_dialog, 'selected_value'):
          self.pending_callback(self.active_dialog.selected_value)
        self.close()
        return True

    elif self.dialog_type == "alert":
      if result == DialogResult.CONFIRM:
        if self.pending_callback:
          self.pending_callback()
        self.close()
        return True

    return False
