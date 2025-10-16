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
#ifdef SUNNYPILOT
class ModelRendererBP : public ModelRendererSP {
#else
class ModelRendererBP : public ModelRenderer {
#endif
public:
  ModelRendererBP();
  virtual ~ModelRendererBP() = default;

  // Lead tracking state struct (public for BluepilotRenderer)
  struct LeadState {
    int active_counter[2] = {0, 0};
    bool virtual_active[2] = {false, false};
    bool stable[2] = {false, false};
    float smoothed_yRel[2] = {0.0f, 0.0f};
    bool prev_status[2] = {false, false};
    bool radar_assisted[2] = {false, false};
    QPointF vertices[2] = {};
    float d_rel[2] = {0.0f, 0.0f};
    float v_lead[2] = {0.0f, 0.0f};
    float v_rel[2] = {0.0f, 0.0f};
  };

  // Stop detection state struct (public for BluepilotRenderer)
  struct StopState {
    bool active = false;
    int stability_counter = 0;
    float stopping_distance = 0.0f;
    float display_distance = 0.0f;
    float fade_alpha = 0.0f;
    QPointF last_valid_position;
  };

  // Main draw method
  void draw(QPainter &painter, const QRect &surface_rect);

  // Getters for BluepilotRenderer compatibility
  const Eigen::Matrix3f& getTransform() const {
#ifdef SUNNYPILOT
    return ModelRendererSP::car_space_transform;
#else
    return ModelRenderer::car_space_transform;
#endif
  }
  const QRectF& getClipRegion() const {
#ifdef SUNNYPILOT
    return ModelRendererSP::clip_region;
#else
    return ModelRenderer::clip_region;
#endif
  }

  // Lead tracking and stop detection (public for BluepilotRenderer access)
  void updateLeadTracking(const UIState &s);
  void updateStopDetection(const UIState &s);

  // State getters
  const LeadState& getLeadState() const { return lead_state; }
  const StopState& getStopState() const { return stop_state; }

protected:
#ifndef SUNNYPILOT
  // Override base class virtual methods when not using SunnyPilot
  void drawPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, const QRect &surface_rect) override;
  void update_model(const cereal::ModelDataV2::Reader &model,
                   const cereal::RadarState::LeadData::Reader &lead) override;
#endif

  // Core update methods
  void updateBluePilotState(const cereal::ModelDataV2::Reader &model);
  void updateLaneLines(const cereal::ModelDataV2::Reader &model, bool enhanced);

  // Enhanced drawing methods (when enhanced UI is enabled)
  void drawEnhancedLaneLines(QPainter &painter);
  void drawLaneGlowEffects(QPainter &painter);
  void drawRoadEdgeGlowEffects(QPainter &painter);
  void drawEnhancedPath(QPainter &painter, const cereal::ModelDataV2::Reader &model,
                        const QRect &surface_rect);
  void applyCustomPathGradient(QLinearGradient &bg, QColor &border_color,
                               const QString &pathColor, const QRect &surface_rect);
  void applyStockPathGradient(QLinearGradient &bg, QColor &border_color,
                              const cereal::ModelDataV2::Reader &model,
                              const QRect &surface_rect);

  // Blindspot methods
  void createBlindspotPolygons(const cereal::ModelDataV2::Reader &model);
  void drawBlindspotOverlays(QPainter &painter);

  // Lead status display methods
  void drawLeadStatus(QPainter &painter, int height, int width);
  void drawLeadStatusAtPosition(QPainter &painter,
                                const cereal::RadarState::LeadData::Reader &lead_data,
                                const QPointF &chevron_pos,
                                int height, int width,
                                const QString &label);

private:
  // Color constants
  static constexpr QRgb BP_ACCENT_BLUE = 0xFF1890FF;
  static constexpr QRgb BP_SUCCESS = 0xFF2AC77A;
  static constexpr QRgb BP_WARNING = 0xFFFFC300;
  static constexpr QRgb BP_DANGER = 0xFFF24855;

  // Utility methods
  int get_path_length_idx(const cereal::XYZTData::Reader &line, float path_height);
  void mapLineToPolygonEnhanced(const cereal::XYZTData::Reader &line, float width, float z_off,
                                QPolygonF *pvd, int max_idx);
  QColor getCurrentPathBorderColor();
  void applySmoothPath(); // Stub - currently disabled

  // Drawing utilities (from bluepilot_renderer)
  void drawLeftTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot);
  void drawRightTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot);
  void drawColoredText(QPainter &painter, int x, int y, const QString &text, QColor color);

  // Enhanced blindspot polygons
  QPolygonF left_blindspot_vertices;
  QPolygonF right_blindspot_vertices;

  // Lead status animation
  float lead_status_alpha = 0.0f;

  // Cached glow paths for performance
  struct GlowCache {
    QPainterPath lane_glow_paths[4];
    QPainterPath edge_glow_paths[2];
    bool needs_update = true;
    int frame_counter = 0;
  } glow_cache;

  // State member variables
  LeadState lead_state;
  StopState stop_state;

  // Path smoothing state
  QPolygonF previous_track_vertices;
};
