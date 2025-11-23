"""
BluePilot Base Panel
JSON-driven panel widget for dynamic settings UI
Orchestrates groups and controls using modular components
"""

import json
import os
import subprocess
import pyray as rl
from dataclasses import dataclass, field
from typing import Optional
from openpilot.common.params import Params
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.scroll_panel import GuiScrollPanel
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget

from system.ui.bluepilot.settings.panels.bp_panel_styles import (
  COLOR_BG, COLOR_GROUP_TITLE, COLOR_DIVIDER,
  COLOR_SEGMENT_SELECTED, COLOR_SEGMENT_BG, COLOR_SEGMENT_HOVER,
  GROUP_TITLE_SIZE, GROUP_MARGIN_TOP, GROUP_PADDING, GROUP_SPACING,
  GROUP_BORDER_RADIUS, CONTROL_MIN_HEIGHT, CONTROL_IN_GROUP_SPACING,
  TAB_HEIGHT, TAB_SPACING, TAB_MAX_WIDTH
)
from system.ui.bluepilot.settings.panels.bp_panel_conditions import PanelConditions
from system.ui.bluepilot.settings.panels.bp_panel_controls import (
  ControlConfig, BPControlBase, BPToggleControl, BPSegmentedControl,
  BPSelectionControl, BPNumericControl, BPButtonControl,
  BPStaticParamDisplay, BPFileParamDisplay, BPStaticText, BPParamToggleButton,
  create_control
)
from system.ui.bluepilot.settings.panels.bp_panel_dialogs import ModalManager


@dataclass
class GroupConfig:
  """Configuration for a control group"""
  name: str
  title: str
  controls: list = field(default_factory=list)
  control_widgets: list = field(default_factory=list)
  enable_reset: bool = False
  hide_dividers: bool = False
  hidden: bool = False
  type: str = "group"  # "group" or "tabPanel"
  tabs: list = field(default_factory=list)


class BPBasePanel(Widget):
  """
  JSON-driven panel widget.
  Matches Qt BPBaseView/BPPanelBase styling and functionality.
  """

  def __init__(self, json_path: str = ""):
    super().__init__()
    self._params = Params()
    self._groups: list[GroupConfig] = []
    self._scroll = GuiScrollPanel()
    self._content_height = 0
    self._json_path = json_path
    self._modal = ModalManager()
    self._conditions = PanelConditions.get_instance()

    # Fonts
    self._font_title = gui_app.font(FontWeight.MEDIUM)
    self._font_normal = gui_app.font(FontWeight.NORMAL)

    # Tab state
    self._active_tab: int = 0

    if json_path:
      self.load_json(json_path)

  def load_json(self, json_path: str) -> bool:
    """Load panel configuration from JSON file"""
    if not os.path.isabs(json_path):
      base_paths = [
        "/data/openpilot",
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(__file__)))))
      ]
      for base in base_paths:
        full_path = os.path.join(base, json_path.lstrip('/'))
        if os.path.exists(full_path):
          json_path = full_path
          break

    try:
      with open(json_path, 'r') as f:
        config = json.load(f)
      self._parse_config(config)
      return True
    except Exception as e:
      print(f"BPBasePanel: Failed to load {json_path}: {e}")
      return False

  def load_config(self, config: dict):
    """Load panel from config dict directly"""
    self._parse_config(config)

  def _parse_config(self, config: dict):
    """Parse JSON configuration into internal structures"""
    self._groups = []

    groups_data = config.get("groups", [])
    for group_data in groups_data:
      self._parse_group(group_data)

  def _parse_group(self, group_data: dict) -> Optional[GroupConfig]:
    """Parse a single group from JSON"""
    if group_data.get("hidden", False):
      return None

    group_type = group_data.get("type", "group")

    group = GroupConfig(
      name=group_data.get("groupName", ""),
      title=group_data.get("title", ""),
      enable_reset=group_data.get("enableResetButton", False),
      hide_dividers=group_data.get("hideDividers", False),
      type=group_type
    )

    if group_type == "tabPanel":
      # Parse tabs
      for tab_data in group_data.get("tabs", []):
        tab = {
          "name": tab_data.get("name", ""),
          "groups": []
        }
        for tab_group in tab_data.get("groups", []):
          parsed_group = self._parse_tab_group(tab_group)
          if parsed_group:
            tab["groups"].append(parsed_group)
        group.tabs.append(tab)
      self._groups.append(group)
      return group

    # Parse regular controls
    controls_data = group_data.get("controls", [])
    for ctrl_data in controls_data:
      config = self._parse_control_config(ctrl_data)
      if config:
        group.controls.append(config)
        # Create control widget
        widget = self._create_control_widget(config)
        if widget:
          group.control_widgets.append(widget)

    if group.controls or group.tabs:
      self._groups.append(group)
      return group
    return None

  def _parse_tab_group(self, group_data: dict) -> Optional[dict]:
    """Parse a tab's group content"""
    result = {
      "title": group_data.get("title", ""),
      "controls": [],
      "control_widgets": []
    }
    for ctrl_data in group_data.get("controls", []):
      config = self._parse_control_config(ctrl_data)
      if config:
        result["controls"].append(config)
        widget = self._create_control_widget(config)
        if widget:
          result["control_widgets"].append(widget)
    return result if result["controls"] else None

  def _parse_control_config(self, ctrl_data: dict) -> Optional[ControlConfig]:
    """Parse a single control from JSON - matches Qt bp_panel_controls JSON schema"""
    if ctrl_data.get("hidden", False):
      return None

    return ControlConfig(
      type=ctrl_data.get("type", ""),
      param=ctrl_data.get("param", ""),
      title=ctrl_data.get("title", ""),
      desc=ctrl_data.get("desc", ""),
      icon=ctrl_data.get("icon", ""),
      options=ctrl_data.get("options", []),
      min_val=ctrl_data.get("min", 0.0),
      max_val=ctrl_data.get("max", 1.0),
      increment=ctrl_data.get("increment", 0.1),
      division=ctrl_data.get("division", 1.0),
      default=ctrl_data.get("default", 0.0),
      hidden=ctrl_data.get("hidden", False),
      visible_conditions=ctrl_data.get("visibleConditions", {}),
      enable_conditions=ctrl_data.get("enableConditions", {}),

      # Dynamic descriptions (Qt descriptionConditions)
      descriptions=ctrl_data.get("descriptions", {}),
      description_conditions=ctrl_data.get("descriptionConditions", {}),
      default_desc=ctrl_data.get("desc", ""),

      # Dynamic title (Qt enableDynamicTitle)
      dynamic_title=ctrl_data.get("dynamicTitle", False),
      title_when_enabled=ctrl_data.get("titleWhenEnabled", ""),
      title_when_disabled=ctrl_data.get("titleWhenDisabled", ""),

      # Dynamic styling (Qt enableDynamicStyling)
      dynamic_styling=ctrl_data.get("dynamicStyling", False),
      styles=ctrl_data.get("styles", {}),
      bg_color_when_enabled=ctrl_data.get("bgColorWhenEnabled", "#2196F3"),
      bg_color_when_disabled=ctrl_data.get("bgColorWhenDisabled", "#808080"),
      bg_color_when_enabled_pressed=ctrl_data.get("bgColorWhenEnabledPressed", ""),
      bg_color_when_disabled_pressed=ctrl_data.get("bgColorWhenDisabledPressed", ""),

      # Dynamic button text (Qt enableDynamicButtonText)
      dynamic_button_text=ctrl_data.get("dynamicButtonText", False),
      button_text_when_enabled=ctrl_data.get("buttonTextWhenEnabled", ""),
      button_text_when_disabled=ctrl_data.get("buttonTextWhenDisabled", ""),

      # Confirmation (Qt setConfirmationTexts)
      confirm=ctrl_data.get("confirm", False),
      confirm_text=ctrl_data.get("confirmText", ""),
      confirm_text_on=ctrl_data.get("confirmTextOn", ""),
      confirm_text_off=ctrl_data.get("confirmTextOff", ""),
      confirm_yes_text=ctrl_data.get("confirmYesText", "Confirm"),
      confirm_no_text=ctrl_data.get("confirmNoText", "Cancel"),

      # Disabled reasons (Qt setDisabledReasons)
      disabled_reasons=ctrl_data.get("disabledReasons", []),

      # Command button specific
      command=ctrl_data.get("command", ""),
      action=ctrl_data.get("action", ""),
      button_text=ctrl_data.get("buttonText", ctrl_data.get("button_text", "")),
      working_dir=ctrl_data.get("workingDir", ctrl_data.get("working_dir", "")),

      # Nested controls
      groups=ctrl_data.get("groups", []),
      panel_title=ctrl_data.get("panelTitle", ctrl_data.get("panel_title", "")),

      # File/path
      path=ctrl_data.get("path", ""),
      file=ctrl_data.get("file", ""),
      header=ctrl_data.get("header", ""),

      # Mutual exclusion
      mutually_exclusive=ctrl_data.get("mutuallyExclusive", ctrl_data.get("mutually_exclusive", [])),
      needs_restart=ctrl_data.get("needsRestart", ctrl_data.get("needs_restart", False)),

      # Value display
      value_processor=ctrl_data.get("valueProcessor", ctrl_data.get("value_processor", "")),
      prefix=ctrl_data.get("prefix", ""),
      suffix=ctrl_data.get("suffix", ""),
      value_color=ctrl_data.get("valueColor", ctrl_data.get("value_color", "#0086E9")),

      # Segmented control specific (Qt optionDescriptions, showDescBottom)
      option_descriptions=ctrl_data.get("optionDescriptions", []),
      show_desc_bottom=ctrl_data.get("showDescBottom", False),
      per_option_conditions=ctrl_data.get("perOptionConditions", []),

      # Selection control specific (Qt hideDescription)
      hide_description=ctrl_data.get("hideDescription", False),

      # Button styling (colors, etc.)
      button_style=ctrl_data.get("button_style", {}),
    )

  def _create_control_widget(self, config: ControlConfig) -> Optional[BPControlBase]:
    """Create a control widget from config, with callbacks wired"""
    widget = create_control(config)
    if not widget:
      return None

    # Wire up callbacks based on control type
    if isinstance(widget, BPSelectionControl):
      widget.on_click = lambda c=config: self._show_selection_dialog(c)

    elif isinstance(widget, BPButtonControl):
      if config.type == "command_button":
        widget.on_click = lambda c=config: self._execute_command(c)
      elif config.type == "restart_ui":
        widget.on_click = lambda c=config: self._handle_restart_ui(c)

    return widget

  # ============================================================
  # Rendering
  # ============================================================

  def _render(self, rect: rl.Rectangle):
    # Handle modal dialogs first
    if self._modal.is_active():
      # Draw panel content underneath (dimmed)
      self._render_panel_content(rect)
      # Draw modal on top
      self._modal.render(rect)
      return

    self._render_panel_content(rect)

  def _render_panel_content(self, rect: rl.Rectangle):
    """Render the main panel content"""
    # Calculate content dimensions
    self._calculate_content_height()

    # Create view and content rectangles for scrolling
    view_rect = rect
    content_rect = rl.Rectangle(rect.x, rect.y, rect.width, self._content_height)

    # Handle scrolling
    offset = self._scroll.handle_scroll(view_rect, content_rect)

    # Begin scissor mode for clipping
    rl.begin_scissor_mode(int(rect.x), int(rect.y), int(rect.width), int(rect.height))

    # Render groups with scroll offset
    y_offset = rect.y + offset.y
    for group in self._groups:
      if group.type == "tabPanel":
        group_height = self._render_tab_panel(group, rect.x, y_offset, rect.width)
      else:
        group_height = self._render_group(group, rect.x, y_offset, rect.width)
      y_offset += group_height + GROUP_SPACING

    rl.end_scissor_mode()

  def _calculate_content_height(self):
    """Calculate total content height for scrolling"""
    total_height = 0
    for group in self._groups:
      if group.type == "tabPanel":
        total_height += self._get_tab_panel_height(group)
      else:
        visible_widgets = self._get_visible_widgets(group)
        if visible_widgets:
          content_height = GROUP_MARGIN_TOP + GROUP_PADDING * 2 + 15
          for i, (config, widget) in enumerate(visible_widgets):
            content_height += self._get_control_height(config)
            if i < len(visible_widgets) - 1:
              content_height += CONTROL_IN_GROUP_SPACING
          total_height += content_height
      total_height += GROUP_SPACING
    self._content_height = total_height

  def _get_visible_widgets(self, group: GroupConfig) -> list:
    """Get list of (config, widget) tuples for visible controls"""
    result = []
    for i, config in enumerate(group.controls):
      if self._check_visibility(config):
        widget = group.control_widgets[i] if i < len(group.control_widgets) else None
        result.append((config, widget))
    return result

  def _check_visibility(self, config: ControlConfig) -> bool:
    """Check if control should be visible"""
    if not config.visible_conditions:
      return True
    return self._conditions.evaluate_conditions(config.visible_conditions)

  def _check_enabled(self, config: ControlConfig) -> bool:
    """Check if control should be enabled"""
    if not config.enable_conditions:
      return True
    return self._conditions.evaluate_conditions(config.enable_conditions)

  def _get_dynamic_description(self, config: ControlConfig) -> str:
    """Get appropriate description based on conditions"""
    if not config.descriptions or not config.description_conditions:
      return config.desc

    for key, cond in config.description_conditions.items():
      if self._conditions.evaluate_conditions(cond):
        return config.descriptions.get(key, config.desc)

    return config.default_desc or config.desc

  def _get_control_height(self, config: ControlConfig) -> float:
    """Get the height of a control"""
    if config.type == "segmented_control":
      # Vertical layout: title (48) + spacing (15) + buttons (80) + spacing (10) + desc (32) + margins (40)
      base_height = CONTROL_MIN_HEIGHT + 85  # 185px minimum for title + buttons
      if config.desc and not config.hide_description:
        return base_height + 40  # Extra space for description
      return base_height
    # Selection controls need extra height for title + selected value + description
    if config.type == "selection" and config.desc and not config.hide_description:
      return CONTROL_MIN_HEIGHT + 30
    # Numeric controls have a taller container (140px) plus labels
    if config.type in ("integer", "float", "numeric"):
      return CONTROL_MIN_HEIGHT + 40
    return CONTROL_MIN_HEIGHT

  def _render_group(self, group: GroupConfig, x: float, y: float, width: float) -> float:
    """Render a control group with Qt-matched styling"""
    visible_widgets = self._get_visible_widgets(group)
    if not visible_widgets:
      return 0

    # Calculate total content height
    content_height = GROUP_MARGIN_TOP + GROUP_PADDING + 15
    for i, (config, widget) in enumerate(visible_widgets):
      content_height += self._get_control_height(config)
      if i < len(visible_widgets) - 1 and not group.hide_dividers:
        content_height += CONTROL_IN_GROUP_SPACING + 1
      else:
        content_height += CONTROL_IN_GROUP_SPACING
    content_height += GROUP_PADDING

    # Draw group background
    group_rect = rl.Rectangle(x, y + GROUP_MARGIN_TOP, width, content_height - GROUP_MARGIN_TOP)
    roundness = GROUP_BORDER_RADIUS / (min(group_rect.width, group_rect.height) / 2)
    rl.draw_rectangle_rounded(group_rect, roundness, 10, COLOR_BG)

    # Draw group title
    if group.title:
      title_y = int(y + GROUP_MARGIN_TOP - GROUP_TITLE_SIZE // 2 + 5)
      title_padding = 15
      title_measure = measure_text_cached(self._font_title, group.title, GROUP_TITLE_SIZE)
      title_bg_rect = rl.Rectangle(x + 35, title_y - 5, title_measure.x + title_padding * 2, GROUP_TITLE_SIZE + 10)

      rl.draw_rectangle_rounded(title_bg_rect, 0.3, 10, COLOR_BG)
      rl.draw_text_ex(self._font_title, group.title,
                      rl.Vector2(x + 35 + title_padding, title_y),
                      GROUP_TITLE_SIZE, 0, COLOR_GROUP_TITLE)

    # Draw controls
    control_y = y + GROUP_MARGIN_TOP + GROUP_PADDING + 15
    for i, (config, widget) in enumerate(visible_widgets):
      # Update widget state
      if widget:
        is_enabled = self._check_enabled(config)
        widget.set_enabled(is_enabled)
        widget.set_description(self._get_dynamic_description(config))

        # Handle disabled reasons (Qt style)
        if not is_enabled and config.enable_conditions:
          # Get reasons from failed conditions
          reasons = self._conditions.get_failed_condition_reasons(config.enable_conditions)
          # Also include any static disabled_reasons from config
          if config.disabled_reasons:
            for reason in config.disabled_reasons:
              if reason and reason not in reasons:
                reasons.append(reason)
          widget.set_disabled_reasons(reasons)
        else:
          # Clear disabled reasons when enabled
          widget.set_disabled_reasons([])

        # Render control
        control_rect = rl.Rectangle(x + GROUP_PADDING, control_y,
                                    width - GROUP_PADDING * 2, self._get_control_height(config))
        widget.render(control_rect)

      control_height = self._get_control_height(config)
      control_y += control_height

      # Draw divider
      if i < len(visible_widgets) - 1:
        if not group.hide_dividers:
          divider_y = control_y + CONTROL_IN_GROUP_SPACING // 2
          rl.draw_line_ex(
            rl.Vector2(x + GROUP_PADDING + 5, divider_y),
            rl.Vector2(x + width - GROUP_PADDING - 5, divider_y),
            1, COLOR_DIVIDER
          )
        control_y += CONTROL_IN_GROUP_SPACING

    return content_height

  def _render_tab_panel(self, group: GroupConfig, x: float, y: float, width: float) -> float:
    """Render a tab panel"""
    if not group.tabs:
      return 0

    # Tab bar
    tab_count = len(group.tabs)
    tab_width = min(TAB_MAX_WIDTH, (width - TAB_SPACING * (tab_count - 1)) / tab_count)

    mouse_pos = rl.get_mouse_position()
    mouse_clicked = rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT)

    for i, tab in enumerate(group.tabs):
      tab_x = x + i * (tab_width + TAB_SPACING)
      tab_rect = rl.Rectangle(tab_x, y, tab_width, TAB_HEIGHT)

      is_selected = (i == self._active_tab)
      is_hovered = rl.check_collision_point_rec(mouse_pos, tab_rect)

      if is_selected:
        tab_color = COLOR_SEGMENT_SELECTED
      elif is_hovered:
        tab_color = COLOR_SEGMENT_HOVER
      else:
        tab_color = COLOR_SEGMENT_BG

      rl.draw_rectangle_rounded(tab_rect, 0.15, 10, tab_color)

      tab_name = tab.get("name", f"Tab {i+1}")
      text_size = 36
      text_measure = measure_text_cached(self._font_title, tab_name, text_size)
      text_x = tab_x + (tab_width - text_measure.x) / 2
      text_y = y + (TAB_HEIGHT - text_size) / 2
      rl.draw_text_ex(self._font_title, tab_name, rl.Vector2(text_x, text_y), text_size, 0, rl.WHITE)

      if mouse_clicked and is_hovered:
        self._active_tab = i

    # Render active tab content
    tab_content_y = y + TAB_HEIGHT + 20
    active_tab = group.tabs[self._active_tab] if self._active_tab < len(group.tabs) else None
    tab_content_height = 0

    if active_tab:
      for tab_group in active_tab.get("groups", []):
        height = self._render_tab_group_content(tab_group, x, tab_content_y, width)
        tab_content_y += height + GROUP_SPACING
        tab_content_height += height + GROUP_SPACING

    return TAB_HEIGHT + 20 + tab_content_height

  def _render_tab_group_content(self, tab_group: dict, x: float, y: float, width: float) -> float:
    """Render a tab's group content"""
    controls = tab_group.get("controls", [])
    control_widgets = tab_group.get("control_widgets", [])

    if not controls:
      return 0

    # Filter visible controls
    visible = []
    for i, config in enumerate(controls):
      if self._check_visibility(config):
        widget = control_widgets[i] if i < len(control_widgets) else None
        visible.append((config, widget))

    if not visible:
      return 0

    # Calculate height
    content_height = GROUP_MARGIN_TOP + GROUP_PADDING + 15
    for i, (config, widget) in enumerate(visible):
      content_height += self._get_control_height(config)
      if i < len(visible) - 1:
        content_height += CONTROL_IN_GROUP_SPACING
    content_height += GROUP_PADDING

    # Draw group background
    group_rect = rl.Rectangle(x, y + GROUP_MARGIN_TOP, width, content_height - GROUP_MARGIN_TOP)
    roundness = GROUP_BORDER_RADIUS / (min(group_rect.width, group_rect.height) / 2)
    rl.draw_rectangle_rounded(group_rect, roundness, 10, COLOR_BG)

    # Draw title if present
    title = tab_group.get("title", "")
    if title:
      title_y = int(y + GROUP_MARGIN_TOP - GROUP_TITLE_SIZE // 2 + 5)
      title_padding = 15
      title_measure = measure_text_cached(self._font_title, title, GROUP_TITLE_SIZE)
      title_bg_rect = rl.Rectangle(x + 35, title_y - 5, title_measure.x + title_padding * 2, GROUP_TITLE_SIZE + 10)
      rl.draw_rectangle_rounded(title_bg_rect, 0.3, 10, COLOR_BG)
      rl.draw_text_ex(self._font_title, title,
                      rl.Vector2(x + 35 + title_padding, title_y),
                      GROUP_TITLE_SIZE, 0, COLOR_GROUP_TITLE)

    # Draw controls
    control_y = y + GROUP_MARGIN_TOP + GROUP_PADDING + 15
    for i, (config, widget) in enumerate(visible):
      if widget:
        widget.set_enabled(self._check_enabled(config))
        widget.set_description(self._get_dynamic_description(config))

        control_rect = rl.Rectangle(x + GROUP_PADDING, control_y,
                                    width - GROUP_PADDING * 2, self._get_control_height(config))
        widget.render(control_rect)

      control_y += self._get_control_height(config)
      if i < len(visible) - 1:
        control_y += CONTROL_IN_GROUP_SPACING

    return content_height

  def _get_tab_panel_height(self, group: GroupConfig) -> float:
    """Calculate tab panel height"""
    active_tab = group.tabs[self._active_tab] if self._active_tab < len(group.tabs) else None
    content_height = 0

    if active_tab:
      for tab_group in active_tab.get("groups", []):
        controls = tab_group.get("controls", [])
        visible = [c for c in controls if self._check_visibility(c)]
        if visible:
          content_height += GROUP_MARGIN_TOP + GROUP_PADDING * 2 + 15
          for i, config in enumerate(visible):
            content_height += self._get_control_height(config)
            if i < len(visible) - 1:
              content_height += CONTROL_IN_GROUP_SPACING
          content_height += GROUP_SPACING

    return TAB_HEIGHT + 20 + content_height

  # ============================================================
  # Dialog Callbacks
  # ============================================================

  def _show_selection_dialog(self, config: ControlConfig):
    """Show selection dialog for a control"""
    options = [opt.get("name", str(i)) for i, opt in enumerate(config.options)]
    current = ""
    current_value = self._get_param_value(config.param)
    for opt in config.options:
      if str(opt.get("value", "")) == current_value:
        current = opt.get("name", "")
        break

    def on_select(selected_name: str):
      for opt in config.options:
        if opt.get("name", "") == selected_name:
          self._set_param_value(config.param, str(opt.get("value", "")))
          break

    self._modal.show_selection(config.title, options, current, on_select)

  def _execute_command(self, config: ControlConfig):
    """Execute a command button's command"""
    if config.confirm:
      confirm_text = config.confirm_text or "Are you sure you want to execute this command?"
      yes_text = config.confirm_yes_text or "Yes"
      no_text = config.confirm_no_text or "No"
      self._modal.show_confirm("Confirmation Required", confirm_text, yes_text, no_text,
                               lambda: self._run_command(config.command, config.working_dir))
    else:
      self._run_command(config.command, config.working_dir)

  def _run_command(self, command: str, working_dir: str = ""):
    """Run a shell command"""
    if not command:
      return
    try:
      cwd = working_dir if working_dir else None
      subprocess.Popen(command, shell=True, cwd=cwd)
    except Exception as e:
      print(f"BPBasePanel: Failed to execute command: {e}")

  def _handle_restart_ui(self, config: ControlConfig):
    """Handle restart UI button"""
    if config.confirm:
      confirm_text = config.confirm_text or "Are you sure you want to restart the user interface?"
      yes_text = config.confirm_yes_text or "Restart"
      no_text = config.confirm_no_text or "Cancel"
      self._modal.show_confirm("Restart UI", confirm_text, yes_text, no_text, self._restart_ui)
    else:
      self._restart_ui()

  def _restart_ui(self):
    """Restart the UI"""
    import sys
    sys.exit(18)

  # ============================================================
  # Parameter Helpers
  # ============================================================

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
      print(f"BPBasePanel: Failed to set {param}: {e}")

  # ============================================================
  # Public Methods
  # ============================================================

  def refresh(self):
    """Refresh all control states from params"""
    for group in self._groups:
      for widget in group.control_widgets:
        if hasattr(widget, 'refresh'):
          widget.refresh()

  def show_event(self):
    """Called when panel becomes visible"""
    super().show_event()
    self._conditions.invalidate_cache()
    self.refresh()

  def hide_event(self):
    """Called when panel is hidden"""
    super().hide_event()
