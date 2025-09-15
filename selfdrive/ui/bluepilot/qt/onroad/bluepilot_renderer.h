#pragma once

#include <QPainter>
#include <QRect>
#include <QPixmap>
#include <QPolygonF>
#include <QLinearGradient>
#include <QPointF>
#include <QString>
#include <QColor>
#include <cmath>
#include <eigen3/Eigen/Dense>
#include "selfdrive/ui/ui.h"

// Forward declaration to avoid circular dependency
class ModelRenderer;

#ifdef SUNNYPILOT
class ModelRendererSP;
#endif

#ifdef BLUEPILOT
class ModelRendererBP;
#endif

class BluepilotRenderer {
public:
  // Single entry point for all BluePilot rendering
#ifdef BLUEPILOT
  static void renderAll(QPainter &painter, const QRect &rect, const UIState &s, const ModelRendererBP &model);
#else
#ifdef SUNNYPILOT
  static void renderAll(QPainter &painter, const QRect &rect, const UIState &s, const ModelRendererSP &model);
#else
  static void renderAll(QPainter &painter, const QRect &rect, const UIState &s, const ModelRenderer &model);
#endif
#endif

private:
  // Optimized state management
  struct FrameState {
    // Blinker state
    bool left_blinker = false, right_blinker = false;
    bool left_blindspot = false, right_blindspot = false;
    int blinker_frame = 0;

    // Standstill state
    bool standstill = false;
    double standstill_start_time = 0.0;
    float standstill_elapsed = 0.0;
    bool prev_standstill = false;
    double standstill_exit_time = 0.0;
    float vehicle_speed = 0.0f;

    // Hybrid data
    bool hybrid_available = false;
    bool battery_available = false;
    float throttle_demand = 0.0f, throttle_threshold = 0.0f;
    QString power_mode, engine_reason;
    float batt_soc_actual = 0.0f, batt_soc_min = 0.0f, batt_soc_max = 0.0f;
    float batt_volt_actual = 0.0f, batt_volt_low = 0.0f, batt_volt_high = 0.0f;
    float batt_amps_actual = 0.0f;

    // Model enhancement state
    bool show_radar = false, show_stop = false;
    Eigen::Matrix3f transform;
    QRectF clip_region;

    // Lane line vertices for stop sign positioning
    QPolygonF lane_line_vertices[4];

    // Lead tracking state
    struct LeadState {
      int active_counter[2] = {0, 0};
      bool virtual_active[2] = {false, false};
      bool stable[2] = {false, false};
      float smoothed_yRel[2] = {0.0f, 0.0f};
      bool prev_status[2] = {false, false};
      bool radar_assisted[2] = {false, false};
      QPointF vertices[2] = {};

      // New fields for time-to-lead calculation
      float d_rel[2] = {0.0f, 0.0f};
      float v_lead[2] = {0.0f, 0.0f};
      float v_rel[2] = {0.0f, 0.0f};
    } lead_state;

    // Stop detection state
    struct StopState {
      bool active = false;
      int stability_counter = 0;
      float stopping_distance = 0.0f;
      float display_distance = 0.0f;
      QPointF last_valid_position;
      float fade_alpha = 0.0f;
      float smoothed_size = 120.0f;

      // Position smoothing from old code
      QPointF smoothed_position;
      bool has_previous_position = false;
      float position_smoothing_factor = 0.15f;
      float size_smoothing_factor = 0.1f;

      // Animation tracking
      int stop_frame_count = 0;
      bool prev_stop_sign_visible = false;
    } stop_state;

    // G-force state
    struct GForceState {
      bool show_gforce = false;
      float lateral_g = 0.0f;         // Left/right G-force
      float longitudinal_g = 0.0f;    // Forward/backward G-force
      float smoothed_lateral = 0.0f;
      float smoothed_longitudinal = 0.0f;
      float max_lateral = 0.0f;       // Peak values for current session
      float max_longitudinal = 0.0f;
      float max_braking = 0.0f;
      QPointF history_lateral[50];    // Trail effect points
      QPointF history_longitudinal[50];
      int history_index = 0;
    } gforce_state;
  };
  static FrameState frame_state;

  // Internal implementation that works with base ModelRenderer
  template<typename ModelType>
  static void renderAllImpl(QPainter &painter, const QRect &rect, const UIState &s, const ModelType &model);

  // Performance-optimized rendering methods
  template<typename ModelType>
  static void updateFrameState(const UIState &s, const ModelType &model);
  static void renderBlinkers(QPainter &painter, const QRect &rect);
  static void renderStandstillTimer(QPainter &painter, const QRect &rect, const UIState &s);
  static void renderHybridGauges(QPainter &painter, const QRect &rect, const UIState &s);
  static void renderModelEnhancements(QPainter &painter, const QRect &rect, const UIState &s);
  static void renderGForceMeter(QPainter &painter, const QRect &rect, const UIState &s);
  static QColor getGForceColor(float g_value);

  // Model enhancement helpers
  static void updateLeadTracking(const UIState &s);
  static void updateStopDetection(const UIState &s);
  static void updateGForceData(const UIState &s);
  static void drawEnhancedLeads(QPainter &painter, const QRect &rect, const UIState &s);
  static void drawStopSignDetection(QPainter &painter, const QRect &rect, const UIState &s);
  static void drawAllRadarPoints(QPainter &painter, const QRect &rect, const UIState &s); // DEBUG
  static void drawGForceMeter(QPainter &painter, const QRect &rect, const UIState &s);

  // Geometry helpers
  static bool mapToScreen(float in_x, float in_y, float in_z, QPointF *out);
  static int get_path_length_idx(const cereal::XYZTData::Reader &line, float path_height);

  // Rendering helpers
  static void drawLeftTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot);
  static void drawRightTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot);
  static void drawColoredText(QPainter &painter, int x, int y, const QString &text, QColor color);
  static void drawStopSignOverlay(QPainter &painter, const QPointF &point, int size, float distance, float v_ego, float alpha);
  static void drawEnhancedLead(QPainter &painter, const cereal::RadarState::LeadData::Reader &lead_data,
                              const QPointF &vd, const QRect &rect, bool radar_assisted, float alpha,
                              float scale_factor, const UIState &s);

  // Performance: cached/pre-computed data
  static QPixmap* radar_icon;
  static QPixmap* vision_icon;
  static QPolygonF octagon_template;
  static bool icons_initialized;
  static bool octagon_initialized;

  // Initialization
  static void initializeStaticData();
  static void initOctagonTemplate();
  static void cleanup();
  static QPixmap createFallbackIcon(const QString &text, const QColor &color);

  // Constants
  static constexpr int BLINKER_SIZE = 120;
  static constexpr int UI_FREQ = 20;
  static constexpr float STANDSTILL_THRESHOLD = 0.1f;
  static constexpr float STANDSTILL_DEBOUNCE_TIME = 0.5f;
  static constexpr float MIN_DRAW_DISTANCE = 10.0f;
  static constexpr float MAX_DRAW_DISTANCE = 100.0f;
  static constexpr float GRAVITY_MS2 = 9.81f;  // Standard gravity in m/s²
};
