#!/usr/bin/env python3
import os
import sys

# Set scale for macOS/PC before importing gui_app (which reads SCALE at import time)
# Default to 0.5 scale on macOS for a 1080x540 window (fits most screens)
if sys.platform == "darwin" and "SCALE" not in os.environ:
  os.environ["SCALE"] = "0.5"

import pyray as rl
from openpilot.common.watchdog import kick_watchdog
from openpilot.system.ui.lib.application import gui_app
from openpilot.system.hardware import PC
from openpilot.selfdrive.ui.ui_state import ui_state

# BluePilot: Use BluePilot layout with custom sidebar and home screen
# Falls back to stock MainLayout if BluePilot UI is not available
try:
  from system.ui.bluepilot.layouts.main_bp import MainLayoutBP as MainLayout
  BLUEPILOT_UI = True
except ImportError:
  from openpilot.selfdrive.ui.layouts.main import MainLayout
  BLUEPILOT_UI = False


def main():
  window_title = "BluePilot" if BLUEPILOT_UI else "UI"
  gui_app.init_window(window_title)

  # On PC/macOS, disable window resizing for consistent UI testing
  if PC:
    rl.clear_window_state(rl.ConfigFlags.FLAG_WINDOW_RESIZABLE)

  main_layout = MainLayout()
  main_layout.set_rect(rl.Rectangle(0, 0, gui_app.width, gui_app.height))

  for _ in gui_app.render():
    ui_state.update()

    # TODO handle brightness and awake state here

    main_layout.render()

    kick_watchdog()


if __name__ == "__main__":
  main()
