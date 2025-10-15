#include "selfdrive/ui/qt/onroad/driver_monitoring.h"
#include <algorithm>
#include <cmath>

#include "selfdrive/ui/qt/onroad/buttons.h"
#include "selfdrive/ui/qt/util.h"

// Default 3D coordinates for face keypoints
static constexpr vec3 DEFAULT_FACE_KPTS_3D[] = {
  {-5.98, -51.20, 8.00}, {-17.64, -49.14, 8.00}, {-23.81, -46.40, 8.00}, {-29.98, -40.91, 8.00}, {-32.04, -37.49, 8.00},
  {-34.10, -32.00, 8.00}, {-36.16, -21.03, 8.00}, {-36.16, 6.40, 8.00}, {-35.47, 10.51, 8.00}, {-32.73, 19.43, 8.00},
  {-29.30, 26.29, 8.00}, {-24.50, 33.83, 8.00}, {-19.01, 41.37, 8.00}, {-14.21, 46.17, 8.00}, {-12.16, 47.54, 8.00},
  {-4.61, 49.60, 8.00}, {4.99, 49.60, 8.00}, {12.53, 47.54, 8.00}, {14.59, 46.17, 8.00}, {19.39, 41.37, 8.00},
  {24.87, 33.83, 8.00}, {29.67, 26.29, 8.00}, {33.10, 19.43, 8.00}, {35.84, 10.51, 8.00}, {36.53, 6.40, 8.00},
  {36.53, -21.03, 8.00}, {34.47, -32.00, 8.00}, {32.42, -37.49, 8.00}, {30.36, -40.91, 8.00}, {24.19, -46.40, 8.00},
  {18.02, -49.14, 8.00}, {6.36, -51.20, 8.00}, {-5.98, -51.20, 8.00},
};

// Modern vibrant colors for driver monitoring
static const QColor DMON_ENGAGED_COLOR = QColor::fromRgbF(0.15, 1.0, 0.35);      // Brighter green
static const QColor DMON_DISENGAGED_COLOR = QColor::fromRgbF(0.85, 0.85, 0.85); // Brighter gray
static const QColor DMON_ACCENT_COLOR = QColor::fromRgbF(0.2, 0.65, 1.0);       // Vibrant blue

DriverMonitorRenderer::DriverMonitorRenderer() : face_kpts_draw(std::size(DEFAULT_FACE_KPTS_3D)) {
  dm_img = loadPixmap("../assets/icons/driver_face.png", {img_size + 5, img_size + 5});
}

void DriverMonitorRenderer::updateState(const UIState &s) {
  auto &sm = *(s.sm);
  is_visible = sm["selfdriveState"].getSelfdriveState().getAlertSize() == cereal::SelfdriveState::AlertSize::NONE &&
               sm.rcv_frame("driverStateV2") > s.scene.started_frame;
  if (!is_visible) return;

  auto dm_state = sm["driverMonitoringState"].getDriverMonitoringState();
  is_active = dm_state.getIsActiveMode();
  is_rhd = dm_state.getIsRHD();
  dm_fade_state = std::clamp(dm_fade_state + 0.2f * (0.5f - is_active), 0.0f, 1.0f);

  const auto &driverstate = sm["driverStateV2"].getDriverStateV2();
  const auto driver_orient = is_rhd ? driverstate.getRightDriverData().getFaceOrientation() : driverstate.getLeftDriverData().getFaceOrientation();

  for (int i = 0; i < 3; ++i) {
    float v_this = (i == 0 ? (driver_orient[i] < 0 ? 0.7 : 0.9) : 0.4) * driver_orient[i];
    driver_pose_diff[i] = std::abs(driver_pose_vals[i] - v_this);
    driver_pose_vals[i] = 0.8f * v_this + (1 - 0.8) * driver_pose_vals[i];
    driver_pose_sins[i] = std::sin(driver_pose_vals[i] * (1.0f - dm_fade_state));
    driver_pose_coss[i] = std::cos(driver_pose_vals[i] * (1.0f - dm_fade_state));
  }

  auto [sin_y, sin_x, sin_z] = driver_pose_sins;
  auto [cos_y, cos_x, cos_z] = driver_pose_coss;

  // Rotation matrix for transforming face keypoints based on driver's head orientation
  const mat3 r_xyz = {{
    cos_x * cos_z, cos_x * sin_z, -sin_x,
    -sin_y * sin_x * cos_z - cos_y * sin_z, -sin_y * sin_x * sin_z + cos_y * cos_z, -sin_y * cos_x,
    cos_y * sin_x * cos_z - sin_y * sin_z, cos_y * sin_x * sin_z + sin_y * cos_z, cos_y * cos_x,
  }};

  // Transform vertices
  for (int i = 0; i < face_kpts_draw.size(); ++i) {
    vec3 kpt = matvecmul3(r_xyz, DEFAULT_FACE_KPTS_3D[i]);
    face_kpts_draw[i] = {{kpt.v[0], kpt.v[1], kpt.v[2] * (1.0f - dm_fade_state) + 8 * dm_fade_state}};
  }
}

void DriverMonitorRenderer::draw(QPainter &painter, const QRect &surface_rect) {
  if (!is_visible) return;

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);

  int offset = UI_BORDER_SIZE + btn_size / 2;
  float x = is_rhd ? surface_rect.width() - offset : offset;

  // Center vertically with hybrid drive gauge when visible, otherwise use stock positioning
  float y;
  float opacity = is_active ? 0.9f : 0.4f;

#ifdef SUNNYPILOT
  dev_ui_info = uiStateSP()->scene.dev_ui_info;
#endif

  // Check if hybrid drive gauge is visible and get its positioning
  int gauge_height = 130;
  int bottom_margin = 30;

#ifdef SUNNYPILOT
  if (uiStateSP()->scene.show_hybrid_drive_overlay) {
    int gauge_scale = uiStateSP()->scene.hybrid_drive_gauge_size;

    // Match hybrid gauge sizing logic (updated with new sizes)
    if (gauge_scale == 1) {
      gauge_height = 100;
    } else if (gauge_scale == 2) {
      gauge_height = 120;
    } else if (gauge_scale == 3) {
      gauge_height = 140;
    } else {
      gauge_height = 100;
    }

    // Match hybrid gauge bottom margin logic
    bottom_margin = 30;
    if (dev_ui_info == 2) {
      bottom_margin += 70; // Move up by 70px when bottom panel is visible
    }

    // Calculate gauge center position to match hybrid gauge exactly
    int gauge_y_position = surface_rect.height() - gauge_height - bottom_margin;
    y = gauge_y_position + (gauge_height / 2); // Center of the gauge
  } else {
    // Stock positioning when hybrid gauge is not visible
    bottom_margin = (dev_ui_info == 2) ? 100 : 30;
    y = surface_rect.height() - (130 / 2) - bottom_margin;
    y -= dev_ui_info > 1 ? 50 : 0;
  }
#else
  // Stock positioning for non-SunnyPilot builds
  bottom_margin = 30;
  y = surface_rect.height() - (130 / 2) - bottom_margin;
#endif

  QColor primary_color = uiState()->engaged() ? DMON_ENGAGED_COLOR : DMON_DISENGAGED_COLOR;

  // Prominent colored background glow
  QRadialGradient bg_glow(x, y, img_size / 1.2);
  bg_glow.setColorAt(0, QColor(primary_color.red(), primary_color.green(), primary_color.blue(), int(55 * opacity)));
  bg_glow.setColorAt(0.5, QColor(primary_color.red(), primary_color.green(), primary_color.blue(), int(25 * opacity)));
  bg_glow.setColorAt(1, QColor(0, 0, 0, 0));
  painter.setPen(Qt::NoPen);
  painter.setBrush(bg_glow);
  painter.drawEllipse(QPointF(x, y), img_size / 1.2, img_size / 1.2);

  // Dark background for contrast
  QRadialGradient bg_dark(x, y, img_size / 1.8);
  bg_dark.setColorAt(0, QColor(10, 15, 20, int(140 * opacity)));
  bg_dark.setColorAt(0.8, QColor(5, 10, 15, int(160 * opacity)));
  bg_dark.setColorAt(1, QColor(0, 0, 0, int(100 * opacity)));
  painter.setBrush(bg_dark);
  painter.drawEllipse(QPointF(x, y), img_size / 1.8, img_size / 1.8);

  drawIcon(painter, QPoint(x, y), dm_img, QColor(0, 0, 0, 50), opacity * 0.8);

  QPointF keypoints[std::size(DEFAULT_FACE_KPTS_3D)];
  for (int i = 0; i < std::size(keypoints); ++i) {
    const auto &v = face_kpts_draw[i].v;
    float kp = (v[2] - 8) / 120.0f + 1.0f;
    keypoints[i] = QPointF(v[0] * kp + x, v[1] * kp + y);
  }

  // Bold blue glow layer
  QColor glow_accent = DMON_ACCENT_COLOR;
  glow_accent.setAlphaF(opacity * 0.5);
  painter.setPen(QPen(glow_accent, 12.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawPolyline(keypoints, std::size(keypoints));

  // Bright white outline
  painter.setPen(QPen(QColor::fromRgbF(1.0, 1.0, 1.0, opacity), 5.5, Qt::SolidLine, Qt::RoundCap));
  painter.drawPolyline(keypoints, std::size(keypoints));

  // Bold, vibrant tracking arcs
  const int arc_l = 145;
  const float arc_t_default = 7.0f;
  const float arc_t_extend = 11.0f;
  float arc_opacity = 0.85f * (1.0f - dm_fade_state);

  float delta_x = -driver_pose_sins[1] * arc_l / 2.0f;
  float delta_y = -driver_pose_sins[0] * arc_l / 2.0f;

  // Calculate dynamic arc width based on movement
  float h_arc_width = arc_t_default + arc_t_extend * std::min(1.0, driver_pose_diff[1] * 5.0);
  float v_arc_width = arc_t_default + arc_t_extend * std::min(1.0, driver_pose_diff[0] * 5.0);

  // Horizontal arc - thick blue glow
  QColor h_glow = DMON_ACCENT_COLOR;
  h_glow.setAlphaF(arc_opacity * 0.6);
  painter.setPen(QPen(h_glow, h_arc_width + 8.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawArc(QRectF(std::min(x + delta_x, x), y - arc_l / 2, std::abs(delta_x), arc_l),
                  (driver_pose_sins[1] > 0 ? 90 : -90) * 16, 180 * 16);

  // Horizontal arc - bright colored main
  QColor h_arc_color = primary_color;
  h_arc_color.setAlphaF(arc_opacity);
  painter.setPen(QPen(h_arc_color, h_arc_width, Qt::SolidLine, Qt::RoundCap));
  painter.drawArc(QRectF(std::min(x + delta_x, x), y - arc_l / 2, std::abs(delta_x), arc_l),
                  (driver_pose_sins[1] > 0 ? 90 : -90) * 16, 180 * 16);

  // Vertical arc - thick blue glow
  QColor v_glow = DMON_ACCENT_COLOR;
  v_glow.setAlphaF(arc_opacity * 0.6);
  painter.setPen(QPen(v_glow, v_arc_width + 8.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawArc(QRectF(x - arc_l / 2, std::min(y + delta_y, y), arc_l, std::abs(delta_y)),
                  (driver_pose_sins[0] > 0 ? 0 : 180) * 16, 180 * 16);

  // Vertical arc - bright colored main
  QColor v_arc_color = primary_color;
  v_arc_color.setAlphaF(arc_opacity);
  painter.setPen(QPen(v_arc_color, v_arc_width, Qt::SolidLine, Qt::RoundCap));
  painter.drawArc(QRectF(x - arc_l / 2, std::min(y + delta_y, y), arc_l, std::abs(delta_y)),
                  (driver_pose_sins[0] > 0 ? 0 : 180) * 16, 180 * 16);

  painter.restore();
}
