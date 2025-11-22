"""
BluePilot Offroad Home Layout
Port of Qt OffroadHome with BluePilot version badges
"""

import time
import os
import pyray as rl
from enum import IntEnum
from collections.abc import Callable
from openpilot.common.params import Params
from openpilot.selfdrive.ui.widgets.offroad_alerts import UpdateAlert, OffroadAlert
from openpilot.selfdrive.ui.widgets.exp_mode_button import ExperimentalModeButton
from openpilot.selfdrive.ui.widgets.prime import PrimeWidget
from openpilot.selfdrive.ui.widgets.setup import SetupWidget
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.lib.application import gui_app, FontWeight, DEFAULT_TEXT_COLOR
from openpilot.system.ui.widgets import Widget
from bluepilot.ui.lib.colors import BPColors
from bluepilot.ui.lib.constants import BPConstants
from bluepilot.ui.widgets.badge_widget import BadgeWidget, VersionBadgeGroup


HEADER_HEIGHT = 80
HEAD_BUTTON_FONT_SIZE = 40
CONTENT_MARGIN = 40
SPACING = 25
RIGHT_COLUMN_WIDTH = 750
REFRESH_INTERVAL = 10.0


class HomeLayoutState(IntEnum):
  HOME = 0
  UPDATE = 1
  ALERTS = 2


class HomeLayoutBP(Widget):
  """
  BluePilot offroad home screen with version badges.

  Features:
  - Version badges in header (Brand, Branch, Commit, Date)
  - Update notification button
  - Alert notification button
  - Prime widget and Experimental mode button
  - Setup widget
  """

  def __init__(self):
    super().__init__()
    self.params = Params()

    self.update_alert = UpdateAlert()
    self.offroad_alert = OffroadAlert()

    self.current_state = HomeLayoutState.HOME
    self.last_refresh = 0
    self.settings_callback: Callable | None = None

    self.update_available = False
    self.alert_count = 0

    # Layout rectangles
    self.header_rect = rl.Rectangle(0, 0, 0, 0)
    self.content_rect = rl.Rectangle(0, 0, 0, 0)
    self.left_column_rect = rl.Rectangle(0, 0, 0, 0)
    self.right_column_rect = rl.Rectangle(0, 0, 0, 0)
    self.update_notif_rect = rl.Rectangle(0, 0, 200, HEADER_HEIGHT - 10)
    self.alert_notif_rect = rl.Rectangle(0, 0, 220, HEADER_HEIGHT - 10)

    # Widgets
    self._prime_widget = PrimeWidget()
    self._setup_widget = SetupWidget()
    self._exp_mode_button = ExperimentalModeButton()

    # BluePilot version badges
    self._version_badges = VersionBadgeGroup()

    self._setup_callbacks()
    self._refresh_version_info()

  def _setup_callbacks(self):
    self.update_alert.set_dismiss_callback(lambda: self._set_state(HomeLayoutState.HOME))
    self.offroad_alert.set_dismiss_callback(lambda: self._set_state(HomeLayoutState.HOME))

  def set_settings_callback(self, callback: Callable):
    self.settings_callback = callback

  def _set_state(self, state: HomeLayoutState):
    self.current_state = state

  def _refresh_version_info(self):
    """Refresh BluePilot version badges"""
    # Get version description
    desc = self.params.get("UpdaterCurrentDescription") or ""
    parts = desc.split(" / ") if desc else []

    # Read BP version from BPVERSION file
    bp_version = ""
    version_paths = [
      "/data/openpilot/BPVERSION",
      "../../BPVERSION",
      os.path.join(os.path.dirname(__file__), "../../../../BPVERSION")
    ]

    for path in version_paths:
      try:
        if os.path.exists(path):
          with open(path, 'r') as f:
            bp_version = f.readline().strip()
            if bp_version:
              break
      except Exception:
        continue

    # Build brand text
    brand = "BluePilot"
    if bp_version:
      brand = f"{brand} v{bp_version}"
    elif len(parts) >= 1:
      brand = f"{brand} {parts[0].strip()}"

    # Update badges
    branch = parts[1].strip() if len(parts) >= 2 else ""
    commit = parts[2].strip() if len(parts) >= 3 else ""
    date = parts[3].strip() if len(parts) >= 4 else ""

    self._version_badges.update_version_info(brand, branch, commit, date)

  def _render(self, rect: rl.Rectangle):
    current_time = time.monotonic()
    if current_time - self.last_refresh >= REFRESH_INTERVAL:
      self._refresh()
      self.last_refresh = current_time

    self._handle_input()
    self._render_header()

    # Render content based on current state
    if self.current_state == HomeLayoutState.HOME:
      self._render_home_content()
    elif self.current_state == HomeLayoutState.UPDATE:
      self._render_update_view()
    elif self.current_state == HomeLayoutState.ALERTS:
      self._render_alerts_view()

  def _update_layout_rects(self):
    self.header_rect = rl.Rectangle(
      self._rect.x + CONTENT_MARGIN,
      self._rect.y + CONTENT_MARGIN,
      self._rect.width - 2 * CONTENT_MARGIN,
      HEADER_HEIGHT
    )

    content_y = self._rect.y + CONTENT_MARGIN + HEADER_HEIGHT + SPACING
    content_height = self._rect.height - CONTENT_MARGIN - HEADER_HEIGHT - SPACING - CONTENT_MARGIN

    self.content_rect = rl.Rectangle(
      self._rect.x + CONTENT_MARGIN,
      content_y,
      self._rect.width - 2 * CONTENT_MARGIN,
      content_height
    )

    left_width = self.content_rect.width - RIGHT_COLUMN_WIDTH - SPACING

    self.left_column_rect = rl.Rectangle(
      self.content_rect.x,
      self.content_rect.y,
      left_width,
      self.content_rect.height
    )

    self.right_column_rect = rl.Rectangle(
      self.content_rect.x + left_width + SPACING,
      self.content_rect.y,
      RIGHT_COLUMN_WIDTH,
      self.content_rect.height
    )

    # Update notification button positions
    self.update_notif_rect.x = self.header_rect.x
    self.update_notif_rect.y = self.header_rect.y + (self.header_rect.height - 60) // 2

    notif_x = self.header_rect.x + (220 if self.update_available else 0)
    self.alert_notif_rect.x = notif_x
    self.alert_notif_rect.y = self.header_rect.y + (self.header_rect.height - 60) // 2

  def _handle_input(self):
    if not rl.is_mouse_button_pressed(rl.MouseButton.MOUSE_BUTTON_LEFT):
      return

    mouse_pos = rl.get_mouse_position()

    if self.update_available and rl.check_collision_point_rec(mouse_pos, self.update_notif_rect):
      self._set_state(HomeLayoutState.UPDATE)
      return

    if self.alert_count > 0 and rl.check_collision_point_rec(mouse_pos, self.alert_notif_rect):
      self._set_state(HomeLayoutState.ALERTS)
      return

    # Content area input handling
    if self.current_state == HomeLayoutState.UPDATE:
      self.update_alert.handle_input(mouse_pos, True)
    elif self.current_state == HomeLayoutState.ALERTS:
      self.offroad_alert.handle_input(mouse_pos, True)

  def _render_header(self):
    font = gui_app.font(FontWeight.MEDIUM)

    # Update notification button
    if self.update_available:
      highlight_color = rl.Color(255, 140, 40, 255) if self.current_state == HomeLayoutState.UPDATE else rl.Color(255, 102, 0, 255)
      rl.draw_rectangle_rounded(self.update_notif_rect, 0.3, 10, highlight_color)

      text = "UPDATE"
      text_width = measure_text_cached(font, text, HEAD_BUTTON_FONT_SIZE).x
      text_x = self.update_notif_rect.x + (self.update_notif_rect.width - text_width) // 2
      text_y = self.update_notif_rect.y + (self.update_notif_rect.height - HEAD_BUTTON_FONT_SIZE) // 2
      rl.draw_text_ex(font, text, rl.Vector2(int(text_x), int(text_y)), HEAD_BUTTON_FONT_SIZE, 0, rl.WHITE)

    # Alert notification button
    if self.alert_count > 0:
      highlight_color = rl.Color(255, 70, 70, 255) if self.current_state == HomeLayoutState.ALERTS else rl.Color(226, 44, 44, 255)
      rl.draw_rectangle_rounded(self.alert_notif_rect, 0.3, 10, highlight_color)

      alert_text = f"{self.alert_count} ALERT{'S' if self.alert_count > 1 else ''}"
      text_width = measure_text_cached(font, alert_text, HEAD_BUTTON_FONT_SIZE).x
      text_x = self.alert_notif_rect.x + (self.alert_notif_rect.width - text_width) // 2
      text_y = self.alert_notif_rect.y + (self.alert_notif_rect.height - HEAD_BUTTON_FONT_SIZE) // 2
      rl.draw_text_ex(font, alert_text, rl.Vector2(int(text_x), int(text_y)), HEAD_BUTTON_FONT_SIZE, 0, rl.WHITE)

    # BluePilot version badges (right aligned)
    badge_rect = rl.Rectangle(
      self.header_rect.x,
      self.header_rect.y + (self.header_rect.height - BPConstants.BADGE_HEIGHT) // 2,
      self.header_rect.width,
      BPConstants.BADGE_HEIGHT
    )
    self._version_badges.render(badge_rect)

  def _render_home_content(self):
    self._render_left_column()
    self._render_right_column()

  def _render_update_view(self):
    self.update_alert.render(self.content_rect)

  def _render_alerts_view(self):
    self.offroad_alert.render(self.content_rect)

  def _render_left_column(self):
    self._prime_widget.render(self.left_column_rect)

  def _render_right_column(self):
    exp_height = 125
    exp_rect = rl.Rectangle(
      self.right_column_rect.x,
      self.right_column_rect.y,
      self.right_column_rect.width,
      exp_height
    )
    self._exp_mode_button.render(exp_rect)

    setup_rect = rl.Rectangle(
      self.right_column_rect.x,
      self.right_column_rect.y + exp_height + SPACING,
      self.right_column_rect.width,
      self.right_column_rect.height - exp_height - SPACING,
    )
    self._setup_widget.render(setup_rect)

  def _refresh(self):
    self.update_available = self.update_alert.refresh()
    self.alert_count = self.offroad_alert.refresh()
    self._update_state_priority(self.update_available, self.alert_count > 0)
    self._refresh_version_info()

  def _update_state_priority(self, update_available: bool, alerts_present: bool):
    current_state = self.current_state

    if not update_available and not alerts_present:
      self.current_state = HomeLayoutState.HOME
    elif update_available and (current_state == HomeLayoutState.HOME or (not alerts_present and current_state == HomeLayoutState.ALERTS)):
      self.current_state = HomeLayoutState.UPDATE
    elif alerts_present and (current_state == HomeLayoutState.HOME or (not update_available and current_state == HomeLayoutState.UPDATE)):
      self.current_state = HomeLayoutState.ALERTS
