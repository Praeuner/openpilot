from openpilot.common.params import Params
from openpilot.system.ui.widgets.scroller import NavScroller
from openpilot.selfdrive.ui.mici.widgets.button import BigButton
from openpilot.selfdrive.ui.mici.layouts.settings.toggles import TogglesLayoutMici
from openpilot.selfdrive.ui.mici.layouts.settings.network.network_layout import NetworkLayoutMici
from openpilot.selfdrive.ui.mici.layouts.settings.device import DeviceLayoutMici, PairBigButton
from openpilot.selfdrive.ui.mici.layouts.settings.developer import DeveloperLayoutMici
from openpilot.selfdrive.ui.mici.layouts.settings.firehose import FirehoseLayout
from openpilot.system.ui.lib.application import gui_app, FontWeight


class SettingsBigButton(BigButton):
  def _get_label_font_size(self):
    return 64


class SettingsLayout(NavScroller):
  """Base settings landing page.

  Subclasses override _get_panel_entries() to inject extra panels (SP adds
  sunnylink / models / vehicle / bluepilot; BP replaces with *BP panels).
  Panel instantiation is lazy — the panel class is stored and only
  instantiated when the user navigates into it, so expensive setups
  (WifiManager, etc.) are deferred.
  """

  # ---- overridable registry ----
  def _get_panel_entries(self):
    """Return list of (label, icon_path, icon_w, icon_h, panel_class) tuples.

    Subclasses should call super() and extend/replace the list.
    """
    return [
      ("toggles", "icons_mici/settings.png", 64, 64, TogglesLayoutMici),
      ("network", "icons_mici/settings/network/wifi_strength_full.png", 76, 56, NetworkLayoutMici),
      ("device", "icons_mici/settings/device_icon.png", 72, 58, DeviceLayoutMici),
      ("firehose", "icons_mici/settings/firehose.png", 52, 62, FirehoseLayout),
      ("developer", "icons_mici/settings/developer_icon.png", 64, 60, DeveloperLayoutMici),
    ]

  def _get_panel_kwargs(self, panel_class):
    """Return kwargs dict for panel_class(...).  Override in subclasses
    that need to pass back_callback or other args."""
    return {}

  def _make_button_widget(self, label, icon_path, icon_w, icon_h):
    """Create and return a button widget for a panel entry.

    Override in subclasses that use a different button style
    (e.g. BP uses BigCategoryTile).
    """
    return SettingsBigButton(label, "", gui_app.texture(icon_path, icon_w, icon_h))

  # ---- panel wiring (overridable by BP to use different visual layout) ----
  def _wire_panels(self):
    """Build button widgets from _get_panel_entries() and return them.

    Returns list of (button_widget,) tuples ready to be added to a scroller.
    Subclasses can override to use different button styles or skip PairBigButton.
    """
    entries = self._get_panel_entries()
    buttons = []
    for label, icon_path, icon_w, icon_h, panel_cls in entries:
      btn = self._make_button_widget(label, icon_path, icon_w, icon_h)
      kwargs = self._get_panel_kwargs(panel_cls)
      def make_cb(b=btn, cls=panel_cls, kw=kwargs):
        def cb():
          gui_app.push_widget(cls(**kw))
        return cb
      btn.set_click_callback(make_cb())
      buttons.append(btn)

    # Insert PairBigButton between device and firehose (index 3 → after device)
    pair_idx = 3  # toggles(0) network(1) device(2) ← pair here
    buttons.insert(pair_idx, PairBigButton())
    return buttons

  # ---- construction ----
  def __init__(self):
    super().__init__()
    self._params = Params()

    self._scroller.add_widgets(self._wire_panels())
    self._font_medium = gui_app.font(FontWeight.MEDIUM)
