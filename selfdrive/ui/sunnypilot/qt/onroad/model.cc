/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/sunnypilot/qt/onroad/model.h"
#include "common/params.h"
#include <chrono>

// Bluepilot blindspot enhancement state
struct BlindspotState {
  bool left_blindspot = false;
  bool right_blindspot = false;
  int blink_counter = 0;
  float opacity = 0.2f;
  QLinearGradient cached_gradient_left;
  QLinearGradient cached_gradient_right;
  bool gradients_dirty = true;
  QRectF last_left_bounds, last_right_bounds;
} blindspot_state;

void ModelRendererSP::update_model(const cereal::ModelDataV2::Reader &model, const cereal::RadarState::LeadData::Reader &lead) {
  ModelRenderer::update_model(model, lead);

  const auto &model_position = model.getPosition();
  const auto &lane_lines = model.getLaneLines();
  float max_distance = std::clamp(*(model_position.getX().end() - 1), MIN_DRAW_DISTANCE, MAX_DRAW_DISTANCE);
  int max_idx = get_path_length_idx(lane_lines[0], max_distance);

  // update blindspot vertices with enhanced styling
  float max_distance_barrier = 100;
  int max_idx_barrier = std::min(max_idx, get_path_length_idx(lane_lines[0], max_distance_barrier));

  // Create enhanced blindspot polygons with wider coverage
  mapLineToPolygon(model.getLaneLines()[1], 0.3, -0.05, &left_blindspot_vertices, max_idx_barrier);
  mapLineToPolygon(model.getLaneLines()[2], 0.3, -0.05, &right_blindspot_vertices, max_idx_barrier);
}

void ModelRendererSP::drawPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, const QRect &surface_rect) {
  auto *s = uiState();
  auto &sm = *(s->sm);

  // Update blindspot state and animation
  const auto car_state = sm["carState"].getCarState();
  blindspot_state.left_blindspot = car_state.getLeftBlindspot();
  blindspot_state.right_blindspot = car_state.getRightBlindspot();

  // Update blink animation
  blindspot_state.blink_counter = (blindspot_state.blink_counter + 1) % (20 * 2);
  float pulse = 0.1 * sin(blindspot_state.blink_counter * M_PI / 20) + 0.25;
  blindspot_state.opacity = pulse;

#ifdef BLUEPILOT
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
    return; // Early return - don't call base drawPath
  }
#endif

  // Draw blindspot overlays for standard path
  drawBlindspotOverlays(painter);

  // Fall back to standard path drawing
  ModelRenderer::drawPath(painter, model, surface_rect.height());
}

void ModelRendererSP::drawBlindspotOverlays(QPainter &painter) {
  // Enhanced blindspot drawing
  bool blindspot = Params().getBool("BlindSpot");
  if (blindspot) {
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (blindspot_state.left_blindspot && !left_blindspot_vertices.isEmpty()) {
      QRectF leftBounds = left_blindspot_vertices.boundingRect();
      if (leftBounds != blindspot_state.last_left_bounds || blindspot_state.gradients_dirty) {
        blindspot_state.cached_gradient_left = QLinearGradient(leftBounds.center().x(), leftBounds.top(), leftBounds.center().x(), leftBounds.bottom());
        blindspot_state.cached_gradient_left.setColorAt(0.0, QColor::fromRgbF(1.0, 0.0, 0.0, 0.0));
        blindspot_state.cached_gradient_left.setColorAt(0.4, QColor::fromRgbF(1.0, 0.0, 0.0, blindspot_state.opacity));
        blindspot_state.cached_gradient_left.setColorAt(1.0, QColor::fromRgbF(1.0, 0.0, 0.0, 1.0));
        blindspot_state.last_left_bounds = leftBounds;
      }
      painter.setBrush(blindspot_state.cached_gradient_left);
      painter.setPen(Qt::NoPen);
      painter.drawPolygon(left_blindspot_vertices);
    }

    if (blindspot_state.right_blindspot && !right_blindspot_vertices.isEmpty()) {
      QRectF rightBounds = right_blindspot_vertices.boundingRect();
      if (rightBounds != blindspot_state.last_right_bounds || blindspot_state.gradients_dirty) {
        blindspot_state.cached_gradient_right = QLinearGradient(rightBounds.center().x(), rightBounds.top(), rightBounds.center().x(), rightBounds.bottom());
        blindspot_state.cached_gradient_right.setColorAt(0.0, QColor::fromRgbF(1.0, 0.0, 0.0, 0.0));
        blindspot_state.cached_gradient_right.setColorAt(0.4, QColor::fromRgbF(1.0, 0.0, 0.0, blindspot_state.opacity));
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