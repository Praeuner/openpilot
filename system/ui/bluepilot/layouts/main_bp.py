"""
BluePilot Main Layout
Integrates BluePilot sidebar and home screen with the main UI
Compatible with the existing MainLayout patterns from openpilot
"""

import pyray as rl
from enum import IntEnum
import cereal.messaging as messaging
from openpilot.selfdrive.ui.ui_state import ui_state, device
from openpilot.system.ui.widgets import Widget
from system.ui.bluepilot.lib.constants import BPConstants
from system.ui.bluepilot.widgets.sidebar import SidebarBP
from system.ui.bluepilot.offroad.home_bp import HomeLayoutBP
from system.ui.bluepilot.settings.settings_bp import BPSettingsLayout, BPPanelType

# Try to import BluePilot enhanced onroad view, fallback to stock
try:
    from system.ui.bluepilot.onroad.augmented_road_view_bp import AugmentedRoadViewBP
    BLUEPILOT_ONROAD = True
except ImportError:
    from openpilot.selfdrive.ui.onroad.augmented_road_view import AugmentedRoadView as AugmentedRoadViewBP
    BLUEPILOT_ONROAD = False


class MainState(IntEnum):
  HOME = 0
  SETTINGS = 1
  ONROAD = 2


class MainLayoutBP(Widget):
  """
  BluePilot main layout with:
  - BluePilot sidebar (wider 460px with network card, metrics, fan)
  - BluePilot home screen (with version badges)
  - Settings panel
  - Onroad view

  Drop-in replacement for MainLayout with BluePilot customizations.
  """

  def __init__(self):
    super().__init__()

    self._pm = messaging.PubMaster(['bookmarkButton'])

    # BluePilot sidebar (wider with more features)
    self._sidebar = SidebarBP()
    self._current_mode = MainState.HOME
    self._prev_onroad = False

    # Initialize layouts - using BluePilot home, settings, and onroad layouts
    self._layouts = {
      MainState.HOME: HomeLayoutBP(),
      MainState.SETTINGS: BPSettingsLayout(),
      MainState.ONROAD: AugmentedRoadViewBP()
    }

    # Layout rectangles
    self._sidebar_rect = rl.Rectangle(0, 0, 0, 0)
    self._content_rect = rl.Rectangle(0, 0, 0, 0)

    # Setup callbacks
    self._setup_callbacks()

  def _render(self, _):
    self._handle_onroad_transition()
    self._render_main_content()

  def _setup_callbacks(self):
    """Setup all button callbacks"""
    self._sidebar.set_callbacks(
      on_settings=self._on_settings_clicked,
      on_flag=self._on_bookmark_clicked,
      on_debug=self._on_debug_clicked,
      on_network=self._on_network_clicked
    )

    # Setup widget callbacks for home layout
    try:
      self._layouts[MainState.HOME]._setup_widget.set_open_settings_callback(
        lambda: self.open_settings(BPPanelType.FIREHOSE)
      )
    except AttributeError:
      # If _setup_widget doesn't exist, skip this
      pass

    self._layouts[MainState.SETTINGS].set_callbacks(on_close=self._set_mode_for_state)
    self._layouts[MainState.ONROAD].set_callbacks(on_click=self._on_onroad_clicked)
    device.add_interactive_timeout_callback(self._set_mode_for_state)

  def _update_layout_rects(self):
    """Calculate layout rectangles with BluePilot wider sidebar"""
    sidebar_width = BPConstants.SIDEBAR_WIDTH
    self._sidebar_rect = rl.Rectangle(self._rect.x, self._rect.y, sidebar_width, self._rect.height)

    x_offset = sidebar_width if self._sidebar.is_visible else 0
    self._content_rect = rl.Rectangle(
      self._rect.x + x_offset,
      self._rect.y,
      self._rect.width - x_offset,
      self._rect.height
    )

  def _handle_onroad_transition(self):
    """Handle transitions between onroad and offroad"""
    if ui_state.started != self._prev_onroad:
      self._prev_onroad = ui_state.started
      self._set_mode_for_state()

  def _set_mode_for_state(self):
    """Set the appropriate mode based on current state"""
    if ui_state.started:
      # Don't hide sidebar from interactive timeout
      if self._current_mode != MainState.ONROAD:
        self._sidebar.set_visible(False)
        self._sidebar.hide_event()
      self._set_current_layout(MainState.ONROAD)
    else:
      self._set_current_layout(MainState.HOME)
      self._sidebar.set_visible(True)
      self._sidebar.show_event()

  def _set_current_layout(self, layout: MainState):
    """Switch to a different layout"""
    if layout != self._current_mode:
      self._layouts[self._current_mode].hide_event()
      self._current_mode = layout
      self._layouts[self._current_mode].show_event()

  def open_settings(self, panel_type: BPPanelType):
    """Open settings panel"""
    self._layouts[MainState.SETTINGS].set_current_panel(panel_type)
    self._set_current_layout(MainState.SETTINGS)
    self._sidebar.set_visible(False)
    self._sidebar.hide_event()

  def _on_settings_clicked(self):
    """Handle settings button click"""
    self.open_settings(BPPanelType.DEVICE)

  def _on_bookmark_clicked(self):
    """Handle bookmark/flag button click"""
    user_bookmark = messaging.new_message('bookmarkButton')
    user_bookmark.valid = True
    self._pm.send('bookmarkButton', user_bookmark)

  def _on_debug_clicked(self):
    """Handle debug button click - opens developer panel"""
    self.open_settings(BPPanelType.DEVELOPER)

  def _on_network_clicked(self):
    """Handle network card click - opens network settings"""
    self.open_settings(BPPanelType.NETWORK)

  def _on_onroad_clicked(self):
    """Handle click on onroad view - toggle sidebar"""
    if self._sidebar.is_visible:
      self._sidebar.set_visible(False)
      self._sidebar.hide_event()
    else:
      self._sidebar.set_visible(True)
      self._sidebar.show_event()

  def _render_main_content(self):
    """Render sidebar and content"""
    # Render sidebar
    if self._sidebar.is_visible:
      self._sidebar.render(self._sidebar_rect)

    content_rect = self._content_rect if self._sidebar.is_visible else self._rect
    self._layouts[self._current_mode].render(content_rect)

  def show_event(self):
    """Called when main layout becomes visible"""
    self._sidebar.show_event()

  def hide_event(self):
    """Called when main layout is hidden"""
    self._sidebar.hide_event()
