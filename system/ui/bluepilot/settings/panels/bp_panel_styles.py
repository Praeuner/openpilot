"""
BluePilot Panel Styles
Qt-matched styling constants for BP panels
Ported from Qt bp_panel_controls.h/bp_panel_base.cc
"""

import pyray as rl


# ============================================================
# Colors (from Qt BPPanelBase and BPToggleControl styles)
# ============================================================

# Background colors
COLOR_BG = rl.Color(36, 36, 36, 255)  # #242424 - control/group background
COLOR_BG_DARK = rl.Color(28, 28, 28, 255)  # #1C1C1C - darker variant
COLOR_TRANSPARENT = rl.Color(0, 0, 0, 0)

# Text colors
COLOR_TITLE = rl.WHITE
COLOR_TITLE_DISABLED = rl.Color(102, 102, 102, 255)  # #666666
COLOR_DESC = rl.Color(170, 170, 170, 255)  # #AAAAAA
COLOR_DESC_DISABLED = rl.Color(68, 68, 68, 255)  # #444444
COLOR_GROUP_TITLE = rl.Color(33, 150, 243, 255)  # #2196F3 - blue title

# Button colors
COLOR_BUTTON_BG = rl.Color(57, 57, 57, 255)  # #393939
COLOR_BUTTON_PRESSED = rl.Color(74, 74, 74, 255)  # #4a4a4a
COLOR_BUTTON_HOVER = rl.Color(64, 64, 64, 255)  # #404040
COLOR_BUTTON_TEXT = rl.Color(228, 228, 228, 255)  # #E4E4E4
COLOR_BUTTON_DISABLED_BG = rl.Color(42, 42, 42, 255)  # #2a2a2a
COLOR_BUTTON_DISABLED_TEXT = rl.Color(119, 119, 119, 255)  # #777777

# Segmented control colors
COLOR_SEGMENT_SELECTED = rl.Color(33, 150, 243, 255)  # #2196F3
COLOR_SEGMENT_BG = rl.Color(48, 48, 48, 255)  # #303030
COLOR_SEGMENT_HOVER = rl.Color(64, 64, 64, 255)  # #404040
COLOR_SEGMENT_DISABLED = rl.Color(32, 32, 32, 255)  # #202020

# Accent colors
COLOR_PRIMARY = rl.Color(33, 150, 243, 255)  # #2196F3 - blue
COLOR_DANGER = rl.Color(211, 57, 57, 255)  # #d33939 - red
COLOR_SUCCESS = rl.Color(76, 175, 80, 255)  # #4CAF50 - green
COLOR_WARNING = rl.Color(255, 193, 7, 255)  # #FFC107 - amber

# Divider
COLOR_DIVIDER = rl.Color(255, 255, 255, 128)  # rgba(255, 255, 255, 0.5)


# ============================================================
# Font Sizes (from Qt BPTextSizes - Medium size default)
# ============================================================

TITLE_SIZE = 50  # Qt: sizes.titleSize (Medium)
DESC_SIZE = 40  # Qt: sizes.descSize (Medium)
GROUP_TITLE_SIZE = 40  # Group box title
BUTTON_TEXT_SIZE = 62  # Qt: sizes.buttonTextSize (Medium) - was incorrectly 36
SMALL_TEXT_SIZE = 32
REASON_LABEL_SIZE = 35  # Qt: sizes.reasonLabelSize (Medium)
SEGMENTED_BUTTON_SIZE = 39  # Qt: sizes.segmentedButtonSize (Medium)
VALUE_DISPLAY_SIZE = 50  # Qt: sizes.valueDisplaySize (Medium)

# Dialog sizes (from Qt bp_panel_dialogs.cc)
DIALOG_TITLE_SIZE = 60  # Qt: titleLabel font-size: 60px
DIALOG_OPTION_SIZE = 50  # Qt: QPushButton font-size: 50px
DIALOG_BUTTON_SIZE = 50  # Qt: action buttons font-size: 50px (scaled to 63px)


# ============================================================
# Layout Dimensions (from Qt sources)
# ============================================================

# Control layout
CONTROL_MIN_HEIGHT = 150  # Qt: min-height: 150px
CONTROL_MARGINS = 25  # Qt: setContentsMargins(25, 25, 25, 25)
CONTROL_SPACING = 50  # Qt: setSpacing(50) between toggle and text
TEXT_SPACING = 10  # Qt: textLayout->setSpacing(10)

# Group layout
GROUP_MARGIN_TOP = 50  # Qt: margin-top: 50px
GROUP_PADDING = 10  # Qt: layout->setContentsMargins(10, 10, 10, 10)
GROUP_SPACING = 50  # Qt: inner_layout.setSpacing(50)
CONTROL_IN_GROUP_SPACING = 10  # Qt: layout->setSpacing(10)

# Border radius
BORDER_RADIUS = 10  # Qt: border-radius: 10px
GROUP_BORDER_RADIUS = 15  # Qt: border-radius: 15px

# Toggle dimensions (matches Qt BPToggle)
TOGGLE_WIDTH = 140
TOGGLE_HEIGHT = 70

# Button dimensions
BUTTON_WIDTH = 200
BUTTON_HEIGHT = 55
BUTTON_MIN_WIDTH = 150
BUTTON_MAX_WIDTH = 300

# Tab dimensions
TAB_HEIGHT = 70
TAB_MIN_WIDTH = 150
TAB_MAX_WIDTH = 250
TAB_SPACING = 5

# Scrolling
SCROLL_SPEED = 50


# ============================================================
# Helper Functions
# ============================================================

def parse_hex_color(hex_str: str, default: rl.Color = COLOR_TITLE) -> rl.Color:
  """Parse a hex color string to rl.Color"""
  if not hex_str:
    return default
  try:
    if hex_str.startswith("#"):
      hex_color = hex_str.lstrip("#")
      if len(hex_color) == 6:
        r = int(hex_color[0:2], 16)
        g = int(hex_color[2:4], 16)
        b = int(hex_color[4:6], 16)
        return rl.Color(r, g, b, 255)
      elif len(hex_color) == 8:
        r = int(hex_color[0:2], 16)
        g = int(hex_color[2:4], 16)
        b = int(hex_color[4:6], 16)
        a = int(hex_color[6:8], 16)
        return rl.Color(r, g, b, a)
  except Exception:
    pass
  return default


def color_with_alpha(color: rl.Color, alpha: int) -> rl.Color:
  """Return a color with modified alpha"""
  return rl.Color(color.r, color.g, color.b, alpha)


def darken_color(color: rl.Color, amount: int = 30) -> rl.Color:
  """Darken a color by reducing RGB values"""
  return rl.Color(
    max(0, color.r - amount),
    max(0, color.g - amount),
    max(0, color.b - amount),
    color.a
  )


def lighten_color(color: rl.Color, amount: int = 30) -> rl.Color:
  """Lighten a color by increasing RGB values"""
  return rl.Color(
    min(255, color.r + amount),
    min(255, color.g + amount),
    min(255, color.b + amount),
    color.a
  )
