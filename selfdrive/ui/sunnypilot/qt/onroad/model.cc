/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/sunnypilot/qt/onroad/model.h"
#include "common/params.h"
#include <chrono>

// Fixed: Use same blindspot constants from model_old.cc
static constexpr float BLINDSPOT_WIDTH = 1.0f; // Width of blind spot indicator in meters

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

  // FIXED: Create blindspot polygons like in model_old.cc
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

      // Left blind spot - same as model_old.cc
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

      // Right blind spot - same as model_old.cc
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

void ModelRendererSP::drawPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, const QRect &surface_rect) {
  auto *s = uiState();
  auto &sm = *(s->sm);

  // Update blindspot state and animation
  const auto car_state = sm["carState"].getCarState();
  blindspot_state.left_blindspot = car_state.getLeftBlindspot();
  blindspot_state.right_blindspot = car_state.getRightBlindspot();

  // Update blink animation - using UI_FREQ from model_old.cc which is 20
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
    
    // Draw lead status after path rendering
    drawLeadStatus(painter, surface_rect.height(), surface_rect.width());
    return; // Early return - don't call base drawPath
  }
#endif

  // Draw blindspot overlays for standard path
  drawBlindspotOverlays(painter);

  // Fall back to standard path drawing
  ModelRenderer::drawPath(painter, model, surface_rect.height());
  
  // Draw lead status after path rendering
  drawLeadStatus(painter, surface_rect.height(), surface_rect.width());
}

void ModelRendererSP::drawBlindspotOverlays(QPainter &painter) {
  // Enhanced blindspot drawing
  bool blindspot = Params().getBool("BlindSpot");
  if (blindspot) {
    painter.setRenderHint(QPainter::Antialiasing, true);

    // FIXED: Use same gradient stops as model_old.cc
    if (blindspot_state.left_blindspot && !left_blindspot_vertices.isEmpty()) {
      QRectF leftBounds = left_blindspot_vertices.boundingRect();
      if (leftBounds != blindspot_state.last_left_bounds || blindspot_state.gradients_dirty) {
        blindspot_state.cached_gradient_left = QLinearGradient(leftBounds.center().x(), leftBounds.top(),
                                                               leftBounds.center().x(), leftBounds.bottom());
        // Use exact gradient stops from model_old.cc
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

    if (blindspot_state.right_blindspot && !right_blindspot_vertices.isEmpty()) {
      QRectF rightBounds = right_blindspot_vertices.boundingRect();
      if (rightBounds != blindspot_state.last_right_bounds || blindspot_state.gradients_dirty) {
        blindspot_state.cached_gradient_right = QLinearGradient(rightBounds.center().x(), rightBounds.top(),
                                                                rightBounds.center().x(), rightBounds.bottom());
        // Use exact gradient stops from model_old.cc
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

void ModelRendererSP::drawLeadStatus(QPainter &painter, int height, int width) {
    auto *s = uiState();
    auto &sm = *(s->sm);

    // Early exit if Bluepilot radar overlay is active
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

void ModelRendererSP::drawLeadStatusAtPosition(QPainter &painter,
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