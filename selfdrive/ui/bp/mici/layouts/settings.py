"""BluePilot MICI settings landing — horizontal row of category tiles.

Inherits panel registry from SettingsLayoutSP (stock → sunnylink/models →
vehicle/bluepilot) but overrides _wire_panels() to use BigCategoryTile
widgets and a horizontal scroller with radial background + topbar.
"""
import pyray as rl
from openpilot.common.params import Params
from openpilot.system.ui.lib.application import gui_app
from openpilot.system.ui.widgets.scroller import _Scroller
from openpilot.selfdrive.ui.sunnypilot.mici.layouts.settings import SettingsLayoutSP

from openpilot.selfdrive.ui.bp.mici.widgets.cards import BigCategoryTile
from openpilot.selfdrive.ui.bp.mici.widgets.bg_radial import BPRadialBackground
from openpilot.selfdrive.ui.bp.mici.widgets.bp_topbar import BPTopbar, TOPBAR_HEIGHT

# --- BP-styled panels (mirror stock content / replace legacy BP panels) ---
from openpilot.selfdrive.ui.bp.mici.layouts.panels.toggles_bp import TogglesLayoutBP
from openpilot.selfdrive.ui.bp.mici.layouts.panels.network_bp import NetworkLayoutBP
from openpilot.selfdrive.ui.bp.mici.layouts.panels.device_bp import DeviceLayoutBP
from openpilot.selfdrive.ui.bp.mici.layouts.panels.firehose_bp import FirehoseLayoutBP
from openpilot.selfdrive.ui.bp.mici.layouts.panels.developer_bp import DeveloperLayoutBP
from openpilot.selfdrive.ui.bp.mici.layouts.bluepilot import BluePilotLayoutBP
from openpilot.selfdrive.ui.bp.mici.layouts.vehicle_bp import VehicleLayoutBP


class SettingsLayoutBP(SettingsLayoutSP):
  """Reuses panel registry from SettingsLayoutSP but renders BP-styled tiles."""

  # ---- override panel registry to use *BP panel classes ----
  def _get_panel_entries(self):
    """Replace stock/SP panel classes with BP-styled variants, keep icon paths."""
    from openpilot.selfdrive.ui.mici.layouts.settings.toggles import TogglesLayoutMici
    from openpilot.selfdrive.ui.mici.layouts.settings.network.network_layout import NetworkLayoutMici
    from openpilot.selfdrive.ui.mici.layouts.settings.device import DeviceLayoutMici
    from openpilot.selfdrive.ui.mici.layouts.settings.firehose import FirehoseLayout
    from openpilot.selfdrive.ui.mici.layouts.settings.developer import DeveloperLayoutMici

    bp_panel_map = {
      TogglesLayoutMici: TogglesLayoutBP,
      NetworkLayoutMici: NetworkLayoutBP,
      DeviceLayoutMici: DeviceLayoutBP,
      FirehoseLayout: FirehoseLayoutBP,
      DeveloperLayoutMici: DeveloperLayoutBP,
    }
    # Also map legacy SP/BP vehicle/bluepilot panels if they appear in SP entries
    from openpilot.selfdrive.ui.bp.mici.layouts.vehicle_bp import VehicleLayoutBP as VehicleLayoutMici
    from openpilot.selfdrive.ui.bp.mici.layouts.bluepilot import BluePilotLayoutBP as BluePilotLayoutMici
    bp_panel_map[VehicleLayoutMici] = VehicleLayoutBP
    bp_panel_map[BluePilotLayoutMici] = BluePilotLayoutBP

    entries = super()._get_panel_entries()
    result = []
    for label, icon_path, icon_w, icon_h, panel_cls in entries:
      bp_cls = bp_panel_map.get(panel_cls, panel_cls)
      result.append((label, icon_path, icon_w, icon_h, bp_cls))
    return result

  def _get_panel_kwargs(self, panel_class):
    # BP panels (vehicle/bluepilot) expect back_callback
    if panel_class in (VehicleLayoutBP, BluePilotLayoutBP):
      return {"back_callback": gui_app.pop_widget}
    return super()._get_panel_kwargs(panel_class)

  # ---- override button widget factory ----
  def _make_button_widget(self, label, icon_path, icon_w, icon_h):
    return BigCategoryTile(label=label, icon=icon_path)

  # ---- override wiring: horizontal scroller, no PairBigButton ----
  def _wire_panels(self):
    entries = self._get_panel_entries()
    tiles = []
    for label, icon_path, icon_w, icon_h, panel_cls in entries:
      tile = self._make_button_widget(label, icon_path, icon_w, icon_h)
      kwargs = self._get_panel_kwargs(panel_cls)
      def make_cb(t=tile, cls=panel_cls, kw=kwargs):
        def cb():
          gui_app.push_widget(cls(**kw))
        return cb
      tile.set_click_callback(make_cb())
      tiles.append(tile)
    return tiles

  # ---- visual layout (distinct from stock) ----
  def __init__(self):
    # Don't call super().__init__() — we control the full visual layout.
    # Only inherit panel registry hooks.
    from openpilot.system.ui.widgets.nav_widget import NavWidget
    NavWidget.__init__(self)
    self._params = Params()  # match base class expectation

    self.set_rect(rl.Rectangle(0, 0, gui_app.width, gui_app.height))
    self._bg = self._child(BPRadialBackground())
    self._topbar = self._child(BPTopbar(title="Settings", on_back=self._on_back))

    self._scroller = self._child(_Scroller(
      self._wire_panels(), horizontal=True, snap_items=False,
      spacing=6, pad=10,
      scroll_indicator=True, edge_shadows=False,
    ))

  def _on_back(self):
    gui_app.pop_widget()

  def _render(self, _):
    r = self._rect
    self._bg.render(r)
    self._topbar.render(rl.Rectangle(r.x, r.y, r.width, TOPBAR_HEIGHT))
    body = rl.Rectangle(r.x, r.y + TOPBAR_HEIGHT, r.width, r.height - TOPBAR_HEIGHT)
    self._scroller.render(body)
