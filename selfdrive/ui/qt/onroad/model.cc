#include "selfdrive/ui/qt/onroad/model.h"
#include <iostream>

constexpr int CLIP_MARGIN = 500;
constexpr float MIN_DRAW_DISTANCE = 10.0;
constexpr float MAX_DRAW_DISTANCE = 100.0;

// Automotive styling constants
constexpr int CORNER_RADIUS = 6;
constexpr int BORDER_WIDTH = 3;

// PERFORMANCE: Static octagon template initialization
QPolygonF ModelRenderer::octagon_template;
bool ModelRenderer::octagon_template_initialized = false;

static int get_path_length_idx(const cereal::XYZTData::Reader &line, const float path_height) {
  const auto &line_x = line.getX();
  int max_idx = 0;
  for (int i = 1; i < line_x.size() && line_x[i] <= path_height; ++i) {
    max_idx = i;
  }
  return max_idx;
}

// Helper function to create automotive metallic gradient
QLinearGradient createAutomotiveGradient(QRect rect, QColor baseColor, bool isVertical = true) {
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
QRadialGradient createAutomotiveBackground(QRect rect) {
  QRadialGradient gradient(rect.center(), rect.width() * 0.7);
  gradient.setColorAt(0, QColor(44, 62, 80)); // Neutral center
  gradient.setColorAt(1, QColor(26, 37, 47)); // Dark edge
  return gradient;
}

// Helper function to draw inset border
void drawInsetBorder(QPainter &painter, QRect rect, QColor borderColor, int radius = CORNER_RADIUS) {
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
void addMetallicHighlight(QPainter &painter, QRect rect, int radius = CORNER_RADIUS) {
  QRect highlightRect = rect.adjusted(BORDER_WIDTH, BORDER_WIDTH, -BORDER_WIDTH, -rect.height()/2);
  QLinearGradient highlight(highlightRect.topLeft(), highlightRect.bottomLeft());
  highlight.setColorAt(0, QColor(255, 255, 255, 20));
  highlight.setColorAt(0.3, QColor(255, 255, 255, 5));
  highlight.setColorAt(1, QColor(255, 255, 255, 0));

  painter.setBrush(highlight);
  painter.setPen(Qt::NoPen);
  painter.drawRoundedRect(highlightRect, radius - 2, radius - 2);
}

void ModelRenderer::initOctagonTemplate() {
  if (octagon_template_initialized) return;

  const float angle_increment = 2 * M_PI / 8;
  const float start_angle = angle_increment / 2;

  octagon_template.clear();
  for (int i = 0; i < 8; i++) {
    float angle = start_angle + i * angle_increment;
    octagon_template << QPointF(cos(angle), sin(angle)); // Unit circle
  }
  octagon_template_initialized = true;
}

void ModelRenderer::draw(QPainter &painter, const QRect &surface_rect) {
  auto *s = uiState();
  auto &sm = *(s->sm);
  if (sm.rcv_frame("liveCalibration") < s->scene.started_frame || sm.rcv_frame("modelV2") < s->scene.started_frame) {
    return;
  }

  clip_region = surface_rect.adjusted(-CLIP_MARGIN, -CLIP_MARGIN, CLIP_MARGIN, CLIP_MARGIN);
  experimental_mode = sm["selfdriveState"].getSelfdriveState().getExperimentalMode();
  longitudinal_control = sm["carParams"].getCarParams().getOpenpilotLongitudinalControl();
  path_offset_z = sm["liveCalibration"].getLiveCalibration().getHeight()[0];

  painter.save();

  const auto &model = sm["modelV2"].getModelV2();
  const auto &radar_state = sm["radarState"].getRadarState();
  const auto &lead_one = radar_state.getLeadOne();
  const auto &car_state = sm["carState"].getCarState();
  float v_ego = car_state.getVEgo();
  bool brake_pressed = car_state.getBrakePressed();
  float brake_value = car_state.getBrake();

  updateBlindspotStatus(car_state);
  update_model(model, lead_one);
  drawLaneLines(painter);
  drawBlindspotIndicators(painter);
  drawPath(painter, model, surface_rect.height());

  // FIXED: Check if vehicle is actually stopped - hide stop sign when stopped
  bool vehicle_stopped = v_ego < 0.5f; // Vehicle is considered stopped below 0.5 m/s

  // If vehicle is stopped, immediately start fading out the stop sign
  if (vehicle_stopped && stop_state.active) {
    stop_state.active = false;
    stop_state.stability_counter = 0;
    // Keep fade_alpha as is to allow smooth fade out
  }

  // FIXED: Stop detection logic with bounds checking and performance limits
  const auto &velocity = model.getVelocity().getX();
  const auto &position_x = model.getPosition().getX();
  const auto &position_y = model.getPosition().getY();
  const auto &position_z = model.getPosition().getZ();

  // Declare variables in proper scope
  int stop_idx = -1;

  if (s->scene.show_stop_indicator_overlay && !vehicle_stopped) { // Only show when not stopped
    // Add comprehensive data validation
    size_t vel_size = velocity.size();
    size_t pos_x_size = position_x.size();
    size_t pos_y_size = position_y.size();
    size_t pos_z_size = position_z.size();

    // Validate array sizes are reasonable and consistent
    const size_t MAX_ARRAY_SIZE = 1000; // Prevent excessive iterations
    const size_t MIN_ARRAY_SIZE = 2;    // Need at least 2 points

    bool data_valid = (vel_size >= MIN_ARRAY_SIZE && vel_size <= MAX_ARRAY_SIZE &&
                       pos_x_size == vel_size && pos_y_size == vel_size && pos_z_size == vel_size);

    if (data_valid) {
      float stopping_distance = -1.0f;

      // Limit search to reasonable distance/time ahead
      size_t max_search_idx = std::min(vel_size, static_cast<size_t>(200)); // Limit iterations

      // Find potential stop point with bounds checking
      for (size_t i = 0; i < max_search_idx; ++i) {
        // Additional bounds check (defensive programming)
        if (i >= vel_size || i >= pos_x_size) {
          break;
        }

        // Skip if position data is unreasonable
        if (position_x[i] < 0 || position_x[i] > 200.0f) { // Max 200m ahead
          continue;
        }

        if (velocity[i] < 0.5f) {
          stopping_distance = position_x[i];
          stop_idx = static_cast<int>(i);
          break;
        }
      }

      // Add better bounds checking and logic for distance calculation
      if (stop_idx >= 0 && stop_idx < static_cast<int>(pos_x_size) && stopping_distance > 0) {
        // Cap maximum distance to a reasonable value
        stopping_distance = std::min(stopping_distance, 50.0f);

        // Calculate actual distance for display (subtract a vehicle length offset ~4.5m)
        // This is because position_x is measured from the front of the car to the stopping point
        float display_distance = std::max(0.1f, stopping_distance - 4.5f);

        // Use this display_distance for the actual UI display
        stop_state.display_distance = display_distance;
      } else {
        stop_state.display_distance = -1.0f;
      }

      // Use radar data for more accurate distance when lead is present
      if (lead_one.getStatus() && lead_one.getDRel() < stopping_distance + 5.0f) {
        // If radar detected lead is closer than model's stopping point (with small margin)
        // Use radar's distance as it's typically more precise
        float radar_distance = lead_one.getDRel();

        // Only use radar distance if it's reasonable (not too close or far)
        if (radar_distance > 3.0f && radar_distance < 50.0f) {
          stopping_distance = radar_distance;

          // Set stability counter high since radar detection is more reliable
          stop_state.stability_counter = std::max(stop_state.stability_counter, 10);

          // Set stop as active with radar-verified distance
          stop_state.active = true;
          stop_state.stopping_distance = stopping_distance;

          // Also check if we need to map to screen for position tracking
          if (stop_idx != -1 && stop_idx < static_cast<int>(pos_x_size)) {
            float x = position_x[stop_idx];
            float y = position_y[stop_idx];
            float z = position_z[stop_idx];

            QPointF screen_point;
            if (mapToScreen(x, y, z + path_offset_z, &screen_point)) {
              stop_state.last_valid_position = screen_point;
            }
          }
        }
      }

      // Use brake data to enhance stop confidence - but not when vehicle is stopped
      if (stop_idx != -1 && stop_idx < static_cast<int>(pos_x_size) && stopping_distance >= 5.0f && stopping_distance <= 50.0f) {
        // Increase stability when brakes are applied
        if (brake_pressed || brake_value > 0.1f) {
          // Braking confirms stop point - increase stability quickly
          stop_state.stability_counter = std::min(stop_state.stability_counter + 2, 20);
        } else {
          // No braking but velocity indicates stop - increase stability slowly
          stop_state.stability_counter = std::min(stop_state.stability_counter + 1, 20);
        }

        // Activate stop sign after sufficient stability
        if (stop_state.stability_counter >= 3) {
          stop_state.active = true;

          // Smooth update of stopping distance
          if (stop_state.stopping_distance > 0) {
            stop_state.stopping_distance = stop_state.stopping_distance * 0.8f + stopping_distance * 0.2f;
          } else {
            stop_state.stopping_distance = stopping_distance;
          }

          // Store position for position tracking
          float x = position_x[stop_idx];
          float y = position_y[stop_idx];
          float z = position_z[stop_idx];

          QPointF screen_point;
          if (mapToScreen(x, y, z + path_offset_z, &screen_point)) {
            stop_state.last_valid_position = screen_point;
          }
        }
      } else {
        // No stop point detected - reduce stability counter
        stop_state.stability_counter = std::max(0, stop_state.stability_counter - 1);

        // Special case: if braking but no stop point, keep sign visible longer
        if ((brake_pressed || brake_value > 0.1f) && stop_state.active) {
          // Braking without a detected stop point - slow down disappearance
          stop_state.stability_counter = std::max(stop_state.stability_counter, 5);
        }

        // Deactivate after stability drops too low
        if (stop_state.stability_counter <= 0) {
          stop_state.active = false;
        }
      }
    } else {
      // Data is invalid - reset stop state safely
      stop_state.active = false;
      stop_state.stability_counter = 0;
      stop_state.stopping_distance = -1.0f;
      stop_state.display_distance = -1.0f;
      stop_state.fade_alpha = std::max(0.0f, stop_state.fade_alpha - 0.1f);
    }
  } else if (vehicle_stopped) {
    // Vehicle is stopped - ensure stop sign is deactivated and fading out
    stop_state.active = false;
    stop_state.stability_counter = 0;
    stop_state.stopping_distance = -1.0f;
    stop_state.display_distance = -1.0f;
  }

  // Handle fade animation
  if (stop_state.active && stop_state.fade_alpha < 1.0f) {
    stop_state.fade_alpha = std::min(1.0f, stop_state.fade_alpha + 0.1f);
  } else if (!stop_state.active && stop_state.fade_alpha > 0.0f) {
    stop_state.fade_alpha = std::max(0.0f, stop_state.fade_alpha - 0.05f);
  }

  // Draw stop sign with fade effect if active or fading out, but never when vehicle is stopped
  if (stop_state.fade_alpha > 0.0f && stop_state.stopping_distance > 0 && !vehicle_stopped) {
    // Get position for stop sign (use last valid if current is invalid)
    QPointF screen_point;
    bool valid_position = false;

    if (stop_idx != -1 && stop_idx < static_cast<int>(position_x.size())) {
      float x = position_x[stop_idx];
      float y = position_y[stop_idx];
      float z = position_z[stop_idx];

      valid_position = mapToScreen(x, y, z + path_offset_z, &screen_point);
    }

    if (!valid_position && !stop_state.last_valid_position.isNull()) {
      // Use last valid position if current is invalid
      screen_point = stop_state.last_valid_position;
      valid_position = true;
    }

    if (valid_position) {
      const int stop_sign_size = 100; // Base size

      // Position relative to lane lines as in original code
      if (!lane_line_vertices[2].isEmpty()) {
        // Find the closest point on the right lane line to the stopping point
        int closest_idx = 0;
        float min_dist = std::numeric_limits<float>::max();
        for (int i = 0; i < lane_line_vertices[2].size(); ++i) {
          float dist = std::hypot(screen_point.x() - lane_line_vertices[2][i].x(), screen_point.y() - lane_line_vertices[2][i].y());
          if (dist < min_dist) {
            min_dist = dist;
            closest_idx = i;
          }
        }

        // Position the stop sign to the right of the closest lane line point
        QPointF lane_point = lane_line_vertices[2][closest_idx];
        QPointF stop_point(lane_point.x() + stop_sign_size * 0.75, lane_point.y());

        // Ensure the stop sign stays within the clip region
        if (clip_region.contains(stop_point)) {
          drawStopSignOverlay(painter, stop_point, stop_sign_size, stop_state.display_distance, v_ego, stop_state.fade_alpha);
        } else {
          // Adjust if partially out of bounds
          float adjusted_x = std::clamp(stop_point.x(), clip_region.left() + stop_sign_size / 2, clip_region.right() - stop_sign_size / 2);
          stop_point.setX(adjusted_x);
          if (clip_region.contains(stop_point)) {
            drawStopSignOverlay(painter, stop_point, stop_sign_size, stop_state.display_distance, v_ego, stop_state.fade_alpha);
          }
        }
      } else {
        // Fallback: Use the original stopping point if no lane line data
        if (clip_region.contains(screen_point)) {
          drawStopSignOverlay(painter, screen_point, stop_sign_size, stop_state.display_distance, v_ego, stop_state.fade_alpha);
        }
      }
    }
  }

  bool showRadarOverlay = !experimental_mode && s->scene.show_new_radar_overlay;
  if ((longitudinal_control || showRadarOverlay) && sm.alive("radarState")) {
    update_leads(radar_state, model.getPosition());

    // We already have lead_one defined above, so only define lead_two here
    const auto &lead_two = radar_state.getLeadTwo();

    // Check virtual lead status instead of direct status
    if (virtual_lead_active[0]) {
      drawLead(painter, lead_one, lead_vertices[0], surface_rect, lead_radar_assisted[0]);
    }

    // For the second lead, also check distance from the first
    if (virtual_lead_active[1] && (!virtual_lead_active[0] || std::abs(lead_one.getDRel() - lead_two.getDRel()) > 3.0)) {
      drawLead(painter, lead_two, lead_vertices[1], surface_rect, lead_radar_assisted[1]);
    }
  }

  painter.restore();
}

void ModelRenderer::update_leads(const cereal::RadarState::Reader &radar_state, const cereal::XYZTData::Reader &line) {
  for (int i = 0; i < 2; ++i) {
    const auto &lead_data = (i == 0) ? radar_state.getLeadOne() : radar_state.getLeadTwo();
    bool current_status = lead_data.getStatus();

    // Enhanced lead tracking with adaptive hysteresis
    if (current_status) {
      float d_rel = lead_data.getDRel();
      float raw_yRel = lead_data.getYRel();
      bool is_radar_assisted = lead_data.getRadar();

      // FIXED: Validate line data before access
      const auto &line_x = line.getX();
      const auto &line_y = line.getY();
      const auto &line_z = line.getZ();

      if (line_x.size() == 0 || line_y.size() != line_x.size() || line_z.size() != line_x.size()) {
        // Invalid data - skip this lead update
        virtual_lead_active[i] = false;
        continue;
      }

      // Get the path's y-coordinate at the lead's distance
      int idx = get_path_length_idx(line, d_rel);

      // FIXED: Ensure idx is within bounds
      if (idx < 0 || idx >= static_cast<int>(line_y.size()) || idx >= static_cast<int>(line_z.size())) {
        // Index out of bounds - use fallback or skip
        virtual_lead_active[i] = false;
        continue;
      }

      float path_y = line_y[idx];
      float path_z = line_z[idx];

      // FIXED: Stricter stability requirements for visual-only detections
      // Radar-assisted leads require less stability, visual-only require much more
      int required_stability = is_radar_assisted ? 2 : 8; // 8 frames for vision-only
      int max_stability = is_radar_assisted ? 10 : 15;

      // FIXED: More restrictive filtering for visual-only detections
      bool should_track = true;

      if (!is_radar_assisted) {
        // For visual-only detections, apply stricter criteria

        // 1. Reasonable distance range
        if (d_rel < 3.0f || d_rel > 80.0f) {
          should_track = false;
        }

        // 2. Consistent lateral position (not jumping around)
        if (prev_lead_status[i] && fabs(raw_yRel - smoothed_yRel[i]) > 0.5) {
          should_track = false;
        }

        // 3. Not too far from path center
        if (fabs(raw_yRel - path_y) > 2.0f) {
          should_track = false;
        }
      }

      // Update stability counter based on tracking decision
      if (should_track && prev_lead_status[i]) {
        lead_active_counter[i] = std::min(lead_active_counter[i] + 1, max_stability);
      } else if (should_track) {
        // First detection - start counter but don't activate yet
        lead_active_counter[i] = 1;
      } else {
        // Failed criteria - decrease counter
        lead_active_counter[i] = std::max(lead_active_counter[i] - 2, 0);
      }

      // FIXED: Only activate after meeting stability requirements
      if (lead_active_counter[i] >= required_stability && should_track) {
        stable_lead[i] = true;
        virtual_lead_active[i] = true;

        // For first detection, initialize with raw values
        if (!prev_lead_status[i]) {
          smoothed_yRel[i] = raw_yRel;
        } else {
          // Adaptive smoothing based on distance and radar assistance
          float path_curvature = (idx > 1) ? fabs(line_y[idx] - line_y[idx - 1]) : 0.0f;

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
          float lateral_diff = raw_yRel - smoothed_yRel[i];
          if (fabs(lateral_diff) > max_lateral_change) {
            // Limit lateral movement rate for stability
            raw_yRel = smoothed_yRel[i] + ((lateral_diff > 0) ? max_lateral_change : -max_lateral_change);
          }

          // First smooth the raw radar reading
          float smoothed_raw = alpha * raw_yRel + (1.0f - alpha) * smoothed_yRel[i];

          // Then blend with the path position using dynamic path weight
          smoothed_yRel[i] = path_weight * path_y + (1.0f - path_weight) * smoothed_raw;
        }

        QPointF current_pos;
        if (mapToScreen(d_rel, smoothed_yRel[i], path_z + path_offset_z, &current_pos)) {
          // FIXED: Check if radar detection is mapping to reasonable screen position
          bool reasonable_position = true;

          if (is_radar_assisted) {
            // For radar detections, check if the screen position is too extreme
            QRectF screen_bounds = clip_region;
            float margin = 100.0f; // Allow some margin beyond normal view
            QRectF extended_bounds = screen_bounds.adjusted(-margin, -margin, margin, margin);

            // If radar detection maps outside extended bounds, it's likely off-camera
            if (!extended_bounds.contains(current_pos)) {
              reasonable_position = false;

              // Additional check: if lateral offset is too extreme, it's definitely off-screen
              if (fabs(smoothed_yRel[i]) > 8.0f) { // More than 8m lateral offset
                reasonable_position = false;
              }
            }

            // For turning scenarios: if the lateral position suggests the object
            // is way off to the side, reduce tracking confidence
            if (fabs(smoothed_yRel[i]) > 5.0f) {
              // Reduce stability counter for extreme lateral positions
              lead_active_counter[i] = std::max(lead_active_counter[i] - 1, 0);

              // If object is very far laterally and we're in a turn,
              // it's likely an off-screen radar detection
              if (fabs(smoothed_yRel[i]) > 6.5f) {
                reasonable_position = false;
              }
            }
          }

          // Only update position if it's reasonable
          if (reasonable_position) {
            lead_vertices[i] = current_pos;
            lead_radar_assisted[i] = is_radar_assisted;
          } else {
            // Off-screen radar detection - reduce stability and don't show
            lead_active_counter[i] = std::max(lead_active_counter[i] - 2, 0);
            virtual_lead_active[i] = false;
          }
        } else {
          // mapToScreen failed - position is definitely off-screen
          virtual_lead_active[i] = false;
        }
      } else {
        // Not stable enough yet - don't activate
        virtual_lead_active[i] = false;
        stable_lead[i] = false;
      }
    } else {
      // FIXED: Improved decay logic to prevent rapid flickering
      if (lead_active_counter[i] > 0) {
        // Slower decay for radar-assisted leads, faster for vision-only
        int decay_rate = lead_radar_assisted[i] ? 1 : 2;
        lead_active_counter[i] = std::max(lead_active_counter[i] - decay_rate, 0);

        // FIXED: Add hysteresis - keep showing until counter drops significantly
        int deactivation_threshold = lead_radar_assisted[i] ? 1 : 3;
        virtual_lead_active[i] = lead_active_counter[i] >= deactivation_threshold;

        if (lead_active_counter[i] == 0) {
          stable_lead[i] = false;
        }
      } else {
        virtual_lead_active[i] = false;
        stable_lead[i] = false;
      }
    }

    prev_lead_status[i] = current_status;
  }
}

void ModelRenderer::update_model(const cereal::ModelDataV2::Reader &model, const cereal::RadarState::LeadData::Reader &lead) {
  const auto &model_position = model.getPosition();
  float max_distance = std::clamp(*(model_position.getX().end() - 1), MIN_DRAW_DISTANCE, MAX_DRAW_DISTANCE);

  // update lane lines
  const auto &lane_lines = model.getLaneLines();
  const auto &line_probs = model.getLaneLineProbs();
  int max_idx = get_path_length_idx(lane_lines[0], max_distance);
  for (int i = 0; i < std::size(lane_line_vertices); i++) {
    lane_line_probs[i] = line_probs[i];
    mapLineToPolygon(lane_lines[i], 0.025 * lane_line_probs[i], 0, &lane_line_vertices[i], max_idx);
  }

  // PERFORMANCE: Safe blindspot polygon creation with vertex limits
  if (lane_lines.size() >= 4) {
    const auto &left_lane = lane_lines[1];
    const auto &right_lane = lane_lines[2];

    // Validate lane data before processing
    if (left_lane.getX().size() == 0 || right_lane.getX().size() == 0) {
      lane_barrier_vertices[0].clear();
      lane_barrier_vertices[1].clear();
    } else {
      // Limit blindspot polygon complexity
      int safe_max_idx = std::min(max_idx, 50); // Limit to 50 points max
      const int MAX_BLINDSPOT_VERTICES = 40; // Vertex limit for performance

      // Left blind spot
      lane_barrier_vertices[0].clear();
      int left_vertex_count = 0;
      for (int i = 0; i <= safe_max_idx && i < static_cast<int>(left_lane.getX().size()) && left_vertex_count < MAX_BLINDSPOT_VERTICES; i++) {
        QPointF lane_pt, offset_pt;
        if (mapToScreen(left_lane.getX()[i], left_lane.getY()[i], left_lane.getZ()[i], &lane_pt) &&
            mapToScreen(left_lane.getX()[i], left_lane.getY()[i] - BLINDSPOT_WIDTH, left_lane.getZ()[i], &offset_pt)) {
          lane_barrier_vertices[0].append(offset_pt);
          left_vertex_count++;
        }
      }
      for (int i = safe_max_idx; i >= 0 && i < static_cast<int>(left_lane.getX().size()) && left_vertex_count < MAX_BLINDSPOT_VERTICES; i--) {
        QPointF lane_pt;
        if (mapToScreen(left_lane.getX()[i], left_lane.getY()[i], left_lane.getZ()[i], &lane_pt)) {
          lane_barrier_vertices[0].append(lane_pt);
          left_vertex_count++;
        }
      }

      // Right blind spot with same limits
      lane_barrier_vertices[1].clear();
      int right_vertex_count = 0;
      for (int i = 0; i <= safe_max_idx && i < static_cast<int>(right_lane.getX().size()) && right_vertex_count < MAX_BLINDSPOT_VERTICES; i++) {
        QPointF lane_pt;
        if (mapToScreen(right_lane.getX()[i], right_lane.getY()[i], right_lane.getZ()[i], &lane_pt)) {
          lane_barrier_vertices[1].append(lane_pt);
          right_vertex_count++;
        }
      }
      for (int i = safe_max_idx; i >= 0 && i < static_cast<int>(right_lane.getX().size()) && right_vertex_count < MAX_BLINDSPOT_VERTICES; i--) {
        QPointF offset_pt;
        if (mapToScreen(right_lane.getX()[i], right_lane.getY()[i] + BLINDSPOT_WIDTH, right_lane.getZ()[i], &offset_pt)) {
          lane_barrier_vertices[1].append(offset_pt);
          right_vertex_count++;
        }
      }
    }
  }

  // PERFORMANCE: Mark gradients dirty after polygon updates
  blindspot_gradients_dirty = true;

  // update road edges
  const auto &road_edges = model.getRoadEdges();
  const auto &edge_stds = model.getRoadEdgeStds();
  for (int i = 0; i < std::size(road_edge_vertices); i++) {
    road_edge_stds[i] = edge_stds[i];
    mapLineToPolygon(road_edges[i], 0.025, 0, &road_edge_vertices[i], max_idx);
  }

  // update path
  if (lead.getStatus()) {
    const float lead_d = lead.getDRel() * 2.;
    max_distance = std::clamp((float)(lead_d - fmin(lead_d * 0.35, 10.)), 0.0f, max_distance);
  }
  max_idx = get_path_length_idx(model_position, max_distance);

  // Store the current track vertices for smoothing
  QPolygonF current_track_vertices;
  mapLineToPolygon(model_position, 0.9, path_offset_z, &current_track_vertices, max_idx, false);

  // Apply smoothing if we have previous vertices
  if (!prev_track_vertices.isEmpty() && prev_track_vertices.size() == current_track_vertices.size()) {
    track_vertices.clear();
    for (int i = 0; i < current_track_vertices.size(); i++) {
      QPointF smoothed_point;
      smoothed_point.setX(prev_track_vertices[i].x() * (1.0 - path_smoothing_factor) + current_track_vertices[i].x() * path_smoothing_factor);
      smoothed_point.setY(prev_track_vertices[i].y() * (1.0 - path_smoothing_factor) + current_track_vertices[i].y() * path_smoothing_factor);
      track_vertices.append(smoothed_point);
    }
  } else {
    // If sizes don't match or it's the first frame, just use current vertices
    track_vertices = current_track_vertices;
  }

  // Store current vertices for next frame
  prev_track_vertices = current_track_vertices;
}

void ModelRenderer::drawLaneLines(QPainter &painter) {
  // Enable anti-aliasing for lane lines
  painter.setRenderHint(QPainter::Antialiasing, true);

  // FIXED: Safe lane line drawing with validation
  for (int i = 0; i < std::size(lane_line_vertices); ++i) {
    const QPolygonF &polygon = lane_line_vertices[i];

    // SAFETY: Skip degenerate or oversized polygons
    if (polygon.isEmpty() || polygon.size() > 400) {
      continue;
    }

    // Validate polygon bounds
    QRectF bounds = polygon.boundingRect();
    if (!bounds.isValid() || bounds.width() > 5000 || bounds.height() > 5000) {
      continue;
    }

    // Check if this line is on the left or right side
    bool is_left_line = (i == 0);
    bool is_right_line = (i == 3);

    // Change color based on blindspot detection
    if ((is_left_line && left_blindspot) || (is_right_line && right_blindspot)) {
      // Use red for blindspot detected
      painter.setBrush(QColor::fromRgbF(1.0, 0.0, 0.0, std::clamp<float>(lane_line_probs[i], 0.3, 0.9)));
    } else {
      // Use normal white for no blindspot
      painter.setBrush(QColor::fromRgbF(1.0, 1.0, 1.0, std::clamp<float>(lane_line_probs[i], 0.0, 0.7)));
    }

    painter.drawPolygon(polygon);
  }

  // FIXED: Safe road edge drawing
  for (int i = 0; i < std::size(road_edge_vertices); ++i) {
    const QPolygonF &polygon = road_edge_vertices[i];

    if (polygon.isEmpty() || polygon.size() > 400) {
      continue;
    }

    QRectF bounds = polygon.boundingRect();
    if (!bounds.isValid() || bounds.width() > 5000 || bounds.height() > 5000) {
      continue;
    }

    painter.setBrush(QColor::fromRgbF(1.0, 0, 0, std::clamp<float>(1.0 - road_edge_stds[i], 0.0, 1.0)));
    painter.drawPolygon(polygon);
  }
}

void ModelRenderer::drawSmoothPath(QPainter &painter) {
  // If not enough points or the path is empty, just return
  if (track_vertices.size() < 4) {
    painter.drawPolygon(track_vertices);
    return;
  }

  // Path should be drawn as two sides (left and right edge of the lane)
  // The track_vertices polygon is structured with right side going from bottom to top
  // followed by left side going from top to bottom
  int midPoint = track_vertices.size() / 2;

  // Create separate polygons for the right and left sides
  QVector<QPointF> rightSide;
  QVector<QPointF> leftSide;

  // Extract the right and left sides
  for (int i = 0; i < midPoint; i++) {
    rightSide.append(track_vertices[i]);
  }

  for (int i = midPoint; i < track_vertices.size(); i++) {
    leftSide.append(track_vertices[i]);
  }

  // Now create a path that preserves the lane shape
  QPainterPath smoothPath;

  // Start with the first point on the right side (bottom)
  if (!rightSide.isEmpty()) {
    smoothPath.moveTo(rightSide.first());

    // Draw right side (going upward)
    for (int i = 1; i < rightSide.size(); i++) {
      smoothPath.lineTo(rightSide[i]);
    }
  }

  // Connect to left side
  if (!leftSide.isEmpty()) {
    // If we have points on the right side, connect to the first point on the left side
    if (!rightSide.isEmpty()) {
      smoothPath.lineTo(leftSide.first());
    } else {
      smoothPath.moveTo(leftSide.first());
    }

    // Draw left side (going downward)
    for (int i = 1; i < leftSide.size(); i++) {
      smoothPath.lineTo(leftSide[i]);
    }
  }

  // Close the path by connecting back to the start
  if (!rightSide.isEmpty() && !leftSide.isEmpty()) {
    smoothPath.lineTo(rightSide.first());
  }

  // Draw the path
  painter.drawPath(smoothPath);
}

void ModelRenderer::drawPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, int height) {
  // Enable anti-aliasing for path
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Set higher quality composition mode
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

  QLinearGradient bg(0, height, 0, 0);
  auto *s = uiState();
  auto &sm = *(s->sm);

  float v_ego = sm["carState"].getCarState().getVEgo();

  // Get the custom path color parameter
  QString pathColor = QString::fromStdString(Params().get("CustomModelPathColor"));

  // Get the current time in seconds for dynamic effect (speed of rainbow movement)
  float time_offset = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0f;
  if (experimental_mode) {
    // The first half of track_vertices are the points for the right side of the path
    const auto &acceleration = model.getAcceleration().getX();
    const int max_len = std::min<int>(track_vertices.length() / 2, acceleration.size());

    for (int i = 0; i < max_len; ++i) {
      // Some points are out of frame
      int track_idx = max_len - i - 1; // flip idx to start from bottom right
      if (track_vertices[track_idx].y() < 0 || track_vertices[track_idx].y() > height)
        continue;

      // Flip so 0 is bottom of frame
      float lin_grad_point = (height - track_vertices[track_idx].y()) / height;

      // speed up: 120, slow down: 0
      float path_hue = fmax(fmin(60 + acceleration[i] * 35, 120), 0);
      // FIXME: painter.drawPolygon can be slow if hue is not rounded
      path_hue = int(path_hue * 100 + 0.5) / 100;

      float saturation = fmin(fabs(acceleration[i] * 1.5), 1);
      float lightness = util::map_val(saturation, 0.0f, 1.0f, 0.95f, 0.62f);       // lighter when grey
      float alpha = util::map_val(lin_grad_point, 0.75f / 2.f, 0.75f, 0.4f, 0.0f); // matches previous alpha fade
      bg.setColorAt(lin_grad_point, QColor::fromHslF(path_hue / 360., saturation, lightness, alpha));

      // Skip a point, unless next is last
      i += (i + 2) < max_len ? 1 : 0;
    }
  } else if (pathColor == "Rainbow") { // Rainbow Mode
    // Rainbow mode logic (existing code)
    const int max_len = track_vertices.length();
    bg.setSpread(QGradient::PadSpread);

    for (int i = 0; i < max_len; i += 2) {
      if (track_vertices[i].y() < 0 || track_vertices[i].y() > height)
        continue;

      float lin_grad_point = (height - track_vertices[i].y()) / height;

      // Use easing for smoother color transitions
      float eased_point = pow(lin_grad_point, 1.5f); // Ease-in effect

      // Dynamic hue with subtle, smooth animation
      float path_hue = fmod(eased_point * 360.0 + (v_ego * 20.0) + (time_offset * 100.0), 360.0);

      // Smooth alpha transition with longer fade
      float alpha = util::map_val(eased_point, 0.2f, 0.75f, 0.8f, 0.0f);

      // Use soft lightness for a premium feel
      bg.setColorAt(eased_point, QColor::fromHslF(path_hue / 360.0, 1.0f, 0.55f, alpha));
    }

  } else if (pathColor == "Blue") {
    // Blue gradient with RGB values
    bg.setColorAt(0.0, QColor(0, 102, 204, 102)); // 40% opacity
    bg.setColorAt(0.5, QColor(51, 153, 255, 89)); // 35% opacity
    bg.setColorAt(1.0, QColor(51, 153, 255, 0));  // 0% opacity
  } else if (pathColor == "Green") {
    // Green gradient
    bg.setColorAt(0.0, QColor(0, 204, 102, 102)); // 40% opacity
    bg.setColorAt(0.5, QColor(51, 255, 153, 89)); // 35% opacity
    bg.setColorAt(1.0, QColor(51, 255, 153, 0));  // 0% opacity
  } else if (pathColor == "Purple") {
    // Purple gradient
    bg.setColorAt(0.0, QColor(153, 51, 204, 102)); // 40% opacity
    bg.setColorAt(0.5, QColor(178, 102, 255, 89)); // 35% opacity
    bg.setColorAt(1.0, QColor(178, 102, 255, 0));  // 0% opacity
  } else if (pathColor == "Orange") {
    // Orange gradient
    bg.setColorAt(0.0, QColor(255, 128, 0, 102)); // 40% opacity
    bg.setColorAt(0.5, QColor(255, 153, 51, 89)); // 35% opacity
    bg.setColorAt(1.0, QColor(255, 153, 51, 0));  // 0% opacity
  } else if (pathColor == "Red") {
    // Red gradient
    bg.setColorAt(0.0, QColor(204, 0, 0, 102));  // 40% opacity
    bg.setColorAt(0.5, QColor(255, 51, 51, 89)); // 35% opacity
    bg.setColorAt(1.0, QColor(255, 51, 51, 0));  // 0% opacity
  } else if (pathColor == "Cyan") {
    // Cyan gradient
    bg.setColorAt(0.0, QColor(0, 204, 204, 102)); // 40% opacity
    bg.setColorAt(0.5, QColor(51, 255, 255, 89)); // 35% opacity
    bg.setColorAt(1.0, QColor(51, 255, 255, 0));  // 0% opacity
  } else if (pathColor == "Yellow") {
    // Yellow gradient
    bg.setColorAt(0.0, QColor(204, 204, 0, 102)); // 40% opacity
    bg.setColorAt(0.5, QColor(255, 255, 51, 89)); // 35% opacity
    bg.setColorAt(1.0, QColor(255, 255, 51, 0));  // 0% opacity
  } else {
    // Stock or default
    updatePathGradient(bg);
  }

  painter.setBrush(bg);

  // Use anti-aliased pen for smoother edges
  painter.setPen(Qt::NoPen);

  // Draw using the smoother path function
  drawSmoothPath(painter);
}

void ModelRenderer::updatePathGradient(QLinearGradient &bg) {
  static const QColor throttle_colors[] = {QColor::fromHslF(148. / 360., 0.94, 0.51, 0.4), QColor::fromHslF(112. / 360., 1.0, 0.68, 0.35),
                                           QColor::fromHslF(112. / 360., 1.0, 0.68, 0.0)};

  static const QColor no_throttle_colors[] = {
      QColor::fromHslF(148. / 360., 0.0, 0.95, 0.4),
      QColor::fromHslF(112. / 360., 0.0, 0.95, 0.35),
      QColor::fromHslF(112. / 360., 0.0, 0.95, 0.0),
  };

  // Transition speed; 0.1 corresponds to 0.5 seconds at UI_FREQ
  constexpr float transition_speed = 0.1f;

  // Start transition if throttle state changes
  bool allow_throttle = (*uiState()->sm)["longitudinalPlan"].getLongitudinalPlan().getAllowThrottle() || !longitudinal_control;
  if (allow_throttle != prev_allow_throttle) {
    prev_allow_throttle = allow_throttle;
    // Invert blend factor for a smooth transition when the state changes mid-animation
    blend_factor = std::max(1.0f - blend_factor, 0.0f);
  }

  const QColor *begin_colors = allow_throttle ? no_throttle_colors : throttle_colors;
  const QColor *end_colors = allow_throttle ? throttle_colors : no_throttle_colors;
  if (blend_factor < 1.0f) {
    blend_factor = std::min(blend_factor + transition_speed, 1.0f);
  }

  // Set gradient colors by blending the start and end colors
  bg.setColorAt(0.0f, blendColors(begin_colors[0], end_colors[0], blend_factor));
  bg.setColorAt(0.5f, blendColors(begin_colors[1], end_colors[1], blend_factor));
  bg.setColorAt(1.0f, blendColors(begin_colors[2], end_colors[2], blend_factor));
}

QColor ModelRenderer::blendColors(const QColor &start, const QColor &end, float t) {
  if (t == 1.0f)
    return end;
  return QColor::fromRgbF((1 - t) * start.redF() + t * end.redF(), (1 - t) * start.greenF() + t * end.greenF(), (1 - t) * start.blueF() + t * end.blueF(),
                          (1 - t) * start.alphaF() + t * end.alphaF());
}

void ModelRenderer::drawLead(QPainter &painter, const cereal::RadarState::LeadData::Reader &lead_data, const QPointF &vd, const QRect &surface_rect, bool isRadarAssisted) {
  const float d_rel = lead_data.getDRel();
  const float v_lead = lead_data.getVLead();

  // Calculate confidence-based opacity
  float confidence_alpha = 1.0;

  // Lower confidence for close, non-radar detections
  if (d_rel < distance_confidence_threshold && !isRadarAssisted) {
    // Find the lead index
    int lead_idx = -1;
    for (int i = 0; i < 2; i++) {
      if (lead_vertices[i] == vd) {
        lead_idx = i;
        break;
      }
    }

    if (lead_idx >= 0) {
      // Scale opacity based on stability
      confidence_alpha = std::min(0.4f + (lead_active_counter[lead_idx] * 0.06f), 1.0f);
    } else {
      confidence_alpha = 0.7; // Default fallback
    }
  }

  // Calculate sizes based on distance for responsive design
  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 3.525;
  float x = std::clamp<float>(vd.x(), 0.f, surface_rect.width() - sz / 2);
  float y = std::min<float>(vd.y(), surface_rect.height() - sz * 0.6);

  // Convert measurements for display
  float distance_m = d_rel;
  float lead_speed_mph = v_lead * 2.237;

  // Enable anti-aliasing for smoother lead indicator
  painter.setRenderHint(QPainter::Antialiasing, true);

  // ========== AUTOMOTIVE STYLING FOR CHEVRON ==========

  // Create the chevron polygon centered on the position
  QPolygonF chevronPolygon;
  chevronPolygon << QPointF(x + (sz * 1.25), y + sz) << QPointF(x, y) << QPointF(x - (sz * 1.25), y + sz);

  // Get automotive colors based on radar assistance
  QColor baseChevronColor;
  if (isRadarAssisted) {
    baseChevronColor = QColor(60, 170, 255); // Blue for radar
  } else {
    baseChevronColor = QColor(241, 196, 15); // Amber for vision
  }

  // Create automotive metallic gradient for chevron
  QRect chevronBounds = chevronPolygon.boundingRect().toRect();
  QLinearGradient chevronGradient = createAutomotiveGradient(chevronBounds, baseChevronColor);

  // Apply confidence alpha to gradient colors
  QGradientStops stops = chevronGradient.stops();
  for (auto &stop : stops) {
    QColor color = stop.second;
    color.setAlpha(int(color.alpha() * confidence_alpha));
    stop.second = color;
  }
  chevronGradient.setStops(stops);

  // Draw chevron with automotive gradient
  painter.setPen(Qt::NoPen);
  painter.setBrush(chevronGradient);
  painter.drawPolygon(chevronPolygon);

  // Add automotive border with inset effect
  QColor borderColor = baseChevronColor.lighter(120);
  borderColor.setAlpha(int(220 * confidence_alpha));
  painter.setPen(QPen(borderColor, 2.5));
  painter.setBrush(Qt::NoBrush);
  painter.drawPolygon(chevronPolygon);

  // Add subtle inner highlight for 3D effect
  QPolygonF innerChevron;
  float insetAmount = 3.0f;
  innerChevron << QPointF(x + (sz * 1.25) - insetAmount, y + sz - insetAmount)
               << QPointF(x, y + insetAmount)
               << QPointF(x - (sz * 1.25) + insetAmount, y + sz - insetAmount);

  QLinearGradient innerHighlight(QPointF(x, y), QPointF(x, y + sz));
  innerHighlight.setColorAt(0, QColor(255, 255, 255, int(30 * confidence_alpha)));
  innerHighlight.setColorAt(1, QColor(255, 255, 255, 0));

  painter.setBrush(innerHighlight);
  painter.setPen(Qt::NoPen);
  painter.drawPolygon(innerChevron);

  // Draw icon in the center of the chevron
  float icon_size = sz * 0.8;
  float icon_center_y = y + sz * 0.6;
  QRectF iconRect(x - icon_size / 2, icon_center_y - icon_size / 2, icon_size, icon_size);

  // FIXED: Simply use cached icons
  QPixmap icon = isRadarAssisted ? radar_icon : vision_icon;

  if (!icon.isNull()) {
    // Apply opacity and render
    QPixmap translucent_icon = icon;
    QPainter icon_painter(&translucent_icon);
    icon_painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    icon_painter.fillRect(translucent_icon.rect(), QColor(0, 0, 0, int(255 * confidence_alpha)));
    icon_painter.end();

    if (isRadarAssisted) {
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

  // ========== AUTOMOTIVE STYLING FOR INFO PANEL ==========

  // Position info panel centered below the lead vehicle
  float panel_width = 300;
  float panel_height = 60;
  float panel_top = y + sz + 15;

  // Center panel horizontally with the chevron
  QRectF infoPanel(x - panel_width / 2, panel_top, panel_width, panel_height);

  // Make sure the panel stays within the surface bounds
  if (panel_top + panel_height > surface_rect.height()) {
    float available_height = surface_rect.height() - panel_top - 5;
    if (available_height < 45) {
      return;
    }
    infoPanel.setHeight(available_height);
  }

  // Horizontal bounds checking
  if (infoPanel.left() < 0) {
    infoPanel.moveLeft(0);
  } else if (infoPanel.right() > surface_rect.width()) {
    infoPanel.moveRight(surface_rect.width());
  }

  // Draw automotive-style metallic background
  painter.setPen(Qt::NoPen);
  QRadialGradient panelBg = createAutomotiveBackground(infoPanel.toRect());

  // Apply confidence alpha to background
  QGradientStops bgStops = panelBg.stops();
  for (auto &stop : bgStops) {
    QColor color = stop.second;
    color.setAlpha(int(color.alpha() * confidence_alpha));
    stop.second = color;
  }
  panelBg.setStops(bgStops);

  painter.setBrush(panelBg);
  painter.drawRoundedRect(infoPanel, CORNER_RADIUS, CORNER_RADIUS);

  // Add automotive border
  drawInsetBorder(painter, infoPanel.toRect(), baseChevronColor);

  // Add metallic highlight to panel
  addMetallicHighlight(painter, infoPanel.toRect());

  // ========== AUTOMOTIVE TEXT STYLING ==========

 // Set up text formatting with responsive font sizing
  QFont infoFont("Inter", 33, QFont::DemiBold);
  painter.setFont(infoFont);

  // Format distance and speed text
  QString distText = QString("%1 m").arg(qRound(distance_m));
  QString speedText = QString("%1 mph").arg(qRound(lead_speed_mph));
  QString combinedText = distText + "  |  " + speedText;

  // Scale font to fit container
  QRectF textRect = infoPanel.adjusted(7, 7, -7, -7);
  QFontMetrics fm(infoFont);
  int textWidth = fm.horizontalAdvance(combinedText);
  int textHeight = fm.height();
  int maxWidth = textRect.width();
  int maxHeight = textRect.height();

  int fontSize = 33;
  int iteration_limit = 8; // Prevent runaway loops
  while ((textWidth > maxWidth || textHeight > maxHeight) && fontSize > 8 && iteration_limit-- > 0) {
      fontSize--;
      infoFont.setPixelSize(fontSize);
      fm = QFontMetrics(infoFont);
      textWidth = fm.horizontalAdvance(combinedText);
      textHeight = fm.height();
  }

  painter.setFont(infoFont);

  // Text shadow
  painter.setPen(QColor(0, 0, 0, int(150 * confidence_alpha)));
  painter.drawText(textRect.adjusted(1, 1, 1, 1), Qt::AlignCenter, combinedText);

  // Main text with automotive color
  painter.setPen(QColor(236, 240, 241, int(255 * confidence_alpha)));
  painter.drawText(textRect, Qt::AlignCenter, combinedText);
}

void ModelRenderer::mapLineToPolygon(const cereal::XYZTData::Reader &line, float y_off, float z_off,
                                    QPolygonF *pvd, int max_idx, bool allow_invert) {
  const auto line_x = line.getX(), line_y = line.getY(), line_z = line.getZ();

  // FIXED: Limit polygon complexity to prevent GPU hangs
  const int MAX_VERTICES = 200;  // Reasonable limit for UI polygons

  QPointF left, right;
  pvd->clear();

  // Ensure max_idx is reasonable
  max_idx = std::min(max_idx, static_cast<int>(line_x.size()) - 1);
  max_idx = std::max(0, max_idx);

  int vertex_count = 0;
  int skip_factor = 1;

  // Calculate skip factor if we'd exceed vertex limit
  if (max_idx * 2 > MAX_VERTICES) {
    skip_factor = (max_idx * 2) / MAX_VERTICES + 1;
  }

  for (int i = 0; i <= max_idx; i += skip_factor) {
    // Safety bounds check
    if (i >= static_cast<int>(line_x.size()) ||
        i >= static_cast<int>(line_y.size()) ||
        i >= static_cast<int>(line_z.size())) {
      break;
    }

    // Skip highly negative x positions that cause flickering
    if (line_x[i] < 0) continue;

    bool l = mapToScreen(line_x[i], line_y[i] - y_off, line_z[i] + z_off, &left);
    bool r = mapToScreen(line_x[i], line_y[i] + y_off, line_z[i] + z_off, &right);

    if (l && r) {
      // Inversion check for wide lines
      if (!allow_invert && pvd->size() && left.y() > pvd->back().y()) {
        continue;
      }

      pvd->push_back(left);
      pvd->push_front(right);
      vertex_count += 2;

      // Emergency brake if polygon gets too complex
      if (vertex_count >= MAX_VERTICES) {
        break;
      }
    }
  }
}

void ModelRenderer::updateBlindspotStatus(const cereal::CarState::Reader &car_state) {
  left_blindspot = car_state.getLeftBlindspot();
  right_blindspot = car_state.getRightBlindspot();

  // Update animation counter
  updateBlindspotAnimation();
}

void ModelRenderer::updateBlindspotAnimation() {
  // Increment blink counter
  blindspot_blink_rate = (blindspot_blink_rate + 1) % (UI_FREQ * 2);

  // Calculate pulsing opacity between 0.15 and 0.35
  float pulse = 0.1 * sin(blindspot_blink_rate * M_PI / UI_FREQ) + 0.25;
  blindspot_opacity = pulse;
}

void ModelRenderer::drawBlindspotIndicators(QPainter &painter) {
  auto *s = uiState();
  if (!s->scene.show_blindspot_indicators) {
    return; // Exit early if blind spot indicators are disabled
  }

  painter.setRenderHint(QPainter::Antialiasing, true);

  // PERFORMANCE: Update cached gradients only when bounds change
  if (blindspot_gradients_dirty) {
    if (left_blindspot && !lane_barrier_vertices[0].isEmpty()) {
      QRectF leftBounds = lane_barrier_vertices[0].boundingRect();
      if (leftBounds != last_left_bounds) {
        cached_blindspot_gradient_left = QLinearGradient(leftBounds.center().x(), leftBounds.top(),
                                                        leftBounds.center().x(), leftBounds.bottom());
        cached_blindspot_gradient_left.setColorAt(0.0, QColor::fromRgbF(1.0, 0.0, 0.0, 0.0));
        cached_blindspot_gradient_left.setColorAt(0.2, QColor::fromRgbF(1.0, 0.0, 0.0, 0.2));
        cached_blindspot_gradient_left.setColorAt(0.4, QColor::fromRgbF(1.0, 0.0, 0.0, blindspot_opacity));
        cached_blindspot_gradient_left.setColorAt(0.7, QColor::fromRgbF(1.0, 0.0, 0.0, 0.9));
        cached_blindspot_gradient_left.setColorAt(1.0, QColor::fromRgbF(1.0, 0.0, 0.0, 1.0));
        last_left_bounds = leftBounds;
      }
    }

    if (right_blindspot && !lane_barrier_vertices[1].isEmpty()) {
      QRectF rightBounds = lane_barrier_vertices[1].boundingRect();
      if (rightBounds != last_right_bounds) {
        cached_blindspot_gradient_right = QLinearGradient(rightBounds.center().x(), rightBounds.top(),
                                                         rightBounds.center().x(), rightBounds.bottom());
        cached_blindspot_gradient_right.setColorAt(0.0, QColor::fromRgbF(1.0, 0.0, 0.0, 0.0));
        cached_blindspot_gradient_right.setColorAt(0.2, QColor::fromRgbF(1.0, 0.0, 0.0, 0.2));
        cached_blindspot_gradient_right.setColorAt(0.4, QColor::fromRgbF(1.0, 0.0, 0.0, blindspot_opacity));
        cached_blindspot_gradient_right.setColorAt(0.7, QColor::fromRgbF(1.0, 0.0, 0.0, 0.9));
        cached_blindspot_gradient_right.setColorAt(1.0, QColor::fromRgbF(1.0, 0.0, 0.0, 1.0));
        last_right_bounds = rightBounds;
      }
    }

    blindspot_gradients_dirty = false;
  }

  // Draw using cached gradients
  if (left_blindspot && !lane_barrier_vertices[0].isEmpty()) {
    painter.setBrush(cached_blindspot_gradient_left);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(lane_barrier_vertices[0]);
  }

  if (right_blindspot && !lane_barrier_vertices[1].isEmpty()) {
    painter.setBrush(cached_blindspot_gradient_right);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(lane_barrier_vertices[1]);
  }
}

void ModelRenderer::drawStopSignOverlay(QPainter &painter, const QPointF &point, int size,
                                       float stopping_distance, float v_ego, float fade_alpha) {
  // PERFORMANCE: Early exits for performance
  if (fade_alpha < 0.02f) return; // Skip if barely visible
  if (stopping_distance <= 0.0f || size <= 0 || size > 500) return;

  // Skip if point is way off-screen and not sliding to corner
  QRectF screen_bounds = painter.clipBoundingRect();
  QRectF extended_bounds = screen_bounds.adjusted(-200, -200, 200, 200);
  if (!extended_bounds.contains(point) && stopping_distance > 15.0f) {
    return; // Far from screen and not close enough to slide
  }

  painter.setRenderHint(QPainter::Antialiasing, true);

  // Track stop sign visibility for animations
  bool stop_sign_visible = true;

  // Update frame counter for animation
  if (prev_stop_sign_visible) {
    stop_frame_count = std::min(stop_frame_count + 1, 20); // Max 20 frames for full opacity
  } else {
    stop_frame_count = 0; // Reset counter when first appears
  }

  // Calculate fade-in opacity
  stop_sign_opacity = std::min(1.0f, stop_frame_count / 10.0f); // Fade in over 10 frames

  // Base size for the stop sign
  const float base_size = 120.0f;

  // Dynamic size based on distance with smoothing
  float distanceFactor = 1.0 - std::min(0.7f, (stopping_distance - 5.0f) / 45.0f);
  float target_size = base_size * distanceFactor;

  // Smooth size changes
  if (stop_state.has_previous_position) {
    stop_state.smoothed_size = stop_state.smoothed_size * (1.0f - stop_state.size_smoothing_factor) +
                               target_size * stop_state.size_smoothing_factor;
  } else {
    stop_state.smoothed_size = target_size;
  }

  int dynamicSize = static_cast<int>(stop_state.smoothed_size);

  // Slide to corner as we get closer (start sliding at 20m, complete at 10m)
  float slideThreshold = 20.0f;
  float slideComplete = 10.0f;
  float slideAmount = 0.0f;

  if (stopping_distance < slideThreshold) {
    // Calculate slide factor (0.0 = original position, 1.0 = corner position)
    slideAmount = 1.0f - std::clamp((stopping_distance - slideComplete) / (slideThreshold - slideComplete), 0.0f, 1.0f);
  }

  // Calculate target position by interpolating between original and corner position
  QPointF cornerPosition(painter.device()->width() - dynamicSize, painter.device()->height() - dynamicSize * 1.5);
  QPointF targetPosition;

  // Robust fallback positioning if point is outside the clip region
  if (!clip_region.contains(point)) {
    // Use a default position in the bottom right if original point is invalid
    targetPosition = cornerPosition;
  } else {
    // Interpolate between original and corner position
    targetPosition.setX(point.x() * (1.0f - slideAmount) + cornerPosition.x() * slideAmount);
    targetPosition.setY(point.y() * (1.0f - slideAmount) + cornerPosition.y() * slideAmount);
  }

  // Apply position smoothing
  QPointF finalPosition;
  if (stop_state.has_previous_position) {
    // Smooth transition between positions
    finalPosition.setX(stop_state.smoothed_position.x() * (1.0f - stop_state.position_smoothing_factor) +
                       targetPosition.x() * stop_state.position_smoothing_factor);
    finalPosition.setY(stop_state.smoothed_position.y() * (1.0f - stop_state.position_smoothing_factor) +
                       targetPosition.y() * stop_state.position_smoothing_factor);
  } else {
    // First frame - use target position directly
    finalPosition = targetPosition;
    stop_state.has_previous_position = true;
  }

  // Store smoothed position for next frame
  stop_state.smoothed_position = finalPosition;

  // Use smoothed final position for all drawing operations
  QPointF drawPoint = finalPosition;

  // Reduced pulsing effect - much more subtle
  float pulseRate = 0.3f + 0.5f * (1.0f - std::min(1.0f, stopping_distance / 50.0f)); // Capped at 0.8 max (was 1.5)
  float pulsePhase = (static_cast<int>(millis_since_boot()) % 2000) / 2000.0f; // Slower pulse (2 seconds instead of 1)

  // Much more subtle pulse - opacity varies between 0.85 and 1.0 instead of 0.7 to 1.0
  float pulseOpacity = (0.85f + 0.15f * sin(pulsePhase * 2 * M_PI * pulseRate)) * stop_sign_opacity * fade_alpha;

  // PERFORMANCE: Use precomputed octagon template
  initOctagonTemplate();

  QPolygonF stopSign;
  const float max_radius = std::min(static_cast<float>(dynamicSize) / 2.0f, 250.0f);

  for (const QPointF &pt : octagon_template) {
    QPointF vertex(drawPoint.x() + max_radius * pt.x(),
                   drawPoint.y() + max_radius * pt.y());

    if (std::isfinite(vertex.x()) && std::isfinite(vertex.y())) {
      stopSign << vertex;
    }
  }

  // Only draw if we have a valid octagon
  if (stopSign.size() == 8) {
    painter.setPen(QPen(Qt::white, 4));
    painter.setBrush(QColor(255, 0, 0, int(220 * pulseOpacity)));
    painter.drawPolygon(stopSign);

    // Draw "STOP" text centered in the middle of the sign
    painter.setPen(Qt::white);
    QFont stopFont = painter.font();
    stopFont.setPointSize(dynamicSize / 4); // Adjusted text size ratio
    stopFont.setBold(true);
    painter.setFont(stopFont);

    // Create a rectangular area for the text that's centered on the stop sign
    QRect textRect(drawPoint.x() - dynamicSize / 2, drawPoint.y() - dynamicSize / 3, dynamicSize, dynamicSize * 2 / 3);

    // Draw the text centered in this rectangle with consistent opacity
    painter.setPen(QColor(255, 255, 255, int(255 * pulseOpacity)));
    painter.drawText(textRect, Qt::AlignCenter, "STOP");

    // Add distance countdown below the stop sign
    if (stopping_distance > 0) {
      // Format distance with proper precision
      QString distanceText = QString("%1 m").arg(stopping_distance, 0, 'f', 1);

      // Use a more reasonable font size - reduced from /3 to /4.5
      QFont distFont = painter.font();
      distFont.setPointSize(dynamicSize / 4.5);
      painter.setFont(distFont);
      painter.setPen(QPen(QColor(255, 255, 255, int(255 * pulseOpacity)), 1.5));

      // Create a wider rectangle to prevent text cutoff
      // Moved down slightly more to avoid overlap with sign
      QRect distRect(drawPoint.x() - dynamicSize * 0.75, drawPoint.y() + dynamicSize * 0.6,
                     dynamicSize * 1.5, // Wider rectangle (1.5x instead of 1x)
                     dynamicSize / 3);

      painter.drawText(distRect, Qt::AlignCenter, distanceText);
    }

    // Draw time countdown circular indicator around stop sign with consistent opacity
    if (v_ego > 0.1) {
      // Calculate time to stop, but limit to reasonable values
      float raw_time_to_stop = stopping_distance / v_ego;
      int arcSize = dynamicSize + 20; // Arc size is 20px larger than the stop sign

      // As we get very close, time calculation becomes unstable, so clamp it
      float timeToStop;

      if (stopping_distance < 3.0f) {
        // When very close, use a fixed small value
        timeToStop = std::max(0.1f, stopping_distance * 0.5f);
      } else if (v_ego < 0.5f) {
        // At very low speeds, cap the maximum time to avoid unreasonable values
        timeToStop = std::min(raw_time_to_stop, 10.0f);
      } else {
        // Normal case - apply reasonable limits
        timeToStop = std::clamp(raw_time_to_stop, 0.1f, 30.0f);
      }

      // Use decreasing arc length as we approach (making the arc grow clockwise)
      int startAngle = 90 * 16; // Start at top (QPainter uses 1/16th of degrees)
      int spanAngle = std::min(360, int(360 * (1.0 - std::min(1.0f, timeToStop / 10.0f)))) * 16;

      // Draw arc with consistent opacity
      painter.setPen(QPen(QColor(255, 255, 255, int(255 * pulseOpacity)), 3));
      painter.drawArc(drawPoint.x() - arcSize / 2, drawPoint.y() - arcSize / 2, arcSize, arcSize, startAngle, spanAngle);

      // Add time text with improved formatting
      QString timeText;
      if (timeToStop < 0.5f) {
        // When time is very short, just show "STOP"
        timeText = "STOP";
      } else if (timeToStop > 9.9f) {
        // For long times, just show integer
        timeText = QString("%1 s").arg(qRound(timeToStop));
      } else {
        // Normal case - one decimal place
        timeText = QString("%1 s").arg(timeToStop, 0, 'f', 1);
      }

      // Draw time text with consistent opacity
      QFont timeFont = painter.font();
      timeFont.setPointSize(dynamicSize / 4.5);
      painter.setFont(timeFont);
      painter.setPen(QColor(255, 255, 255, int(255 * pulseOpacity)));
      QRect timeRect(drawPoint.x() - dynamicSize * 0.75, drawPoint.y() + dynamicSize * 0.9, dynamicSize * 1.5, dynamicSize / 3);
      painter.drawText(timeRect, Qt::AlignCenter, timeText);
    }
  }

  // Update previous visibility state for next frame
  prev_stop_sign_visible = stop_sign_visible;
}

bool ModelRenderer::mapToScreen(float in_x, float in_y, float in_z, QPointF *out) {
  Eigen::Vector3f input(in_x, in_y, in_z);
  auto pt = car_space_transform * input;
  *out = QPointF(pt.x() / pt.z(), pt.y() / pt.z());
  return clip_region.contains(*out);
}