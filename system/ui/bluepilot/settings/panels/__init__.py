"""
BluePilot Settings Panels
Modular JSON-based panel system for dynamic settings UI

Module Structure:
- bp_panel_styles: Qt-matched styling constants (colors, sizes, dimensions)
- bp_panel_conditions: Condition evaluation system for visibility/enable logic
- bp_panel_controls: Individual control widgets (toggle, segmented, selection, etc.)
- bp_panel_dialogs: Modal dialog widgets (confirmation, selection, alert)
- bp_base_panel: Main panel orchestrator that uses all the above

This modular structure matches the Qt BPPanelBase architecture:
- Qt bp_panel_controls.h/cc -> bp_panel_controls.py
- Qt bp_panel_dialogs.h/cc -> bp_panel_dialogs.py
- Qt bp_panel_base.h/cc -> bp_base_panel.py
- Qt BPTextSizes + styles -> bp_panel_styles.py
- Qt PanelConditions -> bp_panel_conditions.py
"""

# Main panel widget
from system.ui.bluepilot.settings.panels.bp_base_panel import BPBasePanel

# Styles
from system.ui.bluepilot.settings.panels.bp_panel_styles import (
  # Colors
  COLOR_BG, COLOR_GROUP_TITLE, COLOR_TITLE, COLOR_DESC, COLOR_DIVIDER,
  COLOR_BUTTON_BG, COLOR_BUTTON_TEXT, COLOR_SEGMENT_SELECTED,
  COLOR_PRIMARY, COLOR_DANGER, COLOR_SUCCESS, COLOR_WARNING,
  COLOR_TITLE_DISABLED, COLOR_DESC_DISABLED,
  COLOR_BUTTON_PRESSED, COLOR_BUTTON_DISABLED_BG, COLOR_BUTTON_DISABLED_TEXT,
  COLOR_SEGMENT_BG, COLOR_SEGMENT_HOVER, COLOR_SEGMENT_DISABLED,
  # Sizes
  TITLE_SIZE, DESC_SIZE, GROUP_TITLE_SIZE, BUTTON_TEXT_SIZE, REASON_LABEL_SIZE,
  SEGMENTED_BUTTON_SIZE, VALUE_DISPLAY_SIZE,
  DIALOG_TITLE_SIZE, DIALOG_OPTION_SIZE, DIALOG_BUTTON_SIZE,
  CONTROL_MIN_HEIGHT, CONTROL_MARGINS, CONTROL_SPACING, TEXT_SPACING,
  TOGGLE_WIDTH, TOGGLE_HEIGHT, BUTTON_WIDTH, BUTTON_HEIGHT,
  # Helpers
  parse_hex_color, color_with_alpha, darken_color, lighten_color
)

# Conditions
from system.ui.bluepilot.settings.panels.bp_panel_conditions import (
  PanelConditions, ControlConditions
)

# Controls
from system.ui.bluepilot.settings.panels.bp_panel_controls import (
  ControlConfig, BPControlBase,
  BPToggleControl, BPSegmentedControl, BPSelectionControl,
  BPNumericControl, BPButtonControl, BPStaticParamDisplay,
  BPStaticText, BPParamToggleButton,
  create_control
)

# Dialogs
from system.ui.bluepilot.settings.panels.bp_panel_dialogs import (
  BPConfirmationDialog, BPSelectionDialog, BPAlertDialog, ModalManager
)

__all__ = [
  # Main panel
  'BPBasePanel',

  # Styles - Colors
  'COLOR_BG', 'COLOR_GROUP_TITLE', 'COLOR_TITLE', 'COLOR_DESC', 'COLOR_DIVIDER',
  'COLOR_BUTTON_BG', 'COLOR_BUTTON_TEXT', 'COLOR_SEGMENT_SELECTED',
  'COLOR_PRIMARY', 'COLOR_DANGER', 'COLOR_SUCCESS', 'COLOR_WARNING',
  'COLOR_TITLE_DISABLED', 'COLOR_DESC_DISABLED',
  'COLOR_BUTTON_PRESSED', 'COLOR_BUTTON_DISABLED_BG', 'COLOR_BUTTON_DISABLED_TEXT',
  'COLOR_SEGMENT_BG', 'COLOR_SEGMENT_HOVER', 'COLOR_SEGMENT_DISABLED',

  # Styles - Sizes
  'TITLE_SIZE', 'DESC_SIZE', 'GROUP_TITLE_SIZE', 'BUTTON_TEXT_SIZE', 'REASON_LABEL_SIZE',
  'SEGMENTED_BUTTON_SIZE', 'VALUE_DISPLAY_SIZE',
  'DIALOG_TITLE_SIZE', 'DIALOG_OPTION_SIZE', 'DIALOG_BUTTON_SIZE',
  'CONTROL_MIN_HEIGHT', 'CONTROL_MARGINS', 'CONTROL_SPACING', 'TEXT_SPACING',
  'TOGGLE_WIDTH', 'TOGGLE_HEIGHT', 'BUTTON_WIDTH', 'BUTTON_HEIGHT',

  # Styles - Helpers
  'parse_hex_color', 'color_with_alpha', 'darken_color', 'lighten_color',

  # Conditions
  'PanelConditions', 'ControlConditions',

  # Controls
  'ControlConfig', 'BPControlBase',
  'BPToggleControl', 'BPSegmentedControl', 'BPSelectionControl',
  'BPNumericControl', 'BPButtonControl', 'BPStaticParamDisplay',
  'BPStaticText', 'BPParamToggleButton',
  'create_control',

  # Dialogs
  'BPConfirmationDialog', 'BPSelectionDialog', 'BPAlertDialog', 'ModalManager',
]
