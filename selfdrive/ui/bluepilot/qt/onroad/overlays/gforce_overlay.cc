#include "selfdrive/ui/bluepilot/qt/onroad/overlays/gforce_overlay.h"
#include "selfdrive/ui/qt/util.h"
#include <iostream>
#include <algorithm>
#include <cmath>

void GForceOverlay::render(QPainter &painter, const QRect &rect, const UIState &s, GForceState &gforce_state) {
  if (!gforce_state.show_gforce) {
    return;
  }

  drawGForceMeter(painter, rect, s, gforce_state);
}

void GForceOverlay::updateGForceData(const UIState &s, GForceState &gforce_state) {
  if (!gforce_state.show_gforce) {
    return;
  }

  const SubMaster &sm = *(s.sm);
  bool using_real_data = false;

  // Debug: Check what services are available
  static int debug_counter = 0;

  // Try to access sensor data
  bool accel_available = false;
  bool gyro_available = false;

  try {
    accel_available = sm.alive("accelerometer") && sm.valid("accelerometer");
    gyro_available = sm.alive("gyroscope") && sm.valid("gyroscope");
  } catch (...) {
    // Services don't exist
  }

  if (accel_available && gyro_available) {
    try {
      // Get the Event messages
      const auto &accel_event = sm["accelerometer"];
      const auto &gyro_event = sm["gyroscope"];

      // Access as SensorEventData directly
      const auto &accel_sensor = accel_event.getAccelerometer();
      const auto &gyro_sensor = gyro_event.getGyroscope();

      // Check if the union contains the data we expect
      if (accel_sensor.which() == cereal::SensorEventData::Which::ACCELERATION &&
          gyro_sensor.which() == cereal::SensorEventData::Which::GYRO_UNCALIBRATED) {

        const auto &accel = accel_sensor.getAcceleration();
        const auto &gyro = gyro_sensor.getGyroUncalibrated();

        auto accel_v = accel.getV();
        auto gyro_v = gyro.getV();

        float ax = accel_v[0];
        // float ay = accel_v[1];
        // float az = accel_v[2];
        float yaw_rate = gyro_v[2];

        // Get vehicle speed for calculations
        float v_ego = 0.0f;
        if (sm.valid("carState")) {
          const auto &car_state = sm["carState"].getCarState();
          v_ego = car_state.getVEgo();
        }

        // SIMPLIFIED: Skip raw accelerometer for now, use vehicle dynamics
        // The accelerometer gravity removal is complex and coordinate-system dependent

        // Use gyroscope + velocity for both axes (more reliable)
        if (v_ego > 0.5f) {
          // Moving: use centripetal acceleration for lateral (fixed sign)
          gforce_state.lateral_g = -(v_ego * yaw_rate) / GRAVITY_MS2;

          // For longitudinal, we need actual acceleration data
          // Try to use the raw accelerometer but with minimal processing
          gforce_state.longitudinal_g = ax / GRAVITY_MS2;

          // If longitudinal is still around 1g, try different axis or remove offset
          if (std::abs(gforce_state.longitudinal_g) > 0.8f) {
            // Gravity is contaminating - try removing constant offset
            static float ax_offset = ax; // Capture initial offset
            gforce_state.longitudinal_g = (ax - ax_offset) / GRAVITY_MS2;
          }
        } else {
          // Stationary: zero out values
          gforce_state.lateral_g = 0.0f;
          gforce_state.longitudinal_g = 0.0f;
        }

        using_real_data = false;  // TEMPORARY: Force simulated data until coordinate system is fixed

        // if (debug_counter % 20 == 0) {
        //   std::cout << "RAW SENSOR - ax: " << ax << " ay: " << ay << " az: " << az
        //             << " yaw: " << yaw_rate << " v_ego: " << v_ego
        //             << " | calc long_g: " << gforce_state.longitudinal_g
        //             << " lat_g: " << gforce_state.lateral_g << std::endl;
        // }
      } else {
        if (debug_counter % 20 == 0) {
          std::cout << "Sensor data union mismatch - accel which: " << (int)accel_sensor.which()
                    << " gyro which: " << (int)gyro_sensor.which() << std::endl;
        }
      }
    } catch (const std::exception &e) {
      if (debug_counter % 20 == 0) {
        std::cout << "Sensor access error: " << e.what() << std::endl;
      }
    }
  } else {
    if (debug_counter % 50 == 0) {
      std::cout << "Sensors not available - accel: " << accel_available
                << " gyro: " << gyro_available << std::endl;
    }
  }

  // Fall back to simulated data (this should work well)
  if (!using_real_data && sm.valid("carState")) {
    const auto &car_state = sm["carState"].getCarState();
    float v_ego = car_state.getVEgo();
    float a_ego = car_state.getAEgo();
    float yaw_rate = car_state.getYawRate();

    // These values should already have gravity removed
    gforce_state.longitudinal_g = a_ego / GRAVITY_MS2;  // Fixed sign
    gforce_state.lateral_g = -(v_ego * yaw_rate) / GRAVITY_MS2;  // Fixed sign

    if (debug_counter % 200 == 0) {
      std::cout << "Using SIMULATED data - v_ego: " << v_ego
                << " a_ego: " << a_ego << " yaw_rate: " << yaw_rate
                << " long_g: " << gforce_state.longitudinal_g
                << " lat_g: " << gforce_state.lateral_g << std::endl;
    }
  }

  // Apply smoothing
  const float smoothing_factor = 0.30f;  // Increased for more real-time responsiveness
  gforce_state.smoothed_longitudinal =
    gforce_state.smoothed_longitudinal * (1.0f - smoothing_factor) +
    gforce_state.longitudinal_g * smoothing_factor;
  gforce_state.smoothed_lateral =
    gforce_state.smoothed_lateral * (1.0f - smoothing_factor) +
    gforce_state.lateral_g * smoothing_factor;

  gforce_state.longitudinal_g = gforce_state.smoothed_longitudinal;
  gforce_state.lateral_g = gforce_state.smoothed_lateral;

  // Update peak values
  float abs_lateral = std::abs(gforce_state.lateral_g);
  if (abs_lateral > gforce_state.max_lateral) {
    gforce_state.max_lateral = abs_lateral;
  }

  if (gforce_state.longitudinal_g > gforce_state.max_longitudinal) {
    gforce_state.max_longitudinal = gforce_state.longitudinal_g;
  }

  if (gforce_state.longitudinal_g < -gforce_state.max_braking) {
    gforce_state.max_braking = -gforce_state.longitudinal_g;
  }

  // Update history for trail effect
  gforce_state.history_lateral[gforce_state.history_index] =
    QPointF(gforce_state.lateral_g, gforce_state.longitudinal_g);
  gforce_state.history_index = (gforce_state.history_index + 1) % 50;

  debug_counter++;
}

// Color scaling function for G-force values
QColor GForceOverlay::getGForceColor(float g_value) {
  float abs_g = std::abs(g_value);

  if (abs_g < 0.3f) {
    // White for normal driving (0.0 - 0.3g)
    return QColor(255, 255, 255);
  } else if (abs_g < 0.6f) {
    // White to Yellow transition (0.3 - 0.6g)
    float ratio = (abs_g - 0.3f) / 0.3f;
    int red = 255;
    int green = 255;
    int blue = static_cast<int>(255 * (1.0f - ratio));
    return QColor(red, green, blue);
  } else if (abs_g < 1.0f) {
    // Yellow to Orange transition (0.6 - 1.0g)
    float ratio = (abs_g - 0.6f) / 0.4f;
    int red = 255;
    int green = static_cast<int>(255 * (1.0f - ratio * 0.5f)); // 255 to 128
    int blue = 0;
    return QColor(red, green, blue);
  } else {
    // Orange to Red transition (1.0g+)
    float ratio = std::min((abs_g - 1.0f) / 0.5f, 1.0f); // Cap at 1.5g
    int red = 255;
    int green = static_cast<int>(128 * (1.0f - ratio)); // 128 to 0
    int blue = 0;
    return QColor(red, green, blue);
  }
}

// G-Force meter with LAT/LONG sections and TOTAL at bottom
void GForceOverlay::drawGForceMeter(QPainter &painter, const QRect &rect, const UIState &s, GForceState &gforce_state) {
  // Responsive width scaling to match hybrid gauge pattern
  int gauge_scale = s.scene.hybrid_drive_gauge_size;
  int meter_width_percent;
  int meter_height = 140; // Default height

  if (gauge_scale == 1) {
    meter_width_percent = 14;
    meter_height = 100;
  } else if (gauge_scale == 2) {
    meter_width_percent = 16;
    meter_height = 115;
  } else if (gauge_scale == 3) {
    meter_width_percent = 18;
    meter_height = 130;
  } else {
    meter_width_percent = 16;
    meter_height = 140;
  }

  int meter_width = rect.width() * (meter_width_percent / 100.0f);

  int x, y;

  // Position to match hybrid gauge exactly
  if (s.scene.show_hybrid_drive_overlay) {
    int gauge_width = rect.width() * 0.39;

    if (gauge_scale == 1) {
      gauge_width = rect.width() * 0.30;
    } else if (gauge_scale == 2) {
      gauge_width = rect.width() * 0.345;
    } else if (gauge_scale == 3) {
      gauge_width = rect.width() * 0.39;
    }

    int bottom_margin = 30;
    int gauge_y = rect.height() - meter_height - bottom_margin;
    int gauge_center_x = rect.width() / 2;
    int gauge_left = gauge_center_x - gauge_width / 2;

    // Position G-force meter to the left of hybrid gauge
    x = gauge_left - meter_width - 10;
    y = gauge_y; // Exact same Y position as hybrid gauge
  } else {
    // When no hybrid gauge, position to the right of driver monitor
    x = 250;
    y = rect.height() - meter_height - 60;
  }

  // Ensure meter stays within bounds
  x = std::max(10, std::min(x, rect.width() - meter_width - 10));
  y = std::max(10, std::min(y, rect.height() - meter_height - 10));

  QRect meter_rect(x, y, meter_width, meter_height);

  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.save();

  // Draw meter background with automotive styling
  painter.setPen(QPen(QColor(100, 149, 237, 200), 3));
  painter.setBrush(QColor(44, 62, 80, 240));
  painter.drawRoundedRect(meter_rect, 12, 12);

  // Calculate sections - 3 equal columns
  int column_width = meter_width / 3;
  QRect lat_section(meter_rect.left(), meter_rect.top(), column_width, meter_height);
  QRect long_section(meter_rect.left() + column_width, meter_rect.top(), column_width, meter_height);
  QRect total_section(meter_rect.left() + (column_width * 2), meter_rect.top(), column_width, meter_height);

  // Draw vertical divider lines
  painter.setPen(QPen(QColor(100, 149, 237, 100), 2));
  painter.drawLine(lat_section.topRight().x(), lat_section.top(),
                   lat_section.topRight().x(), lat_section.bottom());
  painter.drawLine(long_section.topRight().x(), long_section.top(),
                   long_section.topRight().x(), long_section.bottom());

  // Get current G-forces
  float lateral_g = gforce_state.lateral_g;
  float longitudinal_g = gforce_state.longitudinal_g;
  float total_g = std::sqrt(lateral_g * lateral_g + longitudinal_g * longitudinal_g);

  // Prevent -0.0g/0.0g flipping
  if (std::abs(lateral_g) < 0.05f) lateral_g = 0.0f;
  if (std::abs(longitudinal_g) < 0.05f) longitudinal_g = 0.0f;

  // Dynamic font sizes based on display size
  float font_scale = rect.height() / 1080.0f; // Scale for high-DPI displays
  QFont labelFont("Inter", std::max(16, (int)(22 * font_scale)), QFont::Bold);
  QFont valueFont("Inter", std::max(28, (int)(38 * font_scale)), QFont::Bold);

  // === LATERAL SECTION ===
  painter.setFont(labelFont);
  painter.setPen(QColor(0, 255, 127, 200)); // Green label
  painter.drawText(lat_section.adjusted(0, 8, 0, -meter_height * 0.6), Qt::AlignCenter, "LAT");

  // Draw lateral value with color scaling
  painter.setFont(valueFont);
  QColor lat_color = getGForceColor(lateral_g);
  painter.setPen(lat_color);

  // Add glow effect for high values
  if (std::abs(lateral_g) > 0.6f) {
    painter.setPen(QColor(lat_color.red(), lat_color.green(), lat_color.blue(), 100));
    for (int i = 1; i <= 2; ++i) {
      painter.drawText(lat_section.adjusted(-i, meter_height * 0.25 - i, i, -8 + i),
                      Qt::AlignCenter, QString("%1g").arg(lateral_g, 0, 'f', 1));
    }
    painter.setPen(lat_color);
  }

  painter.drawText(lat_section.adjusted(0, meter_height * 0.25, 0, -8), Qt::AlignCenter,
                  QString("%1g").arg(lateral_g, 0, 'f', 1));

  // === LONGITUDINAL SECTION ===
  painter.setFont(labelFont);
  painter.setPen(QColor(100, 149, 237, 200)); // Blue label
  painter.drawText(long_section.adjusted(0, 8, 0, -meter_height * 0.6), Qt::AlignCenter, "LONG");

  // Draw longitudinal value with color scaling
  painter.setFont(valueFont);
  QColor long_color = getGForceColor(longitudinal_g);
  painter.setPen(long_color);

  // Add glow effect for high values
  if (std::abs(longitudinal_g) > 0.6f) {
    painter.setPen(QColor(long_color.red(), long_color.green(), long_color.blue(), 100));
    for (int i = 1; i <= 2; ++i) {
      painter.drawText(long_section.adjusted(-i, meter_height * 0.25 - i, i, -8 + i),
                      Qt::AlignCenter, QString("%1g").arg(longitudinal_g, 0, 'f', 1));
    }
    painter.setPen(long_color);
  }

  painter.drawText(long_section.adjusted(0, meter_height * 0.25, 0, -8), Qt::AlignCenter,
                  QString("%1g").arg(longitudinal_g, 0, 'f', 1));

  // === TOTAL G-FORCE SECTION ===
  QColor total_color = getGForceColor(total_g);

  painter.setFont(labelFont);
  painter.setPen(QColor(255, 255, 255, 200)); // White label
  painter.drawText(total_section.adjusted(0, 8, 0, -meter_height * 0.6), Qt::AlignCenter, "TOTAL");

  // Draw total value with color scaling
  painter.setFont(valueFont);
  painter.setPen(total_color);

  // Add glow effect for high total values
  if (total_g > 0.6f) {
    painter.setPen(QColor(total_color.red(), total_color.green(), total_color.blue(), 100));
    for (int i = 1; i <= 2; ++i) {
      painter.drawText(total_section.adjusted(-i, meter_height * 0.25 - i, i, -8 + i),
                      Qt::AlignCenter, QString("%1g").arg(total_g, 0, 'f', 1));
    }
    painter.setPen(total_color);
  }

  painter.drawText(total_section.adjusted(0, meter_height * 0.25, 0, -8), Qt::AlignCenter,
                  QString("%1g").arg(total_g, 0, 'f', 1));

  painter.restore();
}
