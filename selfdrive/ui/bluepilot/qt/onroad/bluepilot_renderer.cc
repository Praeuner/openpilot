// selfdrive/ui/bluepilot/qt/onroad/bluepilot_renderer.cc
#include "selfdrive/ui/bluepilot/qt/onroad/bluepilot_renderer.h"
#include "selfdrive/ui/bluepilot/qt/onroad/widgets/hybrid_drive_gauge.h"
#include "selfdrive/ui/qt/onroad/model.h"
#include "selfdrive/ui/qt/util.h"
#include "common/timing.h"
#include "common/params.h"
#include <QApplication>
#include <chrono>
#include <algorithm>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/qt/onroad/model.h"
#endif

// Automotive styling constants
constexpr int CORNER_RADIUS = 6;
constexpr int BORDER_WIDTH = 3;

// Static member initialization
BluepilotRenderer::FrameState BluepilotRenderer::frame_state;
QPixmap* BluepilotRenderer::radar_icon = nullptr;
QPixmap* BluepilotRenderer::vision_icon = nullptr;
QPolygonF BluepilotRenderer::octagon_template;
bool BluepilotRenderer::icons_initialized = false;
bool BluepilotRenderer::octagon_initialized = false;

// Helper function to create automotive metallic gradient
static QLinearGradient createAutomotiveGradient(QRect rect, QColor baseColor, bool isVertical = true) {
  QLinearGradient gradient = isVertical ?
    QLinearGradient(rect.topLeft(), rect.bottomLeft()) :
    QLinearGradient(rect.topLeft(), rect.topRight());

  QColor highlight = baseColor.lighter(130);
  QColor shadow = baseColor.darker(130);

  gradient.setColorAt(0, highlight);
  gradient.setColorAt(0.3, baseColor);
  gradient.setColorAt(0.7, baseColor);
  gradient.setColorAt(1, shadow);

  return gradient;
}

// Helper function to create automotive background gradient
static QRadialGradient createAutomotiveBackground(QRect rect) {
  QRadialGradient gradient(rect.center(), rect.width() * 0.7);
  gradient.setColorAt(0, QColor(44, 62, 80)); // Neutral center
  gradient.setColorAt(1, QColor(26, 37, 47)); // Dark edge
  return gradient;
}

// Helper function to draw inset border
static void drawInsetBorder(QPainter &painter, QRect rect, QColor borderColor, int radius = CORNER_RADIUS) {
  // Outer border (highlight)
  painter.setPen(QPen(borderColor, BORDER_WIDTH));
  painter.setBrush(Qt::NoBrush);
  painter.drawRoundedRect(rect, radius, radius);

  // Inner shadow effect
  QRect innerRect = rect.adjusted(BORDER_WIDTH, BORDER_WIDTH, -BORDER_WIDTH, -BORDER_WIDTH);
  QColor shadowColor = borderColor.darker(200);
  shadowColor.setAlpha(100);
  painter.setPen(QPen(shadowColor, 1));
  painter.drawRoundedRect(innerRect, radius - 2, radius - 2);
}

// Helper function to add metallic highlight
static void addMetallicHighlight(QPainter &painter, QRect rect, int radius = CORNER_RADIUS) {
  QRect highlightRect = rect.adjusted(BORDER_WIDTH, BORDER_WIDTH, -BORDER_WIDTH, -rect.height()/2);
  QLinearGradient highlight(highlightRect.topLeft(), highlightRect.bottomLeft());
  highlight.setColorAt(0, QColor(255, 255, 255, 20));
  highlight.setColorAt(0.3, QColor(255, 255, 255, 5));
  highlight.setColorAt(1, QColor(255, 255, 255, 0));

  painter.setBrush(highlight);
  painter.setPen(Qt::NoPen);
  painter.drawRoundedRect(highlightRect, radius - 2, radius - 2);
}

#ifdef SUNNYPILOT
void BluepilotRenderer::renderAll(QPainter &painter, const QRect &rect, const UIState &s, const ModelRendererSP &model) {
  renderAllImpl(painter, rect, s, model);
}
#else
void BluepilotRenderer::renderAll(QPainter &painter, const QRect &rect, const UIState &s, const ModelRenderer &model) {
  renderAllImpl(painter, rect, s, model);
}
#endif

template<typename ModelType>
void BluepilotRenderer::renderAllImpl(QPainter &painter, const QRect &rect, const UIState &s, const ModelType &model) {
  // PERFORMANCE: Early exit if no BluePilot features enabled
  if (!s.scene.show_hybrid_drive_overlay &&
      !s.scene.show_new_radar_overlay &&
      !s.scene.show_stop_indicator_overlay &&
      !s.scene.stand_still_timer) {
    return;
  }

  // PERFORMANCE: Single state update per frame - batch all data gathering
  updateFrameState(s, model);

  // 1. BOTTOM LAYER: Blinkers and standstill timer
  renderBlinkers(painter, rect);
  renderStandstillTimer(painter, rect);

  // 2. MIDDLE LAYER: Model-dependent overlays (radar, stop signs)
  if (frame_state.show_radar || frame_state.show_stop) {
    renderModelEnhancements(painter, rect, s);
  }

  // 3. TOP LAYER: Hybrid gauges (always on top)
  renderHybridGauges(painter, rect, s);
}

template<typename ModelType>
void BluepilotRenderer::updateFrameState(const UIState &s, const ModelType &model) {
  const SubMaster &sm = *(s.sm);
  const auto car_state = sm["carState"].getCarState();

  // Update blinker state
  frame_state.left_blinker = car_state.getLeftBlinker();
  frame_state.right_blinker = car_state.getRightBlinker();
  frame_state.left_blindspot = car_state.getLeftBlindspot();
  frame_state.right_blindspot = car_state.getRightBlindspot();

  // Update standstill state
  frame_state.standstill = car_state.getStandstill();
  frame_state.vehicle_speed = car_state.getVEgo();

  // Update hybrid data if available
  if (sm.updated("carStateBP") && sm.valid("carStateBP")) {
    const auto car_state_bp = sm["carStateBP"].getCarStateBP();

    frame_state.hybrid_available = car_state_bp.getHybridDrive().getDataAvailable();
    if (frame_state.hybrid_available) {
      frame_state.throttle_demand = car_state_bp.getHybridDrive().getThrottleDemandPercent();
      frame_state.throttle_threshold = car_state_bp.getHybridDrive().getThrottleThresholdPercent();
      frame_state.power_mode = QString::fromStdString(car_state_bp.getHybridDrive().getPowerFlowMode());
      frame_state.engine_reason = QString::fromStdString(car_state_bp.getHybridDrive().getEngineOnReason());
    }

    frame_state.battery_available = car_state_bp.getHybridBattery().getDataAvailable();
    if (frame_state.battery_available) {
      frame_state.batt_soc_actual = car_state_bp.getHybridBattery().getSocActual();
      frame_state.batt_soc_min = car_state_bp.getHybridBattery().getSocMinPerc();
      frame_state.batt_soc_max = car_state_bp.getHybridBattery().getSocMaxPerc();
      frame_state.batt_volt_actual = car_state_bp.getHybridBattery().getVoltActual();
      frame_state.batt_volt_low = car_state_bp.getHybridBattery().getVoltLowLimit();
      frame_state.batt_volt_high = car_state_bp.getHybridBattery().getVoltHighLimit();
      frame_state.batt_amps_actual = car_state_bp.getHybridBattery().getAmpsActual();
    }
  }

  // Update model enhancement flags and transforms
  frame_state.show_radar = s.scene.show_new_radar_overlay;
  frame_state.show_stop = s.scene.show_stop_indicator_overlay;

  // FIXED: Properly get transform and clip region from model
  if (frame_state.show_radar || frame_state.show_stop) {
    frame_state.transform = model.getTransform();
    frame_state.clip_region = model.getClipRegion();

    // DEBUG: Add logging to verify transform is valid
    static int debug_counter = 0;
    if (frame_state.transform.isZero() && debug_counter++ % 100 == 0) {
      qDebug() << "WARNING: BluePilot transform is zero - overlays may not work";
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

void BluepilotRenderer::renderStandstillTimer(QPainter &painter, const QRect &rect) {
  if (!frame_state.standstill) return;

  double current_time = millis_since_boot() / 1000.0;

  // Enhanced standstill detection with multiple criteria
  bool velocity_standstill = frame_state.vehicle_speed < STANDSTILL_THRESHOLD;
  bool combined_standstill = frame_state.standstill && velocity_standstill;

  // Additional check: if speed is very low but CAN doesn't report standstill
  if (!frame_state.standstill && frame_state.vehicle_speed < 0.05f) {
    combined_standstill = true;
  }

  // FIXED: Update prev_standStill at the end of the function, not at the beginning
  if (!frame_state.prev_standstill && combined_standstill) {
    // Just entered standstill - start the timer
    frame_state.standstill_start_time = current_time;
    frame_state.standstill_exit_time = 0.0;
    frame_state.standstill_elapsed = 0.0;
  } else if (combined_standstill) {
    // Update the elapsed time while in standstill
    frame_state.standstill_elapsed = current_time - frame_state.standstill_start_time;
    frame_state.standstill_exit_time = 0.0;

    // Add a sanity check to prevent unreasonable values
    if (frame_state.standstill_elapsed > 86400.0) { // 24 hours max
      frame_state.standstill_start_time = current_time - 86400.0;
      frame_state.standstill_elapsed = 86400.0;
    }
  } else {
    // Not in standstill - use debounce mechanism
    if (frame_state.standstill_exit_time == 0.0) {
      frame_state.standstill_exit_time = current_time;
    }

    if (current_time - frame_state.standstill_exit_time > STANDSTILL_DEBOUNCE_TIME) {
      // Reset timer after debounce period
      frame_state.standstill_elapsed = 0.0;
      frame_state.standstill_start_time = current_time;
    }
  }

  // Draw stand still timer if active
  if (frame_state.standstill && frame_state.standstill_elapsed > 0.1 &&
      frame_state.vehicle_speed < STANDSTILL_THRESHOLD) {

    int minute = (int)(frame_state.standstill_elapsed / 60);
    int second = (int)(frame_state.standstill_elapsed) - (minute * 60);

    QString labelText = "STOP";
    QString timeText = QString("%1:%2").arg(minute).arg(second, 2, 10, QChar('0'));

    int x = rect.right() - 200;
    int y = rect.center().y() - 45;

    QRect backgroundRect(x - 120, y - 70, 240, 180);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 120));
    painter.drawRoundedRect(backgroundRect, 15, 15);

    painter.setFont(InterFont(80, QFont::DemiBold));
    drawColoredText(painter, x, y, labelText, QColor(255, 175, 3, 240));

    painter.setFont(InterFont(95, QFont::DemiBold));
    drawColoredText(painter, x, y + 90, timeText, QColor(255, 255, 255, 240));
  }

  // FIXED: Update prev_standstill at the end
  frame_state.prev_standstill = frame_state.standstill;
}

void BluepilotRenderer::renderHybridGauges(QPainter &painter, const QRect &rect, const UIState &s) {
  if (!s.scene.show_hybrid_drive_overlay || !frame_state.hybrid_available) {
    return;
  }

  int gauge_scale = s.scene.hybrid_drive_gauge_size;
  int gauge_width = rect.width() * 0.39;
  int gauge_height = 130;

  if (gauge_scale == 1) {
    gauge_width = rect.width() * 0.30;
    gauge_height = 100;
  } else if (gauge_scale == 2) {
    gauge_width = rect.width() * 0.345;
    gauge_height = 115;
  } else if (gauge_scale == 3) {
    gauge_width = rect.width() * 0.39;
    gauge_height = 130;
  } else {
    gauge_width = rect.width() * 0.30;
    gauge_height = 100;
  }

  int bottom_margin = 30;
  int y_position = rect.height() - gauge_height - bottom_margin;
  QRect gauge_rect((rect.width() - gauge_width) / 2, y_position, gauge_width, gauge_height);

  HybridDriveGauge::drawGauge(painter, gauge_rect, frame_state.throttle_demand, frame_state.throttle_threshold,
                             frame_state.power_mode, frame_state.engine_reason);

  if (s.scene.show_hybrid_battery_overlay && frame_state.battery_available) {
    int batt_width = gauge_width * 0.25;
    QRect battery_rect(gauge_rect.right() + 10, y_position, batt_width, gauge_height);

    HybridBatteryGauge::drawGauge(painter, battery_rect,
                                 frame_state.batt_soc_actual,
                                 frame_state.batt_soc_min,
                                 frame_state.batt_soc_max,
                                 frame_state.batt_volt_actual,
                                 frame_state.batt_volt_low,
                                 frame_state.batt_volt_high,
                                 frame_state.batt_amps_actual);
  }
}

void BluepilotRenderer::renderModelEnhancements(QPainter &painter, const QRect &rect, const UIState &s) {
  updateLeadTracking(s);
  updateStopDetection(s);

  if (frame_state.show_radar) {
    drawEnhancedLeads(painter, rect, s);
  }

  if (frame_state.show_stop) {
    drawStopSignDetection(painter, rect, s);
  }
}

void BluepilotRenderer::updateLeadTracking(const UIState &s) {
  const SubMaster &sm = *(s.sm);
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

      // Calculate curvature for gradual path influence
      float path_curvature = 0.0f;
      if (idx >= 3) {
        float y_diff_near = line_y[idx] - line_y[idx - 1];
        float y_diff_mid = line_y[idx - 1] - line_y[idx - 2];
        path_curvature = fabs(y_diff_near) + fabs(y_diff_mid);
      }

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
        // Allow more deviation from path during curves
        float curve_tolerance = 2.0f + (path_curvature * 2.0f);
        if (fabs(raw_yRel - path_y) > curve_tolerance) should_track = false;
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
          // FIXED: Use stable approach from old code for better curve handling
          // More gradual path influence for curves
          float path_weight = std::min(0.6f + path_curvature * 5.0f, 0.9f);

          // Adaptive alpha based on distance - smoother for close objects, less for distant ones
          float alpha = is_radar_assisted ?
                        0.05f + 0.15f * (d_rel / 25.0f) :    // Radar: 0.05 to 0.2
                        0.025f + 0.125f * (d_rel / 25.0f);   // Vision: 0.025 to 0.15

          // Clamp alpha to reasonable range
          alpha = std::clamp(alpha, 0.025f, 0.25f);

          // Distance-based jitter suppression
          float max_lateral_change = (d_rel < 8.0) ? 0.08f : 0.2f;
          float lateral_diff = raw_yRel - frame_state.lead_state.smoothed_yRel[i];
          if (fabs(lateral_diff) > max_lateral_change) {
            raw_yRel = frame_state.lead_state.smoothed_yRel[i] +
                      ((lateral_diff > 0) ? max_lateral_change : -max_lateral_change);
          }

          // Two-step smoothing: first smooth the raw radar reading
          float smoothed_raw = alpha * raw_yRel + (1.0f - alpha) * frame_state.lead_state.smoothed_yRel[i];

          // Then blend with the path position using dynamic path weight
          frame_state.lead_state.smoothed_yRel[i] = path_weight * path_y + (1.0f - path_weight) * smoothed_raw;
        }

        // FIXED: Use proper z offset calculation from model_old.cc
        float z_offset = 1.22f + (d_rel * 0.02f);
        QPointF current_pos;
        if (mapToScreen(d_rel, -frame_state.lead_state.smoothed_yRel[i], path_z + z_offset, &current_pos)) {
          bool reasonable_position = true;

          if (is_radar_assisted) {
            // Check if radar detection is reasonable
            QRectF screen_bounds = frame_state.clip_region;
            float margin = 100.0f;
            QRectF extended_bounds = screen_bounds.adjusted(-margin, -margin, margin, margin);

            if (!extended_bounds.contains(current_pos) || fabs(frame_state.lead_state.smoothed_yRel[i]) > 8.0f) {
              reasonable_position = false;
            }

            // Allow wider lateral positions during curves
            float curve_lateral_limit = 5.0f + (path_curvature * 2.0f);
            if (fabs(frame_state.lead_state.smoothed_yRel[i]) > curve_lateral_limit) {
              frame_state.lead_state.active_counter[i] = std::max(frame_state.lead_state.active_counter[i] - 1, 0);
              if (fabs(frame_state.lead_state.smoothed_yRel[i]) > curve_lateral_limit + 1.5f) {
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

  const auto &model = sm["modelV2"].getModelV2();
  const auto &radar_state = sm["radarState"].getRadarState();
  const auto &lead_one = radar_state.getLeadOne();
  const auto car_state = sm["carState"].getCarState();
  bool brake_pressed = car_state.getBrakePressed();
  float brake_value = car_state.getBrake();

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

        float z_offset = z + 2.5f;
        QPointF screen_point;
        if (mapToScreen(x, y, z_offset, &screen_point)) {
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

void BluepilotRenderer::drawEnhancedLeads(QPainter &painter, const QRect &rect, const UIState &s) {
  const SubMaster &sm = *(s.sm);
  const auto &radar_state = sm["radarState"].getRadarState();
  bool showRadarOverlay = !sm["selfdriveState"].getSelfdriveState().getExperimentalMode() && frame_state.show_radar;
  bool longitudinal_control = sm["carParams"].getCarParams().getOpenpilotLongitudinalControl();

  static int debug_counter = 0;
  if (debug_counter++ % 100 == 0) {
    qDebug() << "BluePilot radar - showRadarOverlay:" << showRadarOverlay
             << "longitudinal_control:" << longitudinal_control
             << "radarState alive:" << sm.alive("radarState")
             << "show_radar flag:" << frame_state.show_radar;
  }

  if (!(longitudinal_control || showRadarOverlay) || !sm.alive("radarState")) {
    return;
  }

  if (!icons_initialized) {
    initializeStaticData();
  }

  // Get radar overlay size scale
  int radar_scale = s.scene.radar_overlay_size;
  float scale_factor = 1.0f;

  if (radar_scale == 1) {
    scale_factor = 0.7f;  // Small
  } else if (radar_scale == 2) {
    scale_factor = 0.85f; // Medium
  } else if (radar_scale == 3) {
    scale_factor = 1.0f;  // Normal
  } else if (radar_scale == 4) {
    scale_factor = 1.15f; // Large
  } else {
    scale_factor = 1.0f;  // Default to normal
  }

  for (int i = 0; i < 2; ++i) {
    if (!frame_state.lead_state.virtual_active[i]) continue;

    const auto &lead_data = (i == 0) ? radar_state.getLeadOne() : radar_state.getLeadTwo();
    if (!lead_data.getStatus()) continue;

    if (i == 1 && frame_state.lead_state.virtual_active[0]) {
      const auto &lead_one = radar_state.getLeadOne();
      if (std::abs(lead_one.getDRel() - lead_data.getDRel()) <= 3.0) {
        continue;
      }
    }

    // Calculate confidence-based opacity
    float confidence_alpha = 1.0f;
    if (lead_data.getDRel() < 5.0f && !frame_state.lead_state.radar_assisted[i]) {
      confidence_alpha = std::min(0.4f + (frame_state.lead_state.active_counter[i] * 0.06f), 1.0f);
    }

    drawEnhancedLead(painter, lead_data, frame_state.lead_state.vertices[i], rect,
                    frame_state.lead_state.radar_assisted[i], confidence_alpha, scale_factor);
  }
}

void BluepilotRenderer::drawStopSignDetection(QPainter &painter, const QRect &rect, const UIState &s) {
  if (!frame_state.show_stop || frame_state.stop_state.fade_alpha < 0.02f) {
    return;
  }

  float v_ego = frame_state.vehicle_speed;
  if (v_ego < 0.5f) return;

  static int stop_debug_counter = 0;
  if (frame_state.stop_state.active && stop_debug_counter++ % 50 == 0) {
    qDebug() << "BluePilot stop sign - active:" << frame_state.stop_state.active
             << "fade_alpha:" << frame_state.stop_state.fade_alpha
             << "distance:" << frame_state.stop_state.display_distance
             << "show_stop flag:" << frame_state.show_stop;
  }

  if (frame_state.stop_state.active || frame_state.stop_state.fade_alpha > 0.0f) {
    drawStopSignOverlay(painter, frame_state.stop_state.last_valid_position,
                       static_cast<int>(frame_state.stop_state.smoothed_size),
                       frame_state.stop_state.display_distance, v_ego, frame_state.stop_state.fade_alpha);
  }
}

bool BluepilotRenderer::mapToScreen(float in_x, float in_y, float in_z, QPointF *out) {
  if (frame_state.transform.isZero()) {
    static int error_counter = 0;
    if (error_counter++ % 200 == 0) {
      qDebug() << "BluePilot: Transform is zero, cannot map to screen";
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
  for (int i = 1; i < line_x.size() && line_x[i] <= path_height; ++i) {
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

void BluepilotRenderer::drawStopSignOverlay(QPainter &painter, const QPointF &point, int size, float distance, float v_ego, float alpha) {
  if (alpha < 0.02f || distance <= 0.0f || size <= 0 || size > 500) return;

  // Skip if point is way off-screen
  QRectF screen_bounds = painter.clipBoundingRect();
  QRectF extended_bounds = screen_bounds.adjusted(-200, -200, 200, 200);
  if (!extended_bounds.contains(point) && distance > 15.0f) {
    return;
  }

  painter.setRenderHint(QPainter::Antialiasing, true);

  if (!octagon_initialized) {
    initOctagonTemplate();
  }

  // Update animation state
  if (!frame_state.stop_state.prev_stop_sign_visible) {
    frame_state.stop_state.stop_frame_count = 0;
  } else {
    frame_state.stop_state.stop_frame_count = std::min(frame_state.stop_state.stop_frame_count + 1, 20);
  }

  float stop_sign_opacity = std::min(1.0f, frame_state.stop_state.stop_frame_count / 10.0f);

  // Dynamic size with smoothing
  const float base_size = 120.0f;
  float distanceFactor = 1.0 - std::min(0.7f, (distance - 5.0f) / 45.0f);
  float target_size = base_size * distanceFactor;

  // Smooth size changes
  if (frame_state.stop_state.has_previous_position) {
    frame_state.stop_state.smoothed_size = frame_state.stop_state.smoothed_size * (1.0f - frame_state.stop_state.size_smoothing_factor) +
                                           target_size * frame_state.stop_state.size_smoothing_factor;
  } else {
    frame_state.stop_state.smoothed_size = target_size;
  }

  int dynamicSize = static_cast<int>(frame_state.stop_state.smoothed_size);

  // Calculate slide to corner
  float slideThreshold = 20.0f;
  float slideComplete = 10.0f;
  float slideAmount = 0.0f;

  if (distance < slideThreshold) {
    slideAmount = 1.0f - std::clamp((distance - slideComplete) / (slideThreshold - slideComplete), 0.0f, 1.0f);
  }

  // Calculate target position
  QPointF cornerPosition(painter.device()->width() - dynamicSize, painter.device()->height() - dynamicSize * 1.5);
  QPointF targetPosition;

  if (!frame_state.clip_region.contains(point)) {
    targetPosition = cornerPosition;
  } else {
    targetPosition.setX(point.x() * (1.0f - slideAmount) + cornerPosition.x() * slideAmount);
    targetPosition.setY(point.y() * (1.0f - slideAmount) + cornerPosition.y() * slideAmount);
  }

  // Apply position smoothing
  QPointF finalPosition;
  if (frame_state.stop_state.has_previous_position) {
    finalPosition.setX(frame_state.stop_state.smoothed_position.x() * (1.0f - frame_state.stop_state.position_smoothing_factor) +
                       targetPosition.x() * frame_state.stop_state.position_smoothing_factor);
    finalPosition.setY(frame_state.stop_state.smoothed_position.y() * (1.0f - frame_state.stop_state.position_smoothing_factor) +
                       targetPosition.y() * frame_state.stop_state.position_smoothing_factor);
  } else {
    finalPosition = targetPosition;
    frame_state.stop_state.has_previous_position = true;
  }

  frame_state.stop_state.smoothed_position = finalPosition;
  QPointF drawPoint = finalPosition;

  // Subtle pulsing effect
  float pulseRate = 0.3f + 0.5f * (1.0f - std::min(1.0f, distance / 50.0f));
  float pulsePhase = (static_cast<int>(millis_since_boot()) % 2000) / 2000.0f;
  float pulseOpacity = (0.85f + 0.15f * sin(pulsePhase * 2 * M_PI * pulseRate)) * stop_sign_opacity * alpha;

  // Draw octagon
  QPolygonF stopSign;
  const float max_radius = std::min(static_cast<float>(dynamicSize) / 2.0f, 250.0f);

  for (const QPointF &pt : octagon_template) {
    QPointF vertex(drawPoint.x() + max_radius * pt.x(), drawPoint.y() + max_radius * pt.y());
    if (std::isfinite(vertex.x()) && std::isfinite(vertex.y())) {
      stopSign << vertex;
    }
  }

  if (stopSign.size() == 8) {
    painter.setPen(QPen(Qt::white, 4));
    painter.setBrush(QColor(255, 0, 0, int(220 * pulseOpacity)));
    painter.drawPolygon(stopSign);

    // Draw "STOP" text
    painter.setPen(QColor(255, 255, 255, int(255 * pulseOpacity)));
    QFont stopFont = painter.font();
    stopFont.setPointSize(dynamicSize / 4);
    stopFont.setBold(true);
    painter.setFont(stopFont);

    QRect textRect(drawPoint.x() - dynamicSize/2, drawPoint.y() - dynamicSize/3, dynamicSize, dynamicSize * 2/3);
    painter.drawText(textRect, Qt::AlignCenter, "STOP");

    // Add distance countdown
    if (distance > 0) {
      QString distanceText = QString("%1 m").arg(distance, 0, 'f', 1);
      QFont distFont = painter.font();
      distFont.setPointSize(dynamicSize / 4.5);
      painter.setFont(distFont);
      painter.setPen(QPen(QColor(255, 255, 255, int(255 * pulseOpacity)), 1.5));

      QRect distRect(drawPoint.x() - dynamicSize * 0.75, drawPoint.y() + dynamicSize * 0.6,
                    dynamicSize * 1.5, dynamicSize / 3);
      painter.drawText(distRect, Qt::AlignCenter, distanceText);
    }

    // Draw time countdown arc
    if (v_ego > 0.1) {
      float raw_time_to_stop = distance / v_ego;
      int arcSize = dynamicSize + 20;

      float timeToStop;
      if (distance < 3.0f) {
        timeToStop = std::max(0.1f, distance * 0.5f);
      } else if (v_ego < 0.5f) {
        timeToStop = std::min(raw_time_to_stop, 10.0f);
      } else {
        timeToStop = std::clamp(raw_time_to_stop, 0.1f, 30.0f);
      }

      int startAngle = 90 * 16;
      int spanAngle = std::min(360, int(360 * (1.0 - std::min(1.0f, timeToStop / 10.0f)))) * 16;

      painter.setPen(QPen(QColor(255, 255, 255, int(255 * pulseOpacity)), 3));
      painter.drawArc(drawPoint.x() - arcSize / 2, drawPoint.y() - arcSize / 2, arcSize, arcSize, startAngle, spanAngle);

      // Time text
      QString timeText;
      if (timeToStop < 0.5f) {
        timeText = "STOP";
      } else if (timeToStop > 9.9f) {
        timeText = QString("%1 s").arg(qRound(timeToStop));
      } else {
        timeText = QString("%1 s").arg(timeToStop, 0, 'f', 1);
      }

      QFont timeFont = painter.font();
      timeFont.setPointSize(dynamicSize / 4.5);
      painter.setFont(timeFont);
      painter.setPen(QColor(255, 255, 255, int(255 * pulseOpacity)));
      QRect timeRect(drawPoint.x() - dynamicSize * 0.75, drawPoint.y() + dynamicSize * 0.9, dynamicSize * 1.5, dynamicSize / 3);
      painter.drawText(timeRect, Qt::AlignCenter, timeText);
    }
  }

  frame_state.stop_state.prev_stop_sign_visible = true;
}

void BluepilotRenderer::drawEnhancedLead(QPainter &painter, const cereal::RadarState::LeadData::Reader &lead_data,
                                        const QPointF &vd, const QRect &rect, bool radar_assisted, float alpha, float scale_factor) {
  const float d_rel = lead_data.getDRel();
  const float v_lead = lead_data.getVLead();

  // Calculate sizes based on distance for responsive design with scale factor
  float base_sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 3.525;
  float sz = base_sz * scale_factor;

  float x = std::clamp<float>(vd.x(), 0.f, rect.width() - sz / 2);
  float y = std::min<float>(vd.y(), rect.height() - sz * 0.6);

  painter.setRenderHint(QPainter::Antialiasing, true);

  // Create the chevron polygon with scaled size
  QPolygonF chevronPolygon;
  chevronPolygon << QPointF(x + (sz * 1.25), y + sz) << QPointF(x, y) << QPointF(x - (sz * 1.25), y + sz);

  // Get automotive colors based on radar assistance
  QColor baseChevronColor = radar_assisted ? QColor(60, 170, 255) : QColor(241, 196, 15);

  // Create automotive metallic gradient for chevron
  QRect chevronBounds = chevronPolygon.boundingRect().toRect();
  QLinearGradient chevronGradient = createAutomotiveGradient(chevronBounds, baseChevronColor);

  // Apply confidence alpha to gradient colors
  QGradientStops stops = chevronGradient.stops();
  for (auto &stop : stops) {
    QColor color = stop.second;
    color.setAlpha(int(color.alpha() * alpha));
    stop.second = color;
  }
  chevronGradient.setStops(stops);

  // Draw chevron with automotive gradient
  painter.setPen(Qt::NoPen);
  painter.setBrush(chevronGradient);
  painter.drawPolygon(chevronPolygon);

  // Add automotive border with inset effect (scaled border width)
  QColor borderColor = baseChevronColor.lighter(120);
  borderColor.setAlpha(int(220 * alpha));
  painter.setPen(QPen(borderColor, 2.5 * scale_factor));
  painter.setBrush(Qt::NoBrush);
  painter.drawPolygon(chevronPolygon);

  // Add subtle inner highlight for 3D effect
  QPolygonF innerChevron;
  float insetAmount = 3.0f * scale_factor;
  innerChevron << QPointF(x + (sz * 1.25) - insetAmount, y + sz - insetAmount)
               << QPointF(x, y + insetAmount)
               << QPointF(x - (sz * 1.25) + insetAmount, y + sz - insetAmount);

  QLinearGradient innerHighlight(QPointF(x, y), QPointF(x, y + sz));
  innerHighlight.setColorAt(0, QColor(255, 255, 255, int(30 * alpha)));
  innerHighlight.setColorAt(1, QColor(255, 255, 255, 0));

  painter.setBrush(innerHighlight);
  painter.setPen(Qt::NoPen);
  painter.drawPolygon(innerChevron);

  // Draw icon in the center of the chevron with scaled size
  float icon_size = sz * 0.8;
  float icon_center_y = y + sz * 0.6;
  QRectF iconRect(x - icon_size / 2, icon_center_y - icon_size / 2, icon_size, icon_size);

  QPixmap* icon = radar_assisted ? radar_icon : vision_icon;

  if (icon && !icon->isNull()) {
    // Apply opacity and render
    QPixmap translucent_icon = *icon;
    QPainter icon_painter(&translucent_icon);
    icon_painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    icon_painter.fillRect(translucent_icon.rect(), QColor(0, 0, 0, int(255 * alpha)));
    icon_painter.end();

    if (radar_assisted) {
      painter.save();
      painter.translate(iconRect.center());
      painter.rotate(90);
      painter.drawPixmap(QRectF(-iconRect.width() / 2, -iconRect.height() / 2,
                               iconRect.width(), iconRect.height()),
                        translucent_icon, translucent_icon.rect());
      painter.restore();
    } else {
      painter.drawPixmap(iconRect, translucent_icon, translucent_icon.rect());
    }
  }

  // ========== AUTOMOTIVE STYLING FOR INFO PANEL WITH SCALING ==========

  // Position info panel centered below the lead vehicle with scaled dimensions
  float panel_width = 300 * scale_factor;
  float panel_height = 60 * scale_factor;
  float panel_top = y + sz + (15 * scale_factor);

  QRectF infoPanel(x - panel_width / 2, panel_top, panel_width, panel_height);

  // Make sure the panel stays within the surface bounds
  if (panel_top + panel_height > rect.height()) {
    float available_height = rect.height() - panel_top - 5;
    if (available_height < 45 * scale_factor) {
      return;
    }
    infoPanel.setHeight(available_height);
  }

  // Horizontal bounds checking
  if (infoPanel.left() < 0) {
    infoPanel.moveLeft(0);
  } else if (infoPanel.right() > rect.width()) {
    infoPanel.moveRight(rect.width());
  }

  // Draw automotive-style metallic background
  painter.setPen(Qt::NoPen);
  QRadialGradient panelBg = createAutomotiveBackground(infoPanel.toRect());

  // Apply confidence alpha to background
  QGradientStops bgStops = panelBg.stops();
  for (auto &stop : bgStops) {
    QColor color = stop.second;
    color.setAlpha(int(color.alpha() * alpha));
    stop.second = color;
  }
  panelBg.setStops(bgStops);

  painter.setBrush(panelBg);
  painter.drawRoundedRect(infoPanel, CORNER_RADIUS * scale_factor, CORNER_RADIUS * scale_factor);

  // Add automotive border with scaled corner radius
  drawInsetBorder(painter, infoPanel.toRect(), baseChevronColor, CORNER_RADIUS * scale_factor);

  // Add metallic highlight to panel
  addMetallicHighlight(painter, infoPanel.toRect(), CORNER_RADIUS * scale_factor);

  // ========== AUTOMOTIVE TEXT STYLING WITH SCALING ==========

  // Convert measurements for display
  float distance_m = d_rel;
  float lead_speed_mph = v_lead * 2.237;
  QString distText = QString("%1 m").arg(qRound(distance_m));
  QString speedText = QString("%1 mph").arg(qRound(lead_speed_mph));
  QString combinedText = distText + "  |  " + speedText;

  // Set up text formatting with responsive font sizing and scale factor
  QFont infoFont("Inter", int(33 * scale_factor), QFont::DemiBold);
  painter.setFont(infoFont);

  // Scale font to fit container
  QRectF textRect = infoPanel.adjusted(7 * scale_factor, 7 * scale_factor, -7 * scale_factor, -7 * scale_factor);
  QFontMetrics fm(infoFont);
  int textWidth = fm.horizontalAdvance(combinedText);
  int textHeight = fm.height();
  int maxWidth = textRect.width();
  int maxHeight = textRect.height();

  int fontSize = int(33 * scale_factor);
  int iteration_limit = 8;
  while ((textWidth > maxWidth || textHeight > maxHeight) && fontSize > 8 && iteration_limit-- > 0) {
      fontSize--;
      infoFont.setPixelSize(fontSize);
      fm = QFontMetrics(infoFont);
      textWidth = fm.horizontalAdvance(combinedText);
      textHeight = fm.height();
  }

  painter.setFont(infoFont);

  // Text shadow with scaled offset
  painter.setPen(QColor(0, 0, 0, int(150 * alpha)));
  painter.drawText(textRect.adjusted(scale_factor, scale_factor, scale_factor, scale_factor), Qt::AlignCenter, combinedText);

  // Main text with automotive color
  painter.setPen(QColor(236, 240, 241, int(255 * alpha)));
  painter.drawText(textRect, Qt::AlignCenter, combinedText);
}

void BluepilotRenderer::initializeStaticData() {
  if (icons_initialized) return;

  if (!QApplication::instance()) {
    return;
  }

  if (!radar_icon) {
    radar_icon = new QPixmap();
    if (!radar_icon->load("../assets/img_radar.png")) {
      *radar_icon = createFallbackIcon("R", QColor(60, 170, 255));
    }
  }

  if (!vision_icon) {
    vision_icon = new QPixmap();
    if (!vision_icon->load("../assets/img_vision.png")) {
      *vision_icon = createFallbackIcon("V", QColor(255, 255, 0));
    }
  }

  icons_initialized = true;
}

void BluepilotRenderer::cleanup() {
  delete radar_icon;
  delete vision_icon;
  radar_icon = nullptr;
  vision_icon = nullptr;
  icons_initialized = false;
  octagon_initialized = false;
}

void BluepilotRenderer::initOctagonTemplate() {
  if (octagon_initialized) return;

  const float angle_increment = 2 * M_PI / 8;
  const float start_angle = angle_increment / 2;

  octagon_template.clear();
  for (int i = 0; i < 8; i++) {
    float angle = start_angle + i * angle_increment;
    octagon_template << QPointF(cos(angle), sin(angle));
  }
  octagon_initialized = true;
}

QPixmap BluepilotRenderer::createFallbackIcon(const QString &text, const QColor &color) {
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

// Explicit template instantiations to ensure proper compilation
template void BluepilotRenderer::renderAllImpl<ModelRenderer>(QPainter &painter, const QRect &rect, const UIState &s, const ModelRenderer &model);
template void BluepilotRenderer::updateFrameState<ModelRenderer>(const UIState &s, const ModelRenderer &model);

#ifdef SUNNYPILOT
template void BluepilotRenderer::renderAllImpl<ModelRendererSP>(QPainter &painter, const QRect &rect, const UIState &s, const ModelRendererSP &model);
template void BluepilotRenderer::updateFrameState<ModelRendererSP>(const UIState &s, const ModelRendererSP &model);
#endif