#include "selfdrive/ui/bluepilot/qt/onroad/model_bp.h"
#include "selfdrive/ui/qt/util.h"
#include "common/params.h"
#include "common/util.h"
#include "selfdrive/ui/ui.h"
#include <QGraphicsBlurEffect>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <iostream>

// BluePilot blindspot enhancement constants and state
static constexpr float BLINDSPOT_WIDTH = 1.0f; // Width of blind spot indicator in meters

struct BlindspotState {
  bool left_blindspot = false;
  bool right_blindspot = false;
  int blink_counter = 0;
  float opacity = 0.2f;
  QLinearGradient cached_gradient_left;
  QLinearGradient cached_gradient_right;
  bool gradients_dirty = true;
  QRectF last_left_bounds, last_right_bounds;
} static blindspot_state;

#ifdef SUNNYPILOT
ModelRendererBP::ModelRendererBP() : ModelRendererSP() {
  // Initialize any BluePilot-specific state
}
#else
ModelRendererBP::ModelRendererBP() : ModelRenderer() {
  // Initialize any BluePilot-specific state
}
#endif

void ModelRendererBP::draw(QPainter &painter, const QRect &surface_rect) {
#ifdef SUNNYPILOT
  // For SunnyPilot, implement custom BluePilot draw logic
  auto *s = uiState();
  auto &sm = *(s->sm);

  // Check if data is up-to-date
  if (sm.rcv_frame("liveCalibration") < s->scene.started_frame ||
      sm.rcv_frame("modelV2") < s->scene.started_frame) {
    return;
  }

  clip_region = surface_rect.adjusted(-CLIP_MARGIN, -CLIP_MARGIN, CLIP_MARGIN, CLIP_MARGIN);
  experimental_mode = sm["selfdriveState"].getSelfdriveState().getExperimentalMode();
  longitudinal_control = sm["carParams"].getCarParams().getOpenpilotLongitudinalControl();
  path_offset_z = sm["liveCalibration"].getLiveCalibration().getHeight()[0];

  const auto &model = sm["modelV2"].getModelV2();
  const auto &radar_state = sm["radarState"].getRadarState();
  const auto &lead_one = radar_state.getLeadOne();

  // Update model data using base ModelRenderer (since SunnyPilot methods are private)
  ModelRenderer::update_model(model, lead_one);
  updateBluePilotState(model);

  // Apply BluePilot path smoothing to ALL paths for better visual quality
  applySmoothPath();

  painter.save();

  // Check for custom path color FIRST
  QString pathColor = QString::fromStdString(Params().get("CustomModelPathColor"));
  bool hasCustomPath = !pathColor.isEmpty() && pathColor != "Stock" && pathColor != "";

  if (hasCustomPath) {
    // Draw original BluePilot custom path system
    QLinearGradient bg(0, surface_rect.height(), 0, 0);

    if (pathColor == "Rainbow") {
      float v_ego = sm["carState"].getCarState().getVEgo();
      float time_offset = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0f;

      for (int i = 0; i < 10; ++i) {
        float position = i / 10.0f;
        float eased_point = pow(position, 1.5f);
        float hue = fmod(eased_point * 360.0 + (v_ego * 20.0) + (time_offset * 100.0), 360.0);
        float alpha = 0.8f - (eased_point * 0.8f);

        bg.setColorAt(eased_point, QColor::fromHslF(hue / 360.0, 1.0f, 0.55f, alpha));
      }
    } else if (pathColor == "Blue") {
      bg.setColorAt(0.0, QColor(0, 102, 204, 102));
      bg.setColorAt(0.5, QColor(51, 153, 255, 89));
      bg.setColorAt(1.0, QColor(51, 153, 255, 0));
    } else if (pathColor == "Green") {
      bg.setColorAt(0.0, QColor(0, 204, 102, 102));
      bg.setColorAt(0.5, QColor(51, 255, 153, 89));
      bg.setColorAt(1.0, QColor(51, 255, 153, 0));
    } else if (pathColor == "Purple") {
      bg.setColorAt(0.0, QColor(153, 51, 204, 102));
      bg.setColorAt(0.5, QColor(178, 102, 255, 89));
      bg.setColorAt(1.0, QColor(178, 102, 255, 0));
    } else if (pathColor == "Orange") {
      bg.setColorAt(0.0, QColor(255, 128, 0, 102));
      bg.setColorAt(0.5, QColor(255, 153, 51, 89));
      bg.setColorAt(1.0, QColor(255, 153, 51, 0));
    } else if (pathColor == "Red") {
      bg.setColorAt(0.0, QColor(204, 0, 0, 102));
      bg.setColorAt(0.5, QColor(255, 51, 51, 89));
      bg.setColorAt(1.0, QColor(255, 51, 51, 0));
    } else if (pathColor == "Cyan") {
      bg.setColorAt(0.0, QColor(0, 204, 204, 102));
      bg.setColorAt(0.5, QColor(51, 255, 255, 89));
      bg.setColorAt(1.0, QColor(51, 255, 255, 0));
    } else if (pathColor == "Yellow") {
      bg.setColorAt(0.0, QColor(204, 204, 0, 102));
      bg.setColorAt(0.5, QColor(255, 255, 51, 89));
      bg.setColorAt(1.0, QColor(255, 255, 51, 0));
    }

    painter.setBrush(bg);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(track_vertices);

    // Draw blindspot overlays on top of custom path
    drawBlindspotOverlays(painter);

    // Draw lead status after path rendering
    drawLeadStatus(painter, surface_rect.height(), surface_rect.width());
    painter.restore();
    return; // Early return - don't call base drawPath
  } else {
    // Draw BluePilot enhanced lane lines and road edges
    drawBluePilotLaneLines(painter);

    // Add enhanced BluePilot blindspot overlays (superior to SunnyPilot version)
    drawBlindspotOverlays(painter);

    // Fall back to standard path drawing (original BluePilot logic)
    ModelRenderer::drawPath(painter, model, surface_rect);

    // Draw BluePilot enhanced lead status with radar overlay integration
    drawLeadStatus(painter, surface_rect.height(), surface_rect.width());
  }

  // Draw leads (with BluePilot radar overlay suppression)
  if (longitudinal_control && sm.alive("radarState")) {
    update_leads(radar_state, model.getPosition());
    const auto &lead_two = radar_state.getLeadTwo();

    // Prevent drawing lead chevron if BluePilot radar overlay is active
    if (!s->scene.show_bp_radar_overlay) {
      if (lead_one.getStatus()) {
        drawLead(painter, lead_one, lead_vertices[0], surface_rect);
      }
      if (lead_two.getStatus() && (std::abs(lead_one.getDRel() - lead_two.getDRel()) > 3.0)) {
        drawLead(painter, lead_two, lead_vertices[1], surface_rect);
      }
    }
  }

  painter.restore();
#else
  // For stock builds, implement the full draw logic with BluePilot radar suppression
  auto *s = uiState();
  auto &sm = *(s->sm);

  // Check if data is up-to-date
  if (sm.rcv_frame("liveCalibration") < s->scene.started_frame ||
      sm.rcv_frame("modelV2") < s->scene.started_frame) {
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

  ModelRenderer::update_model(model, lead_one);
  updateBluePilotState(model);

  // Apply BluePilot path smoothing to ALL paths for better visual quality
  applySmoothPath();

  // Draw BluePilot enhanced lane lines and road edges
  drawBluePilotLaneLines(painter);

  // Fall back to standard path drawing with smoothed vertices
  ModelRenderer::drawPath(painter, model, surface_rect);

  // Draw leads (with BluePilot radar overlay suppression)
  if (longitudinal_control && sm.alive("radarState")) {
    update_leads(radar_state, model.getPosition());
    const auto &lead_two = radar_state.getLeadTwo();

    // Prevent drawing lead chevron if BluePilot radar overlay is active
    if (!s->scene.show_bp_radar_overlay) {
      if (lead_one.getStatus()) {
        drawLead(painter, lead_one, lead_vertices[0], surface_rect);
      }
      if (lead_two.getStatus() && (std::abs(lead_one.getDRel() - lead_two.getDRel()) > 3.0)) {
        drawLead(painter, lead_two, lead_vertices[1], surface_rect);
      }
    }
  }

  painter.restore();
#endif
}

#ifndef SUNNYPILOT
void ModelRendererBP::drawPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, const QRect &surface_rect) {
  // Call base implementation first
  ModelRenderer::drawPath(painter, model, surface_rect);

  // BluePilot path drawing complete
}
#endif

#ifndef SUNNYPILOT
void ModelRendererBP::update_model(const cereal::ModelDataV2::Reader &model, const cereal::RadarState::LeadData::Reader &lead) {
  // Call base implementation first
  ModelRenderer::update_model(model, lead);

  // Update BluePilot-specific state
  updateBluePilotState(model);
}
#endif

void ModelRendererBP::updateBluePilotState(const cereal::ModelDataV2::Reader &model) {
  // Update BluePilot-specific state
  current_speed = 0.0f; // We'll get this from model data or UI state
  high_speed_mode = current_speed > 50.0f;

  // Create blindspot polygons
  createBlindspotPolygons(model);
}

void ModelRendererBP::createBlindspotPolygons(const cereal::ModelDataV2::Reader &model) {
  const auto &model_position = model.getPosition();
  const auto &lane_lines = model.getLaneLines();
  float max_distance = std::clamp(*(model_position.getX().end() - 1), MIN_DRAW_DISTANCE, MAX_DRAW_DISTANCE);
  int max_idx = get_path_length_idx(lane_lines[0], max_distance);

  // Create blindspot polygons like in original SunnyPilot model.cc
  if (lane_lines.size() >= 4) {
    const auto &left_lane = lane_lines[1];
    const auto &right_lane = lane_lines[2];

    // Validate lane data before processing
    if (left_lane.getX().size() == 0 || right_lane.getX().size() == 0) {
      left_blindspot_vertices.clear();
      right_blindspot_vertices.clear();
    } else {
      // Limit blindspot polygon complexity
      int safe_max_idx = std::min(max_idx, 50); // Limit to 50 points max
      const int MAX_BLINDSPOT_VERTICES = 40; // Vertex limit for performance

      // Left blind spot - same as original model.cc
      left_blindspot_vertices.clear();
      int left_vertex_count = 0;

      // Forward pass along left lane with offset
      for (int i = 0; i <= safe_max_idx && i < static_cast<int>(left_lane.getX().size()) && left_vertex_count < MAX_BLINDSPOT_VERTICES; i++) {
        QPointF lane_pt, offset_pt;
        if (mapToScreen(left_lane.getX()[i], left_lane.getY()[i], left_lane.getZ()[i], &lane_pt) &&
            mapToScreen(left_lane.getX()[i], left_lane.getY()[i] - BLINDSPOT_WIDTH, left_lane.getZ()[i], &offset_pt)) {
          left_blindspot_vertices.append(offset_pt);
          left_vertex_count++;
        }
      }

      // Return pass along left lane without offset
      for (int i = safe_max_idx; i >= 0 && i < static_cast<int>(left_lane.getX().size()) && left_vertex_count < MAX_BLINDSPOT_VERTICES; i--) {
        QPointF lane_pt;
        if (mapToScreen(left_lane.getX()[i], left_lane.getY()[i], left_lane.getZ()[i], &lane_pt)) {
          left_blindspot_vertices.append(lane_pt);
          left_vertex_count++;
        }
      }

      // Right blind spot - same as original model.cc
      right_blindspot_vertices.clear();
      int right_vertex_count = 0;

      // Forward pass along right lane without offset
      for (int i = 0; i <= safe_max_idx && i < static_cast<int>(right_lane.getX().size()) && right_vertex_count < MAX_BLINDSPOT_VERTICES; i++) {
        QPointF lane_pt;
        if (mapToScreen(right_lane.getX()[i], right_lane.getY()[i], right_lane.getZ()[i], &lane_pt)) {
          right_blindspot_vertices.append(lane_pt);
          right_vertex_count++;
        }
      }

      // Return pass along right lane with offset
      for (int i = safe_max_idx; i >= 0 && i < static_cast<int>(right_lane.getX().size()) && right_vertex_count < MAX_BLINDSPOT_VERTICES; i--) {
        QPointF offset_pt;
        if (mapToScreen(right_lane.getX()[i], right_lane.getY()[i] + BLINDSPOT_WIDTH, right_lane.getZ()[i], &offset_pt)) {
          right_blindspot_vertices.append(offset_pt);
          right_vertex_count++;
        }
      }
    }
  }

  // Mark gradients dirty after polygon updates
  blindspot_state.gradients_dirty = true;
}


void ModelRendererBP::drawBluePilotLaneLines(QPainter &painter) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);

  // Draw lane lines with BluePilot enhanced visibility
  for (int i = 0; i < std::size(lane_line_vertices); ++i) {
    if (lane_line_vertices[i].isEmpty()) continue;

    float probability = lane_line_probs[i];
    float enhanced_alpha = std::clamp<float>(probability, 0.2, 0.9);

    QColor lane_color = QColor::fromRgbF(1.0, 1.0, 1.0, enhanced_alpha);
    painter.setBrush(lane_color);
    painter.drawPolygon(lane_line_vertices[i]);
  }

  // Draw road edges with BluePilot enhanced visibility
  for (int i = 0; i < std::size(road_edge_vertices); ++i) {
    if (road_edge_vertices[i].isEmpty()) continue;

    float edge_confidence = 1.0 - road_edge_stds[i];
    float enhanced_alpha = std::clamp<float>(edge_confidence, 0.3, 1.0);

    QColor edge_color = QColor::fromRgbF(1.0, 0.2, 0.2, enhanced_alpha);
    painter.setBrush(edge_color);
    painter.drawPolygon(road_edge_vertices[i]);
  }

  painter.restore();
}

void ModelRendererBP::drawBluePilotPath(QPainter &painter, const QRect &surface_rect) {
  if (track_vertices.isEmpty()) return;

  auto *s = uiState();
  auto &sm = *(s->sm);

  // Use the base ModelRenderer gradient logic but apply to our smoothed track_vertices
  QLinearGradient bg(0, surface_rect.height(), 0, 0);

  if (experimental_mode) {
    // Use experimental mode coloring from ModelRenderer
    const auto model = sm["modelV2"].getModelV2();
    const auto &acceleration = model.getAcceleration().getX();
    const int max_len = std::min<int>(track_vertices.length() / 2, acceleration.size());

    for (int i = 0; i < max_len; ++i) {
      int track_idx = max_len - i - 1;
      if (track_vertices[track_idx].y() < 0 || track_vertices[track_idx].y() > surface_rect.height()) continue;

      float lin_grad_point = (surface_rect.height() - track_vertices[track_idx].y()) / surface_rect.height();
      float path_hue = fmax(fmin(60 + acceleration[i] * 35, 120), 0);
      path_hue = int(path_hue * 100 + 0.5) / 100;

      float saturation = fmin(fabs(acceleration[i] * 1.5), 1);
      float lightness = util::map_val(saturation, 0.0f, 1.0f, 0.95f, 0.62f);
      float alpha = util::map_val(lin_grad_point, 0.75f / 2.f, 0.75f, 0.4f, 0.0f);
      bg.setColorAt(lin_grad_point, QColor::fromHslF(path_hue / 360., saturation, lightness, alpha));

      i += (i + 2) < max_len ? 1 : 0;
    }
  } else {
    // Use standard gradient coloring (from ModelRenderer::updatePathGradient)
    static const QColor throttle_colors[] = {
        QColor::fromHslF(148. / 360., 0.94, 0.51, 0.4),
        QColor::fromHslF(112. / 360., 1.0, 0.68, 0.35),
        QColor::fromHslF(112. / 360., 1.0, 0.68, 0.0)};

    static const QColor no_throttle_colors[] = {
        QColor::fromHslF(148. / 360., 0.0, 0.95, 0.4),
        QColor::fromHslF(112. / 360., 0.0, 0.95, 0.35),
        QColor::fromHslF(112. / 360., 0.0, 0.95, 0.0),
    };

    const QColor *colors = longitudinal_control ? throttle_colors : no_throttle_colors;
    bg.setColorAt(0.0, colors[0]);
    bg.setColorAt(0.5, colors[1]);
    bg.setColorAt(1.0, colors[2]);
  }

  painter.setBrush(bg);
  painter.drawPolygon(track_vertices);
}

void ModelRendererBP::drawCustomPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, const QRect &surface_rect) {
  auto *s = uiState();
  auto &sm = *(s->sm);

  // Check for custom path color FIRST - before any other drawing
  QString pathColor = QString::fromStdString(Params().get("CustomModelPathColor"));
  bool hasCustomPath = !pathColor.isEmpty() && pathColor != "Stock" && pathColor != "";

  if (hasCustomPath) {
    // Draw custom path and return early to prevent standard path drawing
    QLinearGradient bg(0, surface_rect.height(), 0, 0);

    if (pathColor == "Rainbow") {
      float v_ego = sm["carState"].getCarState().getVEgo();
      float time_offset = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0f;

      for (int i = 0; i < 10; ++i) {
        float position = i / 10.0f;
        float eased_point = pow(position, 1.5f);
        float hue = fmod(eased_point * 360.0 + (v_ego * 20.0) + (time_offset * 100.0), 360.0);
        float alpha = 0.8f - (eased_point * 0.8f);

        bg.setColorAt(eased_point, QColor::fromHslF(hue / 360.0, 1.0f, 0.55f, alpha));
      }
    } else if (pathColor == "Blue") {
      bg.setColorAt(0.0, QColor(0, 102, 204, 102));
      bg.setColorAt(0.5, QColor(51, 153, 255, 89));
      bg.setColorAt(1.0, QColor(51, 153, 255, 0));
    } else if (pathColor == "Green") {
      bg.setColorAt(0.0, QColor(0, 204, 102, 102));
      bg.setColorAt(0.5, QColor(51, 255, 153, 89));
      bg.setColorAt(1.0, QColor(51, 255, 153, 0));
    } else if (pathColor == "Purple") {
      bg.setColorAt(0.0, QColor(153, 51, 204, 102));
      bg.setColorAt(0.5, QColor(178, 102, 255, 89));
      bg.setColorAt(1.0, QColor(178, 102, 255, 0));
    } else if (pathColor == "Orange") {
      bg.setColorAt(0.0, QColor(255, 128, 0, 102));
      bg.setColorAt(0.5, QColor(255, 153, 51, 89));
      bg.setColorAt(1.0, QColor(255, 153, 51, 0));
    } else if (pathColor == "Red") {
      bg.setColorAt(0.0, QColor(204, 0, 0, 102));
      bg.setColorAt(0.5, QColor(255, 51, 51, 89));
      bg.setColorAt(1.0, QColor(255, 51, 51, 0));
    } else if (pathColor == "Cyan") {
      bg.setColorAt(0.0, QColor(0, 204, 204, 102));
      bg.setColorAt(0.5, QColor(51, 255, 255, 89));
      bg.setColorAt(1.0, QColor(51, 255, 255, 0));
    } else if (pathColor == "Yellow") {
      bg.setColorAt(0.0, QColor(204, 204, 0, 102));
      bg.setColorAt(0.5, QColor(255, 255, 51, 89));
      bg.setColorAt(1.0, QColor(255, 255, 51, 0));
    }

    painter.setBrush(bg);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(track_vertices);

    // Draw blindspot overlays on top of custom path
    drawBlindspotOverlays(painter);

    // Draw lead status after path rendering
    drawLeadStatus(painter, surface_rect.height(), surface_rect.width());
  }
}

void ModelRendererBP::drawBlindspotOverlays(QPainter &painter) {
  auto *s = uiState();
  auto &sm = *(s->sm);

  // Update blindspot state and animation
  const auto car_state = sm["carState"].getCarState();
  blindspot_state.left_blindspot = car_state.getLeftBlindspot();
  blindspot_state.right_blindspot = car_state.getRightBlindspot();

  // Update blink animation - using UI_FREQ which is 20
  blindspot_state.blink_counter = (blindspot_state.blink_counter + 1) % (20 * 2);
  float pulse = 0.1 * sin(blindspot_state.blink_counter * M_PI / 20) + 0.25;
  blindspot_state.opacity = pulse;

  // Enhanced BluePilot blindspot drawing - overrides SunnyPilot version
  bool blindspot = Params().getBool("BlindSpot");
  if (blindspot) {
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Left blindspot with animated red gradient
    if (blindspot_state.left_blindspot && !left_blindspot_vertices.isEmpty()) {
      QRectF leftBounds = left_blindspot_vertices.boundingRect();
      if (leftBounds != blindspot_state.last_left_bounds || blindspot_state.gradients_dirty) {
        blindspot_state.cached_gradient_left = QLinearGradient(leftBounds.center().x(), leftBounds.top(),
                                                               leftBounds.center().x(), leftBounds.bottom());
        // Enhanced gradient with multiple color stops and animation
        blindspot_state.cached_gradient_left.setColorAt(0.0, QColor::fromRgbF(1.0, 0.0, 0.0, 0.0));
        blindspot_state.cached_gradient_left.setColorAt(0.2, QColor::fromRgbF(1.0, 0.0, 0.0, 0.2));
        blindspot_state.cached_gradient_left.setColorAt(0.4, QColor::fromRgbF(1.0, 0.0, 0.0, blindspot_state.opacity));
        blindspot_state.cached_gradient_left.setColorAt(0.7, QColor::fromRgbF(1.0, 0.0, 0.0, 0.9));
        blindspot_state.cached_gradient_left.setColorAt(1.0, QColor::fromRgbF(1.0, 0.0, 0.0, 1.0));
        blindspot_state.last_left_bounds = leftBounds;
      }
      painter.setBrush(blindspot_state.cached_gradient_left);
      painter.setPen(Qt::NoPen);
      painter.drawPolygon(left_blindspot_vertices);
    }

    // Right blindspot with animated red gradient
    if (blindspot_state.right_blindspot && !right_blindspot_vertices.isEmpty()) {
      QRectF rightBounds = right_blindspot_vertices.boundingRect();
      if (rightBounds != blindspot_state.last_right_bounds || blindspot_state.gradients_dirty) {
        blindspot_state.cached_gradient_right = QLinearGradient(rightBounds.center().x(), rightBounds.top(),
                                                                rightBounds.center().x(), rightBounds.bottom());
        // Enhanced gradient with multiple color stops and animation
        blindspot_state.cached_gradient_right.setColorAt(0.0, QColor::fromRgbF(1.0, 0.0, 0.0, 0.0));
        blindspot_state.cached_gradient_right.setColorAt(0.2, QColor::fromRgbF(1.0, 0.0, 0.0, 0.2));
        blindspot_state.cached_gradient_right.setColorAt(0.4, QColor::fromRgbF(1.0, 0.0, 0.0, blindspot_state.opacity));
        blindspot_state.cached_gradient_right.setColorAt(0.7, QColor::fromRgbF(1.0, 0.0, 0.0, 0.9));
        blindspot_state.cached_gradient_right.setColorAt(1.0, QColor::fromRgbF(1.0, 0.0, 0.0, 1.0));
        blindspot_state.last_right_bounds = rightBounds;
      }
      painter.setBrush(blindspot_state.cached_gradient_right);
      painter.setPen(Qt::NoPen);
      painter.drawPolygon(right_blindspot_vertices);
    }
    blindspot_state.gradients_dirty = false;
  }
}

void ModelRendererBP::drawLeadStatus(QPainter &painter, int height, int width) {
  auto *s = uiState();
  auto &sm = *(s->sm);

  // Original BluePilot enhancement: Early exit if BluePilot radar overlay is active
  if (s->scene.show_bp_radar_overlay) return;

  if (!sm.alive("radarState")) return;

  const auto &radar_state = sm["radarState"].getRadarState();
  const auto &lead_one = radar_state.getLeadOne();
  const auto &lead_two = radar_state.getLeadTwo();

  // Check if we have any active leads
  bool has_lead_one = lead_one.getStatus();
  bool has_lead_two = lead_two.getStatus();

  if (!has_lead_one && !has_lead_two) {
    // Fade out status display
    lead_status_alpha = std::max(0.0f, lead_status_alpha - 0.05f);
    if (lead_status_alpha <= 0.0f) return;
  } else {
    // Fade in status display
    lead_status_alpha = std::min(1.0f, lead_status_alpha + 0.1f);
  }

  if (has_lead_one) {
    drawLeadStatusAtPosition(painter, lead_one, lead_vertices[0], height, width, "L1");
  }

  if (has_lead_two && std::abs(lead_one.getDRel() - lead_two.getDRel()) > 3.0) {
    drawLeadStatusAtPosition(painter, lead_two, lead_vertices[1], height, width, "L2");
  }
}

void ModelRendererBP::drawLeadStatusAtPosition(QPainter &painter,
                                               const cereal::RadarState::LeadData::Reader &lead_data,
                                               const QPointF &chevron_pos,
                                               int height, int width,
                                               const QString &label) {
  float d_rel = lead_data.getDRel();
  float v_rel = lead_data.getVRel();
  auto *s = uiState();
  auto &sm = *(s->sm);
  float v_ego = sm["carState"].getCarState().getVEgo();

  int chevron_data = std::atoi(Params().get("ChevronInfo").c_str());

  // Calculate chevron size (same logic as drawLead)
  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 2.35;

  QFont content_font = painter.font();
  content_font.setPixelSize(42);  // Updated font size from new SP version
  content_font.setBold(true);
  painter.setFont(content_font);

  QFontMetrics fm(content_font);
  bool is_metric = s->scene.is_metric;

  QStringList text_lines;

  const int chevron_types = 3;
  const int chevron_all = chevron_types + 1;  // All metrics (value 4)
  QStringList chevron_text[chevron_types];
  int position;
  float val;

  // Distance display (chevron_data == 1 or all)
  if (chevron_data == 1 || chevron_data == chevron_all) {
    position = 0;
    val = std::max(0.0f, d_rel);
    QString distance_unit = is_metric ? "m" : "ft";
    if (!is_metric) {
      val *= 3.28084f; // Convert meters to feet
    }
    chevron_text[position].append(QString::number(val, 'f', 0) + " " + distance_unit);
  }

  // Absolute velocity display (chevron_data == 2 or all)
  if (chevron_data == 2 || chevron_data == chevron_all) {
    position = (chevron_data == 2) ? 0 : 1;
    val = std::max(0.0f, (v_rel + v_ego) * (is_metric ? static_cast<float>(MS_TO_KPH) : static_cast<float>(MS_TO_MPH)));
    chevron_text[position].append(QString::number(val, 'f', 0) + " " + (is_metric ? "km/h" : "mph"));
  }

  // Time-to-contact display (chevron_data == 3 or all)
  if (chevron_data == 3 || chevron_data == chevron_all) {
    position = (chevron_data == 3) ? 0 : 2;
    val = (d_rel > 0 && v_ego > 0) ? std::max(0.0f, d_rel / v_ego) : 0.0f;
    QString ttc_str = (val > 0 && val < 200) ? QString::number(val, 'f', 1) + "s" : "---";
    chevron_text[position].append(ttc_str);
  }

  // Collect all non-empty text lines
  for (int i = 0; i < chevron_types; ++i) {
    if (!chevron_text[i].isEmpty()) {
      text_lines.append(chevron_text[i]);
    }
  }

  // If no text to display, return early
  if (text_lines.isEmpty()) {
    return;
  }

  // Text box dimensions
  float str_w = 150;  // Width of text area
  float str_h = 45;   // Height per line

  // Position text below chevron, centered horizontally
  float text_x = chevron_pos.x() - str_w / 2;
  float text_y = chevron_pos.y() + sz + 15;

  // Clamp to screen bounds
  text_x = std::clamp(text_x, 10.0f, (float)width - str_w - 10);

  // Shadow offset
  QPoint shadow_offset(2, 2);

  // Draw each line of text with shadow
  for (int i = 0; i < text_lines.size(); ++i) {
    if (!text_lines[i].isEmpty()) {
      QRect textRect(text_x, text_y + (i * str_h), str_w, str_h);

      // Draw shadow
      painter.setPen(QColor(0x0, 0x0, 0x0, (int)(200 * lead_status_alpha)));
      painter.drawText(textRect.translated(shadow_offset.x(), shadow_offset.y()),
                     Qt::AlignBottom | Qt::AlignHCenter, text_lines[i]);

      // Determine text color based on content and danger level
      QColor text_color;

      // Check if this is a distance line (contains 'm' or 'ft')
      if (text_lines[i].contains("m") || text_lines[i].contains("ft")) {
        if (d_rel < 20.0f) {
          text_color = QColor(255, 80, 80, (int)(255 * lead_status_alpha)); // Red - danger
        } else if (d_rel < 40.0f) {
          text_color = QColor(255, 200, 80, (int)(255 * lead_status_alpha)); // Yellow - caution
        } else {
          text_color = QColor(80, 255, 120, (int)(255 * lead_status_alpha)); // Green - safe
        }
      }
      // Enhanced color coding for time-to-contact
      else if (text_lines[i].contains("s") && !text_lines[i].contains("---")) {
        float ttc_val = text_lines[i].left(text_lines[i].length() - 1).toFloat();
        if (ttc_val < 3.0f) {
          text_color = QColor(255, 80, 80, (int)(255 * lead_status_alpha)); // Red - urgent
        } else if (ttc_val < 6.0f) {
          text_color = QColor(255, 200, 80, (int)(255 * lead_status_alpha)); // Yellow - caution
        } else {
          text_color = QColor(0xff, 0xff, 0xff, (int)(255 * lead_status_alpha)); // White - safe
        }
      }
      else {
        text_color = QColor(0xff, 0xff, 0xff, (int)(255 * lead_status_alpha)); // White for other lines
      }

      // Draw main text
      painter.setPen(text_color);
      painter.drawText(textRect, Qt::AlignBottom | Qt::AlignHCenter, text_lines[i]);
    }
  }

  // Reset pen
  painter.setPen(Qt::NoPen);
}

void ModelRendererBP::applySmoothPath() {
  // BluePilot path smoothing to reduce jitter (based on lead position smoothing pattern)
  // DISABLED: Return early to disable smoothing
  return;

  if (track_vertices.isEmpty()) {
    // No current path data, reset smoothing state
    path_smoothing_initialized = false;
    previous_track_vertices.clear();
    smoothed_track_vertices.clear();
    return;
  }

  if (!path_smoothing_initialized) {
    // First frame, initialize with current path
    previous_track_vertices = track_vertices;
    smoothed_track_vertices = track_vertices;
    path_smoothing_initialized = true;
    return;
  }

  // Check for dramatic path changes - reset smoothing if path changes significantly
  if (track_vertices.size() != previous_track_vertices.size() ||
      track_vertices.size() < 2 || previous_track_vertices.size() < 2) {
    // Vertex count changed or insufficient data, reset smoothing
    previous_track_vertices = track_vertices;
    smoothed_track_vertices = track_vertices;
    return;
  }

  // Check if path has changed dramatically by comparing key points
  float max_deviation = 0.0f;
  int check_points = std::min(5, track_vertices.size() / 4); // Check 5 points or 25% of path
  for (int i = 0; i < check_points; ++i) {
    int idx = i * track_vertices.size() / check_points;
    if (idx < track_vertices.size() && idx < previous_track_vertices.size()) {
      QPointF current = track_vertices[idx];
      QPointF previous = previous_track_vertices[idx];
      float deviation = std::sqrt(std::pow(current.x() - previous.x(), 2) +
                                  std::pow(current.y() - previous.y(), 2));
      max_deviation = std::max(max_deviation, deviation);
    }
  }

  // If path changed dramatically (>50 pixels), reset smoothing to prevent artifacts
  if (max_deviation > 50.0f) {
    previous_track_vertices = track_vertices;
    smoothed_track_vertices = track_vertices;
    return;
  }

  // Store current unsmoothed path as previous for next frame (CRITICAL: before smoothing)
  QPolygonF current_unsmoothed = track_vertices;

  // Apply exponential smoothing similar to BluePilot lead position smoothing
  smoothed_track_vertices.clear();

  // Handle case where vertex count has changed between frames
  int min_count = std::min(track_vertices.size(), previous_track_vertices.size());

  // Smooth the common vertices using exponential smoothing
  for (int i = 0; i < min_count; ++i) {
    QPointF current_pt = track_vertices[i];
    QPointF previous_pt = previous_track_vertices[i];

    // Exponential smoothing: smoothed = alpha * current + (1 - alpha) * previous
    QPointF smoothed_pt;
    smoothed_pt.setX(PATH_SMOOTHING_ALPHA * current_pt.x() + (1.0f - PATH_SMOOTHING_ALPHA) * previous_pt.x());
    smoothed_pt.setY(PATH_SMOOTHING_ALPHA * current_pt.y() + (1.0f - PATH_SMOOTHING_ALPHA) * previous_pt.y());

    smoothed_track_vertices.append(smoothed_pt);
  }

  // Handle additional vertices if current path is longer
  if (track_vertices.size() > min_count) {
    for (int i = min_count; i < track_vertices.size(); ++i) {
      smoothed_track_vertices.append(track_vertices[i]);
    }
  }

  // Update track_vertices with smoothed path
  track_vertices = smoothed_track_vertices;

  // Store original unsmoothed path as previous for next frame
  previous_track_vertices = current_unsmoothed;
}

// Core geometry utilities (moved from bluepilot_renderer)
bool ModelRendererBP::mapToScreen(float in_x, float in_y, float in_z, QPointF *out) {
  if (car_space_transform.isZero()) {
    static int error_counter = 0;
    if (error_counter++ % 200 == 0) {
      std::cerr << "ModelRendererBP: Transform is zero, cannot map to screen" << std::endl;
    }
    return false;
  }

  if (!std::isfinite(in_x) || !std::isfinite(in_y) || !std::isfinite(in_z)) {
    return false;
  }

  Eigen::Vector3f input(in_x, in_y, in_z);
  auto pt = car_space_transform * input;

  if (std::abs(pt.z()) < 0.001f) {
    return false;
  }

  QPointF screen_point(pt.x() / pt.z(), pt.y() / pt.z());

  if (!std::isfinite(screen_point.x()) || !std::isfinite(screen_point.y())) {
    return false;
  }

  *out = screen_point;
  return clip_region.contains(*out);
}

int ModelRendererBP::get_path_length_idx(const cereal::XYZTData::Reader &line, float path_height) {
  const auto &line_x = line.getX();
  int max_idx = 0;
  for (int i = 1; i < static_cast<int>(line_x.size()) && line_x[i] <= path_height; ++i) {
    max_idx = i;
  }
  return max_idx;
}

// Lead tracking and stop detection (moved from bluepilot_renderer)
void ModelRendererBP::updateLeadTracking(const UIState &s) {
  const SubMaster &sm = *(s.sm);

  // Validate required messages before accessing
  if (!sm.valid("radarState") || !sm.valid("modelV2")) {
    static int error_counter = 0;
    if (error_counter++ % 50 == 0) {
      std::cerr << "WARNING: ModelRendererBP radarState or modelV2 not valid in updateLeadTracking" << std::endl;
    }
    // Set all leads to inactive
    for (int i = 0; i < 2; ++i) {
      lead_state.virtual_active[i] = false;
      lead_state.stable[i] = false;
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
        lead_state.virtual_active[i] = false;
        continue;
      }

      int idx = get_path_length_idx(position, d_rel);
      if (idx < 0 || idx >= static_cast<int>(line_y.size()) || idx >= static_cast<int>(line_z.size())) {
        lead_state.virtual_active[i] = false;
        continue;
      }

      float path_y = line_y[idx];
      float path_z = line_z[idx];

      // Use the same curvature calculation as model_old.cc
      float path_curvature = (idx > 1) ? fabs(line_y[idx] - line_y[idx - 1]) : 0.0f;

      // Stricter stability requirements for visual-only detections
      int required_stability = is_radar_assisted ? 2 : 8;
      int max_stability = is_radar_assisted ? 10 : 15;

      bool should_track = true;

      if (!is_radar_assisted) {
        // For visual-only detections, apply stricter criteria
        if (d_rel < 3.0f || d_rel > 80.0f) should_track = false;
        if (lead_state.prev_status[i] && fabs(raw_yRel - lead_state.smoothed_yRel[i]) > 0.5) {
          should_track = false;
        }
        if (fabs(raw_yRel - path_y) > 2.0f) should_track = false;
      }

      // Update stability counter based on tracking decision
      if (should_track && lead_state.prev_status[i]) {
        lead_state.active_counter[i] = std::min(lead_state.active_counter[i] + 1, max_stability);
      } else if (should_track) {
        lead_state.active_counter[i] = 1;
      } else {
        lead_state.active_counter[i] = std::max(lead_state.active_counter[i] - 2, 0);
      }

      if (lead_state.active_counter[i] >= required_stability && should_track) {
        lead_state.stable[i] = true;
        lead_state.virtual_active[i] = true;
        lead_state.radar_assisted[i] = is_radar_assisted;

        if (!lead_state.prev_status[i]) {
          lead_state.smoothed_yRel[i] = raw_yRel;
        } else {
          // Use exact approach from model_old.cc for better curve handling
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
          float lateral_diff = raw_yRel - lead_state.smoothed_yRel[i];
          if (fabs(lateral_diff) > max_lateral_change) {
            // Limit lateral movement rate for stability
            raw_yRel = lead_state.smoothed_yRel[i] + ((lateral_diff > 0) ? max_lateral_change : -max_lateral_change);
          }

          // First smooth the raw radar reading
          float smoothed_raw = alpha * raw_yRel + (1.0f - alpha) * lead_state.smoothed_yRel[i];

          // Then blend with the path position using dynamic path weight
          lead_state.smoothed_yRel[i] = path_weight * path_y + (1.0f - path_weight) * smoothed_raw;
        }

        // Use exact same approach as model_old.cc - no Y sign flip, use path_offset_z
        float path_offset_z = 0.0f;
        if (sm.valid("liveCalibration")) {
          const auto &live_calib = sm["liveCalibration"].getLiveCalibration();
          const auto &height_list = live_calib.getHeight();
          if (height_list.size() > 0) {
            path_offset_z = height_list[0];
          }
        }

        QPointF current_pos;
        if (mapToScreen(d_rel, lead_state.smoothed_yRel[i], path_z + path_offset_z, &current_pos)) {
          bool reasonable_position = true;

          if (is_radar_assisted) {
            // Check if radar detection is reasonable
            QRectF screen_bounds = clip_region;
            float margin = 100.0f;
            QRectF extended_bounds = screen_bounds.adjusted(-margin, -margin, margin, margin);

            if (!extended_bounds.contains(current_pos) || fabs(lead_state.smoothed_yRel[i]) > 8.0f) {
              reasonable_position = false;
            }

            if (fabs(lead_state.smoothed_yRel[i]) > 5.0f) {
              lead_state.active_counter[i] = std::max(lead_state.active_counter[i] - 1, 0);
              if (fabs(lead_state.smoothed_yRel[i]) > 6.5f) {
                reasonable_position = false;
              }
            }
          }

          if (reasonable_position) {
            lead_state.vertices[i] = current_pos;
          } else {
            lead_state.active_counter[i] = std::max(lead_state.active_counter[i] - 2, 0);
            lead_state.virtual_active[i] = false;
          }
        } else {
          lead_state.virtual_active[i] = false;
        }
      } else {
        lead_state.virtual_active[i] = false;
        lead_state.stable[i] = false;
      }
    } else {
      // Improved decay logic to prevent rapid flickering
      if (lead_state.active_counter[i] > 0) {
        int decay_rate = lead_state.radar_assisted[i] ? 1 : 2;
        lead_state.active_counter[i] = std::max(lead_state.active_counter[i] - decay_rate, 0);

        int deactivation_threshold = lead_state.radar_assisted[i] ? 1 : 3;
        lead_state.virtual_active[i] = lead_state.active_counter[i] >= deactivation_threshold;

        if (lead_state.active_counter[i] == 0) {
          lead_state.stable[i] = false;
        }
      } else {
        lead_state.virtual_active[i] = false;
        lead_state.stable[i] = false;
      }
    }

    // Store lead data for time-to-lead calculation
    lead_state.d_rel[i] = lead_data.getDRel();
    lead_state.v_lead[i] = lead_data.getVLead();
    lead_state.v_rel[i] = lead_data.getVRel();

    lead_state.prev_status[i] = current_status;
  }
}

void ModelRendererBP::updateStopDetection(const UIState &s) {
  const SubMaster &sm = *(s.sm);

  // Get vehicle speed from carState
  float v_ego = 0.0f;
  if (sm.valid("carState")) {
    const auto car_state = sm["carState"].getCarState();
    v_ego = car_state.getVEgo();
  }

  bool vehicle_stopped = v_ego < 0.5f;

  if (vehicle_stopped && stop_state.active) {
    stop_state.active = false;
    stop_state.stability_counter = 0;
  }

  if (!s.scene.show_stop_indicator_overlay || vehicle_stopped) {
    stop_state.fade_alpha = std::max(0.0f, stop_state.fade_alpha - 0.05f);
    return;
  }

  // Validate required messages before accessing
  if (!sm.valid("modelV2") || !sm.valid("radarState") || !sm.valid("carState")) {
    static int error_counter = 0;
    if (error_counter++ % 50 == 0) {
      std::cerr << "WARNING: ModelRendererBP required messages not valid in updateStopDetection" << std::endl;
    }
    stop_state.active = false;
    stop_state.stability_counter = 0;
    stop_state.fade_alpha = std::max(0.0f, stop_state.fade_alpha - 0.1f);
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
    stop_state.active = false;
    stop_state.stability_counter = 0;
    stop_state.fade_alpha = std::max(0.0f, stop_state.fade_alpha - 0.1f);
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
    stop_state.display_distance = std::max(0.1f, stopping_distance - 4.5f);

    // Use radar data for more accurate distance when lead is present
    if (lead_one.getStatus() && lead_one.getDRel() < stopping_distance + 5.0f) {
      float radar_distance = lead_one.getDRel();
      if (radar_distance > 3.0f && radar_distance < 50.0f) {
        stopping_distance = radar_distance;
        stop_state.stability_counter = std::max(stop_state.stability_counter, 10);
        stop_state.active = true;
        stop_state.stopping_distance = stopping_distance;
      }
    }

    if (stopping_distance >= 5.0f && stopping_distance <= 50.0f) {
      // Increase stability based on braking
      if (brake_pressed || brake_value > 0.1f) {
        stop_state.stability_counter = std::min(stop_state.stability_counter + 2, 20);
      } else {
        stop_state.stability_counter = std::min(stop_state.stability_counter + 1, 20);
      }

      if (stop_state.stability_counter >= 3) {
        stop_state.active = true;

        if (stop_state.stopping_distance > 0) {
          stop_state.stopping_distance = stop_state.stopping_distance * 0.8f + stopping_distance * 0.2f;
        } else {
          stop_state.stopping_distance = stopping_distance;
        }

        float x = position_x[stop_idx];
        float y = position_y[stop_idx];
        float z = position_z[stop_idx];

        // Use path_offset_z like in model_old.cc
        QPointF screen_point;
        if (mapToScreen(x, y, z + path_offset_z, &screen_point)) {
          stop_state.last_valid_position = screen_point;
        }

        // Update smoothed size based on distance
        float target_size = 120.0f * (1.0 - std::min(0.7f, (stopping_distance - 5.0f) / 45.0f));
        stop_state.smoothed_size = stop_state.smoothed_size * 0.9f + target_size * 0.1f;
      }
    } else {
      stop_state.stability_counter = std::max(0, stop_state.stability_counter - 1);

      // Keep sign visible longer if braking
      if ((brake_pressed || brake_value > 0.1f) && stop_state.active) {
        stop_state.stability_counter = std::max(stop_state.stability_counter, 5);
      }

      if (stop_state.stability_counter <= 0) {
        stop_state.active = false;
      }
    }
  } else {
    stop_state.stability_counter = std::max(0, stop_state.stability_counter - 1);
    if (stop_state.stability_counter <= 0) {
      stop_state.active = false;
    }
  }

  if (stop_state.active && stop_state.fade_alpha < 1.0f) {
    stop_state.fade_alpha = std::min(1.0f, stop_state.fade_alpha + 0.1f);
  } else if (!stop_state.active && stop_state.fade_alpha > 0.0f) {
    stop_state.fade_alpha = std::max(0.0f, stop_state.fade_alpha - 0.05f);
  }
}

// Drawing utilities (moved from bluepilot_renderer)
void ModelRendererBP::drawLeftTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot) {
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

void ModelRendererBP::drawRightTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot) {
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

void ModelRendererBP::drawColoredText(QPainter &painter, int x, int y, const QString &text, QColor color) {
  QRect real_rect = painter.fontMetrics().boundingRect(text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});
  painter.setPen(color);
  painter.drawText(real_rect.x(), real_rect.bottom(), text);
}

