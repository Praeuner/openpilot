"""
BluePilot Enhanced Model Renderer
Extends stock ModelRenderer with glow effects, blindspot overlays, and enhanced visualization
"""

import math
import numpy as np
import pyray as rl
from dataclasses import dataclass, field
from openpilot.common.params import Params
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.selfdrive.ui.onroad.model_renderer import ModelRenderer
from openpilot.system.ui.lib.shader_polygon import draw_polygon


# Constants
BLINDSPOT_WIDTH = 1.0
GLOW_LAYER_COUNT = 3


@dataclass
class BlindspotState:
    """State for blindspot detection and animation"""
    left_active: bool = False
    right_active: bool = False
    blink_counter: int = 0
    opacity: float = 0.2
    left_vertices: np.ndarray = field(default_factory=lambda: np.empty((0, 2), dtype=np.float32))
    right_vertices: np.ndarray = field(default_factory=lambda: np.empty((0, 2), dtype=np.float32))


class ModelRendererBP(ModelRenderer):
    """
    BluePilot enhanced model renderer with:
    - Lane line glow effects for better visibility
    - Blindspot warning overlays
    - Enhanced path with custom colors
    - Wider lane lines in enhanced mode
    """

    # Glow layer configurations
    LANE_GLOW_WIDTHS = [1.5, 1.3, 1.15]  # Width multipliers for glow layers
    LANE_GLOW_ALPHAS = [0.05, 0.10, 0.20]  # Alpha values for each layer

    EDGE_GLOW_WIDTHS = [1.6, 1.4, 1.2]
    EDGE_GLOW_ALPHAS = [0.03, 0.07, 0.15]

    def __init__(self):
        super().__init__()
        self._blindspot_state = BlindspotState()
        self._enhanced_ui = False
        self._show_blindspot = False
        self._params = Params()
        self._last_params_check = 0

    def _render(self, rect: rl.Rectangle):
        """Render with BluePilot enhancements"""
        sm = ui_state.sm

        # Check if data is up-to-date
        if (sm.recv_frame["liveCalibration"] < ui_state.started_frame or
            sm.recv_frame["modelV2"] < ui_state.started_frame):
            return

        # Update settings periodically
        self._update_settings()

        # Update blindspot polygons if enabled
        if self._show_blindspot:
            self._update_blindspot_state(sm)

        # Call parent render for base functionality
        super()._render(rect)

        # Draw blindspot overlays on top
        if self._show_blindspot:
            self._draw_blindspot_overlays(rect)

    def _update_settings(self):
        """Update settings from params (cached)"""
        import time
        current_time = time.monotonic()
        if current_time - self._last_params_check < 1.0:
            return
        self._last_params_check = current_time

        self._enhanced_ui = self._params.get_bool("BluepilotShowEnhancedOnroadUI")
        self._show_blindspot = self._params.get_bool("BlindSpot")

    def _draw_lane_lines(self):
        """Override to add glow effects for enhanced visibility"""
        if self._enhanced_ui:
            self._draw_enhanced_lane_lines()
        else:
            super()._draw_lane_lines()

    def _draw_enhanced_lane_lines(self):
        """Draw lane lines with glow effects"""
        # Draw glow layers first (behind the actual lines)
        for i, lane_line in enumerate(self._lane_lines):
            if lane_line.projected_points.size == 0:
                continue

            prob = self._lane_line_probs[i]
            if prob < 0.4:
                continue

            base_alpha = min(0.8, prob * 0.8)
            is_current_lane = (i == 1 or i == 2)
            if not is_current_lane:
                base_alpha *= 0.4  # Dim outer lanes

            # Draw glow layers (wider, semi-transparent)
            for layer_idx in range(GLOW_LAYER_COUNT):
                glow_alpha = base_alpha * self.LANE_GLOW_ALPHAS[layer_idx]
                glow_color = rl.Color(255, 255, 255, int(glow_alpha * 255))

                # Scale the polygon width for glow
                glow_points = self._scale_polygon_width(
                    lane_line.projected_points,
                    self.LANE_GLOW_WIDTHS[layer_idx]
                )
                if glow_points is not None:
                    draw_polygon(self._rect, glow_points, glow_color)

        # Draw the solid lane lines on top
        for i, lane_line in enumerate(self._lane_lines):
            if lane_line.projected_points.size == 0:
                continue

            prob = self._lane_line_probs[i]
            base_alpha = min(0.8, prob * 0.8)
            is_current_lane = (i == 1 or i == 2)
            if not is_current_lane:
                base_alpha *= 0.4

            color = rl.Color(255, 255, 255, int(base_alpha * 255))
            draw_polygon(self._rect, lane_line.projected_points, color)

        # Draw road edges with glow
        for i, road_edge in enumerate(self._road_edges):
            if road_edge.projected_points.size == 0:
                continue

            edge_alpha = max(0.0, min(1.0, 1.0 - self._road_edge_stds[i]))
            if edge_alpha < 0.3:
                continue

            # Draw edge glow layers
            for layer_idx in range(GLOW_LAYER_COUNT):
                glow_alpha = edge_alpha * self.EDGE_GLOW_ALPHAS[layer_idx]
                glow_color = rl.Color(255, 0, 0, int(glow_alpha * 255))

                glow_points = self._scale_polygon_width(
                    road_edge.projected_points,
                    self.EDGE_GLOW_WIDTHS[layer_idx]
                )
                if glow_points is not None:
                    draw_polygon(self._rect, glow_points, glow_color)

            # Draw solid road edge
            color = rl.Color(255, 0, 0, int(edge_alpha * 0.6 * 255))
            draw_polygon(self._rect, road_edge.projected_points, color)

    def _scale_polygon_width(self, points: np.ndarray, scale: float) -> np.ndarray | None:
        """Scale a polygon's width by a factor (for glow effects)"""
        if points.shape[0] < 4:
            return None

        n = points.shape[0]
        half = n // 2

        # Split into left and right sides
        left_points = points[:half]
        right_points = points[half:][::-1]

        if len(left_points) != len(right_points):
            return None

        # Calculate center line and scale outward
        scaled_points = np.zeros_like(points)

        for i in range(half):
            center = (left_points[i] + right_points[i]) / 2
            left_offset = left_points[i] - center
            right_offset = right_points[i] - center

            scaled_points[i] = center + left_offset * scale
            scaled_points[n - 1 - i] = center + right_offset * scale

        return scaled_points

    # -------------------------------------------------------------------------
    # Blindspot Overlay
    # -------------------------------------------------------------------------

    def _update_blindspot_state(self, sm):
        """Update blindspot detection state"""
        if not sm.valid.get('carState', False):
            return

        car_state = sm['carState']
        self._blindspot_state.left_active = car_state.leftBlindspot
        self._blindspot_state.right_active = car_state.rightBlindspot

        # Update animation
        self._blindspot_state.blink_counter = (self._blindspot_state.blink_counter + 1) % 40
        pulse = 0.1 * math.sin(self._blindspot_state.blink_counter * math.pi / 20) + 0.25
        self._blindspot_state.opacity = pulse

        # Create blindspot polygons from lane lines
        self._create_blindspot_polygons(sm)

    def _create_blindspot_polygons(self, sm):
        """Create blindspot warning polygons from lane line data"""
        if not sm.valid.get('modelV2', False):
            self._blindspot_state.left_vertices = np.empty((0, 2), dtype=np.float32)
            self._blindspot_state.right_vertices = np.empty((0, 2), dtype=np.float32)
            return

        # Use lane lines 1 (left) and 2 (right) for current lane boundaries
        if len(self._lane_lines) < 4:
            return

        left_lane = self._lane_lines[1]
        right_lane = self._lane_lines[2]

        max_idx = min(50, min(
            len(left_lane.raw_points) if left_lane.raw_points.size else 0,
            len(right_lane.raw_points) if right_lane.raw_points.size else 0
        ))

        if max_idx < 2:
            return

        # Build left blindspot polygon (between lane line and offset)
        self._blindspot_state.left_vertices = self._build_blindspot_polygon(
            left_lane.raw_points[:max_idx], -BLINDSPOT_WIDTH
        )

        # Build right blindspot polygon
        self._blindspot_state.right_vertices = self._build_blindspot_polygon(
            right_lane.raw_points[:max_idx], BLINDSPOT_WIDTH
        )

    def _build_blindspot_polygon(self, lane_points: np.ndarray, y_offset: float) -> np.ndarray:
        """Build a blindspot polygon from lane line points"""
        if lane_points.shape[0] < 2:
            return np.empty((0, 2), dtype=np.float32)

        # Create offset points
        inner_points = []
        outer_points = []

        for i in range(len(lane_points)):
            x, y, z = lane_points[i]
            if x < 0:
                continue

            # Inner point (at lane line)
            inner_pt = self._map_to_screen(x, -y, z + self._path_offset_z)
            # Outer point (offset for blindspot area)
            outer_pt = self._map_to_screen(x, -(y + y_offset), z + self._path_offset_z)

            if inner_pt and outer_pt:
                inner_points.append(inner_pt)
                outer_points.append(outer_pt)

        if len(inner_points) < 2:
            return np.empty((0, 2), dtype=np.float32)

        # Combine into polygon (inner forward, outer backward)
        polygon = inner_points + outer_points[::-1]
        return np.array(polygon, dtype=np.float32)

    def _draw_blindspot_overlays(self, rect: rl.Rectangle):
        """Draw blindspot warning overlays"""
        opacity = self._blindspot_state.opacity

        # Draw left blindspot
        if (self._blindspot_state.left_active and
            self._blindspot_state.left_vertices.size > 0):
            self._draw_blindspot_polygon(
                self._blindspot_state.left_vertices, opacity, rect
            )

        # Draw right blindspot
        if (self._blindspot_state.right_active and
            self._blindspot_state.right_vertices.size > 0):
            self._draw_blindspot_polygon(
                self._blindspot_state.right_vertices, opacity, rect
            )

    def _draw_blindspot_polygon(self, vertices: np.ndarray, opacity: float, rect: rl.Rectangle):
        """Draw a single blindspot warning polygon with gradient"""
        if vertices.shape[0] < 3:
            return

        # Create gradient effect (more opaque toward bottom)
        gradient = {
            'start': (0.0, 0.0),  # Top (near car)
            'end': (0.0, 1.0),    # Bottom (far from car)
            'colors': [
                rl.Color(255, 0, 0, int(255 * opacity)),  # Full opacity at top
                rl.Color(255, 0, 0, int(255 * 0.2)),      # Fade to 20%
                rl.Color(255, 0, 0, 0),                   # Fade to transparent
            ],
            'stops': [0.4, 0.7, 1.0],
        }

        draw_polygon(rect, vertices, gradient=gradient)
