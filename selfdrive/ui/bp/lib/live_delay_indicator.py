"""Onroad steering-lag calibration indicator (shared by TICI and MICI HUDs).

Shows the icon while liveDelay is not yet estimated; green once vEgo is above
MIN_VEGO, meaning samples are actually being collected. Hidden once estimated.
"""
import pyray as rl

from openpilot.selfdrive.locationd.lagd import MIN_VEGO
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import gui_app

ICON_ASPECT = 270 / 387  # liveDelay.png is 387x270

IDLE = rl.Color(255, 255, 255, 200)     # too slow to collect samples
ACTIVE = rl.Color(60, 220, 120, 235)    # above MIN_VEGO, estimating
BACKDROP = rl.Color(0, 0, 0, 65)
PAD = 10


class LiveDelayIndicator:
  def __init__(self, width: int = 64):
    self.width = width
    self.height = round(width * ICON_ASPECT)
    self._icon = gui_app.texture("icons/liveDelay.png", width, self.height)

  def render(self, x: float, y: float) -> None:
    sm = ui_state.sm
    if not sm.valid.get('liveDelay') or sm['liveDelay'].status == 'estimated':
      return

    backdrop = rl.Rectangle(x - PAD, y - PAD, self.width + PAD * 2, self.height + PAD * 2)
    rl.draw_rectangle_rounded(backdrop, 0.15, 10, BACKDROP)
    rl.draw_texture(self._icon, int(x), int(y), ACTIVE if sm['carState'].vEgo >= MIN_VEGO else IDLE)


def demo():
  ind = LiveDelayIndicator(width=64)
  assert ind.height == 45
  assert MIN_VEGO > 0
  print("ok")


if __name__ == "__main__":
  demo()
