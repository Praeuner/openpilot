#include "selfdrive/ui/bluepilot/qt/onroad/overlays/radar_overlay.h"
#include "selfdrive/ui/qt/util.h"
#include <QApplication>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPolygonF>
#include <QPen>
#include <QFontMetrics>
#include <QPixmap>
#include <algorithm>
#include <iostream>
#include <cmath>

QPixmap* RadarOverlay::radar_icon = nullptr;
QPixmap* RadarOverlay::vision_icon = nullptr;
bool RadarOverlay::icons_initialized = false;

void RadarOverlay::render(QPainter &painter, const QRect &rect, const UIState &s,
                         const QPointF vertices[2], const bool radar_assisted[2],
                         const bool virtual_active[2], const int active_counter[2],
                         float scale_factor, const Eigen::Matrix3f &transform, const QRectF &clip_region) {
  const SubMaster &sm = *(s.sm);

  if (!sm.alive("radarState") || !sm.valid("radarState")) {
    return;
  }

  if (!icons_initialized) {
    initializeIcons();
  }

  const auto &radar_state = sm["radarState"].getRadarState();

  for (int i = 0; i < 2; ++i) {
    if (!virtual_active[i]) continue;

    const auto &lead_data = (i == 0) ? radar_state.getLeadOne() : radar_state.getLeadTwo();
    if (!lead_data.getStatus()) continue;

    if (i == 1 && virtual_active[0]) {
      const auto &lead_one = radar_state.getLeadOne();
      if (std::abs(lead_one.getDRel() - lead_data.getDRel()) <= 3.0) {
        continue;
      }
    }

    // Calculate confidence-based opacity
    float confidence_alpha = 1.0f;
    if (lead_data.getDRel() < 5.0f && !radar_assisted[i]) {
      confidence_alpha = std::min(0.4f + (active_counter[i] * 0.06f), 1.0f);
    }

    drawEnhancedLead(painter, lead_data, vertices[i], rect,
                    radar_assisted[i], confidence_alpha, scale_factor, s);
  }
}

void RadarOverlay::drawEnhancedLead(QPainter &painter, const cereal::RadarState::LeadData::Reader &lead_data,
                                   const QPointF &vd, const QRect &rect, bool radar_assisted,
                                   float alpha, float scale_factor, const UIState &s) {
  const float d_rel = lead_data.getDRel();
  const float v_lead = lead_data.getVLead();
  const float v_rel = lead_data.getVRel();
  const SubMaster &sm = *(s.sm);
  const float v_ego = sm.valid("carState") ? sm["carState"].getCarState().getVEgo() : 0.0f;

  // Calculate sizes based on distance for responsive design with scale factor
  float base_sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 3.0;
  float sz = base_sz * scale_factor;

  float x = std::clamp<float>(vd.x(), 0.f, rect.width() - sz / 2);
  float y = std::min<float>(vd.y(), rect.height() - sz * 0.6);

  painter.setRenderHint(QPainter::Antialiasing, true);

  // Create the chevron polygon with scaled size
  QPolygonF chevronPolygon;
  chevronPolygon << QPointF(x + (sz * 1.25), y + sz)
                 << QPointF(x, y)
                 << QPointF(x - (sz * 1.25), y + sz);

  // Get automotive colors based on radar assistance
  QColor baseChevronColor = radar_assisted ? QColor(60, 170, 255) : QColor(241, 196, 15);

  // Create automotive metallic gradient for chevron
  QRect chevronBounds = chevronPolygon.boundingRect().toRect();
  QLinearGradient chevronGradient(chevronBounds.topLeft(), chevronBounds.bottomLeft());
  chevronGradient.setColorAt(0, baseChevronColor.lighter(130));
  chevronGradient.setColorAt(0.3, baseChevronColor);
  chevronGradient.setColorAt(0.7, baseChevronColor.darker(110));
  chevronGradient.setColorAt(1, baseChevronColor.darker(140));

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

  // ========== THREE SEPARATE INFO BOXES ==========

  // Convert measurements for display
  float distance_m = d_rel;

  // Get metric setting from UI state to determine speed unit
  bool is_metric = s.scene.is_metric;
  float lead_speed_display = v_lead * (is_metric ? 3.6f : 2.237f); // MS_TO_KPH or MS_TO_MPH

  // Calculate time-to-lead (following time)
  float time_to_lead = 0.0f;
  if (v_ego > 1.0f) { // If ego vehicle is moving
    time_to_lead = d_rel / v_ego;

    // If approaching (closing distance), show time to collision instead
    if (v_rel < -0.5f) { // Negative v_rel means approaching
      float time_to_collision = d_rel / std::abs(v_rel);
      time_to_lead = std::min(time_to_lead, time_to_collision);
    }
  }

  QString distText = QString("%1m").arg(qRound(distance_m));
  QString speedText = QString("%1%2").arg(qRound(lead_speed_display)).arg(is_metric ? "km/h" : "mph");
  QString timeText;

  if (v_ego < 1.0f) {
    timeText = "--s";
  } else if (time_to_lead > 10.0f) {
    timeText = ">10s";
  } else {
    timeText = QString("%1s").arg(time_to_lead, 0, 'f', 1);
  }

  // Calculate dynamic width for speed box
  QFont valueFont("Inter", int(26 * scale_factor), QFont::DemiBold);
  QFontMetrics fm(valueFont);

  float dist_box_width = 100 * scale_factor;  // Fixed width for distance
  float speed_box_width = fm.horizontalAdvance(speedText) + (20 * scale_factor); // Dynamic width + padding
  float time_box_width = 100 * scale_factor;  // Fixed width for time

  float box_height = 55 * scale_factor;   // Increased from 45
  float box_gap = 12 * scale_factor;      // Increased from 10
  float total_width = dist_box_width + speed_box_width + time_box_width + (box_gap * 2);
  float box_top = y + sz + (20 * scale_factor);  // Increased from 15

  // Starting x position for the leftmost box
  float start_x = x - total_width / 2;

  // Make sure boxes stay within bounds
  if (start_x < 5) {
    start_x = 5;
  } else if (start_x + total_width > rect.width() - 5) {
    start_x = rect.width() - total_width - 5;
  }

  // Check if boxes would go off bottom
  if (box_top + box_height > rect.height() - 5) {
    return; // Don't draw boxes if they would be cut off
  }

  // Common box styling
  auto drawInfoBox = [&](float box_x, float box_w, const QString& value, bool isWarning = false) {
    QRectF boxRect(box_x, box_top, box_w, box_height);

    // Background gradient
    QRadialGradient boxGradient(boxRect.center(), box_w * 0.7);
    boxGradient.setColorAt(0, QColor(44, 62, 80, int(200 * alpha)));
    boxGradient.setColorAt(1, QColor(26, 37, 47, int(220 * alpha)));

    painter.setPen(Qt::NoPen);
    painter.setBrush(boxGradient);
    painter.drawRoundedRect(boxRect, 6 * scale_factor, 6 * scale_factor);

    // Border
    QColor boxBorderColor = baseChevronColor;
    boxBorderColor.setAlpha(int(180 * alpha));
    painter.setPen(QPen(boxBorderColor, 2 * scale_factor));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(boxRect, 6 * scale_factor, 6 * scale_factor);

    // Metallic highlight
    QRect highlightRect = boxRect.toRect().adjusted(2, 2, -2, -boxRect.height() / 2);
    QLinearGradient highlight(highlightRect.topLeft(), highlightRect.bottomLeft());
    highlight.setColorAt(0, QColor(255, 255, 255, int(15 * alpha)));
    highlight.setColorAt(0.3, QColor(255, 255, 255, int(5 * alpha)));
    highlight.setColorAt(1, QColor(255, 255, 255, 0));

    painter.setBrush(highlight);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(highlightRect, 4 * scale_factor, 4 * scale_factor);

    // Value text - centered vertically in the box
    QFont valueFont("Inter", int(26 * scale_factor), QFont::DemiBold);  // Increased from 22
    painter.setFont(valueFont);

    // Shadow
    painter.setPen(QColor(0, 0, 0, int(150 * alpha)));
    painter.drawText(boxRect.adjusted(scale_factor, scale_factor, scale_factor, scale_factor),
                     Qt::AlignCenter, value);

    // Main text
    QColor textColor = isWarning ? QColor(255, 100, 100, int(255 * alpha))
                                : QColor(236, 240, 241, int(255 * alpha));
    painter.setPen(textColor);
    painter.drawText(boxRect, Qt::AlignCenter, value);
  };

  // Draw the three boxes with dynamic positioning
  drawInfoBox(start_x, dist_box_width, distText);
  drawInfoBox(start_x + dist_box_width + box_gap, speed_box_width, speedText);
  drawInfoBox(start_x + dist_box_width + speed_box_width + (box_gap * 2), time_box_width, timeText,
              time_to_lead < 2.0f && v_ego > 1.0f);
}

void RadarOverlay::drawAllRadarPoints(QPainter &painter, const QRect &rect, const UIState &s,
                                     const Eigen::Matrix3f &transform, const QRectF &clip_region) {
  const SubMaster &sm = *(s.sm);
  if (!sm.alive("radarState")) return;

  const auto &radar_state = sm["radarState"].getRadarState();
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Get path offset for z calculations
  float path_offset_z = 0.0f;
  if (sm.valid("liveCalibration")) {
    const auto &live_calib = sm["liveCalibration"].getLiveCalibration();
    const auto &height_list = live_calib.getHeight();
    if (height_list.size() > 0) {
      path_offset_z = height_list[0];
    }
  }

  // Draw lead one
  const auto &lead_one = radar_state.getLeadOne();
  if (lead_one.getStatus() && lead_one.getDRel() > 0) {
    QPointF pt;
    if (mapToScreen(lead_one.getDRel(), lead_one.getYRel(), path_offset_z, &pt, transform, clip_region)) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(255, 0, 0, 200)); // Red dot
      painter.drawEllipse(pt, 10, 10);

      // Add distance text
      painter.setPen(Qt::white);
      painter.setFont(QFont("Inter", 10));
      painter.drawText(pt.x() + 15, pt.y(), QString("%1m").arg(lead_one.getDRel(), 0, 'f', 1));
    }
  }

  // Draw lead two
  const auto &lead_two = radar_state.getLeadTwo();
  if (lead_two.getStatus() && lead_two.getDRel() > 0) {
    QPointF pt;
    if (mapToScreen(lead_two.getDRel(), lead_two.getYRel(), path_offset_z, &pt, transform, clip_region)) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(0, 255, 0, 200)); // Green dot
      painter.drawEllipse(pt, 8, 8);

      // Add distance text
      painter.setPen(Qt::white);
      painter.setFont(QFont("Inter", 9));
      painter.drawText(pt.x() + 12, pt.y(), QString("%1m").arg(lead_two.getDRel(), 0, 'f', 1));
    }
  }
}

void RadarOverlay::initializeIcons() {
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

void RadarOverlay::cleanup() {
  delete radar_icon;
  delete vision_icon;
  radar_icon = nullptr;
  vision_icon = nullptr;
  icons_initialized = false;
}

QPixmap RadarOverlay::createFallbackIcon(const QString &text, const QColor &color) {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Draw circle background
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  painter.drawEllipse(2, 2, 28, 28);

  // Draw text
  painter.setPen(Qt::white);
  painter.setFont(QFont("Inter", 16, QFont::Bold));
  painter.drawText(pixmap.rect(), Qt::AlignCenter, text);

  return pixmap;
}

bool RadarOverlay::mapToScreen(float in_x, float in_y, float in_z, QPointF *out,
                              const Eigen::Matrix3f &transform, const QRectF &clip_region) {
  if (transform.isZero()) {
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
  auto pt = transform * input;

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
