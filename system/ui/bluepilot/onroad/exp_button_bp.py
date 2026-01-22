"""
BluePilot Enhanced Experimental Button
Extends stock ExpButton with animated steering wheel rotation
"""

import pyray as rl
from openpilot.common.params import Params
from openpilot.selfdrive.ui.onroad.exp_button import ExpButton
from openpilot.selfdrive.ui.ui_state import ui_state


class ExpButtonBP(ExpButton):
    """
    Enhanced experimental mode button with BluePilot features.

    Features:
    - Animated steering wheel that rotates with actual steering angle
    - DEC (Dynamic Experimental Control) mode indicator
    """

    def __init__(self, button_size: int, icon_size: int):
        super().__init__(button_size, icon_size)

        # BluePilot params
        self._params_bp = Params("/dev/shm/params_bp")

        # Animated wheel state
        self._steering_angle: float = 0.0
        self._show_animated_wheel: bool = False

        # DEC mode state (from SunnyPilot)
        self._dec_active: bool = False
        self._dec_mode: int = 0

        # Pre-load textures for DEC mode (half icons)
        self._icon_size = icon_size

    def _update_state(self) -> None:
        """Update state including steering angle for animation"""
        super()._update_state()

        sm = ui_state.sm

        # Update animated wheel setting
        self._show_animated_wheel = self._params_bp.get_bool("ShowAnimatedWheelAngle")

        # Update steering angle for animation
        if sm.recv_frame.get("carState", 0) >= ui_state.started_frame:
            car_state = sm["carState"]
            self._steering_angle = car_state.steeringAngleDeg

        # Update DEC mode state
        if sm.recv_frame.get("longitudinalPlanSP", 0) >= ui_state.started_frame:
            try:
                long_plan_sp = sm["longitudinalPlanSP"]
                dec = long_plan_sp.dec
                self._dec_active = dec.active
                self._dec_mode = int(dec.state)
            except (AttributeError, KeyError):
                self._dec_active = False
                self._dec_mode = 0

    def _render(self, rect: rl.Rectangle) -> None:
        """Render button with optional steering wheel rotation"""
        center_x = int(self._rect.x + self._rect.width // 2)
        center_y = int(self._rect.y + self._rect.height // 2)

        mouse_over = rl.check_collision_point_rec(rl.get_mouse_position(), self._rect)
        mouse_down = rl.is_mouse_button_down(rl.MouseButton.MOUSE_BUTTON_LEFT) and self.is_pressed
        self._white_color.a = 180 if (mouse_down and mouse_over) or not self._engageable else 255

        # Draw background circle
        rl.draw_circle(center_x, center_y, self._rect.width / 2, self._black_bg)

        # Determine which texture to show
        show_experimental = self._held_or_actual_mode()

        if self._dec_active:
            # DEC mode: show split icon with different opacities
            self._draw_dec_mode_icon(center_x, center_y)
        elif self._show_animated_wheel and not show_experimental:
            # Animated wheel mode: rotate the wheel icon
            self._draw_rotated_wheel(center_x, center_y)
        else:
            # Normal mode: show static icon
            texture = self._txt_exp if show_experimental else self._txt_wheel
            rl.draw_texture(texture, center_x - texture.width // 2,
                          center_y - texture.height // 2, self._white_color)

    def _draw_rotated_wheel(self, center_x: int, center_y: int) -> None:
        """Draw the steering wheel icon rotated by the current steering angle"""
        texture = self._txt_wheel

        # Calculate rotation (negative because steering left = positive angle)
        rotation = -self._steering_angle

        # Source rectangle (full texture)
        source = rl.Rectangle(0, 0, texture.width, texture.height)

        # Destination rectangle (centered at button center)
        dest = rl.Rectangle(center_x, center_y, texture.width, texture.height)

        # Origin point for rotation (center of the texture)
        origin = rl.Vector2(texture.width / 2, texture.height / 2)

        # Draw with rotation
        rl.draw_texture_pro(texture, source, dest, origin, rotation, self._white_color)

    def _draw_dec_mode_icon(self, center_x: int, center_y: int) -> None:
        """
        Draw DEC mode split icon.
        Shows half wheel + half experimental with varying opacity based on mode.
        """
        wheel_tex = self._txt_wheel
        exp_tex = self._txt_exp

        # Calculate half widths
        half_width = wheel_tex.width // 2
        tex_height = wheel_tex.height

        # Left half from wheel (opacity based on dec_mode)
        wheel_opacity = 0.1 if self._dec_mode == 1 else 1.0
        wheel_color = rl.Color(255, 255, 255, int(255 * wheel_opacity * (self._white_color.a / 255)))

        # Right half from experimental
        exp_opacity = 1.0 if self._dec_mode == 1 else 0.1
        exp_color = rl.Color(255, 255, 255, int(255 * exp_opacity * (self._white_color.a / 255)))

        # Draw left half of wheel
        left_source = rl.Rectangle(0, 0, half_width, tex_height)
        left_dest = rl.Rectangle(center_x - half_width, center_y - tex_height // 2,
                                 half_width, tex_height)
        rl.draw_texture_pro(wheel_tex, left_source, left_dest,
                           rl.Vector2(0, 0), 0, wheel_color)

        # Draw right half of experimental
        right_source = rl.Rectangle(half_width, 0, half_width, tex_height)
        right_dest = rl.Rectangle(center_x, center_y - tex_height // 2,
                                  half_width, tex_height)
        rl.draw_texture_pro(exp_tex, right_source, right_dest,
                           rl.Vector2(0, 0), 0, exp_color)
