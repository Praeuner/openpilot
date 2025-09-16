#pragma once

#include <QPainter>
#include <QPolygonF>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QColor>
#include <cmath>
#include <eigen3/Eigen/Dense>

// Forward declarations
struct UIState;

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/qt/onroad/model.h"
#else
#include "selfdrive/ui/qt/onroad/model.h"
#endif

// BluePilot enhanced model renderer with automotive-style visual effects
// Inherits from ModelRendererSP when SUNNYPILOT is enabled, ModelRenderer otherwise
#ifdef SUNNYPILOT
class ModelRendererBP : public ModelRendererSP {
#else
class ModelRendererBP : public ModelRenderer {
#endif
public:
  ModelRendererBP();
  virtual ~ModelRendererBP() = default;

  void draw(QPainter &painter, const QRect &surface_rect);

  // Getter methods for BluepilotRenderer compatibility
  const Eigen::Matrix3f& getTransform() const { return car_space_transform; }
  const QRectF& getClipRegion() const { return clip_region; }

protected:
  // Override the virtual methods that are available
#ifndef SUNNYPILOT
  void drawPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, const QRect &surface_rect) override;
  void update_model(const cereal::ModelDataV2::Reader &model, const cereal::RadarState::LeadData::Reader &lead) override;
#endif

  // BluePilot enhancement methods
  void updateBluePilotState(const cereal::ModelDataV2::Reader &model);

  // BluePilot specific methods
  void drawBluePilotLaneLines(QPainter &painter);  // Enhanced lane lines and road edges
  void drawBluePilotPath(QPainter &painter, const QRect &surface_rect);  // Original BluePilot path rendering
  void drawBlindspotOverlays(QPainter &painter);  // Enhanced version superior to SunnyPilot
  void drawLeadStatus(QPainter &painter, int height, int width);  // Enhanced with radar overlay integration
  void drawLeadStatusAtPosition(QPainter &painter,
                               const cereal::RadarState::LeadData::Reader &lead_data,
                               const QPointF &chevron_pos,
                               int height, int width,
                               const QString &label);
  void drawCustomPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, const QRect &surface_rect);
  void createBlindspotPolygons(const cereal::ModelDataV2::Reader &model);

private:
  // BluePilot color palette
  static constexpr QRgb BP_ACCENT_BLUE = 0xFF1890FF;  // RGB(24, 144, 255)
  static constexpr QRgb BP_SUCCESS = 0xFF2AC77A;      // RGB(42, 199, 122)
  static constexpr QRgb BP_WARNING = 0xFFFFC300;      // RGB(255, 195, 0)
  static constexpr QRgb BP_DANGER = 0xFFF24855;       // RGB(242, 72, 85)

  // BluePilot helpers
  void applySmoothPath();  // BluePilot path smoothing to reduce jitter

  // Core geometry utilities (moved from bluepilot_renderer)
  bool mapToScreen(float in_x, float in_y, float in_z, QPointF *out);
  int get_path_length_idx(const cereal::XYZTData::Reader &line, float path_height);

  // Lead tracking and stop detection (moved from bluepilot_renderer)
  void updateLeadTracking(const UIState &s);
  void updateStopDetection(const UIState &s);

  // Drawing utilities (moved from bluepilot_renderer)
  void drawLeftTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot);
  void drawRightTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot);
  void drawColoredText(QPainter &painter, int x, int y, const QString &text, QColor color);

  // Frame state for optimizations
  float current_speed = 0.0f;
  bool high_speed_mode = false;

  // BluePilot path smoothing to reduce jitter
  QPolygonF previous_track_vertices;
  QPolygonF smoothed_track_vertices;
  bool path_smoothing_initialized = false;
  static constexpr float PATH_SMOOTHING_ALPHA = 0.15f;  // Based on BluePilot patterns

  // BluePilot enhanced blindspot polygons (for superior animated blindspot display)
  QPolygonF left_blindspot_vertices;
  QPolygonF right_blindspot_vertices;

  // BluePilot lead status animation
  float lead_status_alpha = 0.0f;

  // Lead tracking state (moved from bluepilot_renderer)
  struct LeadState {
    int active_counter[2] = {0, 0};
    bool virtual_active[2] = {false, false};
    bool stable[2] = {false, false};
    float smoothed_yRel[2] = {0.0f, 0.0f};
    bool prev_status[2] = {false, false};
    bool radar_assisted[2] = {false, false};
    QPointF vertices[2] = {};

    // Fields for time-to-lead calculation
    float d_rel[2] = {0.0f, 0.0f};
    float v_lead[2] = {0.0f, 0.0f};
    float v_rel[2] = {0.0f, 0.0f};
  } lead_state;

  // Stop detection state (moved from bluepilot_renderer)
  struct StopState {
    bool active = false;
    int stability_counter = 0;
    float stopping_distance = 0.0f;
    float display_distance = 0.0f;
    float smoothed_size = 120.0f;
    float fade_alpha = 0.0f;
    QPointF last_valid_position;
  } stop_state;
};
