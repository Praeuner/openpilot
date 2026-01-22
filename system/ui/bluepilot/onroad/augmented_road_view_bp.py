"""
BluePilot Augmented Road View
Extends the stock AugmentedRoadView with BluePilot-specific overlays and enhancements
"""

import pyray as rl
from openpilot.selfdrive.ui.onroad.augmented_road_view import AugmentedRoadView
from openpilot.selfdrive.ui.ui_state import ui_state
from system.ui.bluepilot.lib.ui_state_bp import UIStateBP
from system.ui.bluepilot.onroad.alert_renderer_bp import AlertRendererBP
from system.ui.bluepilot.onroad.model_renderer_bp import ModelRendererBP
from system.ui.bluepilot.onroad.hud_renderer_bp import HudRendererBP


class AugmentedRoadViewBP(AugmentedRoadView):
    """
    BluePilot enhanced onroad view with:
    - Hybrid gauges overlay (Ford power/battery display)
    - Enhanced radar overlay with info boxes
    - Stop sign detection overlay
    - Standstill timer overlay
    - Enhanced model rendering with glow effects
    - Pill-style alerts
    """

    def __init__(self):
        super().__init__()

        # Replace stock renderers with BluePilot versions
        self.model_renderer = ModelRendererBP()
        self._hud_renderer = HudRendererBP()
        self.alert_renderer = AlertRendererBP()

        # BluePilot UI state for overlay visibility flags
        self._bp_state = UIStateBP()

        # Initialize overlays (lazy import to avoid circular dependencies)
        self._hybrid_overlay = None
        self._radar_overlay = None
        self._stop_overlay = None
        self._standstill_overlay = None
        self._overlays_initialized = False

        # Debug panels (lazy initialized)
        self._lateral_debug = None
        self._long_debug = None
        self._debug_panels_initialized = False

    def _init_overlays(self):
        """Lazy initialization of overlays"""
        if self._overlays_initialized:
            return

        try:
            from system.ui.bluepilot.onroad.overlays.hybrid_gauges import HybridGaugesOverlay
            from system.ui.bluepilot.onroad.overlays.radar_overlay import RadarOverlay
            from system.ui.bluepilot.onroad.overlays.stop_sign_overlay import StopSignOverlay
            from system.ui.bluepilot.onroad.overlays.standstill_timer import StandstillTimerOverlay

            self._hybrid_overlay = HybridGaugesOverlay()
            self._radar_overlay = RadarOverlay()
            self._stop_overlay = StopSignOverlay()
            self._standstill_overlay = StandstillTimerOverlay()
            self._overlays_initialized = True
        except ImportError as e:
            print(f"BluePilot overlays not available: {e}")
            self._overlays_initialized = True  # Don't retry

    def _init_debug_panels(self):
        """Lazy initialization of debug panels"""
        if self._debug_panels_initialized:
            return

        try:
            from system.ui.bluepilot.onroad.debug.lateral_debug_panel import LateralDebugPanel
            from system.ui.bluepilot.onroad.debug.long_debug_panel import LongDebugPanel

            self._lateral_debug = LateralDebugPanel()
            self._long_debug = LongDebugPanel()
            self._debug_panels_initialized = True
        except ImportError as e:
            print(f"BluePilot debug panels not available: {e}")
            self._debug_panels_initialized = True  # Don't retry

    def _render(self, rect):
        """Render the augmented road view with BluePilot overlays"""
        # Only render when system is started to avoid invalid data access
        if not ui_state.started:
            return

        # Initialize overlays and debug panels on first render
        self._init_overlays()
        self._init_debug_panels()

        # Update BluePilot state
        self._bp_state.update()

        # Call parent render which handles:
        # - Camera stream switching
        # - Calibration updates
        # - Border drawing
        # - Scissor mode for clipping
        # - Model rendering
        # - HUD rendering
        # - Alert rendering
        # - Driver state rendering
        super()._render(rect)

        # Render BluePilot overlays after base rendering
        # Note: We're inside the scissor mode from parent, so overlays are clipped
        self._render_bp_overlays(self._content_rect)

        # Render debug panels if enabled (dev_ui_info > 0)
        if self._bp_state.dev_ui_info > 0:
            self._render_debug_panels(self._content_rect)

    def _render_bp_overlays(self, rect: rl.Rectangle):
        """Render BluePilot-specific overlays"""
        # Check if we should hide overlays (e.g., during alerts)
        if self._should_hide_overlays():
            return

        # Render hybrid gauges overlay (Ford hybrid vehicles)
        if self._hybrid_overlay and self._bp_state.show_hybrid_drive_overlay:
            self._hybrid_overlay.render(rect, self._bp_state)

        # Render radar overlay (enhanced lead visualization)
        if self._radar_overlay and self._bp_state.show_bp_radar_overlay:
            # Pass model renderer for coordinate transforms
            self._radar_overlay.render(rect, self._bp_state, self.model_renderer)

        # Render stop sign overlay
        if self._stop_overlay and self._bp_state.show_stop_indicator_overlay:
            self._stop_overlay.render(rect, self._bp_state)

        # Render standstill timer overlay
        if self._standstill_overlay and self._bp_state.show_standstill_timer:
            self._standstill_overlay.render(rect, self._bp_state)

    def _render_debug_panels(self, rect: rl.Rectangle):
        """Render debug panels based on dev_ui_info setting"""
        # dev_ui_info: 0=off, 1=right panel (lateral), 2=right+bottom (lateral+long)

        # Panel positioning
        panel_margin = 15
        panel_padding = 10

        # Render lateral debug panel on the right side
        if self._lateral_debug and self._bp_state.dev_ui_info >= 1:
            lateral_rect = rl.Rectangle(
                rect.x + rect.width - self._lateral_debug.PANEL_WIDTH - panel_margin,
                rect.y + panel_margin,
                self._lateral_debug.PANEL_WIDTH,
                self._lateral_debug.PANEL_HEIGHT
            )
            self._lateral_debug.render(lateral_rect)

        # Render longitudinal debug panel below lateral panel
        if self._long_debug and self._bp_state.dev_ui_info >= 2:
            long_rect = rl.Rectangle(
                rect.x + rect.width - self._long_debug.PANEL_WIDTH - panel_margin,
                rect.y + panel_margin + self._lateral_debug.PANEL_HEIGHT + panel_padding,
                self._long_debug.PANEL_WIDTH,
                self._long_debug.PANEL_HEIGHT
            )
            self._long_debug.render(long_rect)

    def _should_hide_overlays(self) -> bool:
        """Check if overlays should be hidden (e.g., during critical alerts)"""
        sm = ui_state.sm

        # Get current alert state
        if not sm.valid.get('selfdriveState', False):
            return False

        ss = sm['selfdriveState']
        alert_size = ss.alertSize
        alert_type = ss.alertType

        # Hide overlays for all alerts except dashcam mode
        # This allows hybrid gauge to remain visible when in dashcam-only mode
        from cereal import log
        if alert_size != log.SelfdriveState.AlertSize.none and alert_type != "dashcamMode":
            return True

        return False

    def show_event(self):
        """Called when view becomes visible"""
        super().show_event() if hasattr(super(), 'show_event') else None

    def hide_event(self):
        """Called when view is hidden"""
        super().hide_event() if hasattr(super(), 'hide_event') else None
