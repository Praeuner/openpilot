"""
BluePilot Radar Overlay
Enhanced lead vehicle visualization with distance, speed, and time-to-lead info boxes
"""

import pyray as rl
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached


class RadarOverlay:
    """
    Renders enhanced lead vehicle visualization.

    Features:
    - Chevron markers for lead vehicles
    - Color coding: blue for radar-assisted, yellow for vision-only
    - Three info boxes: distance, speed, time-to-lead
    - Metallic gradient styling
    """

    # Colors
    COLOR_RADAR = rl.Color(60, 170, 255, 255)      # Blue for radar-assisted
    COLOR_VISION = rl.Color(241, 196, 15, 255)     # Yellow for vision-only
    COLOR_BG = rl.Color(44, 62, 80, 200)
    COLOR_BG_DARK = rl.Color(26, 37, 47, 220)
    COLOR_TEXT = rl.Color(236, 240, 241, 255)
    COLOR_TEXT_SHADOW = rl.Color(0, 0, 0, 150)
    COLOR_WARNING = rl.Color(255, 100, 100, 255)

    def __init__(self):
        self._font_bold = gui_app.font(FontWeight.BOLD)
        self._font_semi_bold = gui_app.font(FontWeight.SEMI_BOLD)

    def render(self, rect: rl.Rectangle, bp_state, model_renderer) -> None:
        """Render radar overlay with enhanced lead visualization"""
        sm = ui_state.sm

        # Check if radar state is valid
        if not sm.valid.get('radarState', False):
            return

        radar_state = sm['radarState']

        # Get scale factor from settings
        scale_factor = self._get_scale_factor(bp_state)

        # Draw lead vehicles
        leads = [radar_state.leadOne, radar_state.leadTwo]
        for i, lead in enumerate(leads):
            if not lead.status:
                continue

            # Skip lead two if it's too close to lead one
            if i == 1 and leads[0].status:
                if abs(leads[0].dRel - lead.dRel) <= 3.0:
                    continue

            # Get lead screen position from model renderer
            lead_pos = self._get_lead_screen_position(model_renderer, i)
            if lead_pos is None:
                continue

            # Determine if radar-assisted (simplified detection)
            radar_assisted = lead.radar

            # Calculate confidence alpha
            alpha = 1.0
            if lead.dRel < 5.0 and not radar_assisted:
                alpha = min(0.6, 1.0)

            self._draw_enhanced_lead(
                rect, lead, lead_pos,
                radar_assisted, alpha, scale_factor
            )

    def _get_scale_factor(self, bp_state) -> float:
        """Get scale factor based on overlay size setting"""
        size_setting = getattr(bp_state, 'radar_overlay_size', 2)
        if size_setting == 1:
            return 0.95
        elif size_setting == 3:
            return 1.35
        else:
            return 1.15

    def _get_lead_screen_position(self, model_renderer, lead_index: int):
        """Get lead vehicle screen position from model renderer"""
        try:
            lead_vehicles = model_renderer._lead_vehicles
            if lead_index < len(lead_vehicles):
                lead = lead_vehicles[lead_index]
                if lead.chevron:
                    # Get center x and top y from chevron
                    return (lead.chevron[1][0], lead.chevron[1][1])
        except (AttributeError, IndexError):
            pass
        return None

    def _draw_enhanced_lead(self, rect: rl.Rectangle, lead_data, screen_pos: tuple,
                             radar_assisted: bool, alpha: float, scale_factor: float) -> None:
        """Draw enhanced lead vehicle with chevron and info boxes"""
        d_rel = lead_data.dRel
        v_lead = lead_data.vLead
        v_rel = lead_data.vRel

        # Get ego velocity for time calculation
        sm = ui_state.sm
        v_ego = sm['carState'].vEgo if sm.valid.get('carState', False) else 0.0

        # Calculate chevron size based on distance
        base_sz = max(15.0, min(30.0, (25 * 30) / (d_rel / 3 + 30))) * 3.0
        sz = base_sz * scale_factor

        x, y = screen_pos
        x = max(0, min(rect.width - sz / 2, x))
        y = min(rect.height - sz * 0.6, y)

        # Get base color
        base_color = self.COLOR_RADAR if radar_assisted else self.COLOR_VISION

        # Draw chevron (inverted triangle pointing up)
        self._draw_chevron(x, y, sz, base_color, alpha)

        # Draw info boxes below chevron
        self._draw_info_boxes(rect, x, y, sz, d_rel, v_lead, v_ego, v_rel,
                              base_color, alpha, scale_factor)

    def _draw_chevron(self, x: float, y: float, sz: float, base_color: rl.Color, alpha: float) -> None:
        """Draw the lead vehicle chevron"""
        # Chevron points
        p1 = rl.Vector2(x + sz * 1.25, y + sz)  # Right
        p2 = rl.Vector2(x, y)                     # Top center
        p3 = rl.Vector2(x - sz * 1.25, y + sz)  # Left

        # Apply alpha to color
        fill_color = rl.Color(
            base_color.r, base_color.g, base_color.b,
            int(255 * alpha)
        )

        # Draw filled triangle
        rl.draw_triangle(p2, p1, p3, fill_color)

        # Draw border
        border_color = rl.Color(
            min(255, base_color.r + 40),
            min(255, base_color.g + 40),
            min(255, base_color.b + 40),
            int(220 * alpha)
        )
        rl.draw_line_ex(p1, p2, 2.5, border_color)
        rl.draw_line_ex(p2, p3, 2.5, border_color)
        rl.draw_line_ex(p3, p1, 2.5, border_color)

        # Draw icon indicator (R or V)
        icon_text = "R" if base_color == self.COLOR_RADAR else "V"
        icon_size = sz * 0.4
        icon_x = x - icon_size / 4
        icon_y = y + sz * 0.4
        font_size = int(icon_size)

        rl.draw_text_ex(
            self._font_bold, icon_text,
            rl.Vector2(icon_x, icon_y),
            font_size, 0,
            rl.Color(255, 255, 255, int(200 * alpha))
        )

    def _draw_info_boxes(self, rect: rl.Rectangle, x: float, y: float, sz: float,
                          d_rel: float, v_lead: float, v_ego: float, v_rel: float,
                          base_color: rl.Color, alpha: float, scale_factor: float) -> None:
        """Draw the three info boxes below the chevron"""
        is_metric = ui_state.is_metric

        # Convert speed for display
        speed_conversion = 3.6 if is_metric else 2.237  # m/s to km/h or mph
        lead_speed_display = v_lead * speed_conversion

        # Calculate time-to-lead
        time_to_lead = 0.0
        if v_ego > 1.0:
            time_to_lead = d_rel / v_ego
            # If approaching, show time to collision
            if v_rel < -0.5:
                time_to_collision = d_rel / abs(v_rel)
                time_to_lead = min(time_to_lead, time_to_collision)

        # Format text
        dist_text = f"{int(d_rel)}m"
        speed_text = f"{int(lead_speed_display)}{'km/h' if is_metric else 'mph'}"

        if v_ego < 1.0:
            time_text = "--s"
        elif time_to_lead > 10.0:
            time_text = ">10s"
        else:
            time_text = f"{time_to_lead:.1f}s"

        # Box dimensions
        box_height = 55 * scale_factor
        box_gap = 12 * scale_factor
        dist_box_width = 100 * scale_factor
        time_box_width = 100 * scale_factor

        # Calculate speed box width dynamically
        font_size = int(26 * scale_factor)
        speed_size = measure_text_cached(self._font_semi_bold, speed_text, font_size)
        speed_box_width = speed_size.x + 20 * scale_factor

        total_width = dist_box_width + speed_box_width + time_box_width + box_gap * 2
        box_top = y + sz + 20 * scale_factor

        # Center boxes below chevron
        start_x = x - total_width / 2

        # Keep boxes in bounds
        if start_x < 5:
            start_x = 5
        elif start_x + total_width > rect.width - 5:
            start_x = rect.width - total_width - 5

        # Check if boxes would be cut off at bottom
        if box_top + box_height > rect.height - 5:
            return

        # Draw boxes
        is_warning = time_to_lead < 2.0 and v_ego > 1.0

        self._draw_info_box(start_x, box_top, dist_box_width, box_height,
                            dist_text, base_color, alpha, scale_factor, False)

        self._draw_info_box(start_x + dist_box_width + box_gap, box_top,
                            speed_box_width, box_height, speed_text,
                            base_color, alpha, scale_factor, False)

        self._draw_info_box(start_x + dist_box_width + speed_box_width + box_gap * 2,
                            box_top, time_box_width, box_height, time_text,
                            base_color, alpha, scale_factor, is_warning)

    def _draw_info_box(self, x: float, y: float, width: float, height: float,
                        text: str, border_color: rl.Color, alpha: float,
                        scale_factor: float, is_warning: bool) -> None:
        """Draw a single info box"""
        box_rect = rl.Rectangle(x, y, width, height)

        # Background
        bg_color = rl.Color(44, 62, 80, int(200 * alpha))
        rl.draw_rectangle_rounded(box_rect, 0.15, 10, bg_color)

        # Border
        border = rl.Color(border_color.r, border_color.g, border_color.b, int(180 * alpha))
        rl.draw_rectangle_rounded_lines_ex(box_rect, 0.15, 10, int(2 * scale_factor), border)

        # Highlight
        highlight_rect = rl.Rectangle(x + 2, y + 2, width - 4, height * 0.4)
        rl.draw_rectangle_rounded(highlight_rect, 0.15, 10, rl.Color(255, 255, 255, int(15 * alpha)))

        # Text
        font_size = int(26 * scale_factor)
        text_size = measure_text_cached(self._font_semi_bold, text, font_size)
        text_x = x + (width - text_size.x) / 2
        text_y = y + (height - text_size.y) / 2

        # Shadow
        rl.draw_text_ex(self._font_semi_bold, text,
                        rl.Vector2(text_x + scale_factor, text_y + scale_factor),
                        font_size, 0, rl.Color(0, 0, 0, int(150 * alpha)))

        # Main text
        text_color = self.COLOR_WARNING if is_warning else self.COLOR_TEXT
        text_color = rl.Color(text_color.r, text_color.g, text_color.b, int(255 * alpha))
        rl.draw_text_ex(self._font_semi_bold, text,
                        rl.Vector2(text_x, text_y), font_size, 0, text_color)
