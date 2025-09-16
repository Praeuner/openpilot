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
#include "selfdrive/ui/bluepilot/qt/onroad/overlays/radar_overlay.h"
#include "selfdrive/ui/bluepilot/qt/onroad/overlays/stop_sign_overlay.h"
#include "selfdrive/ui/bluepilot/qt/onroad/overlays/standstill_timer_overlay.h"
#include "selfdrive/ui/bluepilot/qt/onroad/overlays/gforce_overlay.h"
#include "selfdrive/ui/bluepilot/qt/onroad/overlays/hybrid_gauges_overlay.h"

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
    StandstillTimerOverlay::StandstillState standstill_state;
    float vehicle_speed = 0.0f;

    // Hybrid state
    HybridGaugesOverlay::HybridState hybrid_state;

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
    StopSignOverlay::StopState stop_state;

    // G-force state
    GForceOverlay::GForceState gforce_state;
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
  static void renderModelEnhancements(QPainter &painter, const QRect &rect, const UIState &s);

  // Model enhancement helpers (deprecated - moved to ModelRendererBP)
  static void updateLeadTracking(const UIState &s);
  static void updateStopDetection(const UIState &s);

  // Geometry helpers (deprecated - moved to ModelRendererBP)
  static bool mapToScreen(float in_x, float in_y, float in_z, QPointF *out);
  static int get_path_length_idx(const cereal::XYZTData::Reader &line, float path_height);

  // Rendering helpers (deprecated - moved to ModelRendererBP)
  static void drawLeftTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot);
  static void drawRightTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot);
  static void drawColoredText(QPainter &painter, int x, int y, const QString &text, QColor color);

  // Cleanup
  static void cleanup();

  // Constants
  static constexpr int BLINKER_SIZE = 120;
  static constexpr int UI_FREQ = 20;
  static constexpr float STANDSTILL_THRESHOLD = 0.1f;
  static constexpr float STANDSTILL_DEBOUNCE_TIME = 0.5f;
  static constexpr float MIN_DRAW_DISTANCE = 10.0f;
  static constexpr float MAX_DRAW_DISTANCE = 100.0f;
  static constexpr float GRAVITY_MS2 = 9.81f;  // Standard gravity in m/s²
};
