#include "selfdrive/ui/qt/onroad/model.h"

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
  auto *s = uiState();
  auto &sm = *(s->sm);
  // Check if data is up-to-date
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

  update_model(model, lead_one);
  drawLaneLines(painter);
  drawPath(painter, model, surface_rect.height());

  // Check if we should show radar overlay regardless of longitudinal control
  bool showRadarOverlay = !experimental_mode && Params().getBool("FordPrefShowRadarLeadOverlay");

  // Modified condition: show leads if longitudinal_control is enabled OR if radar overlay
  // is enabled by the user preference
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
    if (lead_data.getStatus()) {
      float z = line.getZ()[get_path_length_idx(line, lead_data.getDRel())];
      mapToScreen(lead_data.getDRel(), -lead_data.getYRel(), z + path_offset_z, &lead_vertices[i]);

      // Get radar flag directly from the lead data
      lead_radar_assisted[i] = lead_data.getRadar();
    }
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

  // Calculate lane offsets (distance from car to lane lines)
  if (!lane_line_vertices[1].isEmpty() && !lane_line_vertices[2].isEmpty()) {
    // Find the closest point to the car for lane lines 1 (left) and 2 (right)
    // Assuming the path center is at y=0 in car space coordinates
    int bottom_idx_1 = lane_line_vertices[1].size() - 1;
    int bottom_idx_2 = lane_line_vertices[2].size() - 1;

    if (bottom_idx_1 >= 0 && bottom_idx_2 >= 0) {
      // Get the y-values of the lane lines at the point closest to the car
      float left_y = -lane_lines[1].getY()[0]; // Negate because of coordinate system
      float right_y = lane_lines[2].getY()[0];

      // Store the lane offsets
      left_lane_offset = std::abs(left_y);
      right_lane_offset = std::abs(right_y);
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
  mapLineToPolygon(model_position, 0.9, path_offset_z, &track_vertices, max_idx, false);
}

void ModelRenderer::drawLaneLines(QPainter &painter) {
  // lanelines
  for (int i = 0; i < std::size(lane_line_vertices); ++i) {
    painter.setBrush(QColor::fromRgbF(1.0, 1.0, 1.0, std::clamp<float>(lane_line_probs[i], 0.0, 0.7)));
    painter.drawPolygon(lane_line_vertices[i]);
  }

  // road edges
  for (int i = 0; i < std::size(road_edge_vertices); ++i) {
    painter.setBrush(QColor::fromRgbF(1.0, 0, 0, std::clamp<float>(1.0 - road_edge_stds[i], 0.0, 1.0)));
    painter.drawPolygon(road_edge_vertices[i]);
  }
}

void ModelRenderer::drawPath(QPainter &painter, const cereal::ModelDataV2::Reader &model, int height) {
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
  painter.drawPolygon(track_vertices);

  drawLaneOffsets(painter);
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
  const float v_lead = lead_data.getVLead(); // Get absolute lead speed

  // Calculate sizes based on distance for responsive design
  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 2.35;
  float x = std::clamp<float>(vd.x(), 0.f, surface_rect.width() - sz / 2);
  float y = std::min<float>(vd.y(), surface_rect.height() - sz * 0.6);

  // Convert measurements for display
  float distance_ft = d_rel * 3.281;     // Convert to feet
  float lead_speed_mph = v_lead * 2.237; // Convert absolute lead speed to mph

  // Manually create polygon for the chevron
  QPolygonF chevronPolygon;
  chevronPolygon << QPointF(x + (sz * 1.25), y + sz) << QPointF(x, y) << QPointF(x - (sz * 1.25), y + sz);

  // Create gradient based on radar assistance
  QLinearGradient chevGradient(QPointF(x, y), QPointF(x, y + sz));
  if (isRadarAssisted) {
    // Blue gradient for radar-assisted leads
    chevGradient.setColorAt(0, QColor(60, 170, 255, 230));
    chevGradient.setColorAt(1, QColor(30, 144, 255, 200));
  } else {
    // Red gradient for non-radar-assisted leads
    chevGradient.setColorAt(0, QColor(230, 60, 60, 230));
    chevGradient.setColorAt(1, QColor(200, 30, 30, 200));
  }

  // Draw chevron with gradient
  painter.setPen(Qt::NoPen);
  painter.setBrush(chevGradient);
  painter.drawPolygon(chevronPolygon);

  // Draw chevron border
  painter.setPen(QPen(QColor(255, 255, 255, 80), 1.5));
  painter.setBrush(Qt::NoBrush);
  painter.drawPolygon(chevronPolygon);

  // Create wider info panel BELOW the chevron
  float panel_top = y + sz + 10;                 // Position below the chevron with a small gap
  QRectF infoPanel(x - 100, panel_top, 200, 40); // Wider panel (200px) with single line height

  // Make sure the panel stays within the surface bounds
  if (panel_top + 40 > surface_rect.height()) {
    // If panel would go off-screen, adjust position or reduce height
    float available_height = surface_rect.height() - panel_top - 5;
    if (available_height < 30) {
      // Not enough space below, abort showing panel
      return;
    }
    infoPanel.setHeight(available_height);
  }

  // Draw a semi-transparent panel with a subtle gradient
  QLinearGradient panelGradient(infoPanel.topLeft(), infoPanel.bottomLeft());
  panelGradient.setColorAt(0, QColor(20, 20, 20, 220)); // Slightly more opaque
  panelGradient.setColorAt(1, QColor(40, 40, 40, 220));
  painter.setPen(Qt::NoPen);
  painter.setBrush(panelGradient);
  painter.drawRoundedRect(infoPanel, 10, 10);

  // Add a subtle border
  painter.setPen(QPen(QColor(150, 150, 150, 120), 1));
  painter.drawRoundedRect(infoPanel, 10, 10);

  // Set up text formatting with larger size
  QFont infoFont = painter.font();
  infoFont.setPixelSize(22); // Larger text
  infoFont.setWeight(QFont::DemiBold);
  painter.setFont(infoFont);

  // Format distance and speed text
  QString distText = QString("%1 ft").arg(qRound(distance_ft));
  QString speedText = QString("%1 mph").arg(qRound(lead_speed_mph));

  // Display distance and speed on the same line
  painter.setPen(Qt::white);
  QString combinedText = distText + "  |  " + speedText;

  // Center the text in the panel
  QRectF textRect = infoPanel.adjusted(5, 5, -5, -5);
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

bool ModelRenderer::mapToScreen(float in_x, float in_y, float in_z, QPointF *out) {
  Eigen::Vector3f input(in_x, in_y, in_z);
  auto pt = car_space_transform * input;
  *out = QPointF(pt.x() / pt.z(), pt.y() / pt.z());
  return clip_region.contains(*out);
}

void ModelRenderer::drawLaneOffsets(QPainter &painter) {
  // Only draw if we have valid lane data
  if (lane_line_vertices[1].isEmpty() || lane_line_vertices[2].isEmpty()) {
    return;
  }

  // Format offset values in feet (convert from meters)
  QString left_text = QString::number(left_lane_offset * 3.281, 'f', 1) + " ft";
  QString right_text = QString::number(right_lane_offset * 3.281, 'f', 1) + " ft";

  // Get viewport for screen dimensions
  QRect screen_rect = painter.viewport();

  // Find the bottom points of the lane lines
  QPointF left_lane_bottom;
  QPointF right_lane_bottom;

  // Find bottom points of lane line 1 (left line)
  float max_y_left = 0;
  for (int i = 0; i < lane_line_vertices[1].size(); i++) {
    if (lane_line_vertices[1][i].y() > max_y_left && lane_line_vertices[1][i].y() < screen_rect.height() - 10) {
      max_y_left = lane_line_vertices[1][i].y();
      left_lane_bottom = lane_line_vertices[1][i];
    }
  }

  // Find bottom points of lane line 2 (right line)
  float max_y_right = 0;
  for (int i = 0; i < lane_line_vertices[2].size(); i++) {
    if (lane_line_vertices[2][i].y() > max_y_right && lane_line_vertices[2][i].y() < screen_rect.height() - 10) {
      max_y_right = lane_line_vertices[2][i].y();
      right_lane_bottom = lane_line_vertices[2][i];
    }
  }

  // Fall back to fixed positions if we couldn't find the lane lines
  if (max_y_left < 10 || max_y_right < 10) {
    int center_x = screen_rect.width() / 2;
    int offset_from_center = screen_rect.width() / 6;
    left_lane_bottom = QPointF(center_x - offset_from_center, 0);
    right_lane_bottom = QPointF(center_x + offset_from_center, 0);
  }

  // Lock vertical position to fixed distance from bottom
  int fixed_bottom_y = screen_rect.height() - 60; // 60px from bottom of screen

  // Calculate horizontal positions - move slightly inward from lane lines
  float left_x = left_lane_bottom.x() + 30;   // Move right from left lane
  float right_x = right_lane_bottom.x() - 30; // Move left from right lane

  // Apply smoothing to reduce jitter (if needed)
  // You would need to add static variables to keep track of previous positions
  static float prev_left_x = left_x;
  static float prev_right_x = right_x;

  // Simple exponential smoothing with 0.2 weight for new values
  left_x = prev_left_x * 0.8 + left_x * 0.2;
  right_x = prev_right_x * 0.8 + right_x * 0.2;

  // Update previous values for next frame
  prev_left_x = left_x;
  prev_right_x = right_x;

  // Function to draw pill shape with text
  auto drawPill = [&](float x, const QString &text, bool isLeft) {
    // Pill dimensions
    const int width = 80;
    const int height = 40;
    const int radius = height / 2;

    // Set pill position
    QRectF pillRect = QRectF(x - width / 2, fixed_bottom_y - height / 2, width, height);

    // Set color based on distance
    float distance = isLeft ? left_lane_offset : right_lane_offset;
    QColor baseColor;

    if (distance < 0.5) {
      // Red when very close
      baseColor = QColor(255, 60, 60, 230);
    } else if (distance < 1.0) {
      // Orange/Yellow for medium distance
      baseColor = QColor(255, 165, 0, 230);
    } else {
      // Green for safe distance
      baseColor = QColor(60, 200, 60, 230);
    }

    // Create gradient for pill
    QLinearGradient grad(pillRect.topLeft(), pillRect.bottomLeft());
    grad.setColorAt(0, baseColor);
    grad.setColorAt(1, baseColor.darker(120));

    // Draw pill (rounded rectangle)
    painter.setPen(QPen(Qt::white, 1.5));
    painter.setBrush(grad);
    painter.drawRoundedRect(pillRect, radius, radius);

    // Draw text with larger font
    QFont font = painter.font();
    font.setPixelSize(20);
    font.setBold(true);
    painter.setFont(font);

    // Draw text with shadow for better visibility
    painter.setPen(Qt::black);
    painter.drawText(pillRect.adjusted(1, 1, 1, 1), Qt::AlignCenter, text);
    painter.setPen(Qt::white);
    painter.drawText(pillRect, Qt::AlignCenter, text);
  };

  // Draw the pill indicators
  drawPill(left_x, left_text, true);
  drawPill(right_x, right_text, false);
}