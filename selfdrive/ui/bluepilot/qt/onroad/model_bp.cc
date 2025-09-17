#include "selfdrive/ui/bluepilot/qt/onroad/model_bp.h"
#include "selfdrive/ui/qt/util.h"
#include "common/params.h"
#include "common/util.h"
#include "selfdrive/ui/ui.h"
#include <QGraphicsBlurEffect>
#include <QPainterPathStroker>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <iostream>

// ================ Constants & Static State ================
static constexpr float BLINDSPOT_WIDTH = 1.0f;

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

// ================ Constructor ================
#ifdef SUNNYPILOT
ModelRendererBP::ModelRendererBP() : ModelRendererSP() {}
#else
ModelRendererBP::ModelRendererBP() : ModelRenderer() {}
#endif

// ================ Main Draw Method ================
void ModelRendererBP::draw(QPainter &painter, const QRect &surface_rect) {
  auto *s = uiState();
  auto &sm = *(s->sm);
  
  // Validate required data
  if (sm.rcv_frame("liveCalibration") < s->scene.started_frame ||
      sm.rcv_frame("modelV2") < s->scene.started_frame) {
    return;
  }

  // Initialize common state
  clip_region = surface_rect.adjusted(-CLIP_MARGIN, -CLIP_MARGIN, CLIP_MARGIN, CLIP_MARGIN);
  experimental_mode = sm["selfdriveState"].getSelfdriveState().getExperimentalMode();
  longitudinal_control = sm["carParams"].getCarParams().getOpenpilotLongitudinalControl();
  path_offset_z = sm["liveCalibration"].getLiveCalibration().getHeight()[0];
  
  const auto &model = sm["modelV2"].getModelV2();
  const auto &radar_state = sm["radarState"].getRadarState();
  const auto &lead_one = radar_state.getLeadOne();

  // Check transform validity
  if (car_space_transform.isZero()) {
    static int warn_count = 0;
    if (warn_count++ < 5) {
      std::cerr << "WARNING: BluePilot transform is zero - overlays disabled" << std::endl;
    }
    return;
  }

  // Update model and BluePilot state
#ifdef SUNNYPILOT
  ModelRenderer::update_model(model, lead_one);
#else
  update_model(model, lead_one);
#endif
  updateBluePilotState(model);
  applySmoothPath(); // Currently disabled but kept for compatibility

  painter.save();

  // Check if enhanced UI is enabled once
  bool enhanced_ui = Params().getBool("BluepilotShowEnhancedOnroadUI");
  
  // Draw lane lines (enhanced or stock)
  if (enhanced_ui) {
    drawEnhancedLaneLines(painter);
  } else {
    // Use exact stock rendering from ModelRenderer
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < std::size(lane_line_vertices); ++i) {
      painter.setBrush(QColor::fromRgbF(1.0, 1.0, 1.0, std::clamp<float>(lane_line_probs[i], 0.0, 0.7)));
      painter.drawPolygon(lane_line_vertices[i]);
    }
    for (int i = 0; i < std::size(road_edge_vertices); ++i) {
      painter.setBrush(QColor::fromRgbF(1.0, 0, 0, std::clamp<float>(1.0 - road_edge_stds[i], 0.0, 1.0)));
      painter.drawPolygon(road_edge_vertices[i]);
    }
  }

  // Draw blindspot overlays (always enhanced when enabled)
  drawBlindspotOverlays(painter);
  
  // Draw path (enhanced or stock)
  if (enhanced_ui) {
    drawEnhancedPath(painter, model, surface_rect);
  } else {
    ModelRenderer::drawPath(painter, model, surface_rect);
  }

  // Draw lead status if not suppressed by radar overlay
  if (!s->scene.show_bp_radar_overlay) {
    drawLeadStatus(painter, surface_rect.height(), surface_rect.width());
    
    // Draw leads
    if (longitudinal_control && sm.alive("radarState")) {
      update_leads(radar_state, model.getPosition());
      const auto &lead_two = radar_state.getLeadTwo();
      
      if (lead_one.getStatus()) {
        drawLead(painter, lead_one, lead_vertices[0], surface_rect);
      }
      if (lead_two.getStatus() && (std::abs(lead_one.getDRel() - lead_two.getDRel()) > 3.0)) {
        drawLead(painter, lead_two, lead_vertices[1], surface_rect);
      }
    }
  }

  painter.restore();
}

// ================ Model Update Methods ================
#ifndef SUNNYPILOT
void ModelRendererBP::drawPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, const QRect &surface_rect) {
  // Check if enhanced UI is enabled
  bool enhanced_ui = Params().getBool("BluepilotShowEnhancedOnroadUI");
  
  if (enhanced_ui) {
    drawEnhancedPath(painter, model, surface_rect);
  } else {
    // Use base class stock path rendering
    ModelRenderer::drawPath(painter, model, surface_rect.height());
  }
}

void ModelRendererBP::update_model(const cereal::ModelDataV2::Reader &model, 
                                   const cereal::RadarState::LeadData::Reader &lead) {
  ModelRenderer::update_model(model, lead);
  updateBluePilotState(model);
}
#endif

void ModelRendererBP::updateBluePilotState(const cereal::ModelDataV2::Reader &model) {
  // Update lane lines based on enhanced UI setting
  bool enhanced_ui = Params().getBool("BluepilotShowEnhancedOnroadUI");
  updateLaneLines(model, enhanced_ui);
  
  // Create blindspot polygons
  createBlindspotPolygons(model);
  
  // Mark glow cache for update
  glow_cache.needs_update = true;
}

void ModelRendererBP::updateLaneLines(const cereal::ModelDataV2::Reader &model, bool enhanced) {
  const auto &model_position = model.getPosition();
  float max_distance = std::clamp(*(model_position.getX().end() - 1), MIN_DRAW_DISTANCE, MAX_DRAW_DISTANCE);
  
  const auto &lane_lines = model.getLaneLines();
  const auto &line_probs = model.getLaneLineProbs();
  int max_idx = get_path_length_idx(lane_lines[0], max_distance);
  
  // Use wider width (0.05) for enhanced UI, stock width (0.025) otherwise
  float line_width_multiplier = enhanced ? 0.05f : 0.025f;
  
  for (int i = 0; i < std::size(lane_line_vertices); i++) {
    lane_line_probs[i] = line_probs[i];
    if (enhanced) {
      mapLineToPolygonEnhanced(lane_lines[i], line_width_multiplier * lane_line_probs[i], 0, 
                               &lane_line_vertices[i], max_idx);
    } else {
      mapLineToPolygon(lane_lines[i], line_width_multiplier * lane_line_probs[i], 0, 
                      &lane_line_vertices[i], max_idx);
    }
  }
  
  // Update road edges
  const auto &road_edges = model.getRoadEdges();
  const auto &edge_stds = model.getRoadEdgeStds();
  for (int i = 0; i < std::size(road_edge_vertices); i++) {
    road_edge_stds[i] = edge_stds[i];
    if (enhanced) {
      mapLineToPolygonEnhanced(road_edges[i], line_width_multiplier, 0, 
                               &road_edge_vertices[i], max_idx);
    } else {
      mapLineToPolygon(road_edges[i], line_width_multiplier, 0, 
                      &road_edge_vertices[i], max_idx);
    }
  }
}

// ================ Enhanced Drawing Methods ================
void ModelRendererBP::drawEnhancedLaneLines(QPainter &painter) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  
  // Draw wide lane line polygons with enhanced visibility
  for (int i = 0; i < std::size(lane_line_vertices); ++i) {
    if (lane_line_vertices[i].isEmpty() || lane_line_probs[i] < 0.4f) continue;
    
    float base_alpha = std::clamp<float>(lane_line_probs[i] * 0.8f, 0.3f, 0.8f);
    bool is_current_lane = (i == 1 || i == 2);
    if (!is_current_lane) base_alpha *= 0.4f; // Dim outer lanes
    
    painter.setBrush(QColor::fromRgbF(1.0, 1.0, 1.0, base_alpha));
    painter.drawPolygon(lane_line_vertices[i]);
  }
  
  // Add horizontal glow effects for enhanced visibility
  drawLaneGlowEffects(painter);
  
  // Draw road edges with enhanced red warning
  for (int i = 0; i < std::size(road_edge_vertices); ++i) {
    if (road_edge_vertices[i].isEmpty()) continue;
    
    float edge_alpha = std::clamp<float>(1.0 - road_edge_stds[i], 0.0, 1.0);
    painter.setBrush(QColor::fromRgbF(1.0, 0.0, 0.0, edge_alpha * 0.6f));
    painter.drawPolygon(road_edge_vertices[i]);
  }
  
  // Add road edge glow effects
  drawRoadEdgeGlowEffects(painter);
  
  painter.restore();
}

void ModelRendererBP::drawLaneGlowEffects(QPainter &painter) {
  QPainterPathStroker stroker;
  stroker.setCapStyle(Qt::RoundCap);
  stroker.setJoinStyle(Qt::RoundJoin);
  
  // Cache glow paths if needed
  if (glow_cache.needs_update) {
    for (int i = 0; i < std::size(lane_line_vertices); ++i) {
      if (!lane_line_vertices[i].isEmpty() && lane_line_probs[i] >= 0.4f) {
        QPainterPath path;
        path.addPolygon(lane_line_vertices[i]);
        glow_cache.lane_glow_paths[i] = path;
      } else {
        glow_cache.lane_glow_paths[i] = QPainterPath();
      }
    }
    glow_cache.needs_update = false;
  }
  
  // Draw cached glow layers
  for (int i = 0; i < std::size(glow_cache.lane_glow_paths); ++i) {
    if (glow_cache.lane_glow_paths[i].isEmpty()) continue;
    
    float base_alpha = std::clamp<float>(lane_line_probs[i] * 0.8f, 0.3f, 0.8f);
    bool is_current_lane = (i == 1 || i == 2);
    if (!is_current_lane) base_alpha *= 0.4f;
    
    // Three-layer glow for smooth falloff
    const float glow_widths[] = {24.0f, 16.0f, 8.0f};
    const float glow_alphas[] = {0.08f, 0.15f, 0.3f};
    
    for (int layer = 0; layer < 3; ++layer) {
      stroker.setWidth(glow_widths[layer]);
      QPainterPath glow = stroker.createStroke(glow_cache.lane_glow_paths[i]);
      painter.setBrush(QColor::fromRgbF(1.0, 1.0, 1.0, base_alpha * glow_alphas[layer]));
      painter.drawPath(glow);
    }
  }
}

void ModelRendererBP::drawRoadEdgeGlowEffects(QPainter &painter) {
  QPainterPathStroker stroker;
  stroker.setCapStyle(Qt::RoundCap);
  stroker.setJoinStyle(Qt::RoundJoin);
  
  for (int i = 0; i < std::size(road_edge_vertices); ++i) {
    if (road_edge_vertices[i].isEmpty()) continue;
    
    float edge_alpha = std::clamp<float>(1.0 - road_edge_stds[i], 0.0, 1.0);
    if (edge_alpha < 0.3f) continue;
    
    QPainterPath edge_path;
    edge_path.addPolygon(road_edge_vertices[i]);
    
    // Red warning glow with three layers
    const float glow_widths[] = {36.0f, 24.0f, 12.0f};
    const float glow_alphas[] = {0.05f, 0.1f, 0.2f};
    
    for (int layer = 0; layer < 3; ++layer) {
      stroker.setWidth(glow_widths[layer]);
      QPainterPath glow = stroker.createStroke(edge_path);
      painter.setBrush(QColor::fromRgbF(1.0, 0.0, 0.0, edge_alpha * glow_alphas[layer]));
      painter.drawPath(glow);
    }
  }
}

void ModelRendererBP::drawEnhancedPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, 
                                       const QRect &surface_rect) {
  if (track_vertices.isEmpty()) return;
  
  // Build gradient (stock or custom color)
  QLinearGradient bg(0, surface_rect.height(), 0, 0);
  QColor border_color;
  
  QString pathColor = QString::fromStdString(Params().get("CustomModelPathColor"));
  bool hasCustomPath = !pathColor.isEmpty() && pathColor != "Stock";
  
  if (hasCustomPath) {
    applyCustomPathGradient(bg, border_color, pathColor, surface_rect);
  } else {
    applyStockPathGradient(bg, border_color, model, surface_rect);
  }
  
  // Draw enhanced path with glow and border
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  
  // Multi-layer glow effect - wider spread with subtle transparency
  const float glow_widths[] = {40.0f, 28.0f, 18.0f, 10.0f, 4.0f};
  const float glow_alphas[] = {0.03f, 0.06f, 0.10f, 0.18f, 0.30f};
  
  for (int i = 0; i < 5; ++i) {
    border_color.setAlphaF(glow_alphas[i]);
    painter.setPen(QPen(border_color, glow_widths[i], Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(track_vertices);
  }
  
  // Draw filled path
  painter.setPen(Qt::NoPen);
  painter.setBrush(bg);
  painter.drawPolygon(track_vertices);
  
  painter.restore();
}

void ModelRendererBP::applyCustomPathGradient(QLinearGradient &bg, QColor &border_color, 
                                              const QString &pathColor, const QRect &surface_rect) {
  auto *s = uiState();
  auto &sm = *(s->sm);
  
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
    float hue = fmod((v_ego * 20.0) + (time_offset * 100.0), 360.0);
    border_color = QColor::fromHslF(hue / 360.0, 1.0f, 0.6f, 1.0f);
  } else {
    // Define color presets
    struct ColorPreset {
      QColor stops[3];
      QColor border;
    };
    
    std::map<QString, ColorPreset> presets = {
      {"Blue", {{QColor(0, 102, 204, 102), QColor(51, 153, 255, 89), QColor(51, 153, 255, 0)}, 
                QColor(51, 153, 255, 255)}},
      {"Green", {{QColor(0, 204, 102, 102), QColor(51, 255, 153, 89), QColor(51, 255, 153, 0)},
                 QColor(51, 255, 153, 255)}},
      {"Purple", {{QColor(153, 51, 204, 102), QColor(178, 102, 255, 89), QColor(178, 102, 255, 0)},
                  QColor(178, 102, 255, 255)}},
      {"Orange", {{QColor(255, 128, 0, 102), QColor(255, 153, 51, 89), QColor(255, 153, 51, 0)},
                  QColor(255, 153, 51, 255)}},
      {"Red", {{QColor(204, 0, 0, 102), QColor(255, 51, 51, 89), QColor(255, 51, 51, 0)},
               QColor(255, 51, 51, 255)}},
      {"Cyan", {{QColor(0, 204, 204, 102), QColor(51, 255, 255, 89), QColor(51, 255, 255, 0)},
                QColor(51, 255, 255, 255)}},
      {"Yellow", {{QColor(204, 204, 0, 102), QColor(255, 255, 51, 89), QColor(255, 255, 51, 0)},
                  QColor(255, 255, 51, 255)}}
    };
    
    if (presets.find(pathColor) != presets.end()) {
      const auto &preset = presets[pathColor];
      bg.setColorAt(0.0, preset.stops[0]);
      bg.setColorAt(0.5, preset.stops[1]);
      bg.setColorAt(1.0, preset.stops[2]);
      border_color = preset.border;
    }
  }
}

void ModelRendererBP::applyStockPathGradient(QLinearGradient &bg, QColor &border_color,
                                             const cereal::ModelDataV2::Reader &model, 
                                             const QRect &surface_rect) {
  if (experimental_mode) {
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
    border_color = QColor::fromHslF(90.0 / 360., 0.94, 0.51, 1.0);
  } else {
    updatePathGradient(bg);
    border_color = getCurrentPathBorderColor();
  }
}

// ================ Blindspot Methods ================
void ModelRendererBP::createBlindspotPolygons(const cereal::ModelDataV2::Reader &model) {
  const auto &model_position = model.getPosition();
  const auto &lane_lines = model.getLaneLines();
  
  if (car_space_transform.isZero() || lane_lines.size() < 4) {
    left_blindspot_vertices.clear();
    right_blindspot_vertices.clear();
    return;
  }
  
  float max_distance = std::clamp(*(model_position.getX().end() - 1), MIN_DRAW_DISTANCE, MAX_DRAW_DISTANCE);
  int max_idx = std::min(get_path_length_idx(lane_lines[0], max_distance), 50);
  const int MAX_VERTICES = 40;
  
  const auto &left_lane = lane_lines[1];
  const auto &right_lane = lane_lines[2];
  
  if (left_lane.getX().size() == 0 || right_lane.getX().size() == 0) {
    left_blindspot_vertices.clear();
    right_blindspot_vertices.clear();
    return;
  }
  
  // Build left blindspot polygon
  left_blindspot_vertices.clear();
  int vertex_count = 0;
  
  for (int i = 0; i <= max_idx && i < static_cast<int>(left_lane.getX().size()) && vertex_count < MAX_VERTICES; i++) {
    QPointF offset_pt;
    if (mapToScreen(left_lane.getX()[i], left_lane.getY()[i] - BLINDSPOT_WIDTH, left_lane.getZ()[i], &offset_pt)) {
      left_blindspot_vertices.append(offset_pt);
      vertex_count++;
    }
  }
  
  for (int i = max_idx; i >= 0 && i < static_cast<int>(left_lane.getX().size()) && vertex_count < MAX_VERTICES; i--) {
    QPointF lane_pt;
    if (mapToScreen(left_lane.getX()[i], left_lane.getY()[i], left_lane.getZ()[i], &lane_pt)) {
      left_blindspot_vertices.append(lane_pt);
      vertex_count++;
    }
  }
  
  // Build right blindspot polygon
  right_blindspot_vertices.clear();
  vertex_count = 0;
  
  for (int i = 0; i <= max_idx && i < static_cast<int>(right_lane.getX().size()) && vertex_count < MAX_VERTICES; i++) {
    QPointF lane_pt;
    if (mapToScreen(right_lane.getX()[i], right_lane.getY()[i], right_lane.getZ()[i], &lane_pt)) {
      right_blindspot_vertices.append(lane_pt);
      vertex_count++;
    }
  }
  
  for (int i = max_idx; i >= 0 && i < static_cast<int>(right_lane.getX().size()) && vertex_count < MAX_VERTICES; i--) {
    QPointF offset_pt;
    if (mapToScreen(right_lane.getX()[i], right_lane.getY()[i] + BLINDSPOT_WIDTH, right_lane.getZ()[i], &offset_pt)) {
      right_blindspot_vertices.append(offset_pt);
      vertex_count++;
    }
  }
  
  blindspot_state.gradients_dirty = true;
}

void ModelRendererBP::drawBlindspotOverlays(QPainter &painter) {
  auto *s = uiState();
  auto &sm = *(s->sm);
  
  if (!Params().getBool("BlindSpot")) return;
  
  const auto car_state = sm["carState"].getCarState();
  blindspot_state.left_blindspot = car_state.getLeftBlindspot();
  blindspot_state.right_blindspot = car_state.getRightBlindspot();
  
  // Update animation
  blindspot_state.blink_counter = (blindspot_state.blink_counter + 1) % 40;
  float pulse = 0.1 * sin(blindspot_state.blink_counter * M_PI / 20) + 0.25;
  blindspot_state.opacity = pulse;
  
  painter.setRenderHint(QPainter::Antialiasing, true);
  
  // Draw left blindspot
  if (blindspot_state.left_blindspot && !left_blindspot_vertices.isEmpty()) {
    QRectF bounds = left_blindspot_vertices.boundingRect();
    if (bounds != blindspot_state.last_left_bounds || blindspot_state.gradients_dirty) {
      blindspot_state.cached_gradient_left = QLinearGradient(bounds.center().x(), bounds.top(),
                                                             bounds.center().x(), bounds.bottom());
      blindspot_state.cached_gradient_left.setColorAt(0.0, QColor::fromRgbF(1.0, 0.0, 0.0, 0.0));
      blindspot_state.cached_gradient_left.setColorAt(0.2, QColor::fromRgbF(1.0, 0.0, 0.0, 0.2));
      blindspot_state.cached_gradient_left.setColorAt(0.4, QColor::fromRgbF(1.0, 0.0, 0.0, blindspot_state.opacity));
      blindspot_state.cached_gradient_left.setColorAt(0.7, QColor::fromRgbF(1.0, 0.0, 0.0, 0.9));
      blindspot_state.cached_gradient_left.setColorAt(1.0, QColor::fromRgbF(1.0, 0.0, 0.0, 1.0));
      blindspot_state.last_left_bounds = bounds;
    }
    painter.setBrush(blindspot_state.cached_gradient_left);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(left_blindspot_vertices);
  }
  
  // Draw right blindspot
  if (blindspot_state.right_blindspot && !right_blindspot_vertices.isEmpty()) {
    QRectF bounds = right_blindspot_vertices.boundingRect();
    if (bounds != blindspot_state.last_right_bounds || blindspot_state.gradients_dirty) {
      blindspot_state.cached_gradient_right = QLinearGradient(bounds.center().x(), bounds.top(),
                                                              bounds.center().x(), bounds.bottom());
      blindspot_state.cached_gradient_right.setColorAt(0.0, QColor::fromRgbF(1.0, 0.0, 0.0, 0.0));
      blindspot_state.cached_gradient_right.setColorAt(0.2, QColor::fromRgbF(1.0, 0.0, 0.0, 0.2));
      blindspot_state.cached_gradient_right.setColorAt(0.4, QColor::fromRgbF(1.0, 0.0, 0.0, blindspot_state.opacity));
      blindspot_state.cached_gradient_right.setColorAt(0.7, QColor::fromRgbF(1.0, 0.0, 0.0, 0.9));
      blindspot_state.cached_gradient_right.setColorAt(1.0, QColor::fromRgbF(1.0, 0.0, 0.0, 1.0));
      blindspot_state.last_right_bounds = bounds;
    }
    painter.setBrush(blindspot_state.cached_gradient_right);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(right_blindspot_vertices);
  }
  
  blindspot_state.gradients_dirty = false;
}

// ================ Lead Display Methods ================
void ModelRendererBP::drawLeadStatus(QPainter &painter, int height, int width) {
  auto *s = uiState();
  auto &sm = *(s->sm);
  
  if (s->scene.show_bp_radar_overlay || !sm.alive("radarState")) return;
  
  const auto &radar_state = sm["radarState"].getRadarState();
  const auto &lead_one = radar_state.getLeadOne();
  const auto &lead_two = radar_state.getLeadTwo();
  
  bool has_lead_one = lead_one.getStatus();
  bool has_lead_two = lead_two.getStatus();
  
  if (!has_lead_one && !has_lead_two) {
    lead_status_alpha = std::max(0.0f, lead_status_alpha - 0.05f);
    if (lead_status_alpha <= 0.0f) return;
  } else {
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
  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 2.35;
  
  QFont content_font = painter.font();
  content_font.setPixelSize(42);
  content_font.setBold(true);
  painter.setFont(content_font);
  
  bool is_metric = s->scene.is_metric;
  QStringList text_lines;
  
  const int chevron_all = 4;
  QStringList chevron_text[3];
  
  // Distance
  if (chevron_data == 1 || chevron_data == chevron_all) {
    float val = std::max(0.0f, d_rel);
    if (!is_metric) val *= 3.28084f;
    chevron_text[0].append(QString::number(val, 'f', 0) + " " + (is_metric ? "m" : "ft"));
  }
  
  // Velocity
  if (chevron_data == 2 || chevron_data == chevron_all) {
    int pos = (chevron_data == 2) ? 0 : 1;
    float val = std::max(0.0f, (v_rel + v_ego) * (is_metric ? static_cast<float>(MS_TO_KPH) : static_cast<float>(MS_TO_MPH)));
    chevron_text[pos].append(QString::number(val, 'f', 0) + " " + (is_metric ? "km/h" : "mph"));
  }
  
  // Time to contact
  if (chevron_data == 3 || chevron_data == chevron_all) {
    int pos = (chevron_data == 3) ? 0 : 2;
    float val = (d_rel > 0 && v_ego > 0) ? std::max(0.0f, d_rel / v_ego) : 0.0f;
    QString ttc = (val > 0 && val < 200) ? QString::number(val, 'f', 1) + "s" : "---";
    chevron_text[pos].append(ttc);
  }
  
  for (int i = 0; i < 3; ++i) {
    if (!chevron_text[i].isEmpty()) text_lines.append(chevron_text[i]);
  }
  
  if (text_lines.isEmpty()) return;
  
  float str_w = 150;
  float str_h = 45;
  float text_x = std::clamp(static_cast<float>(chevron_pos.x()) - str_w / 2, 10.0f, (float)width - str_w - 10);
  float text_y = chevron_pos.y() + sz + 15;
  
  for (int i = 0; i < text_lines.size(); ++i) {
    QRect textRect(text_x, text_y + (i * str_h), str_w, str_h);
    
    // Shadow
    painter.setPen(QColor(0, 0, 0, (int)(200 * lead_status_alpha)));
    painter.drawText(textRect.translated(2, 2), Qt::AlignBottom | Qt::AlignHCenter, text_lines[i]);
    
    // Determine color based on content
    QColor text_color = QColor(0xff, 0xff, 0xff, (int)(255 * lead_status_alpha));
    
    if (text_lines[i].contains("m") || text_lines[i].contains("ft")) {
      if (d_rel < 20.0f) {
        text_color = QColor(255, 80, 80, (int)(255 * lead_status_alpha));
      } else if (d_rel < 40.0f) {
        text_color = QColor(255, 200, 80, (int)(255 * lead_status_alpha));
      } else {
        text_color = QColor(80, 255, 120, (int)(255 * lead_status_alpha));
      }
    } else if (text_lines[i].contains("s") && !text_lines[i].contains("---")) {
      float ttc_val = text_lines[i].left(text_lines[i].length() - 1).toFloat();
      if (ttc_val < 3.0f) {
        text_color = QColor(255, 80, 80, (int)(255 * lead_status_alpha));
      } else if (ttc_val < 6.0f) {
        text_color = QColor(255, 200, 80, (int)(255 * lead_status_alpha));
      }
    }
    
    painter.setPen(text_color);
    painter.drawText(textRect, Qt::AlignBottom | Qt::AlignHCenter, text_lines[i]);
  }
  
  painter.setPen(Qt::NoPen);
}

// ================ Utility Methods ================
int ModelRendererBP::get_path_length_idx(const cereal::XYZTData::Reader &line, float path_height) {
  const auto &line_x = line.getX();
  int max_idx = 0;
  for (int i = 1; i < static_cast<int>(line_x.size()) && line_x[i] <= path_height; ++i) {
    max_idx = i;
  }
  return max_idx;
}

void ModelRendererBP::mapLineToPolygonEnhanced(const cereal::XYZTData::Reader &line, float width, float z_off,
                                               QPolygonF *pvd, int max_idx) {
  const auto line_x = line.getX(), line_y = line.getY(), line_z = line.getZ();
  pvd->clear();
  
  // Collect raw points first
  QVector<QPointF> left_points, right_points;
  
  for (int i = 0; i <= max_idx; i++) {
    if (line_x[i] < 0) continue;
    
    QPointF l, r;
    bool l_ok = mapToScreen(line_x[i], line_y[i] - width, line_z[i] + z_off, &l);
    bool r_ok = mapToScreen(line_x[i], line_y[i] + width, line_z[i] + z_off, &r);
    
    if (l_ok && r_ok) {
      left_points.append(l);
      right_points.append(r);
    }
  }
  
  // Apply smoothing for curves - add interpolated points where needed
  for (int i = 0; i < left_points.size(); i++) {
    if (i > 0) {
      // Check angle change between segments
      QPointF prev_left = left_points[i-1];
      QPointF curr_left = left_points[i];
      QPointF prev_right = right_points[i-1];
      QPointF curr_right = right_points[i];
      
      float dist = QLineF(prev_left, curr_left).length();
      
      // Add interpolated points for smoother curves (if segment is long enough)
      if (dist > 15.0f) {
        int num_interp = std::min(3, static_cast<int>(dist / 10.0f));
        for (int j = 1; j < num_interp; j++) {
          float t = j / float(num_interp);
          // Cubic interpolation for smoother curves
          float t2 = t * t;
          float t3 = t2 * t;
          float blend = t3 * (t3 - 2.0f * t2 + t) + t2 * (3.0f - 2.0f * t);
          
          QPointF interp_left(
            prev_left.x() + (curr_left.x() - prev_left.x()) * blend,
            prev_left.y() + (curr_left.y() - prev_left.y()) * blend
          );
          QPointF interp_right(
            prev_right.x() + (curr_right.x() - prev_right.x()) * blend,
            prev_right.y() + (curr_right.y() - prev_right.y()) * blend
          );
          
          if (pvd->isEmpty() || interp_left.y() <= pvd->back().y()) {
            pvd->push_back(interp_left);
            pvd->push_front(interp_right);
          }
        }
      }
    }
    
    // Add the actual point
    if (pvd->isEmpty() || left_points[i].y() <= pvd->back().y()) {
      pvd->push_back(left_points[i]);
      pvd->push_front(right_points[i]);
    }
  }
}

QColor ModelRendererBP::getCurrentPathBorderColor() {
  static const QColor throttle_colors[] = {
      QColor::fromHslF(148. / 360., 0.94, 0.51, 1.0),
      QColor::fromHslF(112. / 360., 1.0, 0.68, 1.0),
      QColor::fromHslF(112. / 360., 1.0, 0.68, 1.0)};
  
  static const QColor no_throttle_colors[] = {
      QColor::fromHslF(148. / 360., 0.0, 0.95, 1.0),
      QColor::fromHslF(112. / 360., 0.0, 0.95, 1.0),
      QColor::fromHslF(112. / 360., 0.0, 0.95, 1.0)};
  
  auto *s = uiState();
  auto &sm = *(s->sm);
  bool allow_throttle = sm["longitudinalPlan"].getLongitudinalPlan().getAllowThrottle() || !longitudinal_control;
  
  if (allow_throttle) {
    return blend_factor < 1.0f ?
           blendColors(no_throttle_colors[0], throttle_colors[0], blend_factor) :
           throttle_colors[0];
  } else {
    return blend_factor < 1.0f ?
           blendColors(throttle_colors[0], no_throttle_colors[0], blend_factor) :
           no_throttle_colors[0];
  }
}

// ================ Lead Tracking (from bluepilot_renderer) ================
void ModelRendererBP::updateLeadTracking(const UIState &s) {
  const SubMaster &sm = *(s.sm);
  
  if (!sm.valid("radarState") || !sm.valid("modelV2")) {
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
      float path_curvature = (idx > 1) ? fabs(line_y[idx] - line_y[idx - 1]) : 0.0f;
      
      int required_stability = is_radar_assisted ? 2 : 8;
      int max_stability = is_radar_assisted ? 10 : 15;
      
      bool should_track = true;
      
      if (!is_radar_assisted) {
        if (d_rel < 3.0f || d_rel > 80.0f) should_track = false;
        if (lead_state.prev_status[i] && fabs(raw_yRel - lead_state.smoothed_yRel[i]) > 0.5) {
          should_track = false;
        }
        if (fabs(raw_yRel - path_y) > 2.0f) should_track = false;
      }
      
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
          float path_weight = std::min(0.6f + path_curvature * 5.0f, 0.9f);
          float alpha = is_radar_assisted ?
                        0.05f + 0.15f * (d_rel / 25.0f) :
                        0.025f + 0.125f * (d_rel / 25.0f);
          alpha = std::clamp(alpha, 0.025f, 0.25f);
          
          float max_lateral_change = (d_rel < 8.0) ? 0.08f : 0.2f;
          float lateral_diff = raw_yRel - lead_state.smoothed_yRel[i];
          if (fabs(lateral_diff) > max_lateral_change) {
            raw_yRel = lead_state.smoothed_yRel[i] + ((lateral_diff > 0) ? max_lateral_change : -max_lateral_change);
          }
          
          float smoothed_raw = alpha * raw_yRel + (1.0f - alpha) * lead_state.smoothed_yRel[i];
          lead_state.smoothed_yRel[i] = path_weight * path_y + (1.0f - path_weight) * smoothed_raw;
        }
        
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
    
    lead_state.d_rel[i] = lead_data.getDRel();
    lead_state.v_lead[i] = lead_data.getVLead();
    lead_state.v_rel[i] = lead_data.getVRel();
    lead_state.prev_status[i] = current_status;
  }
}

void ModelRendererBP::updateStopDetection(const UIState &s) {
  const SubMaster &sm = *(s.sm);
  
  float v_ego = 0.0f;
  if (sm.valid("carState")) {
    v_ego = sm["carState"].getCarState().getVEgo();
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
  
  if (!sm.valid("modelV2") || !sm.valid("radarState") || !sm.valid("carState")) {
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
  
  bool data_valid = (vel_size >= 2 && vel_size <= 1000 &&
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
      if (brake_pressed || brake_value > 0.1f) {
        stop_state.stability_counter = std::min(stop_state.stability_counter + 2, 20);
      } else {
        stop_state.stability_counter = std::min(stop_state.stability_counter + 1, 20);
      }
      
      if (stop_state.stability_counter >= 3) {
        stop_state.active = true;
        
        stop_state.stopping_distance = stop_state.stopping_distance > 0 ?
                                       stop_state.stopping_distance * 0.8f + stopping_distance * 0.2f :
                                       stopping_distance;
        
        float x = position_x[stop_idx];
        float y = position_y[stop_idx];
        float z = position_z[stop_idx];
        
        QPointF screen_point;
        if (mapToScreen(x, y, z + path_offset_z, &screen_point)) {
          stop_state.last_valid_position = screen_point;
        }
        
        float target_size = 120.0f * (1.0 - std::min(0.7f, (stopping_distance - 5.0f) / 45.0f));
        stop_state.smoothed_size = stop_state.smoothed_size * 0.9f + target_size * 0.1f;
      }
    } else {
      stop_state.stability_counter = std::max(0, stop_state.stability_counter - 1);
      
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

// ================ Drawing Utilities (from bluepilot_renderer) ================
void ModelRendererBP::drawLeftTurnSignal(QPainter &painter, int x, int y, int size, int state, bool blindspot) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  
  QColor circle_color = blindspot ? 
    (state ? QColor(204, 0, 1) : QColor(164, 0, 1)) :
    (state ? QColor(30, 215, 96) : QColor(22, 156, 69));
  QColor arrow_color = blindspot ?
    (state ? QColor(255, 255, 255) : QColor(72, 1, 1)) :
    (state ? QColor(255, 255, 255) : QColor(9, 56, 27));
  
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
  
  QColor circle_color = blindspot ?
    (state ? QColor(204, 0, 1) : QColor(164, 0, 1)) :
    (state ? QColor(30, 215, 96) : QColor(22, 156, 69));
  QColor arrow_color = blindspot ?
    (state ? QColor(255, 255, 255) : QColor(72, 1, 1)) :
    (state ? QColor(255, 255, 255) : QColor(9, 56, 27));
  
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

// Path smoothing - disabled to prevent swaying
void ModelRendererBP::applySmoothPath() {
  // Apply lateral smoothing to reduce swaying
  if (track_vertices.size() < 4) return;
  
  int n = track_vertices.size();
  QPolygonF smoothed;
  
  // Smooth Y-axis more aggressively (lateral movement)
  for (int i = 0; i < n; i++) {
    QPointF pt = track_vertices[i];
    
    if (i > 1 && i < n - 1) {
      float y_smooth = 0.0f;
      float weight_sum = 0.0f;
      
      // Gaussian weights for nearby points
      for (int j = -2; j <= 2; j++) {
        int idx = i + j;
        if (idx >= 0 && idx < n) {
          float weight = exp(-0.5f * j * j);
          y_smooth += track_vertices[idx].y() * weight;
          weight_sum += weight;
        }
      }
      
      if (weight_sum > 0) {
        pt.setY(y_smooth / weight_sum);
      }
    }
    
    smoothed.append(pt);
  }
  
  // Apply temporal damping to reduce oscillation
  if (!previous_track_vertices.isEmpty() && previous_track_vertices.size() == smoothed.size()) {
    for (int i = 0; i < smoothed.size(); i++) {
      float damping = 0.3f;  // Higher = more damping
      float y_diff = smoothed[i].y() - previous_track_vertices[i].y();
      smoothed[i].setY(previous_track_vertices[i].y() + y_diff * (1.0f - damping));
    }
  }
  
  previous_track_vertices = smoothed;
  track_vertices = smoothed;
}
