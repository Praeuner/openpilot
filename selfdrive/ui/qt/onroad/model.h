#pragma once

#include <QPainter>
#include <QPolygonF>
#include <QPainterPath>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui.h"
#else
#include "selfdrive/ui/ui.h"
#endif

class ModelRenderer {
public:
  ModelRenderer() {
    // Initialize icons once in constructor
    initializeIcons();
  }
  void setTransform(const Eigen::Matrix3f &transform) { car_space_transform = transform; }
  void draw(QPainter &painter, const QRect &surface_rect);

private:
  bool mapToScreen(float in_x, float in_y, float in_z, QPointF *out);
  void mapLineToPolygon(const cereal::XYZTData::Reader &line, float y_off, float z_off,
                        QPolygonF *pvd, int max_idx, bool allow_invert = true);
  void drawLead(QPainter &painter, const cereal::RadarState::LeadData::Reader &lead_data, const QPointF &vd, const QRect &surface_rect, bool isRadarAssisted);
  void update_leads(const cereal::RadarState::Reader &radar_state, const cereal::XYZTData::Reader &line);
  void update_model(const cereal::ModelDataV2::Reader &model, const cereal::RadarState::LeadData::Reader &lead);
  void drawLaneLines(QPainter &painter);
  void drawPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, int height);
  void updatePathGradient(QLinearGradient &bg);
  QColor blendColors(const QColor &start, const QColor &end, float t);
  void drawSmoothPath(QPainter &painter);

  bool longitudinal_control = false;
  bool experimental_mode = false;
  float blend_factor = 1.0f;
  bool prev_allow_throttle = true;
  float lane_line_probs[4] = {};
  float road_edge_stds[2] = {};
  float path_offset_z = 1.22f;
  QPolygonF track_vertices;
  QPolygonF prev_track_vertices;
  float path_smoothing_factor = 0.2;
  QPolygonF lane_line_vertices[4] = {};
  QPolygonF road_edge_vertices[2] = {};
  QPointF lead_vertices[2] = {};
  bool lead_radar_assisted[2] = {false, false}; // Track which leads are radar-assisted
  Eigen::Matrix3f car_space_transform = Eigen::Matrix3f::Zero();
  QRectF clip_region;

  int lead_active_counter[2] = {0, 0};
  bool virtual_lead_active[2] = {false, false};
  float distance_confidence_threshold = 5.0f; // Minimum distance for high confidence
  bool stable_lead[2] = {false, false};

  QPointF prev_lead_positions[2] = {QPointF(0, 0), QPointF(0, 0)}; // Track previous lead positions
  float smoothed_yRel[2] = {0.0f, 0.0f};                           // Smoothed lateral positions for two leads
  bool prev_lead_status[2] = {false, false};                       // Previous status of each lead

  // Cached icons - loaded once
  QPixmap radar_icon;
  QPixmap vision_icon;
  bool icons_initialized = false;

  void initializeIcons() {
    if (icons_initialized) return;

    // Try loading real icons first
    bool radar_loaded = radar_icon.load("../assets/img_radar.png");
    bool vision_loaded = vision_icon.load("../assets/img_vision.png");

    // Create fallbacks only if loading failed
    if (!radar_loaded) {
      radar_icon = createFallbackIcon("R", QColor(60, 170, 255));
    }
    if (!vision_loaded) {
      vision_icon = createFallbackIcon("V", QColor(255, 255, 0));
    }

    icons_initialized = true;
  }

  QPixmap createFallbackIcon(const QString &text, const QColor &color) {
    QPixmap fallback(32, 32);
    fallback.fill(Qt::transparent);
    QPainter painter(&fallback);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(color);
    painter.setPen(Qt::white);
    painter.drawEllipse(2, 2, 28, 28);
    QFont font;
    font.setPixelSize(14);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(fallback.rect(), Qt::AlignCenter, text);
    return fallback;
  }

  struct StopState {
    bool active = false;
    int stability_counter = 0;
    float stopping_distance = 0.0f;
    float display_distance = 0.0f;
    QPointF last_valid_position;
    float fade_alpha = 0.0f;
  };
  StopState stop_state;

  // Stop sign overlay tracking
  int stop_frame_count = 0;
  bool prev_stop_sign_visible = false;
  float stop_sign_opacity = 0.0f;

  // Blindspot detection
  bool left_blindspot = false;
  bool right_blindspot = false;
  QPolygonF lane_barrier_vertices[2]; // Left and right barrier polygons
  int blindspot_blink_rate = 0;       // Counter for animated blinking effect
  float blindspot_opacity = 0.2;      // Base opacity for blindspot indicators
  static constexpr float BLINDSPOT_WIDTH = 1.0f; // Width of blind spot indicator in meters

  void updateBlindspotStatus(const cereal::CarState::Reader &car_state);
  void updateBlindspotAnimation();
  void drawBlindspotIndicators(QPainter &painter);

  void drawStopSignOverlay(QPainter &painter, const QPointF &point, int size, float stopping_distance, float v_ego, float fade_alpha);

};