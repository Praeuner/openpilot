#include "selfdrive/ui/qt/onroad/model.h"
#include <iostream>

constexpr int CLIP_MARGIN = 500;
constexpr float MIN_DRAW_DISTANCE = 10.0;
constexpr float MAX_DRAW_DISTANCE = 100.0;

static int get_path_length_idx(const cereal::XYZTData::Reader &line, const float path_height) {
  const auto &line_x = line.getX();
  int max_idx = 0;
  for (int i = 1; i < line_x.size() && line_x[i] <= path_height; ++i) {
    max_idx = i;
  }
  return max_idx;
}

void ModelRenderer::draw(QPainter &painter, const QRect &surface_rect) {
  // Existing initial setup code remains unchanged
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
  // float v_ego = sm["carState"].getCarState().getVEgo();

  updateBlindspotStatus(sm["carState"].getCarState());
  update_model(model, lead_one);
  drawLaneLines(painter);
  drawBlindspotIndicators(painter);
  drawPath(painter, model, surface_rect.height());

  // New stop detection logic using velocity
  const auto &velocity = model.getVelocity().getX();
  const auto &position_x = model.getPosition().getX();
  const auto &position_y = model.getPosition().getY();
  const auto &position_z = model.getPosition().getZ();

  if (s->scene.show_stop_indicator_overlay) {
    if (velocity.size() > 0 && position_x.size() == velocity.size() && position_y.size() == velocity.size() && position_z.size() == velocity.size()) {
      float stopping_distance = -1.0f;
      int stop_idx = -1;
      for (size_t i = 0; i < velocity.size(); ++i) {
        if (velocity[i] < 0.5f) {
          stopping_distance = position_x[i];
          stop_idx = i;
          break;
        }
      }

      if (stop_idx != -1 && stopping_distance >= 5.0f && stopping_distance <= 50.0f) {
        float x = position_x[stop_idx];
        float y = position_y[stop_idx];
        float z = position_z[stop_idx];
        QPointF screen_point;
        if (mapToScreen(x, y, z + path_offset_z, &screen_point)) {
          // Adjust position to the right of the right lane line
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
            const int stop_sign_size = 100; // Increased size
            QPointF stop_point(lane_point.x() + stop_sign_size * 0.75, lane_point.y());

            // Ensure the stop sign stays within the clip region
            if (clip_region.contains(stop_point)) {
              drawStopSignOverlay(painter, stop_point, stop_sign_size);
              // std::cout << "Stop sign drawn at distance: " << stopping_distance << " m, screen: (" << stop_point.x() << ", " << stop_point.y() << ")" << std::endl;
            } else {
              // Adjust if partially out of bounds
              float adjusted_x = std::clamp(stop_point.x(), clip_region.left() + stop_sign_size / 2, clip_region.right() - stop_sign_size / 2);
              stop_point.setX(adjusted_x);
              if (clip_region.contains(stop_point)) {
                drawStopSignOverlay(painter, stop_point, stop_sign_size);
                // std::cout << "Stop sign adjusted to: (" << stop_point.x() << ", " << stop_point.y() << ")" << std::endl;
              }
            }
          } else {
            // Fallback: Use the original stopping point if no lane line data
            if (clip_region.contains(screen_point)) {
              const int stop_sign_size = 100; // Increased size
              drawStopSignOverlay(painter, screen_point, stop_sign_size);
              // std::cout << "Stop sign (fallback) drawn at distance: " << stopping_distance << " m" << std::endl;
            }
          }
        }
      }
    }
  }

  // Existing lead drawing code remains unchanged
  bool showRadarOverlay = !experimental_mode && s->scene.show_new_radar_overlay;
  if ((longitudinal_control || showRadarOverlay) && sm.alive("radarState")) {
    update_leads(radar_state, model.getPosition());
    const auto &lead_two = radar_state.getLeadTwo();
    if (lead_one.getStatus()) {
      drawLead(painter, lead_one, lead_vertices[0], surface_rect, lead_radar_assisted[0]);
    }
    if (lead_two.getStatus() && (std::abs(lead_one.getDRel() - lead_two.getDRel()) > 3.0)) {
      drawLead(painter, lead_two, lead_vertices[1], surface_rect, lead_radar_assisted[1]);
    }
  }

  painter.restore();
}

void ModelRenderer::update_leads(const cereal::RadarState::Reader &radar_state, const cereal::XYZTData::Reader &line) {
  for (int i = 0; i < 2; ++i) {
    const auto &lead_data = (i == 0) ? radar_state.getLeadOne() : radar_state.getLeadTwo();
    bool current_status = lead_data.getStatus();
    if (current_status) {
      float raw_yRel = lead_data.getYRel();

      // Get the path's y-coordinate at the lead's distance
      int idx = get_path_length_idx(line, lead_data.getDRel());
      float path_y = line.getY()[idx];
      float path_z = line.getZ()[idx];

      // For first detection, initialize with raw values
      if (!prev_lead_status[i]) {
        smoothed_yRel[i] = raw_yRel;
      } else {
        // Apply stronger path-based smoothing
        // Prioritize the path curve more when on curves
        float path_curvature = (idx > 1) ? fabs(line.getY()[idx] - line.getY()[idx - 1]) : 0.0f;

        // Dynamically adjust path influence based on curvature
        // Higher curvature = more path influence
        float path_weight = std::min(0.6f + path_curvature * 5.0f, 0.9f);

        // Low-pass filter for temporal smoothing
        float alpha = 0.2f; // Adjust for smoothness

        // First smooth the raw radar reading
        float smoothed_raw = alpha * raw_yRel + (1.0f - alpha) * smoothed_yRel[i];

        // Then blend with the path position using dynamic path weight
        smoothed_yRel[i] = path_weight * path_y + (1.0f - path_weight) * smoothed_raw;
      }

      QPointF current_pos;
      mapToScreen(lead_data.getDRel(), smoothed_yRel[i], path_z + path_offset_z, &current_pos);
      lead_vertices[i] = current_pos;
      lead_radar_assisted[i] = lead_data.getRadar();
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

  // Create lane barrier polygons for blindspot indication
  // Now using current lane lines with wider gradient effect
  if (lane_lines.size() >= 4) {
    // Left blind spot: From lane line to left (y - BLINDSPOT_WIDTH)
    const auto &left_lane = lane_lines[1];
    lane_barrier_vertices[0].clear();
    for (int i = 0; i <= max_idx; i++) {
      QPointF lane_pt, offset_pt;
      if (mapToScreen(left_lane.getX()[i], left_lane.getY()[i], left_lane.getZ()[i], &lane_pt) &&
          mapToScreen(left_lane.getX()[i], left_lane.getY()[i] - BLINDSPOT_WIDTH, left_lane.getZ()[i], &offset_pt)) {
        lane_barrier_vertices[0].append(offset_pt); // Outer left edge
      }
    }
    for (int i = max_idx; i >= 0; i--) {
      QPointF lane_pt;
      if (mapToScreen(left_lane.getX()[i], left_lane.getY()[i], left_lane.getZ()[i], &lane_pt)) {
        lane_barrier_vertices[0].append(lane_pt); // Lane line edge
      }
    }

    // Right blind spot: From lane line to right (y + BLINDSPOT_WIDTH)
    const auto &right_lane = lane_lines[2];
    lane_barrier_vertices[1].clear();
    for (int i = 0; i <= max_idx; i++) {
      QPointF lane_pt;
      if (mapToScreen(right_lane.getX()[i], right_lane.getY()[i], right_lane.getZ()[i], &lane_pt)) {
        lane_barrier_vertices[1].append(lane_pt); // Lane line edge
      }
    }
    for (int i = max_idx; i >= 0; i--) {
      QPointF offset_pt;
      if (mapToScreen(right_lane.getX()[i], right_lane.getY()[i] + BLINDSPOT_WIDTH, right_lane.getZ()[i], &offset_pt)) {
        lane_barrier_vertices[1].append(offset_pt); // Outer right edge
      }
    }
  }

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

  // lanelines
  for (int i = 0; i < std::size(lane_line_vertices); ++i) {
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

    painter.drawPolygon(lane_line_vertices[i]);
  }

  // road edges
  for (int i = 0; i < std::size(road_edge_vertices); ++i) {
    painter.setBrush(QColor::fromRgbF(1.0, 0, 0, std::clamp<float>(1.0 - road_edge_stds[i], 0.0, 1.0)));
    painter.drawPolygon(road_edge_vertices[i]);
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

  // Calculate sizes based on distance for responsive design
  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 3.525;
  float x = std::clamp<float>(vd.x(), 0.f, surface_rect.width() - sz / 2);
  float y = std::min<float>(vd.y(), surface_rect.height() - sz * 0.6);

  // Convert measurements for display
  float distance_m = d_rel;
  float lead_speed_mph = v_lead * 2.237;

  // Enable anti-aliasing for smoother lead indicator
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Create the chevron polygon centered on the position
  QPolygonF chevronPolygon;
  chevronPolygon << QPointF(x + (sz * 1.25), y + sz) << QPointF(x, y) << QPointF(x - (sz * 1.25), y + sz);

  // Create gradient based on radar assistance
  QLinearGradient chevGradient(QPointF(x, y), QPointF(x, y + sz));
  if (isRadarAssisted) {
    chevGradient.setColorAt(0, QColor(60, 170, 255, 230)); // Blue
    chevGradient.setColorAt(1, QColor(30, 144, 255, 200)); // Darker blue
  } else {
    chevGradient.setColorAt(0, QColor(255, 255, 0, 230)); // Yellow
    chevGradient.setColorAt(1, QColor(220, 220, 0, 200)); // Darker yellow
  }

  // Draw chevron with gradient
  painter.setPen(Qt::NoPen);
  painter.setBrush(chevGradient);
  painter.drawPolygon(chevronPolygon);

  // Draw border with color based on radar assistance
  // Change: Use white for radar-assisted, black for vision-only
  if (isRadarAssisted) {
    painter.setPen(QPen(QColor(255, 255, 255, 220), 2.5)); // White border
  } else {
    painter.setPen(QPen(QColor(0, 0, 0, 220), 2.5)); // Black border
  }
  painter.setBrush(Qt::NoBrush);
  painter.drawPolygon(chevronPolygon);

  // Draw icon in the center of the chevron
  // Calculate icon size based on chevron size
  float icon_size = sz * 0.8;

  // For a chevron, the vertical center is roughly at y + sz/2
  // Move it down a bit more to visually center it in the chevron shape
  float icon_center_y = y + sz * 0.6; // Adjusted to move the icon down in the chevron

  QRectF iconRect(x - icon_size / 2,             // Horizontal center
                  icon_center_y - icon_size / 2, // Vertical center, adjusted for chevron shape
                  icon_size, icon_size);

  // Load and draw the appropriate icon
  QPixmap icon;
  if (isRadarAssisted) {
    icon.load("../assets/img_radar.png");
  } else {
    icon.load("../assets/img_vision.png");
  }

  if (!icon.isNull()) {
    if (isRadarAssisted) {
      // Rotate radar-assisted icon by 90 degrees
      painter.save();
      painter.translate(iconRect.center()); // Move origin to icon center
      painter.rotate(90);                   // Rotate 90 degrees clockwise
      painter.drawPixmap(QRectF(-iconRect.width() / 2, -iconRect.height() / 2, iconRect.width(), iconRect.height()), icon, icon.rect());
      painter.restore();
    } else {
      // Draw vision icon without rotation
      painter.drawPixmap(iconRect, icon, icon.rect());
    }
  }

  // Position info panel centered below the lead vehicle
  // Anchor panel to the chevron's bottom center point
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

  // Draw a more transparent panel with a subtle gradient
  QLinearGradient panelGradient(infoPanel.topLeft(), infoPanel.bottomLeft());
  panelGradient.setColorAt(0, QColor(20, 20, 20, 120)); // Much more transparent background (120 alpha)
  panelGradient.setColorAt(1, QColor(40, 40, 40, 120)); // Much more transparent background (120 alpha)
  painter.setPen(Qt::NoPen);
  painter.setBrush(panelGradient);
  painter.drawRoundedRect(infoPanel, 15, 15);

  // Add a subtle border
  painter.setPen(QPen(QColor(150, 150, 150, 80), 1)); // More transparent border
  painter.drawRoundedRect(infoPanel, 15, 15);

  // Set up text formatting with larger size
  QFont infoFont = painter.font();
  infoFont.setPixelSize(33);
  infoFont.setWeight(QFont::DemiBold);
  painter.setFont(infoFont);

  // Format distance and speed text
  QString distText = QString("%1 m").arg(qRound(distance_m));
  QString speedText = QString("%1 mph").arg(qRound(lead_speed_mph));

  // Display distance and speed on the same line
  painter.setPen(Qt::white);
  QString combinedText = distText + "  |  " + speedText;

  // Center the text in the panel
  QRectF textRect = infoPanel.adjusted(7, 7, -7, -7);
  painter.drawText(textRect, Qt::AlignCenter, combinedText);
}

void ModelRenderer::mapLineToPolygon(const cereal::XYZTData::Reader &line, float y_off, float z_off, QPolygonF *pvd, int max_idx, bool allow_invert) {
  const auto line_x = line.getX(), line_y = line.getY(), line_z = line.getZ();
  QPointF left, right;
  pvd->clear();
  for (int i = 0; i <= max_idx; i++) {
    // highly negative x positions  are drawn above the frame and cause flickering, clip to zy plane of camera
    if (line_x[i] < 0)
      continue;

    bool l = mapToScreen(line_x[i], line_y[i] - y_off, line_z[i] + z_off, &left);
    bool r = mapToScreen(line_x[i], line_y[i] + y_off, line_z[i] + z_off, &right);
    if (l && r) {
      // For wider lines the drawn polygon will "invert" when going over a hill and cause artifacts
      if (!allow_invert && pvd->size() && left.y() > pvd->back().y()) {
        continue;
      }
      pvd->push_back(left);
      pvd->push_front(right);
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

  // Draw left blindspot indicator
  if (left_blindspot && !lane_barrier_vertices[0].isEmpty()) {
    QRectF leftBounds = lane_barrier_vertices[0].boundingRect();
    QLinearGradient leftGradient(leftBounds.center().x(), leftBounds.top(), leftBounds.center().x(), leftBounds.bottom());
    leftGradient.setColorAt(0.0, QColor::fromRgbF(1.0, 0.0, 0.0, 0.0));               // Top - transparent
    leftGradient.setColorAt(0.2, QColor::fromRgbF(1.0, 0.0, 0.0, 0.2));              // Start fade
    leftGradient.setColorAt(0.4, QColor::fromRgbF(1.0, 0.0, 0.0, blindspot_opacity)); // Middle - animated opacity
    leftGradient.setColorAt(0.7, QColor::fromRgbF(1.0, 0.0, 0.0, 0.9));              // Stronger opacity
    leftGradient.setColorAt(1.0, QColor::fromRgbF(1.0, 0.0, 0.0, 1.0));               // Bottom - 100% opacity
    painter.setBrush(leftGradient);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(lane_barrier_vertices[0]);
  }

  // Draw right blindspot indicator
  if (right_blindspot && !lane_barrier_vertices[1].isEmpty()) {
    QRectF rightBounds = lane_barrier_vertices[1].boundingRect();
    QLinearGradient rightGradient(rightBounds.center().x(), rightBounds.top(), rightBounds.center().x(), rightBounds.bottom());
    rightGradient.setColorAt(0.0, QColor::fromRgbF(1.0, 0.0, 0.0, 0.0));               // Top - transparent
    rightGradient.setColorAt(0.2, QColor::fromRgbF(1.0, 0.0, 0.0, 0.2));              // Start fade
    rightGradient.setColorAt(0.4, QColor::fromRgbF(1.0, 0.0, 0.0, blindspot_opacity)); // Middle - animated opacity
    rightGradient.setColorAt(0.7, QColor::fromRgbF(1.0, 0.0, 0.0, 0.9));              // Stronger opacity
    rightGradient.setColorAt(1.0, QColor::fromRgbF(1.0, 0.0, 0.0, 1.0));               // Bottom - 100% opacity
    painter.setBrush(rightGradient);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(lane_barrier_vertices[1]);
  }
}

void ModelRenderer::drawStopSignOverlay(QPainter &painter, const QPointF &point, int size) {
  // Enable anti-aliasing for smoother shape
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Draw octagon shape for stop sign
  QPolygonF stopSign;
  const float angle_increment = 2 * M_PI / 8;    // 8 sides
  const float start_angle = angle_increment / 2; // Rotate to get flat sides top/bottom

  for (int i = 0; i < 8; i++) {
    float angle = start_angle + i * angle_increment;
    stopSign << QPointF(point.x() + size / 2 * cos(angle), point.y() + size / 2 * sin(angle));
  }

  // Draw red stop sign with white border
  painter.setPen(QPen(Qt::white, 3));
  painter.setBrush(QColor(255, 0, 0, 220)); // Red with some transparency
  painter.drawPolygon(stopSign);

  // Draw "STOP" text centered in the middle of the sign
  painter.setPen(Qt::white);
  // Use a standard font instead of InterFont
  QFont stopFont = painter.font();
  stopFont.setPointSize(size / 3);
  stopFont.setBold(true);
  painter.setFont(stopFont);

  // Create a rectangular area for the text that's centered on the stop sign
  QRect textRect(point.x() - size / 2, point.y() - size / 3, size, size * 2 / 3);

  // Draw the text centered in this rectangle
  painter.drawText(textRect, Qt::AlignCenter, "STOP");
}

bool ModelRenderer::mapToScreen(float in_x, float in_y, float in_z, QPointF *out) {
  Eigen::Vector3f input(in_x, in_y, in_z);
  auto pt = car_space_transform * input;
  *out = QPointF(pt.x() / pt.z(), pt.y() / pt.z());
  return clip_region.contains(*out);
}