"""
BluePilot Badge Widget
Version badge component for offroad home header
"""

import pyray as rl
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget
from bluepilot.ui.lib.colors import BPColors
from bluepilot.ui.lib.constants import BPConstants


class BadgeWidget(Widget):
  """
  A styled badge widget with accent bar on the left.
  Used for version info display in the offroad home header.
  """

  def __init__(self, text: str = "", accent_color: rl.Color = None):
    super().__init__()
    self._text = text
    self._accent_color = accent_color if accent_color else BPColors.ACCENT
    self._font = gui_app.font(FontWeight.SEMI_BOLD)
    self._calculated_width = BPConstants.BADGE_MIN_WIDTH

  def set_text(self, text: str):
    """Update the badge text"""
    self._text = text
    self._calculate_width()

  def set_accent_color(self, color: rl.Color):
    """Update the accent color"""
    self._accent_color = color

  def get_preferred_width(self) -> float:
    """Get the preferred width based on text content"""
    self._calculate_width()
    return self._calculated_width

  def _calculate_width(self):
    """Calculate required width based on text"""
    if self._text:
      text_size = measure_text_cached(self._font, self._text, BPConstants.FONT_SIZE_LARGE)
      self._calculated_width = max(BPConstants.BADGE_MIN_WIDTH, text_size.x + 35)
    else:
      self._calculated_width = BPConstants.BADGE_MIN_WIDTH

  def _render(self, rect: rl.Rectangle) -> None:
    if not self._text:
      return

    # Draw card shadow
    shadow_rect = rl.Rectangle(rect.x + 2, rect.y + 2, rect.width, rect.height)
    rl.draw_rectangle_rounded(shadow_rect, 0.2, 10, BPColors.SHADOW)

    # Draw card background
    rl.draw_rectangle_rounded(rect, 0.2, 10, BPColors.CARD_BACKGROUND)

    # Draw accent bar on left (clipped)
    rl.begin_scissor_mode(int(rect.x), int(rect.y), BPConstants.BADGE_ACCENT_WIDTH, int(rect.height))
    rl.draw_rectangle_rounded(rect, 0.2, 10, self._accent_color)
    rl.end_scissor_mode()

    # Draw text (vertically centered)
    text_size = measure_text_cached(self._font, self._text, BPConstants.FONT_SIZE_LARGE)
    text_x = rect.x + 18  # Left padding after accent bar
    text_y = rect.y + (rect.height - text_size.y) / 2
    text_pos = rl.Vector2(text_x, text_y)
    rl.draw_text_ex(self._font, self._text, text_pos,
                    BPConstants.FONT_SIZE_LARGE, 0, BPColors.WHITE)


class VersionBadgeGroup(Widget):
  """
  A group of version badges for displaying BluePilot version info.
  Includes: Brand, Branch, Commit, Date badges
  """

  def __init__(self):
    super().__init__()
    self._brand_badge = BadgeWidget("", BPColors.BADGE_BRAND)
    self._branch_badge = BadgeWidget("", BPColors.BADGE_BRANCH)
    self._commit_badge = BadgeWidget("", BPColors.BADGE_COMMIT)
    self._date_badge = BadgeWidget("", BPColors.BADGE_DATE)

    self._badges = [
      self._brand_badge,
      self._branch_badge,
      self._commit_badge,
      self._date_badge
    ]

    self._badge_spacing = 8

  def update_version_info(self, brand: str, branch: str = "", commit: str = "", date: str = ""):
    """Update all badge texts"""
    self._brand_badge.set_text(brand)
    self._branch_badge.set_text(branch)
    self._commit_badge.set_text(commit)
    self._date_badge.set_text(date)

  def get_total_width(self) -> float:
    """Calculate total width of all badges"""
    total = 0
    for badge in self._badges:
      if badge._text:
        total += badge.get_preferred_width() + self._badge_spacing
    return max(0, total - self._badge_spacing)  # Remove last spacing

  def _render(self, rect: rl.Rectangle) -> None:
    # Right-align badges within the rect
    total_width = self.get_total_width()
    x = rect.x + rect.width - total_width

    for badge in self._badges:
      if not badge._text:
        continue

      badge_width = badge.get_preferred_width()
      badge_rect = rl.Rectangle(x, rect.y, badge_width, BPConstants.BADGE_HEIGHT)
      badge.render(badge_rect)
      x += badge_width + self._badge_spacing
