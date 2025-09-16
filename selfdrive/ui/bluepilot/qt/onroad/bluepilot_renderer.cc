// selfdrive/ui/bluepilot/qt/onroad/bluepilot_renderer.cc
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

  // FIXED: Properly get transform and clip region from model
  if (frame_state.show_radar || frame_state.show_stop) {
#ifdef BLUEPILOT
    // Only ModelRendererBP has the getter methods
    if constexpr (std::is_same_v<ModelType, ModelRendererBP>) {
      frame_state.transform = model.getTransform();
      frame_state.clip_region = model.getClipRegion();
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
        std::cerr << "WARNING: BluePilot transform is zero - overlays may not work properly" << std::endl;
      }
    }

    // Update lane line vertices even if transform might be zero
    // The transform might be set later in the frame

    // sValidate modelV2 message before accessing
    if (!sm.valid("modelV2")) {
      if (debug_counter % 50 == 0) {
        std::cerr << "WARNING: BluePilot modelV2 not valid, skipping lane line processing" << std::endl;
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
          std::cerr << "WARNING: BluePilot liveCalibration height list is empty, using default value" << std::endl;
        }
      }
    } else {
      if (debug_counter % 50 == 0) {
        std::cerr << "WARNING: BluePilot liveCalibration not valid, using default path_offset_z" << std::endl;
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
            std::cerr << "WARNING: BluePilot lane line " << i << " has inconsistent data sizes" << std::endl;
          }
          continue;
        }

        // Map line points to screen, limiting to reasonable distance
        for (int j = 0; j < static_cast<int>(line_x.size()) && line_x[j] < 100.0f; ++j) {
          if (line_x[j] < 0) continue;

          QPointF left, right;
          float y_offset = 0.025f; // Lane line width
          if (mapToScreen(line_x[j], line_y[j] - y_offset, line_z[j] + path_offset_z, &left) &&
              mapToScreen(line_x[j], line_y[j] + y_offset, line_z[j] + path_offset_z, &right)) {
            frame_state.lane_line_vertices[i].push_back(left);
            frame_state.lane_line_vertices[i].push_front(right);
          }
        }
      }
    } else {
      if (debug_counter % 100 == 0) {
        std::cerr << "WARNING: BluePilot skipping lane line mapping - transform zero: "
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
    drawLeftTurnSignal(painter, rect.center().x() - (blinker_x + BLINKER_SIZE),
                      blinker_y, BLINKER_SIZE, state, frame_state.left_blindspot);
  }
  if (frame_state.right_blinker) {
    drawRightTurnSignal(painter, rect.center().x() + blinker_x,
                       blinker_y, BLINKER_SIZE, state, frame_state.right_blindspot);
  }
}

void BluepilotRenderer::renderModelEnhancements(QPainter &painter, const QRect &rect, const UIState &s) {
  updateLeadTracking(s);
  updateStopDetection(s);

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

void BluepilotRenderer::updateLeadTracking(const UIState &s) {
  const SubMaster &sm = *(s.sm);

  // FIXED: Validate required messages before accessing
  if (!sm.valid("radarState") || !sm.valid("modelV2")) {
    static int error_counter = 0;
    if (error_counter++ % 50 == 0) {
      std::cerr << "WARNING: BluePilot radarState or modelV2 not valid in updateLeadTracking" << std::endl;
    }
    // Set all leads to inactive
    for (int i = 0; i < 2; ++i) {
      frame_state.lead_state.virtual_active[i] = false;
      frame_state.lead_state.stable[i] = false;
    }
    return;
  }

  const auto &radar_state = sm["radarState"].getRadarState();
  const auto &model = sm["modelV2"].getModelV2();

  for (int i = 0; i < 2; ++i) {
    const auto &lead_data = (i == 0) ? radar_state.getLeadOne() : radar_state.getLeadTwo();
    bool current_status = lead_data.getStatus();

    if (current_status) {
      float d_rel = lead_data.getDRel();
      float raw_yRel = lead_data.getYRel();
      bool is_radar_assisted = lead_data.getRadar();

      // Get path position at lead distance
      const auto &position = model.getPosition();
      const auto &line_x = position.getX();
      const auto &line_y = position.getY();
      const auto &line_z = position.getZ();

      if (line_x.size() == 0 || line_y.size() != line_x.size() || line_z.size() != line_x.size()) {
        frame_state.lead_state.virtual_active[i] = false;
        continue;
      }

      int idx = get_path_length_idx(position, d_rel);
      if (idx < 0 || idx >= static_cast<int>(line_y.size()) || idx >= static_cast<int>(line_z.size())) {
        frame_state.lead_state.virtual_active[i] = false;
        continue;
      }

      float path_y = line_y[idx];
      float path_z = line_z[idx];

      // FIXED: Use the same curvature calculation as model_old.cc
      float path_curvature = (idx > 1) ? fabs(line_y[idx] - line_y[idx - 1]) : 0.0f;

      // Stricter stability requirements for visual-only detections
      int required_stability = is_radar_assisted ? 2 : 8;
      int max_stability = is_radar_assisted ? 10 : 15;

      bool should_track = true;

      if (!is_radar_assisted) {
        // For visual-only detections, apply stricter criteria
        if (d_rel < 3.0f || d_rel > 80.0f) should_track = false;
        if (frame_state.lead_state.prev_status[i] && fabs(raw_yRel - frame_state.lead_state.smoothed_yRel[i]) > 0.5) {
          should_track = false;
        }
        if (fabs(raw_yRel - path_y) > 2.0f) should_track = false;
      }

      // Update stability counter based on tracking decision
      if (should_track && frame_state.lead_state.prev_status[i]) {
        frame_state.lead_state.active_counter[i] = std::min(frame_state.lead_state.active_counter[i] + 1, max_stability);
      } else if (should_track) {
        frame_state.lead_state.active_counter[i] = 1;
      } else {
        frame_state.lead_state.active_counter[i] = std::max(frame_state.lead_state.active_counter[i] - 2, 0);
      }

      if (frame_state.lead_state.active_counter[i] >= required_stability && should_track) {
        frame_state.lead_state.stable[i] = true;
        frame_state.lead_state.virtual_active[i] = true;
        frame_state.lead_state.radar_assisted[i] = is_radar_assisted;

        if (!frame_state.lead_state.prev_status[i]) {
          frame_state.lead_state.smoothed_yRel[i] = raw_yRel;
        } else {
          // FIXED: Use exact approach from model_old.cc for better curve handling
          // More path influence for curves
          float path_weight = std::min(0.6f + path_curvature * 5.0f, 0.9f);

          // Adaptive alpha based on distance - smoother for close objects, less for distant ones
          float alpha = is_radar_assisted ?
                        0.05f + 0.15f * (d_rel / 25.0f) : // Radar: 0.05 to 0.2
                        0.025f + 0.125f * (d_rel / 25.0f); // Vision: 0.025 to 0.15

          // Clamp alpha to reasonable range
          alpha = std::clamp(alpha, 0.025f, 0.25f);

          // Add distance-based jitter suppression
          float max_lateral_change = (d_rel < 8.0) ? 0.08f : 0.2f;
          float lateral_diff = raw_yRel - frame_state.lead_state.smoothed_yRel[i];
          if (fabs(lateral_diff) > max_lateral_change) {
            // Limit lateral movement rate for stability
            raw_yRel = frame_state.lead_state.smoothed_yRel[i] + ((lateral_diff > 0) ? max_lateral_change : -max_lateral_change);
          }

          // First smooth the raw radar reading
          float smoothed_raw = alpha * raw_yRel + (1.0f - alpha) * frame_state.lead_state.smoothed_yRel[i];

          // Then blend with the path position using dynamic path weight
          frame_state.lead_state.smoothed_yRel[i] = path_weight * path_y + (1.0f - path_weight) * smoothed_raw;
        }

        // FIXED: Use exact same approach as model_old.cc - no Y sign flip, use path_offset_z
        float path_offset_z = 0.0f;
        if (sm.valid("liveCalibration")) {
          const auto &live_calib = sm["liveCalibration"].getLiveCalibration();
          const auto &height_list = live_calib.getHeight();
          if (height_list.size() > 0) {
            path_offset_z = height_list[0];
          }
        }

        QPointF current_pos;
        if (mapToScreen(d_rel, frame_state.lead_state.smoothed_yRel[i], path_z + path_offset_z, &current_pos)) {
          bool reasonable_position = true;

          if (is_radar_assisted) {
            // Check if radar detection is reasonable
            QRectF screen_bounds = frame_state.clip_region;
            float margin = 100.0f;
            QRectF extended_bounds = screen_bounds.adjusted(-margin, -margin, margin, margin);

            if (!extended_bounds.contains(current_pos) || fabs(frame_state.lead_state.smoothed_yRel[i]) > 8.0f) {
              reasonable_position = false;
            }

            if (fabs(frame_state.lead_state.smoothed_yRel[i]) > 5.0f) {
              frame_state.lead_state.active_counter[i] = std::max(frame_state.lead_state.active_counter[i] - 1, 0);
              if (fabs(frame_state.lead_state.smoothed_yRel[i]) > 6.5f) {
                reasonable_position = false;
              }
            }
          }

          if (reasonable_position) {
            frame_state.lead_state.vertices[i] = current_pos;
          } else {
            frame_state.lead_state.active_counter[i] = std::max(frame_state.lead_state.active_counter[i] - 2, 0);
            frame_state.lead_state.virtual_active[i] = false;
          }
        } else {
          frame_state.lead_state.virtual_active[i] = false;
        }
      } else {
        frame_state.lead_state.virtual_active[i] = false;
        frame_state.lead_state.stable[i] = false;
      }
    } else {
      // Improved decay logic to prevent rapid flickering
      if (frame_state.lead_state.active_counter[i] > 0) {
        int decay_rate = frame_state.lead_state.radar_assisted[i] ? 1 : 2;
        frame_state.lead_state.active_counter[i] = std::max(frame_state.lead_state.active_counter[i] - decay_rate, 0);

        int deactivation_threshold = frame_state.lead_state.radar_assisted[i] ? 1 : 3;
        frame_state.lead_state.virtual_active[i] = frame_state.lead_state.active_counter[i] >= deactivation_threshold;

        if (frame_state.lead_state.active_counter[i] == 0) {
          frame_state.lead_state.stable[i] = false;
        }
      } else {
        frame_state.lead_state.virtual_active[i] = false;
        frame_state.lead_state.stable[i] = false;
      }
    }

    // Store lead data for time-to-lead calculation
    frame_state.lead_state.d_rel[i] = lead_data.getDRel();
    frame_state.lead_state.v_lead[i] = lead_data.getVLead();
    frame_state.lead_state.v_rel[i] = lead_data.getVRel();

    frame_state.lead_state.prev_status[i] = current_status;
  }
}

void BluepilotRenderer::updateStopDetection(const UIState &s) {
  const SubMaster &sm = *(s.sm);

  float v_ego = frame_state.vehicle_speed;
  bool vehicle_stopped = v_ego < 0.5f;

  if (vehicle_stopped && frame_state.stop_state.active) {
    frame_state.stop_state.active = false;
    frame_state.stop_state.stability_counter = 0;
  }

  if (!s.scene.show_stop_indicator_overlay || vehicle_stopped) {
    frame_state.stop_state.fade_alpha = std::max(0.0f, frame_state.stop_state.fade_alpha - 0.05f);
    return;
  }

  // FIXED: Validate required messages before accessing
  if (!sm.valid("modelV2") || !sm.valid("radarState") || !sm.valid("carState")) {
    static int error_counter = 0;
    if (error_counter++ % 50 == 0) {
      std::cerr << "WARNING: BluePilot required messages not valid in updateStopDetection" << std::endl;
    }
    frame_state.stop_state.active = false;
    frame_state.stop_state.stability_counter = 0;
    frame_state.stop_state.fade_alpha = std::max(0.0f, frame_state.stop_state.fade_alpha - 0.1f);
    return;
  }

  const auto &model = sm["modelV2"].getModelV2();
  const auto &radar_state = sm["radarState"].getRadarState();
  const auto &lead_one = radar_state.getLeadOne();
  const auto car_state = sm["carState"].getCarState();
  bool brake_pressed = car_state.getBrakePressed();
  float brake_value = car_state.getBrake();

  // Get path offset for z calculations
  float path_offset_z = 0.0f;
  if (sm.valid("liveCalibration")) {
    const auto &live_calib = sm["liveCalibration"].getLiveCalibration();
    const auto &height_list = live_calib.getHeight();
    if (height_list.size() > 0) {
      path_offset_z = height_list[0];
    }
  }

  const auto &velocity = model.getVelocity().getX();
  const auto &position_x = model.getPosition().getX();
  const auto &position_y = model.getPosition().getY();
  const auto &position_z = model.getPosition().getZ();

  size_t vel_size = velocity.size();
  size_t pos_x_size = position_x.size();
  const size_t MAX_ARRAY_SIZE = 1000;
  const size_t MIN_ARRAY_SIZE = 2;

  bool data_valid = (vel_size >= MIN_ARRAY_SIZE && vel_size <= MAX_ARRAY_SIZE &&
                     pos_x_size == vel_size && position_y.size() == vel_size &&
                     position_z.size() == vel_size);

  if (!data_valid) {
    frame_state.stop_state.active = false;
    frame_state.stop_state.stability_counter = 0;
    frame_state.stop_state.fade_alpha = std::max(0.0f, frame_state.stop_state.fade_alpha - 0.1f);
    return;
  }

  float stopping_distance = -1.0f;
  int stop_idx = -1;
  size_t max_search_idx = std::min(vel_size, static_cast<size_t>(200));

  for (size_t i = 0; i < max_search_idx; ++i) {
    if (i >= vel_size || i >= pos_x_size) break;

    if (position_x[i] < 0 || position_x[i] > 200.0f) continue;

    if (velocity[i] < 0.5f) {
      stopping_distance = position_x[i];
      stop_idx = static_cast<int>(i);
      break;
    }
  }

  if (stop_idx >= 0 && stop_idx < static_cast<int>(pos_x_size) && stopping_distance > 0) {
    stopping_distance = std::min(stopping_distance, 50.0f);
    frame_state.stop_state.display_distance = std::max(0.1f, stopping_distance - 4.5f);

    // Use radar data for more accurate distance when lead is present
    if (lead_one.getStatus() && lead_one.getDRel() < stopping_distance + 5.0f) {
      float radar_distance = lead_one.getDRel();
      if (radar_distance > 3.0f && radar_distance < 50.0f) {
        stopping_distance = radar_distance;
        frame_state.stop_state.stability_counter = std::max(frame_state.stop_state.stability_counter, 10);
        frame_state.stop_state.active = true;
        frame_state.stop_state.stopping_distance = stopping_distance;
      }
    }

    if (stopping_distance >= 5.0f && stopping_distance <= 50.0f) {
      // Increase stability based on braking
      if (brake_pressed || brake_value > 0.1f) {
        frame_state.stop_state.stability_counter = std::min(frame_state.stop_state.stability_counter + 2, 20);
      } else {
        frame_state.stop_state.stability_counter = std::min(frame_state.stop_state.stability_counter + 1, 20);
      }

      if (frame_state.stop_state.stability_counter >= 3) {
        frame_state.stop_state.active = true;

        if (frame_state.stop_state.stopping_distance > 0) {
          frame_state.stop_state.stopping_distance = frame_state.stop_state.stopping_distance * 0.8f + stopping_distance * 0.2f;
        } else {
          frame_state.stop_state.stopping_distance = stopping_distance;
        }

        float x = position_x[stop_idx];
        float y = position_y[stop_idx];
        float z = position_z[stop_idx];

        // FIXED: Use path_offset_z like in model_old.cc
        QPointF screen_point;
        if (mapToScreen(x, y, z + path_offset_z, &screen_point)) {
          frame_state.stop_state.last_valid_position = screen_point;
        }

        // Update smoothed size based on distance
        float target_size = 120.0f * (1.0 - std::min(0.7f, (stopping_distance - 5.0f) / 45.0f));
        frame_state.stop_state.smoothed_size = frame_state.stop_state.smoothed_size * 0.9f + target_size * 0.1f;
      }
    } else {
      frame_state.stop_state.stability_counter = std::max(0, frame_state.stop_state.stability_counter - 1);

      // Keep sign visible longer if braking
      if ((brake_pressed || brake_value > 0.1f) && frame_state.stop_state.active) {
        frame_state.stop_state.stability_counter = std::max(frame_state.stop_state.stability_counter, 5);
      }

      if (frame_state.stop_state.stability_counter <= 0) {
        frame_state.stop_state.active = false;
      }
    }
  } else {
    frame_state.stop_state.stability_counter = std::max(0, frame_state.stop_state.stability_counter - 1);
    if (frame_state.stop_state.stability_counter <= 0) {
      frame_state.stop_state.active = false;
    }
  }

  if (frame_state.stop_state.active && frame_state.stop_state.fade_alpha < 1.0f) {
    frame_state.stop_state.fade_alpha = std::min(1.0f, frame_state.stop_state.fade_alpha + 0.1f);
  } else if (!frame_state.stop_state.active && frame_state.stop_state.fade_alpha > 0.0f) {
    frame_state.stop_state.fade_alpha = std::max(0.0f, frame_state.stop_state.fade_alpha - 0.05f);
  }
}



bool BluepilotRenderer::mapToScreen(float in_x, float in_y, float in_z, QPointF *out) {
  if (frame_state.transform.isZero()) {
    static int error_counter = 0;
    if (error_counter++ % 200 == 0) {
      std::cerr << "BluePilot: Transform is zero, cannot map to screen" << std::endl;
    }
    return false;
  }

  if (!std::isfinite(in_x) || !std::isfinite(in_y) || !std::isfinite(in_z)) {
    return false;
  }

  Eigen::Vector3f input(in_x, in_y, in_z);
  auto pt = frame_state.transform * input;

  if (std::abs(pt.z()) < 0.001f) {
    return false;
  }

  QPointF screen_point(pt.x() / pt.z(), pt.y() / pt.z());

  if (!std::isfinite(screen_point.x()) || !std::isfinite(screen_point.y())) {
    return false;
  }

  *out = screen_point;
  return frame_state.clip_region.contains(*out);
}

int BluepilotRenderer::get_path_length_idx(const cereal::XYZTData::Reader &line, float path_height) {
  const auto &line_x = line.getX();
  int max_idx = 0;
  for (int i = 1; i < static_cast<int>(line_x.size()) && line_x[i] <= path_height; ++i) {
    max_idx = i;
  }
  return max_idx;
}

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

void BluepilotRenderer::drawColoredText(QPainter &painter, int x, int y, const QString &text, QColor color) {
  QRect real_rect = painter.fontMetrics().boundingRect(text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});
  painter.setPen(color);
  painter.drawText(real_rect.x(), real_rect.bottom(), text);
}

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
