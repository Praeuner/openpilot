"""
BluePilot Settings Window
Modern, streamlined settings interface with consistent BP styling
Port of Qt BPSettingsWindow
"""

import pyray as rl
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Optional
from collections.abc import Callable
from openpilot.selfdrive.ui.layouts.settings.firehose import FirehoseLayout
from openpilot.selfdrive.ui.layouts.settings.software import SoftwareLayout
from openpilot.system.ui.lib.application import gui_app, FontWeight, MousePos
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.lib.wifi_manager import WifiManager
from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.widgets.network import WifiManagerUI
from system.ui.bluepilot.lib.colors import BPColors
from system.ui.bluepilot.settings.panels.bp_base_panel import BPBasePanel


# BP Settings Constants (from Qt BPSettingsWindow)
SIDEBAR_WIDTH = 440  # Qt: sidebar_widget->setFixedWidth(440)
CLOSE_BTN_SIZE = 100  # Qt: setFixedSize(100, 100)
NAV_BTN_HEIGHT = 70  # Qt: setMinimumHeight(70)
NAV_BTN_SPACING = 12  # Qt: setSpacing(12)
PANEL_MARGIN = 50
ICON_SIZE = 50  # Qt: setIconSize(QSize(50, 50))
NAV_BTN_PADDING_LEFT = 18  # Qt: padding-left: 18px
NAV_BTN_FONT_SIZE = 48  # Qt: font-size: 48px
NAV_BTN_RADIUS = 12  # Qt: border-radius: 12px

# BP Color scheme (from Qt settings.h)
BP_BACKGROUND = rl.Color(32, 33, 35, 255)
BP_CARD_BACKGROUND = rl.Color(48, 49, 51, 255)
BP_ACCENT = rl.Color(0, 132, 255, 255)  # #0084FF active button
BP_TEXT_PRIMARY = rl.Color(255, 255, 255, 255)
BP_TEXT_SECONDARY = rl.Color(189, 189, 189, 255)
BP_BUTTON_HOVER = rl.Color(60, 61, 63, 255)
BP_BUTTON_PRESSED = rl.Color(70, 71, 73, 255)
BP_BORDER = rl.Color(255, 255, 255, 26)  # rgba(255, 255, 255, 0.1)

# Icon paths for navigation buttons (from Qt settings.cc PanelInfo)
NAV_ICONS = {
  "Device": "selfdrive/assets/offroad/icon_openpilot.png",
  "Network": "selfdrive/assets/offroad/icon_network.png",
  "Toggles": "selfdrive/assets/offroad/icon_toggle.png",
  "Steering": "selfdrive/assets/offroad/icon_steering.png",
  "Cruise": "selfdrive/assets/offroad/icon_speed_limit.png",
  "Visuals": "selfdrive/assets/offroad/icon_visuals.png",
  "Display": "selfdrive/assets/offroad/icon_display.png",
  "Software": "selfdrive/assets/offroad/icon_software.png",
  "Vehicle": "selfdrive/assets/offroad/icon_vehicle.png",
  "Firehose": "selfdrive/assets/offroad/icon_upload.png",
  "Developer": "selfdrive/assets/offroad/icon_shell.png",
}

# JSON panel paths (relative to project root)
BP_PANEL_PATHS = {
  "device": "selfdrive/ui/bluepilot/menus/bp_device_panel.json",
  "toggles": "selfdrive/ui/bluepilot/menus/bp_toggles_panel.json",
  "steering": "selfdrive/ui/bluepilot/menus/bp_steering_panel.json",
  "cruise": "selfdrive/ui/bluepilot/menus/bp_cruise_panel.json",
  "visuals": "selfdrive/ui/bluepilot/menus/bp_visuals_panel.json",
  "display": "selfdrive/ui/bluepilot/menus/bp_display_panel.json",
  "vehicle": "selfdrive/ui/bluepilot/menus/bp_vehicle_panel.json",
  "developer": "selfdrive/ui/bluepilot/menus/bp_developer_panel.json",
}


class BPPanelType(IntEnum):
  """BluePilot settings panels - matches Qt BPSettingsWindow order"""
  DEVICE = 0
  NETWORK = 1
  TOGGLES = 2
  STEERING = 3
  CRUISE = 4
  VISUALS = 5
  DISPLAY = 6
  SOFTWARE = 7
  VEHICLE = 8
  FIREHOSE = 9
  DEVELOPER = 10


@dataclass
class BPPanelInfo:
  """Panel configuration"""
  name: str
  instance: Widget
  icon_path: str = ""
  button_rect: rl.Rectangle = field(default_factory=lambda: rl.Rectangle(0, 0, 0, 0))


class BPSettingsLayout(Widget):
  """
  BluePilot Settings Window
  Modern styling matching Qt BPSettingsWindow
  """

  def __init__(self):
    super().__init__()
    self._current_panel = BPPanelType.DEVICE

    # Initialize panels
    wifi_manager = WifiManager()
    wifi_manager.set_active(False)

    # Create JSON-based panels using BPBasePanel
    device_panel = BPBasePanel(BP_PANEL_PATHS["device"])
    toggles_panel = BPBasePanel(BP_PANEL_PATHS["toggles"])
    steering_panel = BPBasePanel(BP_PANEL_PATHS["steering"])
    cruise_panel = BPBasePanel(BP_PANEL_PATHS["cruise"])
    visuals_panel = BPBasePanel(BP_PANEL_PATHS["visuals"])
    display_panel = BPBasePanel(BP_PANEL_PATHS["display"])
    vehicle_panel = BPBasePanel(BP_PANEL_PATHS["vehicle"])
    developer_panel = BPBasePanel(BP_PANEL_PATHS["developer"])

    self._panels = {
      BPPanelType.DEVICE: BPPanelInfo("Device", device_panel, NAV_ICONS.get("Device", "")),
      BPPanelType.NETWORK: BPPanelInfo("Network", WifiManagerUI(wifi_manager), NAV_ICONS.get("Network", "")),
      BPPanelType.TOGGLES: BPPanelInfo("Toggles", toggles_panel, NAV_ICONS.get("Toggles", "")),
      BPPanelType.STEERING: BPPanelInfo("Steering", steering_panel, NAV_ICONS.get("Steering", "")),
      BPPanelType.CRUISE: BPPanelInfo("Cruise", cruise_panel, NAV_ICONS.get("Cruise", "")),
      BPPanelType.VISUALS: BPPanelInfo("Visuals", visuals_panel, NAV_ICONS.get("Visuals", "")),
      BPPanelType.DISPLAY: BPPanelInfo("Display", display_panel, NAV_ICONS.get("Display", "")),
      BPPanelType.SOFTWARE: BPPanelInfo("Software", SoftwareLayout(), NAV_ICONS.get("Software", "")),
      BPPanelType.VEHICLE: BPPanelInfo("Vehicle", vehicle_panel, NAV_ICONS.get("Vehicle", "")),
      BPPanelType.FIREHOSE: BPPanelInfo("Firehose", FirehoseLayout(), NAV_ICONS.get("Firehose", "")),
      BPPanelType.DEVELOPER: BPPanelInfo("Developer", developer_panel, NAV_ICONS.get("Developer", "")),
    }

    # Fonts
    self._font_medium = gui_app.font(FontWeight.MEDIUM)
    self._font_semi_bold = gui_app.font(FontWeight.SEMI_BOLD)

    # Callbacks
    self._close_callback: Callable | None = None

    # UI state
    self._close_btn_rect = rl.Rectangle(0, 0, 0, 0)
    self._hovered_button: BPPanelType | None = None

    # Icon textures cache
    self._icon_textures: dict[str, rl.Texture2D] = {}

  def set_callbacks(self, on_close: Callable):
    self._close_callback = on_close

  def _render(self, rect: rl.Rectangle):
    # Calculate layout
    sidebar_rect = rl.Rectangle(rect.x, rect.y, SIDEBAR_WIDTH, rect.height)
    panel_rect = rl.Rectangle(rect.x + SIDEBAR_WIDTH, rect.y, rect.width - SIDEBAR_WIDTH, rect.height)

    # Draw components
    self._draw_sidebar(sidebar_rect)
    self._draw_current_panel(panel_rect)

  def _load_icon(self, icon_path: str) -> Optional[rl.Texture2D]:
    """Load and cache an icon texture"""
    if not icon_path:
      return None
    if icon_path in self._icon_textures:
      return self._icon_textures[icon_path]
    try:
      import os
      if os.path.exists(icon_path):
        texture = rl.load_texture(icon_path)
        self._icon_textures[icon_path] = texture
        return texture
    except Exception:
      pass
    return None

  def _draw_sidebar(self, rect: rl.Rectangle):
    # Sidebar background (Qt: background-color: %1, border-right: 1px solid rgba(255,255,255,0.1))
    rl.draw_rectangle_rec(rect, BP_BACKGROUND)
    # Right border
    rl.draw_line_ex(
      rl.Vector2(rect.x + rect.width - 1, rect.y),
      rl.Vector2(rect.x + rect.width - 1, rect.y + rect.height),
      1, BP_BORDER
    )

    # Close button (Qt: QPushButton#bpCloseButton)
    close_margin = 30
    close_btn_rect = rl.Rectangle(
      rect.x + close_margin,
      rect.y + close_margin,
      CLOSE_BTN_SIZE,
      CLOSE_BTN_SIZE
    )
    self._close_btn_rect = close_btn_rect

    mouse_pos = rl.get_mouse_position()
    is_hovered = rl.check_collision_point_rec(mouse_pos, close_btn_rect)
    is_pressed = is_hovered and rl.is_mouse_button_down(rl.MouseButton.MOUSE_BUTTON_LEFT)

    if is_pressed:
      close_bg = BP_BUTTON_PRESSED
    elif is_hovered:
      close_bg = BP_BUTTON_HOVER
    else:
      close_bg = BP_CARD_BACKGROUND

    # Draw close button (circular, Qt: border-radius: 50px)
    rl.draw_rectangle_rounded(close_btn_rect, 1.0, 20, close_bg)
    rl.draw_rectangle_rounded_lines_ex(close_btn_rect, 1.0, 20, 2, BP_BORDER)

    # Close button text (Qt: font-size: 90px)
    close_text = "x"
    close_font_size = 70
    close_text_size = measure_text_cached(self._font_medium, close_text, close_font_size)
    close_text_pos = rl.Vector2(
      close_btn_rect.x + (close_btn_rect.width - close_text_size.x) / 2,
      close_btn_rect.y + (close_btn_rect.height - close_text_size.y) / 2 - 5
    )
    text_color = BP_TEXT_PRIMARY if is_pressed else BP_TEXT_SECONDARY
    rl.draw_text_ex(self._font_medium, close_text, close_text_pos, close_font_size, 0, text_color)

    # Navigation buttons (Qt: QPushButton#bpNavButton)
    nav_y = rect.y + close_margin + CLOSE_BTN_SIZE + 40
    nav_x = rect.x + close_margin
    nav_width = rect.width - close_margin - 50  # Account for margins

    for panel_type, panel_info in self._panels.items():
      button_rect = rl.Rectangle(nav_x, nav_y, nav_width, NAV_BTN_HEIGHT)
      panel_info.button_rect = button_rect

      is_selected = panel_type == self._current_panel
      is_btn_hovered = rl.check_collision_point_rec(mouse_pos, button_rect)
      is_btn_pressed = is_btn_hovered and rl.is_mouse_button_down(rl.MouseButton.MOUSE_BUTTON_LEFT)

      # Button background (Qt style)
      if is_selected:
        btn_bg = BP_ACCENT  # #0084FF when selected
      elif is_btn_pressed:
        btn_bg = BP_BUTTON_PRESSED
      elif is_btn_hovered:
        btn_bg = BP_BUTTON_HOVER
      else:
        btn_bg = rl.Color(0, 0, 0, 0)  # Transparent

      # Draw button (Qt: border-radius: 12px)
      btn_roundness = NAV_BTN_RADIUS / NAV_BTN_HEIGHT
      rl.draw_rectangle_rounded(button_rect, btn_roundness, 10, btn_bg)

      # Icon (Qt: setIcon, setIconSize(50, 50))
      icon_x = button_rect.x + NAV_BTN_PADDING_LEFT
      icon_y = button_rect.y + (button_rect.height - ICON_SIZE) / 2
      text_offset = NAV_BTN_PADDING_LEFT  # Start with padding

      icon_texture = self._load_icon(panel_info.icon_path)
      if icon_texture and icon_texture.id != 0:
        # Draw icon with tint (white when selected, secondary otherwise)
        icon_tint = BP_TEXT_PRIMARY if is_selected else BP_TEXT_SECONDARY
        # Preserve aspect ratio - fit within ICON_SIZE box
        tex_w, tex_h = icon_texture.width, icon_texture.height
        if tex_w > 0 and tex_h > 0:
          scale = min(ICON_SIZE / tex_w, ICON_SIZE / tex_h)
          draw_w = tex_w * scale
          draw_h = tex_h * scale
          # Center icon within the ICON_SIZE box
          draw_x = icon_x + (ICON_SIZE - draw_w) / 2
          draw_y = icon_y + (ICON_SIZE - draw_h) / 2
          dest_rect = rl.Rectangle(draw_x, draw_y, draw_w, draw_h)
          source_rect = rl.Rectangle(0, 0, tex_w, tex_h)
          rl.draw_texture_pro(icon_texture, source_rect, dest_rect, rl.Vector2(0, 0), 0, icon_tint)
        text_offset += ICON_SIZE + 12  # Space after icon

      # Button text (Qt: font-size: 48px, text-align: left, padding-left: 18px)
      text_color = BP_TEXT_PRIMARY if is_selected else BP_TEXT_SECONDARY
      text_x = button_rect.x + text_offset
      text_y = button_rect.y + (button_rect.height - NAV_BTN_FONT_SIZE) / 2
      rl.draw_text_ex(self._font_semi_bold, panel_info.name, rl.Vector2(text_x, text_y),
                      NAV_BTN_FONT_SIZE, 0, text_color)

      nav_y += NAV_BTN_HEIGHT + NAV_BTN_SPACING

  def _draw_current_panel(self, rect: rl.Rectangle):
    # Panel background (Qt: background-color: black, border-radius: 30px)
    panel_bg_rect = rl.Rectangle(rect.x + 10, rect.y + 10, rect.width - 20, rect.height - 20)
    rl.draw_rectangle_rounded(panel_bg_rect, 0.02, 30, rl.BLACK)

    # Content area
    content_rect = rl.Rectangle(
      rect.x + PANEL_MARGIN,
      rect.y + 25,
      rect.width - (PANEL_MARGIN * 2),
      rect.height - 50
    )

    panel = self._panels[self._current_panel]
    if panel.instance:
      panel.instance.render(content_rect)

  def _handle_mouse_release(self, mouse_pos: MousePos) -> bool:
    # Check close button
    if rl.check_collision_point_rec(mouse_pos, self._close_btn_rect):
      if self._close_callback:
        self._close_callback()
      return True

    # Check navigation buttons
    for panel_type, panel_info in self._panels.items():
      if rl.check_collision_point_rec(mouse_pos, panel_info.button_rect):
        self.set_current_panel(panel_type)
        return True

    return False

  def set_current_panel(self, panel_type: BPPanelType):
    if panel_type != self._current_panel:
      self._panels[self._current_panel].instance.hide_event()
      self._current_panel = panel_type
      self._panels[self._current_panel].instance.show_event()

  def show_event(self):
    super().show_event()
    self._panels[self._current_panel].instance.show_event()

  def hide_event(self):
    super().hide_event()
    self._panels[self._current_panel].instance.hide_event()
