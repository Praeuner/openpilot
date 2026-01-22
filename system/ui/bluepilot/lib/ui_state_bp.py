"""
BluePilot Extended UI State
Extends the base UIState with BluePilot-specific state tracking
"""

import time
from cereal import log
from openpilot.common.params import Params
from openpilot.selfdrive.ui.ui_state import UIState, Device


class UIStateBP(UIState):
  """
  Extended UIState with BluePilot-specific features:
  - Recording audio status
  - SunnyLink status
  - Display brightness settings
  - BluePilot overlay flags
  """

  _instance: 'UIStateBP | None' = None

  def __new__(cls):
    if cls._instance is None:
      cls._instance = super(UIState, cls).__new__(cls)
      cls._instance._initialize()
      cls._instance._initialize_bp()
    return cls._instance

  def _initialize_bp(self):
    """Initialize BluePilot-specific state"""
    self.params_bp = Params()

    # Recording state
    self.recording_audio: bool = False

    # SunnyLink state
    self.sunnylink_enabled: bool = False
    self.sunnylink_status: str = "DISABLED"
    self.sunnylink_last_ping: int = 0

    # Display brightness
    self.onroad_display_brightness: int = 0
    self.display_brightness_auto: float = 0.0
    self.display_brightness_manual_enabled: bool = False
    self.display_brightness_manual: int = 0

    # BluePilot overlay flags
    self.show_hybrid_drive_overlay: bool = False
    self.hybrid_drive_gauge_size: int = 2  # 1=small, 2=medium, 3=large
    self.radar_overlay_size: int = 2       # 1=small, 2=medium, 3=large
    self.show_hybrid_battery_overlay: bool = False
    self.show_animated_wheel_angle: bool = False
    self.show_bp_radar_overlay: bool = False
    self.show_blindspot_indicators: bool = False
    self.show_stop_indicator_overlay: bool = False
    self.show_standstill_timer: bool = False
    self.show_gforce_meter: bool = False
    self.show_brake_status: bool = False
    self.dev_ui_info: int = 0  # 0=off, 1=right panel, 2=right+bottom

    # Sidebar visibility
    self.sidebar_visible: bool = True

    # Last update timestamps
    self._last_params_update: float = 0
    self._params_update_interval: float = 1.0  # Update params every 1 second

  def update(self) -> None:
    """Update state including BluePilot extensions"""
    super().update()
    self._update_bp_state()

  def _update_bp_state(self) -> None:
    """Update BluePilot-specific state"""
    current_time = time.monotonic()

    # Update params periodically (not every frame for performance)
    if current_time - self._last_params_update >= self._params_update_interval:
      self._update_bp_params()
      self._last_params_update = current_time

    # Update recording audio from carState if available
    if self.sm.updated.get("carState"):
      try:
        # Check if recording audio flag exists
        car_state = self.sm["carState"]
        # This may not exist in all versions
        self.recording_audio = getattr(car_state, 'recordingAudio', False)
      except Exception:
        self.recording_audio = False

  def _update_bp_params(self) -> None:
    """Update BluePilot params-based state"""
    try:
      # SunnyLink status
      self.sunnylink_enabled = self.params_bp.get_bool("SunnylinkEnabled")

      if self.sunnylink_enabled:
        ping_str = self.params_bp.get("LastSunnylinkPingTime") or "0"
        self.sunnylink_last_ping = int(ping_str) if ping_str else 0

        if self.sunnylink_last_ping == 0:
          dongle_id = self.params_bp.get("SunnylinkDongleId")
          self.sunnylink_status = "OFFLINE" if dongle_id else "REGIST..."
        else:
          elapsed = time.monotonic_ns() - self.sunnylink_last_ping
          if elapsed < 80_000_000_000:  # 80 seconds
            self.sunnylink_status = "ONLINE"
          else:
            self.sunnylink_status = "ERROR"
      else:
        self.sunnylink_status = "DISABLED"

      # Display brightness settings
      self.display_brightness_manual_enabled = self.params_bp.get_bool("DisplayBrightnessManualEnabled")
      if self.display_brightness_manual_enabled:
        brightness_str = self.params_bp.get("DisplayBrightnessManual") or "50"
        self.display_brightness_manual = int(brightness_str) if brightness_str else 50

      # Overlay flags (read less frequently as they're usually static)
      self.show_hybrid_drive_overlay = self.params_bp.get_bool("ShowHybridDriveOverlay")
      self.show_hybrid_battery_overlay = self.params_bp.get_bool("ShowHybridBatteryOverlay")
      self.show_bp_radar_overlay = self.params_bp.get_bool("ShowBPRadarOverlay")
      self.show_blindspot_indicators = self.params_bp.get_bool("ShowBlindspotIndicators")
      self.show_stop_indicator_overlay = self.params_bp.get_bool("ShowStopIndicatorOverlay")
      self.show_standstill_timer = self.params_bp.get_bool("StandstillTimer")
      self.show_brake_status = self.params_bp.get_bool("ShowBrakeStatus")

      # Dev UI info (debug panels)
      dev_ui_str = self.params_bp.get("DevUIInfo") or "0"
      self.dev_ui_info = int(dev_ui_str) if dev_ui_str.isdigit() else 0

      # Overlay size settings
      gauge_size_str = self.params_bp.get("HybridDriveGaugeSize") or "2"
      self.hybrid_drive_gauge_size = int(gauge_size_str) if gauge_size_str.isdigit() else 2
      radar_size_str = self.params_bp.get("RadarOverlaySize") or "2"
      self.radar_overlay_size = int(radar_size_str) if radar_size_str.isdigit() else 2

    except Exception:
      # Silently handle missing params
      pass

  def get_sunnylink_color(self):
    """Get the appropriate color for SunnyLink status"""
    from system.ui.bluepilot.lib.colors import BPColors

    if not self.sunnylink_enabled:
      return BPColors.DISABLED
    elif self.sunnylink_status == "ONLINE":
      return BPColors.GOOD
    elif self.sunnylink_status in ("OFFLINE", "REGIST..."):
      return BPColors.WARNING if self.sunnylink_status == "OFFLINE" else BPColors.PROGRESS
    else:
      return BPColors.DANGER


# Create singleton instance
def get_ui_state_bp() -> UIStateBP:
  """Get the BluePilot UI state singleton"""
  return UIStateBP()


# For compatibility, also expose as ui_state_bp
ui_state_bp = UIStateBP()
