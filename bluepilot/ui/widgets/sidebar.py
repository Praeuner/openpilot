"""
BluePilot Sidebar Component for Raylib UI
Port of Qt SidebarBP with network card, metric cards, fan animation, and buttons

State Management:
- Uses ui_state.sm for deviceState, pandaStates (already subscribed in main loop)
- Manages BluePilot-specific params locally (SunnyLink, overlays, etc.)
- Updates metrics at a reduced rate for performance
"""

import time
import pyray as rl
from collections.abc import Callable
from cereal import log
from openpilot.common.params import Params
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import gui_app, FontWeight, MousePos
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget
from bluepilot.ui.lib.colors import BPColors
from bluepilot.ui.lib.constants import BPConstants, get_carrier_name
from bluepilot.ui.widgets.metric_card import MetricCard, MetricData
from bluepilot.ui.widgets.network_card import NetworkCard
from bluepilot.ui.widgets.icon_button import IconButton, FanWidget


ThermalStatus = log.DeviceState.ThermalStatus
NetworkType = log.DeviceState.NetworkType

NETWORK_TYPES = {
  NetworkType.none: "Offline",
  NetworkType.wifi: "WiFi",
  NetworkType.cell2G: "2G",
  NetworkType.cell3G: "3G",
  NetworkType.cell4G: "LTE",
  NetworkType.cell5G: "5G",
  NetworkType.ethernet: "Ethernet",
}

# Update intervals (in frames at 20Hz UI rate)
METRICS_UPDATE_INTERVAL = 20  # ~1 second
PARAMS_UPDATE_INTERVAL = 40   # ~2 seconds


class SidebarBP(Widget):
  """
  BluePilot sidebar with:
  - Network card with carrier/SSID and signal bars
  - CPU, GPU, Memory metric cards (compact layout)
  - Vehicle, Connect, SunnyLink metric cards (standard layout)
  - Animated fan icon
  - Settings, Flag/Home, Debug buttons
  """

  def __init__(self):
    super().__init__()
    self.params = Params()

    # Network state
    self._net_type = "Offline"
    self._net_carrier_ssid = "No Connection"
    self._net_strength = 0

    # Metric values
    self._cpu_usage = "0%"
    self._cpu_temp = "N/A"
    self._gpu_usage = "0%"
    self._gpu_temp = "N/A"
    self._memory_usage = "0%"
    self._fan_demand = "0%"

    # Status values
    self._panda_status = ("VEHICLE", "ONLINE", BPColors.GOOD)
    self._connect_status = ("CONNECT", "OFFLINE", BPColors.WARNING)
    self._sunnylink_status = ("SUNNYLINK", "DISABLED", BPColors.DISABLED)

    # Recording state
    self._recording_audio = False

    # Callbacks
    self._on_settings_click: Callable | None = None
    self._on_flag_click: Callable | None = None
    self._on_debug_click: Callable | None = None
    self._on_network_click: Callable | None = None

    # Create child widgets
    self._network_card = NetworkCard()
    self._network_card.set_on_click(self._handle_network_click)

    # Metric cards (compact)
    self._cpu_card = MetricCard(compact=True)
    self._gpu_card = MetricCard(compact=True)
    self._memory_card = MetricCard(compact=True)

    # Metric cards (standard)
    self._vehicle_card = MetricCard(compact=False)
    self._connect_card = MetricCard(compact=False)
    self._sunnylink_card = MetricCard(compact=False)

    # Buttons
    self._settings_btn = IconButton("../assets/offroad/icon_settings.png")
    self._settings_btn.set_on_click(self._handle_settings_click)
    self._settings_btn.set_scale(0.55)

    self._flag_btn = IconButton("../assets/offroad/icon_flag.png")
    self._flag_btn.set_on_click(self._handle_flag_click)
    self._flag_btn.set_scale(0.45)

    self._debug_btn = IconButton("../assets/offroad/icon_debug.png")
    self._debug_btn.set_on_click(self._handle_debug_click)
    self._debug_btn.set_scale(0.65)

    # Fan widget - start animation on initial load (like Qt version)
    self._fan_widget = FanWidget()
    self._fan_widget.start_animation()

    # State tracking - frame counters for throttled updates
    self._metrics_counter = 0
    self._params_counter = 0
    self._last_update = 0

    # Layout rects (calculated in _update_layout_rects)
    self._network_rect = rl.Rectangle(0, 0, 0, 0)
    self._card_rects = []
    self._button_rects = {}

  def set_callbacks(self, on_settings: Callable = None, on_flag: Callable = None,
                    on_debug: Callable = None, on_network: Callable = None):
    """Set button callbacks"""
    self._on_settings_click = on_settings
    self._on_flag_click = on_flag
    self._on_debug_click = on_debug
    self._on_network_click = on_network

  def _handle_settings_click(self):
    if self._on_settings_click:
      self._on_settings_click()

  def _handle_flag_click(self):
    if self._on_flag_click:
      self._on_flag_click()

  def _handle_debug_click(self):
    if self._on_debug_click:
      self._on_debug_click()

  def _handle_network_click(self):
    if self._on_network_click:
      self._on_network_click()

  def _update_state(self):
    """Update state from ui_state.sm (SubMaster) and local params"""
    sm = ui_state.sm

    # Always animate fan if visible
    if self._fan_widget._is_animating:
      self._fan_widget.animate_step(2.0)

    # Only update on deviceState changes for most metrics
    if sm.updated.get('deviceState'):
      device_state = sm['deviceState']

      self._update_network_status(device_state)
      self._update_connection_status(device_state)
      self._update_performance_metrics(device_state)

      # Update widget states
      self._network_card.update_network(self._net_type, self._net_carrier_ssid, self._net_strength)

    # Update panda status on pandaStates changes
    if sm.updated.get('pandaStates'):
      self._update_panda_status()

    # Update params-based state at reduced rate (SunnyLink, etc.)
    self._params_counter += 1
    if self._params_counter >= PARAMS_UPDATE_INTERVAL:
      self._params_counter = 0
      self._update_sunnylink_status()

    # Update metric cards with current values
    self._update_metric_cards()

  def _update_network_status(self, device_state):
    """Update network type and strength"""
    self._net_type = NETWORK_TYPES.get(device_state.networkType.raw, "Unknown")
    strength = device_state.networkStrength
    self._net_strength = max(0, min(5, strength.raw + 1)) if strength > 0 else 0

    # Get carrier/SSID
    net_type = device_state.networkType
    if net_type == NetworkType.wifi:
      self._net_carrier_ssid = "WiFi Connected"  # Would need async SSID detection
    elif net_type in (NetworkType.cell2G, NetworkType.cell3G, NetworkType.cell4G, NetworkType.cell5G):
      # Try to get carrier name from network info
      try:
        if device_state.networkInfo and device_state.networkInfo.operator:
          operator_code = device_state.networkInfo.operator
          self._net_carrier_ssid = get_carrier_name(operator_code)
        else:
          self._net_carrier_ssid = self._net_type
      except Exception:
        self._net_carrier_ssid = self._net_type
    elif net_type == NetworkType.ethernet:
      self._net_carrier_ssid = "Ethernet"
    else:
      self._net_carrier_ssid = "No Connection"

  def _update_connection_status(self, device_state):
    """Update Athena connection status"""
    last_ping = device_state.lastAthenaPingTime
    if last_ping == 0:
      self._connect_status = ("CONNECT", "OFFLINE", BPColors.WARNING)
    elif time.monotonic_ns() - last_ping < 80_000_000_000:  # 80 seconds
      self._connect_status = ("CONNECT", "ONLINE", BPColors.GOOD)
    else:
      self._connect_status = ("CONNECT", "ERROR", BPColors.DANGER)

  def _update_panda_status(self):
    """Update vehicle/panda connection status"""
    if ui_state.panda_type == log.PandaState.PandaType.unknown:
      self._panda_status = ("VEHICLE", "OFFLINE", BPColors.DANGER)
    else:
      self._panda_status = ("VEHICLE", "ONLINE", BPColors.GOOD)

  def _update_performance_metrics(self, device_state):
    """Update CPU, GPU, Memory metrics at reduced rate"""
    self._metrics_counter += 1
    if self._metrics_counter < METRICS_UPDATE_INTERVAL:
      return
    self._metrics_counter = 0

    # CPU
    max_temp = device_state.maxTempC
    if -50.0 <= max_temp <= 150.0:
      self._cpu_temp = f"{max_temp:.1f}\u00b0C"
    else:
      self._cpu_temp = "N/A"

    cpu_usages = device_state.cpuUsagePercent
    if cpu_usages:
      avg_cpu = sum(cpu_usages) / len(cpu_usages)
      self._cpu_usage = f"{avg_cpu:.0f}%"
    else:
      self._cpu_usage = "0%"

    # GPU
    gpu_temps = device_state.gpuTempC
    if gpu_temps:
      self._gpu_temp = f"{gpu_temps[0]:.1f}\u00b0C"
    else:
      self._gpu_temp = "N/A"

    gpu_usage = device_state.gpuUsagePercent
    self._gpu_usage = f"{gpu_usage:.0f}%"

    # Memory
    mem_usage = device_state.memoryUsagePercent
    self._memory_usage = f"{mem_usage:.0f}%"

    # Fan
    fan_demand = device_state.fanSpeedPercentDesired
    self._fan_demand = f"{fan_demand:.0f}%"
    self._fan_widget.set_demand(self._fan_demand)

  def _update_sunnylink_status(self):
    """Update SunnyLink status"""
    try:
      sunnylink_enabled = self.params.get_bool("SunnylinkEnabled")
      last_ping_str = self.params.get("LastSunnylinkPingTime") or "0"
      last_ping = int(last_ping_str) if last_ping_str else 0

      if not sunnylink_enabled:
        self._sunnylink_status = ("SUNNYLINK", "DISABLED", BPColors.DISABLED)
      elif last_ping == 0:
        # Check if dongle ID exists
        dongle_id = self.params.get("SunnylinkDongleId")
        if dongle_id:
          self._sunnylink_status = ("SUNNYLINK", "OFFLINE", BPColors.WARNING)
        else:
          self._sunnylink_status = ("SUNNYLINK", "REGISTERING", BPColors.PROGRESS)
      else:
        elapsed = time.monotonic_ns() - last_ping
        if elapsed < 80_000_000_000:
          self._sunnylink_status = ("SUNNYLINK", "ONLINE", BPColors.GOOD)
        else:
          self._sunnylink_status = ("SUNNYLINK", "ERROR", BPColors.DANGER)
    except Exception:
      self._sunnylink_status = ("SUNNYLINK", "DISABLED", BPColors.DISABLED)

  def _update_metric_cards(self):
    """Update metric card data"""
    # CPU card
    cpu_color = BPColors.GOOD
    try:
      temp_val = float(self._cpu_temp.replace("\u00b0C", ""))
      if temp_val > 80:
        cpu_color = BPColors.DANGER
      elif temp_val > 70:
        cpu_color = BPColors.WARNING
    except ValueError:
      pass
    self._cpu_card.set_data(MetricData("CPU", "", self._cpu_usage, self._cpu_temp, cpu_color))

    # GPU card
    gpu_color = BPColors.GOOD
    try:
      temp_val = float(self._gpu_temp.replace("\u00b0C", ""))
      if temp_val > 75:
        gpu_color = BPColors.DANGER
      elif temp_val > 65:
        gpu_color = BPColors.WARNING
    except ValueError:
      pass
    self._gpu_card.set_data(MetricData("GPU", "", self._gpu_usage, self._gpu_temp, gpu_color))

    # Memory card
    mem_color = BPColors.GOOD
    try:
      mem_val = float(self._memory_usage.replace("%", ""))
      if mem_val > 85:
        mem_color = BPColors.DANGER
      elif mem_val > 70:
        mem_color = BPColors.WARNING
    except ValueError:
      pass
    self._memory_card.set_data(MetricData("MEMORY", "", self._memory_usage, "", mem_color))

    # Vehicle card
    self._vehicle_card.set_data(MetricData(
      self._panda_status[0], self._panda_status[1], "", "", self._panda_status[2]
    ))

    # Connect card
    self._connect_card.set_data(MetricData(
      self._connect_status[0], self._connect_status[1], "", "", self._connect_status[2]
    ))

    # SunnyLink card
    self._sunnylink_card.set_data(MetricData(
      self._sunnylink_status[0], self._sunnylink_status[1], "", "", self._sunnylink_status[2]
    ))

  def _update_layout_rects(self):
    """Calculate all layout rectangles"""
    rect = self._rect
    card_x = BPConstants.CARD_MARGIN
    card_width = BPConstants.CARD_WIDTH

    # Network card
    self._network_rect = rl.Rectangle(
      rect.x + card_x,
      rect.y + BPConstants.NETWORK_CARD_Y,
      card_width,
      BPConstants.NETWORK_CARD_HEIGHT
    )

    # Metric cards - calculate Y positions
    start_y = rect.y + BPConstants.NETWORK_CARD_Y + BPConstants.NETWORK_CARD_HEIGHT + BPConstants.CARD_SPACING

    self._card_rects = []
    y = start_y

    # Compact cards: CPU, GPU, Memory
    for _ in range(3):
      self._card_rects.append(rl.Rectangle(
        rect.x + card_x, y, card_width, BPConstants.METRIC_CARD_COMPACT_HEIGHT
      ))
      y += BPConstants.METRIC_CARD_COMPACT_HEIGHT + BPConstants.CARD_SPACING

    # Standard cards: Vehicle, Connect, SunnyLink
    for _ in range(3):
      self._card_rects.append(rl.Rectangle(
        rect.x + card_x, y, card_width, BPConstants.METRIC_CARD_STANDARD_HEIGHT
      ))
      y += BPConstants.METRIC_CARD_STANDARD_HEIGHT + BPConstants.CARD_SPACING

    # Button layout (right side)
    btn_size = BPConstants.BUTTON_SIZE
    btn_spacing = BPConstants.BUTTON_SPACING
    btn_margin = BPConstants.BUTTON_MARGIN
    btn_x = rect.x + rect.width - btn_size - btn_margin

    # Fan at top
    self._button_rects['fan'] = rl.Rectangle(btn_x, rect.y + 10, BPConstants.FAN_SIZE, BPConstants.FAN_SIZE)

    # Buttons from bottom up
    bottom_y = rect.y + rect.height - btn_margin - btn_size

    self._button_rects['settings'] = rl.Rectangle(btn_x, bottom_y, btn_size, btn_size)
    bottom_y -= btn_size + btn_spacing

    self._button_rects['flag'] = rl.Rectangle(btn_x, bottom_y, btn_size, btn_size)
    bottom_y -= btn_size + btn_spacing

    self._button_rects['debug'] = rl.Rectangle(btn_x, bottom_y, btn_size, btn_size)

  def _render(self, rect: rl.Rectangle) -> None:
    # Draw background with gradient
    self._draw_background(rect)

    # Draw network card
    self._network_card.render(self._network_rect)

    # Draw metric cards
    if len(self._card_rects) >= 6:
      self._cpu_card.render(self._card_rects[0])
      self._gpu_card.render(self._card_rects[1])
      self._memory_card.render(self._card_rects[2])
      self._vehicle_card.render(self._card_rects[3])
      self._connect_card.render(self._card_rects[4])
      self._sunnylink_card.render(self._card_rects[5])

    # Draw fan widget
    if 'fan' in self._button_rects:
      self._fan_widget.render(self._button_rects['fan'])

    # Draw buttons
    if 'settings' in self._button_rects:
      self._settings_btn.render(self._button_rects['settings'])

    # Only show flag and debug buttons when onroad
    if ui_state.started:
      if 'flag' in self._button_rects:
        self._flag_btn.render(self._button_rects['flag'])
      if 'debug' in self._button_rects:
        self._debug_btn.render(self._button_rects['debug'])

  def _draw_background(self, rect: rl.Rectangle):
    """Draw sidebar background with gradient"""
    # Main background
    rl.draw_rectangle_rec(rect, BPColors.BACKGROUND)

    # Subtle gradient effect on right side
    gradient_start = int(rect.x + rect.width * 0.7)
    gradient_width = int(rect.width * 0.3)

    for i in range(gradient_width):
      alpha = int(20 * (i / gradient_width))
      color = rl.Color(0, 0, 0, alpha)
      rl.draw_line(
        gradient_start + i, int(rect.y),
        gradient_start + i, int(rect.y + rect.height),
        color
      )

    # Right edge border
    rl.draw_line(
      int(rect.x + rect.width - 1), int(rect.y),
      int(rect.x + rect.width - 1), int(rect.y + rect.height),
      BPColors.with_alpha(BPColors.SHADOW, 60)
    )

  def show_event(self):
    """Called when sidebar becomes visible"""
    self._fan_widget.start_animation()

  def hide_event(self):
    """Called when sidebar is hidden"""
    self._fan_widget.stop_animation()
