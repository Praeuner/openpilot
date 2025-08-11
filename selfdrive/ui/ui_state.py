import pyray as rl
import numpy as np
import time
import threading
from collections.abc import Callable
from enum import Enum
from cereal import messaging, log
from openpilot.common.filter_simple import FirstOrderFilter
from openpilot.common.params import Params, UnknownKeyName
from openpilot.common.swaglog import cloudlog
from openpilot.selfdrive.ui.lib.prime_state import PrimeState
from openpilot.system.ui.lib.application import DEFAULT_FPS
from openpilot.system.hardware import HARDWARE
from openpilot.system.ui.lib.application import gui_app

UI_BORDER_SIZE = 30
BACKLIGHT_OFFROAD = 50


class UIStatus(Enum):
  DISENGAGED = "disengaged"
  ENGAGED = "engaged"
  OVERRIDE = "override"


class UIState:
  _instance: 'UIState | None' = None

  def __new__(cls):
    if cls._instance is None:
      cls._instance = super().__new__(cls)
      cls._instance._initialize()
    return cls._instance

  def _initialize(self):
    self.params = Params()
    self.sm = messaging.SubMaster(
      [
        "modelV2",
        "controlsState",
        "liveCalibration",
        "radarState",
        "deviceState",
        "pandaStates",
        "carParams",
        "driverMonitoringState",
        "carState",
        "carStateBP",
        "driverStateV2",
        "roadCameraState",
        "wideRoadCameraState",
        "managerState",
        "selfdriveState",
        "longitudinalPlan",
      ]
    )

    self.prime_state = PrimeState()

    # UI Status tracking
    self.status: UIStatus = UIStatus.DISENGAGED
    self.started_frame: int = 0
    self._engaged_prev: bool = False
    self._started_prev: bool = False

    # Core state variables
    self.is_metric: bool = self.params.get_bool("IsMetric")
    self.started: bool = False
    self.ignition: bool = False
    self.panda_type: log.PandaState.PandaType = log.PandaState.PandaType.unknown
    self.personality: log.LongitudinalPersonality = log.LongitudinalPersonality.standard
    self.light_sensor: float = -1.0

    self._update_params()

  @property
  def engaged(self) -> bool:
    return self.started and self.sm["selfdriveState"].enabled

  def is_onroad(self) -> bool:
    return self.started

  def is_offroad(self) -> bool:
    return not self.started

  def update(self) -> None:
    self.sm.update(0)
    self._update_state()
    self._update_status()
    device.update()

  def _update_state(self) -> None:
    # Handle panda states updates
    if self.sm.updated["pandaStates"]:
      panda_states = self.sm["pandaStates"]

      if len(panda_states) > 0:
        # Get panda type from first panda
        self.panda_type = panda_states[0].pandaType
        # Check ignition status across all pandas
        if self.panda_type != log.PandaState.PandaType.unknown:
          self.ignition = any(state.ignitionLine or state.ignitionCan for state in panda_states)
    elif self.sm.frame - self.sm.recv_frame["pandaStates"] > 5 * rl.get_fps():
      self.panda_type = log.PandaState.PandaType.unknown

    # Handle wide road camera state updates
    if self.sm.updated["wideRoadCameraState"]:
      cam_state = self.sm["wideRoadCameraState"]

      # Scale factor based on sensor type
      scale = 6.0 if cam_state.sensor == 'ar0231' else 1.0
      self.light_sensor = max(100.0 - scale * cam_state.exposureValPercent, 0.0)
    elif not self.sm.alive["wideRoadCameraState"] or not self.sm.valid["wideRoadCameraState"]:
      self.light_sensor = -1

    # Update started state
    self.started = self.sm["deviceState"].started and self.ignition

  def _update_status(self) -> None:
    if self.started and self.sm.updated["selfdriveState"]:
      ss = self.sm["selfdriveState"]
      state = ss.state

      if state in (log.SelfdriveState.OpenpilotState.preEnabled, log.SelfdriveState.OpenpilotState.overriding):
        self.status = UIStatus.OVERRIDE
      else:
        self.status = UIStatus.ENGAGED if ss.enabled else UIStatus.DISENGAGED

    # Check for engagement state changes
    if self.engaged != self._engaged_prev:
      self._engaged_prev = self.engaged

    # Handle onroad/offroad transition
    if self.started != self._started_prev or self.sm.frame == 1:
      if self.started:
        self.status = UIStatus.DISENGAGED
        self.started_frame = self.sm.frame

      self._started_prev = self.started

  def _update_params(self) -> None:
    try:
      self.is_metric = self.params.get_bool("IsMetric")
    except UnknownKeyName:
      self.is_metric = False


class Device:
  def __init__(self):
    self.params = Params()
    self._ignition = False
    self._interaction_time: float = -1
    self._interactive_timeout_callbacks: list[Callable] = []
    self._prev_timed_out = False
    self._awake = False

    # Onroad display behavior tracking
    self._onroad_start_time: float = -1
    self._onroad_display_behavior: int = 0  # Default: Do Nothing
    self._onroad_display_timeout: int = 30  # Default: 30 seconds
    self._onroad_display_applied: bool = False
    self._original_brightness: int = 0

    self._offroad_brightness: int = BACKLIGHT_OFFROAD
    self._last_brightness: int = 0
    self._brightness_filter = FirstOrderFilter(BACKLIGHT_OFFROAD, 10.00, 1 / DEFAULT_FPS)
    self._brightness_thread: threading.Thread | None = None

  def reset_interactive_timeout(self, timeout: int = -1) -> None:
    if timeout == -1:
      timeout = 10 if ui_state.ignition else 30
    self._interaction_time = time.monotonic() + timeout

  def add_interactive_timeout_callback(self, callback: Callable):
    self._interactive_timeout_callbacks.append(callback)

  def update(self):
    # do initial reset
    if self._interaction_time <= 0:
      self.reset_interactive_timeout()

    self._update_onroad_display_behavior()
    self._update_brightness()
    self._update_wakefulness()

  def set_offroad_brightness(self, brightness: int):
    # TODO: not yet used, should be used in prime widget for QR code, etc.
    self._offroad_brightness = min(max(brightness, 0), 100)

  def _update_onroad_display_behavior(self):
    """Handle onroad display behavior based on user settings."""
    try:
      # Get current settings
      self._onroad_display_behavior = int(self.params.get("OnroadDisplayBehavior", return_default=True))
      timeout_index = int(self.params.get("OnroadDisplayTimeout", return_default=True))

      # Convert timeout index to actual seconds
      timeout_seconds_map = {
        0: 30,    # 30 seconds
        1: 60,    # 1 minute
        2: 120,   # 2 minutes
        3: 180,   # 3 minutes
        4: 300,   # 5 minutes
        5: 600,   # 10 minutes
        6: 900,   # 15 minutes
      }
      self._onroad_display_timeout = timeout_seconds_map.get(timeout_index, 30)
    except (ValueError, KeyError):
      # Use defaults if parameters don't exist or are invalid
      self._onroad_display_behavior = 0
      self._onroad_display_timeout = 30

    # Only apply onroad behavior when actually onroad (started and not offroad)
    is_onroad = ui_state.started and not ui_state.is_offroad()

    if is_onroad and self._onroad_display_behavior > 0:  # Not "Do Nothing"
      # Start tracking onroad time
      if self._onroad_start_time < 0:
        self._onroad_start_time = time.monotonic()
        self._onroad_display_applied = False
        # Store the current brightness as original (will be updated in _update_brightness)
        if self._original_brightness == 0:
          self._original_brightness = self._last_brightness

      # Check if timeout has elapsed
      time_onroad = time.monotonic() - self._onroad_start_time
      if time_onroad >= self._onroad_display_timeout and not self._onroad_display_applied:
        self._apply_onroad_display_behavior()
        self._onroad_display_applied = True
    else:
      # Reset onroad tracking when not onroad or behavior is "Do Nothing"
      if self._onroad_start_time >= 0:
        self._reset_onroad_display_behavior()
        self._onroad_start_time = -1
        self._onroad_display_applied = False

  def _apply_onroad_display_behavior(self):
    """Apply the selected onroad display behavior."""
    # Ensure we have a valid original brightness
    if self._original_brightness <= 0:
      self._original_brightness = max(self._last_brightness, 50)  # Fallback to 50% if needed

    if self._onroad_display_behavior == 1:  # Dim 70%
      target_brightness = max(int(self._original_brightness * 0.7), 10)
    elif self._onroad_display_behavior == 2:  # Dim 50%
      target_brightness = max(int(self._original_brightness * 0.5), 10)
    elif self._onroad_display_behavior == 3:  # Dim 30%
      target_brightness = max(int(self._original_brightness * 0.3), 10)
    elif self._onroad_display_behavior == 4:  # Turn Off
      target_brightness = 0
    else:
      return  # Do Nothing

    # Apply the brightness change
    if target_brightness != self._last_brightness:
      cloudlog.debug(f"applying onroad display behavior: {self._onroad_display_behavior}, brightness: {target_brightness}")
      if self._brightness_thread is None or not self._brightness_thread.is_alive():
        self._brightness_thread = threading.Thread(target=HARDWARE.set_screen_brightness, args=(target_brightness,))
        self._brightness_thread.start()
        self._last_brightness = target_brightness

  def _reset_onroad_display_behavior(self):
    """Reset display to normal brightness when exiting onroad behavior."""
    if self._onroad_display_applied and self._onroad_display_behavior > 0:
      cloudlog.debug("resetting onroad display behavior")
      # Force brightness update to restore normal behavior
      self._last_brightness = -1  # Force update

  def _update_brightness(self):
    # Update original brightness if we're not in onroad behavior
    if not self._onroad_display_applied and self._last_brightness > 0:
      self._original_brightness = self._last_brightness

    clipped_brightness = self._offroad_brightness

    if ui_state.started and ui_state.light_sensor >= 0:
      clipped_brightness = ui_state.light_sensor

      # CIE 1931 - https://www.photonstophotos.net/GeneralTopics/Exposure/Psychometric_Lightness_and_Gamma.htm
      if clipped_brightness <= 8:
        clipped_brightness = clipped_brightness / 903.3
      else:
        clipped_brightness = ((clipped_brightness + 16.0) / 116.0) ** 3.0

      clipped_brightness = float(np.clip(100 * clipped_brightness, 10, 100))

    brightness = round(self._brightness_filter.update(clipped_brightness))

    # Don't override brightness if onroad behavior is applied
    if not self._awake:
      brightness = 0
    elif self._onroad_display_applied and self._onroad_display_behavior > 0:
      # Keep the applied onroad brightness
      return

    if brightness != self._last_brightness:
      if self._brightness_thread is None or not self._brightness_thread.is_alive():
        cloudlog.debug(f"setting display brightness {brightness}")
        self._brightness_thread = threading.Thread(target=HARDWARE.set_screen_brightness, args=(brightness,))
        self._brightness_thread.start()
        self._last_brightness = brightness

  def _update_wakefulness(self):
    # Handle interactive timeout
    ignition_just_turned_off = not ui_state.ignition and self._ignition
    self._ignition = ui_state.ignition

    if ignition_just_turned_off or any(ev.left_down for ev in gui_app.mouse_events):
      self.reset_interactive_timeout()

    interaction_timeout = time.monotonic() > self._interaction_time
    if interaction_timeout and not self._prev_timed_out:
      for callback in self._interactive_timeout_callbacks:
        callback()
    self._prev_timed_out = interaction_timeout

    self._set_awake(ui_state.ignition or not interaction_timeout)

  def _set_awake(self, on: bool):
    if on != self._awake:
      self._awake = on
      cloudlog.debug(f"setting display power {int(on)}")
      HARDWARE.set_display_power(on)


# Global instance
ui_state = UIState()
device = Device()
