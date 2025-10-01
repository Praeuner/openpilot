// selfdrive/ui/bluepilot/qt/onroad/bluepilot_renderer.cc

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include "selfdrive/ui/bluepilot/qt/onroad/bluepilot_renderer.h"
#include "selfdrive/ui/qt/onroad/model.h"
#include "selfdrive/ui/qt/util.h"
#include "common/timing.h"
#include "common/params.h"
#include <QApplication>
#include <QPainterPath>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <type_traits>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/qt/onroad/model.h"
#endif

#ifdef BLUEPILOT
#include "selfdrive/ui/bluepilot/qt/onroad/model_bp.h"
#endif

// Include overlay classes
#include "selfdrive/ui/bluepilot/qt/onroad/overlays/radar_overlay.h"
#include "selfdrive/ui/bluepilot/qt/onroad/overlays/stop_sign_overlay.h"
#include "selfdrive/ui/bluepilot/qt/onroad/overlays/standstill_timer_overlay.h"
#include "selfdrive/ui/bluepilot/qt/onroad/overlays/gforce_overlay.h"
#include "selfdrive/ui/bluepilot/qt/onroad/overlays/hybrid_gauges_overlay.h"


// Static member initialization
BluepilotRenderer::FrameState BluepilotRenderer::frame_state;

#ifdef BLUEPILOT
void BluepilotRenderer::renderAll(QPainter &painter, const QRect &rect, const UIState &s, const ModelRendererBP &model) {
  renderAllImpl(painter, rect, s, model);
}
#else
#ifdef SUNNYPILOT
void BluepilotRenderer::renderAll(QPainter &painter, const QRect &rect, const UIState &s, const ModelRendererSP &model) {
  renderAllImpl(painter, rect, s, model);
}
#else
void BluepilotRenderer::renderAll(QPainter &painter, const QRect &rect, const UIState &s, const ModelRenderer &model) {
  renderAllImpl(painter, rect, s, model);
}
#endif
#endif

template<typename ModelType>
void BluepilotRenderer::renderAllImpl(QPainter &painter, const QRect &rect, const UIState &s, const ModelType &model) {
  // Debug early to see what's enabled
  // static int global_debug_counter = 0;
  // if (global_debug_counter++ % 100 == 0) {
  //   std::cout << "BluePilot renderAll - hybrid: " << s.scene.show_hybrid_drive_overlay
  //             << " radar: " << s.scene.show_bp_radar_overlay
  //             << " stop: " << s.scene.show_stop_indicator_overlay
  //             << " timer: " << s.scene.stand_still_timer << std::endl;
  // }

  // PERFORMANCE: Early exit if no BluePilot features enabled
  if (!s.scene.show_hybrid_drive_overlay &&
      !s.scene.show_bp_radar_overlay &&
      !s.scene.show_stop_indicator_overlay &&
      !s.scene.stand_still_timer &&
      !s.scene.show_gforce_meter) {
    return;
  }

  // PERFORMANCE: Single state update per frame - batch all data gathering
  updateFrameState(s, model);

  // 1. BOTTOM LAYER: Blinkers and standstill timer
  renderBlinkers(painter, rect);
  StandstillTimerOverlay::render(painter, rect, s, frame_state.standstill_state);

  // 2. MIDDLE LAYER: Model-dependent overlays (radar, stop signs)
  if (frame_state.show_radar || frame_state.show_stop) {
    renderModelEnhancements(painter, rect, s);
  }

  // 3. TOP LAYER: Hybrid gauges (always on top)
  HybridGaugesOverlay::render(painter, rect, s, frame_state.hybrid_state);

  // 4. G-FORCE METER: Bottom right corner
  GForceOverlay::render(painter, rect, s, frame_state.gforce_state);
}

template<typename ModelType>
void BluepilotRenderer::updateFrameState(const UIState &s, const ModelType &model) {
  const SubMaster &sm = *(s.sm);

  // FIXED: Validate carState message before accessing
  if (!sm.valid("carState")) {
    static int error_counter = 0;
    if (error_counter++ % 50 == 0) {
      std::cerr << "ERROR: BluePilot carState message not valid, skipping frame state update" << std::endl;
    }
    return;
  }

  const auto car_state = sm["carState"].getCarState();

  // Update blinker state
  frame_state.left_blinker = car_state.getLeftBlinker();
  frame_state.right_blinker = car_state.getRightBlinker();
  frame_state.left_blindspot = car_state.getLeftBlindspot();
  frame_state.right_blindspot = car_state.getRightBlindspot();

  // Update standstill state
  frame_state.standstill_state.standstill = car_state.getStandstill();
  frame_state.vehicle_speed = car_state.getVEgo();
  frame_state.standstill_state.vehicle_speed = frame_state.vehicle_speed;

  // Update hybrid data if available
  if (sm.updated("carStateBP") && sm.valid("carStateBP")) {
    const auto car_state_bp = sm["carStateBP"].getCarStateBP();

    frame_state.hybrid_state.hybrid_available = car_state_bp.getHybridDrive().getDataAvailable();
    if (frame_state.hybrid_state.hybrid_available) {
      frame_state.hybrid_state.throttle_demand = car_state_bp.getHybridDrive().getThrottleDemandPercent();
      frame_state.hybrid_state.throttle_threshold = car_state_bp.getHybridDrive().getThrottleThresholdPercent();
      frame_state.hybrid_state.power_mode = QString::fromStdString(car_state_bp.getHybridDrive().getPowerFlowMode());
      frame_state.hybrid_state.engine_reason = QString::fromStdString(car_state_bp.getHybridDrive().getEngineOnReason());
    }

    frame_state.hybrid_state.battery_available = car_state_bp.getHybridBattery().getDataAvailable();
    if (frame_state.hybrid_state.battery_available) {
      frame_state.hybrid_state.batt_soc_actual = car_state_bp.getHybridBattery().getSocActual();
      frame_state.hybrid_state.batt_soc_min = car_state_bp.getHybridBattery().getSocMinPerc();
      frame_state.hybrid_state.batt_soc_max = car_state_bp.getHybridBattery().getSocMaxPerc();
      frame_state.hybrid_state.batt_volt_actual = car_state_bp.getHybridBattery().getVoltActual();
      frame_state.hybrid_state.batt_volt_low = car_state_bp.getHybridBattery().getVoltLowLimit();
      frame_state.hybrid_state.batt_volt_high = car_state_bp.getHybridBattery().getVoltHighLimit();
      frame_state.hybrid_state.batt_amps_actual = car_state_bp.getHybridBattery().getAmpsActual();
    }
  }

  // Update model enhancement flags and transforms
  frame_state.show_radar = s.scene.show_bp_radar_overlay;
  frame_state.show_stop = s.scene.show_stop_indicator_overlay;
  frame_state.gforce_state.show_gforce = s.scene.show_gforce_meter;

  // Update G-force data
  GForceOverlay::updateGForceData(s, frame_state.gforce_state);

  // Debug logging
  static int debug_counter = 0;
  // if (debug_counter++ % 100 == 0) {
  //   std::cout << "BluePilot Debug - show_radar: " << frame_state.show_radar
  //             << " show_stop: " << frame_state.show_stop
  //             << " scene.show_bp_radar_overlay: " << s.scene.show_bp_radar_overlay
  //             << " scene.show_stop_indicator_overlay: " << s.scene.show_stop_indicator_overlay << std::endl;
  // }

  // Get transform and clip region from model, and update lead/stop tracking
  if (frame_state.show_radar || frame_state.show_stop) {
#ifdef BLUEPILOT
    // Only ModelRendererBP has the getter methods and tracking functions
    if constexpr (std::is_same_v<ModelType, ModelRendererBP>) {
      frame_state.transform = model.getTransform();
      frame_state.clip_region = model.getClipRegion();

      // Call the model's tracking methods to update its internal state
      const_cast<ModelRendererBP&>(model).updateLeadTracking(s);
      const_cast<ModelRendererBP&>(model).updateStopDetection(s);

      // Copy the results from the model to our frame state
      const auto& model_lead_state = model.getLeadState();
      const auto& model_stop_state = model.getStopState();

      // Copy lead state
      for (int i = 0; i < 2; ++i) {
        frame_state.lead_state.active_counter[i] = model_lead_state.active_counter[i];
        frame_state.lead_state.virtual_active[i] = model_lead_state.virtual_active[i];
        frame_state.lead_state.stable[i] = model_lead_state.stable[i];
        frame_state.lead_state.smoothed_yRel[i] = model_lead_state.smoothed_yRel[i];
        frame_state.lead_state.prev_status[i] = model_lead_state.prev_status[i];
        frame_state.lead_state.radar_assisted[i] = model_lead_state.radar_assisted[i];
        frame_state.lead_state.vertices[i] = model_lead_state.vertices[i];
        frame_state.lead_state.d_rel[i] = model_lead_state.d_rel[i];
        frame_state.lead_state.v_lead[i] = model_lead_state.v_lead[i];
        frame_state.lead_state.v_rel[i] = model_lead_state.v_rel[i];
      }

      // Copy stop state
      frame_state.stop_state.active = model_stop_state.active;
      frame_state.stop_state.stability_counter = model_stop_state.stability_counter;
      frame_state.stop_state.stopping_distance = model_stop_state.stopping_distance;
      frame_state.stop_state.display_distance = model_stop_state.display_distance;
      frame_state.stop_state.smoothed_size = model_stop_state.smoothed_size;
      frame_state.stop_state.fade_alpha = model_stop_state.fade_alpha;
      frame_state.stop_state.last_valid_position = model_stop_state.last_valid_position;
    } else {
      // For base ModelRenderer/ModelRendererSP, we don't have access to these
      // Model enhancements will be disabled for non-BluePilot builds
      frame_state.show_radar = false;
      frame_state.show_stop = false;
      return;
    }
#else
    // Non-BluePilot builds don't have these features
    frame_state.show_radar = false;
    frame_state.show_stop = false;
    return;
#endif

    // Check if transform is valid but don't return early
    if (frame_state.transform.isZero()) {
      if (debug_counter % 20 == 0) {
        BPLog::bpWarn() << "[bp.onroad.bluepilot_renderer] updateFrameState | BluePilot transform is zero - overlays may not work properly" << std::endl;
      }
    }

    // Update lane line vertices even if transform might be zero
    // The transform might be set later in the frame

    // sValidate modelV2 message before accessing
    if (!sm.valid("modelV2")) {
      if (debug_counter % 50 == 0) {
        BPLog::bpWarn() << "[bp.onroad.bluepilot_renderer] updateFrameState | WARNING: BluePilot modelV2 not valid, skipping lane line processing" << std::endl;
      }
      return;
    }

    const auto &modelV2 = sm["modelV2"].getModelV2();
    const auto &lane_lines = modelV2.getLaneLines();

    // FIXED: Add proper validation for liveCalibration height access
    float path_offset_z = 0.0f; // Default value
    if (sm.valid("liveCalibration")) {
      const auto &live_calib = sm["liveCalibration"].getLiveCalibration();
      const auto &height_list = live_calib.getHeight();
      if (height_list.size() > 0) {
        path_offset_z = height_list[0];
      } else {
        if (debug_counter % 50 == 0) {
          BPLog::bpWarn() << "[bp.onroad.bluepilot_renderer] updateFrameState | WARNING: BluePilot liveCalibration height list is empty, using default value" << std::endl;
        }
      }
    } else {
      if (debug_counter % 50 == 0) {
        BPLog::bpWarn() << "[bp.onroad.bluepilot_renderer] updateFrameState | WARNING: BluePilot liveCalibration not valid, using default path_offset_z" << std::endl;
      }
    }

    // Only map lane lines if transform is valid and we have lane line data
    if (!frame_state.transform.isZero() && lane_lines.size() > 0) {
      for (int i = 0; i < 4 && i < static_cast<int>(lane_lines.size()); ++i) {
        frame_state.lane_line_vertices[i].clear();
        const auto &line = lane_lines[i];
        const auto line_x = line.getX(), line_y = line.getY(), line_z = line.getZ();

        // Additional safety check for line data consistency
        if (line_x.size() == 0 || line_y.size() != line_x.size() || line_z.size() != line_x.size()) {
          if (debug_counter % 100 == 0) {
            BPLog::bpWarn() << "[bp.onroad.bluepilot_renderer] updateFrameState | WARNING: BluePilot lane line " << i << " has inconsistent data sizes" << std::endl;
          }
          continue;
        }

        // Map line points to screen, limiting to reasonable distance
        for (int j = 0; j < static_cast<int>(line_x.size()) && line_x[j] < 100.0f; ++j) {
          if (line_x[j] < 0) continue;

          QPointF left, right;
          float y_offset = 0.025f; // Lane line width

          // Map to screen using transform directly
          Eigen::Vector3f left_input(line_x[j], line_y[j] - y_offset, line_z[j] + path_offset_z);
          Eigen::Vector3f right_input(line_x[j], line_y[j] + y_offset, line_z[j] + path_offset_z);
          auto left_pt = frame_state.transform * left_input;
          auto right_pt = frame_state.transform * right_input;

          if (std::abs(left_pt.z()) > 0.001f && std::abs(right_pt.z()) > 0.001f) {
            left = QPointF(left_pt.x() / left_pt.z(), left_pt.y() / left_pt.z());
            right = QPointF(right_pt.x() / right_pt.z(), right_pt.y() / right_pt.z());

            if (frame_state.clip_region.contains(left) && frame_state.clip_region.contains(right)) {
              frame_state.lane_line_vertices[i].push_back(left);
              frame_state.lane_line_vertices[i].push_front(right);
            }
          }
        }
      }
    } else {
      if (debug_counter % 100 == 0) {
        BPLog::bpWarn() << "[bp.onroad.bluepilot_renderer] updateFrameState | WARNING: BluePilot skipping lane line mapping - transform zero: "
                  << frame_state.transform.isZero() << " lane_lines size: " << lane_lines.size() << std::endl;
      }
    }
  }
}

void BluepilotRenderer::renderBlinkers(QPainter &painter, const QRect &rect) {
  if (!frame_state.left_blinker && !frame_state.right_blinker) {
    frame_state.blinker_frame = 0;
    return;
  }

  frame_state.blinker_frame++;
  int state = (frame_state.blinker_frame % UI_FREQ < (UI_FREQ / 2)) ? 1 : 0;

  int blinker_x = 180;
  int blinker_y = 90;

  if (frame_state.left_blinker) {
    // Note: Drawing functions are now in ModelRendererBP
    // For now, we'll keep the original implementation
    drawLeftTurnSignal(painter, rect.center().x() - (blinker_x + BLINKER_SIZE),
                      blinker_y, BLINKER_SIZE, state, frame_state.left_blindspot);
  }
  if (frame_state.right_blinker) {
    drawRightTurnSignal(painter, rect.center().x() + blinker_x,
                       blinker_y, BLINKER_SIZE, state, frame_state.right_blindspot);
  }
}

void BluepilotRenderer::renderModelEnhancements(QPainter &painter, const QRect &rect, const UIState &s) {
  // Lead tracking and stop detection are handled by ModelRendererBP
  // We just use the results from frame_state that was populated in updateFrameState

  if (frame_state.show_radar) {
    RadarOverlay::render(painter, rect, s, frame_state.lead_state.vertices,
                        frame_state.lead_state.radar_assisted,
                        frame_state.lead_state.virtual_active,
                        frame_state.lead_state.active_counter,
                        s.scene.radar_overlay_size == 1 ? 0.95f :
                        s.scene.radar_overlay_size == 2 ? 1.15f :
                        s.scene.radar_overlay_size == 3 ? 1.35f :
                        s.scene.radar_overlay_size == 4 ? 1.55f : 1.35f,
                        frame_state.transform, frame_state.clip_region);
  }

  if (frame_state.show_stop) {
    StopSignOverlay::render(painter, rect, s, frame_state.stop_state,
                           frame_state.clip_region, frame_state.lane_line_vertices,
                           frame_state.vehicle_speed);
  }
}

// Removed deprecated functions - now handled by ModelRendererBP

void BluepilotRenderer::drawLeftTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot) {

  painter.setRenderHint(QPainter::Antialiasing, true);

  QColor circle_color, arrow_color;
  if (blindspot) {
    circle_color = state ? QColor(204, 0, 1) : QColor(164, 0, 1);
    arrow_color = state ? QColor(255, 255, 255) : QColor(72, 1, 1);
  } else {
    circle_color = state ? QColor(30, 215, 96) : QColor(22, 156, 69);
    arrow_color = state ? QColor(255, 255, 255) : QColor(9, 56, 27);
  }

  painter.setPen(Qt::NoPen);
  painter.setBrush(circle_color);
  painter.drawEllipse(x, y, size, size);

  int arrowSize = 50;
  int arrowX = x + (size - arrowSize) / 4;
  int arrowY = y + (size - arrowSize) / 2;
  painter.setBrush(arrow_color);

  QPolygon arrowPolygon;
  arrowPolygon << QPoint(arrowX + 10, arrowY + arrowSize / 2)
               << QPoint(arrowX + arrowSize - 3, arrowY)
               << QPoint(arrowX + arrowSize, arrowY)
               << QPoint(arrowX + arrowSize, arrowY + arrowSize)
               << QPoint(arrowX + arrowSize - 3, arrowY + arrowSize)
               << QPoint(arrowX + 10, arrowY + arrowSize / 2);
  painter.drawPolygon(arrowPolygon);

  int tailWidth = arrowSize / 2.25;
  int tailHeight = arrowSize / 2;
  QRect tailRect(arrowX + arrowSize - 3, arrowY + arrowSize / 4, tailWidth, tailHeight);
  painter.fillRect(tailRect, arrow_color);
}

void BluepilotRenderer::drawRightTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot) {

  painter.setRenderHint(QPainter::Antialiasing, true);

  QColor circle_color, arrow_color;
  if (blindspot) {
    circle_color = state ? QColor(204, 0, 1) : QColor(164, 0, 1);
    arrow_color = state ? QColor(255, 255, 255) : QColor(72, 1, 1);
  } else {
    circle_color = state ? QColor(30, 215, 96) : QColor(22, 156, 69);
    arrow_color = state ? QColor(255, 255, 255) : QColor(9, 56, 27);
  }

  painter.setPen(Qt::NoPen);
  painter.setBrush(circle_color);
  painter.drawEllipse(x, y, size, size);

  int arrowSize = 50;
  int arrowX = x + (size - arrowSize) / 2 + (arrowSize / 2.5) - 3;
  int arrowY = y + (size - arrowSize) / 2;
  painter.setBrush(arrow_color);

  QPolygon arrowPolygon;
  arrowPolygon << QPoint(arrowX + arrowSize - 10, arrowY + arrowSize / 2)
               << QPoint(arrowX + 3, arrowY)
               << QPoint(arrowX, arrowY)
               << QPoint(arrowX, arrowY + arrowSize)
               << QPoint(arrowX + 3, arrowY + arrowSize)
               << QPoint(arrowX + arrowSize - 10, arrowY + arrowSize / 2);
  painter.drawPolygon(arrowPolygon);

  int tailWidth = arrowSize / 2.25;
  int tailHeight = arrowSize / 2;
  QRect tailRect(arrowX - tailWidth + 3, arrowY + arrowSize / 4, tailWidth, tailHeight);
  painter.fillRect(tailRect, arrow_color);
}

// drawColoredText removed - use ModelRendererBP::drawColoredText instead

// Explicit template instantiations to ensure proper compilation
template void BluepilotRenderer::renderAllImpl<ModelRenderer>(QPainter &painter, const QRect &rect, const UIState &s, const ModelRenderer &model);
template void BluepilotRenderer::updateFrameState<ModelRenderer>(const UIState &s, const ModelRenderer &model);

#ifdef SUNNYPILOT
template void BluepilotRenderer::renderAllImpl<ModelRendererSP>(QPainter &painter, const QRect &rect, const UIState &s, const ModelRendererSP &model);
template void BluepilotRenderer::updateFrameState<ModelRendererSP>(const UIState &s, const ModelRendererSP &model);
#endif

void BluepilotRenderer::cleanup() {
  RadarOverlay::cleanup();
  // StopSignOverlay doesn't need cleanup as octagon_template is just geometry data
}
