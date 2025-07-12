// selfdrive/ui/bluepilot/qt/onroad/bluepilot_renderer.cc
#include "selfdrive/ui/bluepilot/qt/onroad/bluepilot_renderer.h"
#include "selfdrive/ui/bluepilot/qt/onroad/widgets/hybrid_drive_gauge.h"
#include "selfdrive/ui/qt/onroad/model.h"
#include "selfdrive/ui/qt/util.h"
#include "common/timing.h"
#include "common/params.h"
#include <QApplication>
#include <QPainterPath>
#include <chrono>
#include <algorithm>
#include <iostream>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/qt/onroad/model.h"
#endif



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
  // Debug early to see what's enabled
  // static int global_debug_counter = 0;
  // if (global_debug_counter++ % 100 == 0) {
  //   std::cout << "BluePilot renderAll - hybrid: " << s.scene.show_hybrid_drive_overlay
  //             << " radar: " << s.scene.show_new_radar_overlay
  //             << " stop: " << s.scene.show_stop_indicator_overlay
  //             << " timer: " << s.scene.stand_still_timer << std::endl;
  // }

  // PERFORMANCE: Early exit if no BluePilot features enabled
  if (!s.scene.show_hybrid_drive_overlay &&
      !s.scene.show_new_radar_overlay &&
      !s.scene.show_stop_indicator_overlay &&
      !s.scene.stand_still_timer &&
      !s.scene.show_gforce_meter) {
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

  // 4. G-FORCE METER: Bottom right corner
  renderGForceMeter(painter, rect, s);
}

template<typename ModelType>
void BluepilotRenderer::updateFrameState(const UIState &s, const ModelType &model) {
  const SubMaster &sm = *(s.sm);

  // FIXED: Validate carState message before accessing
  if (!sm.valid("carState")) {
    static int error_counter = 0;
    if (error_counter++ % 50 == 0) {
      std::cerr << "ERROR: BluePilot carState message not valid, skipping frame state update" << std::endl;
    }
    return;
  }

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
  frame_state.gforce_state.show_gforce = s.scene.show_gforce_meter;

  // Update G-force data
  updateGForceData(s);

  // Debug logging
  static int debug_counter = 0;
  // if (debug_counter++ % 100 == 0) {
  //   std::cout << "BluePilot Debug - show_radar: " << frame_state.show_radar
  //             << " show_stop: " << frame_state.show_stop
  //             << " scene.show_new_radar_overlay: " << s.scene.show_new_radar_overlay
  //             << " scene.show_stop_indicator_overlay: " << s.scene.show_stop_indicator_overlay << std::endl;
  // }

  // FIXED: Properly get transform and clip region from model
  if (frame_state.show_radar || frame_state.show_stop) {
    frame_state.transform = model.getTransform();
    frame_state.clip_region = model.getClipRegion();

    // Check if transform is valid but don't return early
    if (frame_state.transform.isZero()) {
      if (debug_counter % 20 == 0) {
        std::cerr << "WARNING: BluePilot transform is zero - overlays may not work properly" << std::endl;
      }
    }

    // Update lane line vertices even if transform might be zero
    // The transform might be set later in the frame

    // FIXED: Validate modelV2 message before accessing
    if (!sm.valid("modelV2")) {
      if (debug_counter % 50 == 0) {
        std::cerr << "WARNING: BluePilot modelV2 not valid, skipping lane line processing" << std::endl;
      }
      return;
    }

    const auto &modelV2 = sm["modelV2"].getModelV2();
    const auto &lane_lines = modelV2.getLaneLines();

    // FIXED: Add proper validation for liveCalibration height access
    float path_offset_z = 0.0f; // Default value
    if (sm.valid("liveCalibration")) {
      const auto &live_calib = sm["liveCalibration"].getLiveCalibration();
      const auto &height_list = live_calib.getHeight();
      if (height_list.size() > 0) {
        path_offset_z = height_list[0];
      } else {
        if (debug_counter % 50 == 0) {
          std::cerr << "WARNING: BluePilot liveCalibration height list is empty, using default value" << std::endl;
        }
      }
    } else {
      if (debug_counter % 50 == 0) {
        std::cerr << "WARNING: BluePilot liveCalibration not valid, using default path_offset_z" << std::endl;
      }
    }

    // Only map lane lines if transform is valid and we have lane line data
    if (!frame_state.transform.isZero() && lane_lines.size() > 0) {
      for (int i = 0; i < 4 && i < static_cast<int>(lane_lines.size()); ++i) {
        frame_state.lane_line_vertices[i].clear();
        const auto &line = lane_lines[i];
        const auto line_x = line.getX(), line_y = line.getY(), line_z = line.getZ();

        // Additional safety check for line data consistency
        if (line_x.size() == 0 || line_y.size() != line_x.size() || line_z.size() != line_x.size()) {
          if (debug_counter % 100 == 0) {
            std::cerr << "WARNING: BluePilot lane line " << i << " has inconsistent data sizes" << std::endl;
          }
          continue;
        }

        // Map line points to screen, limiting to reasonable distance
        for (int j = 0; j < static_cast<int>(line_x.size()) && line_x[j] < 100.0f; ++j) {
          if (line_x[j] < 0) continue;

          QPointF left, right;
          float y_offset = 0.025f; // Lane line width
          if (mapToScreen(line_x[j], line_y[j] - y_offset, line_z[j] + path_offset_z, &left) &&
              mapToScreen(line_x[j], line_y[j] + y_offset, line_z[j] + path_offset_z, &right)) {
            frame_state.lane_line_vertices[i].push_back(left);
            frame_state.lane_line_vertices[i].push_front(right);
          }
        }
      }
    } else {
      if (debug_counter % 100 == 0) {
        std::cerr << "WARNING: BluePilot skipping lane line mapping - transform zero: "
                  << frame_state.transform.isZero() << " lane_lines size: " << lane_lines.size() << std::endl;
      }
    }
  }
}

void BluepilotRenderer::updateGForceData(const UIState &s) {
  if (!frame_state.gforce_state.show_gforce) {
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

      // Print what type of event we got
      // if (debug_counter % 200 == 0) {
      //   std::cout << "Got accelerometer event, trying to access as accelerometer..." << std::endl;
      //   std::cout << "Got gyroscope event, trying to access as gyroscope..." << std::endl;
      // }

      // Access as SensorEventData directly
      const auto &accel_sensor = accel_event.getAccelerometer();
      const auto &gyro_sensor = gyro_event.getGyroscope();

      // if (debug_counter % 200 == 0) {
      //   std::cout << "Accel sensor which(): " << (int)accel_sensor.which() << std::endl;
      //   std::cout << "Gyro sensor which(): " << (int)gyro_sensor.which() << std::endl;
      // }

      // Check if the union contains the data we expect
      if (accel_sensor.which() == cereal::SensorEventData::Which::ACCELERATION &&
          gyro_sensor.which() == cereal::SensorEventData::Which::GYRO_UNCALIBRATED) {

        const auto &accel = accel_sensor.getAcceleration();
        // const auto &gyro = gyro_sensor.getGyroUncalibrated();

        auto accel_v = accel.getV();
        // auto gyro_v = gyro.getV();

        float ax = accel_v[0];
        float ay = accel_v[1];
        float az = accel_v[2];
        // float yaw_rate = gyro_v[2];

        // Remove gravity component - when stationary, accelerometer reads gravity
        // Calculate total acceleration magnitude
        float total_accel = std::sqrt(ax*ax + ay*ay + az*az);

        // If close to gravity magnitude, we're mostly measuring gravity
        if (total_accel > 8.0f && total_accel < 12.0f) {
          // Apply high-pass filter to remove gravity DC component
          static float ax_baseline = ax;
          static float ay_baseline = ay;
          static bool initialized = false;

          if (!initialized) {
            ax_baseline = ax;
            ay_baseline = ay;
            initialized = true;
          } else {
            // Slowly track baseline (gravity orientation)
            ax_baseline = ax_baseline * 0.999f + ax * 0.001f;
            ay_baseline = ay_baseline * 0.999f + ay * 0.001f;
          }

          // Subtract baseline to get actual vehicle acceleration
          ax = ax - ax_baseline;
          ay = ay - ay_baseline;
        }

        frame_state.gforce_state.longitudinal_g = ax / GRAVITY_MS2;
        frame_state.gforce_state.lateral_g = ay / GRAVITY_MS2;

        using_real_data = true;

        // if (debug_counter % 20 == 0) {
        //   std::cout << "SUCCESS: Using REAL sensor data - accel: [" << ax << ", " << ay << ", " << az
        //             << "] gyro Z: " << yaw_rate << std::endl;
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
    if (debug_counter % 20 == 0) {
      std::cout << "Sensors not available" << std::endl;
    }
  }

  // Fall back to simulated data
  if (!using_real_data && sm.valid("carState")) {
    const auto &car_state = sm["carState"].getCarState();
    float v_ego = car_state.getVEgo();
    float a_ego = car_state.getAEgo();
    float yaw_rate = car_state.getYawRate();

    frame_state.gforce_state.longitudinal_g = -a_ego / GRAVITY_MS2;
    frame_state.gforce_state.lateral_g = (v_ego * yaw_rate) / GRAVITY_MS2;

    if (debug_counter % 200 == 0) {
      std::cout << "Using SIMULATED data - v_ego: " << v_ego
                << " a_ego: " << a_ego << " yaw_rate: " << yaw_rate << std::endl;
    }
  }

  // Apply smoothing
  const float smoothing_factor = 0.15f;
  frame_state.gforce_state.smoothed_longitudinal =
    frame_state.gforce_state.smoothed_longitudinal * (1.0f - smoothing_factor) +
    frame_state.gforce_state.longitudinal_g * smoothing_factor;
  frame_state.gforce_state.smoothed_lateral =
    frame_state.gforce_state.smoothed_lateral * (1.0f - smoothing_factor) +
    frame_state.gforce_state.lateral_g * smoothing_factor;

  frame_state.gforce_state.longitudinal_g = frame_state.gforce_state.smoothed_longitudinal;
  frame_state.gforce_state.lateral_g = frame_state.gforce_state.smoothed_lateral;

  // Update peak values
  float abs_lateral = std::abs(frame_state.gforce_state.lateral_g);
  if (abs_lateral > frame_state.gforce_state.max_lateral) {
    frame_state.gforce_state.max_lateral = abs_lateral;
  }

  if (frame_state.gforce_state.longitudinal_g > frame_state.gforce_state.max_longitudinal) {
    frame_state.gforce_state.max_longitudinal = frame_state.gforce_state.longitudinal_g;
  }

  if (frame_state.gforce_state.longitudinal_g < -frame_state.gforce_state.max_braking) {
    frame_state.gforce_state.max_braking = -frame_state.gforce_state.longitudinal_g;
  }

  // Update history for trail effect
  frame_state.gforce_state.history_lateral[frame_state.gforce_state.history_index] =
    QPointF(frame_state.gforce_state.lateral_g, frame_state.gforce_state.longitudinal_g);
  frame_state.gforce_state.history_index = (frame_state.gforce_state.history_index + 1) % 50;
}

void BluepilotRenderer::renderGForceMeter(QPainter &painter, const QRect &rect, const UIState &s) {
  if (!frame_state.gforce_state.show_gforce) {
    return;
  }

  drawGForceMeter(painter, rect, s);
}

void BluepilotRenderer::drawGForceMeter(QPainter &painter, const QRect &rect, const UIState &s) {
  const int meter_width = 350;  // Reduced width
  int meter_height = 100;       // Will be set to match hybrid gauge height
  const int margin = 10;

  int x, y;

  // Check if hybrid gauge is shown and available
  if (s.scene.show_hybrid_drive_overlay && frame_state.hybrid_available) {

    // Get hybrid gauge dimensions - match height exactly
    int gauge_scale = s.scene.hybrid_drive_gauge_size;
    int gauge_width = rect.width() * 0.39;

    if (gauge_scale == 1) {
      gauge_width = rect.width() * 0.30;
      meter_height = 100;  // Match hybrid gauge height
    } else if (gauge_scale == 2) {
      gauge_width = rect.width() * 0.345;
      meter_height = 115;  // Match hybrid gauge height
    } else if (gauge_scale == 3) {
      gauge_width = rect.width() * 0.39;
      meter_height = 130;  // Match hybrid gauge height
    } else {
      meter_height = 100;  // Default height
    }

    int bottom_margin = 30;
    int gauge_y = rect.height() - meter_height - bottom_margin;
    int gauge_center_x = rect.width() / 2;
    int gauge_left = gauge_center_x - gauge_width / 2;

    // Position G-force meter to the left of hybrid gauge
    x = gauge_left - meter_width - margin;
    y = gauge_y; // Exact same Y position as hybrid gauge

  } else {
    // When no hybrid gauge, position to the right of driver monitor widget (bottom left)
    meter_height = 130;  // Larger default size
    x = 250;  // Right of driver monitor with margin
    y = rect.height() - meter_height - 60;
  }

  // Ensure meter stays within bounds
  x = std::max(10, std::min(x, rect.width() - meter_width - 10));
  y = std::max(10, std::min(y, rect.height() - meter_height - 10));

  QRect meter_rect(x, y, meter_width, meter_height);

  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.save();

  // Draw meter background with automotive styling (match hybrid gauge exact color)
  painter.setPen(QPen(QColor(100, 149, 237, 200), 3));
  painter.setBrush(QColor(44, 62, 80, 240));
  painter.drawRoundedRect(meter_rect, 12, 12);

  // Draw grid background - narrower grid to match height
  painter.setClipRect(meter_rect.adjusted(110, 15, -110, -15)); // Adjusted for new width
  painter.setPen(QPen(QColor(100, 149, 237, 50), 1));

  // Draw grid lines - fewer vertical lines for narrower area
  for (int i = 1; i < 5; ++i) {  // 4 vertical sections instead of 8
    int grid_x = meter_rect.left() + 110 + (i * (meter_width - 220) / 5);
    painter.drawLine(grid_x, meter_rect.top() + 15, grid_x, meter_rect.bottom() - 15);
  }
  // Horizontal grid lines based on height
  int h_sections = (meter_height > 120) ? 5 : 4;
  for (int i = 1; i < h_sections; ++i) {
    int grid_y = meter_rect.top() + 15 + (i * (meter_height - 30) / h_sections);
    painter.drawLine(meter_rect.left() + 15, grid_y, meter_rect.right() - 15, grid_y);
  }

  painter.setClipping(false);

  // Draw crosshairs - match narrower grid area
  QPointF center = meter_rect.center();
  painter.setPen(QPen(QColor(100, 149, 237, 100), 2));
  painter.drawLine(meter_rect.left() + 110, center.y(), meter_rect.right() - 110, center.y());
  painter.drawLine(center.x(), meter_rect.top() + 15, center.x(), meter_rect.bottom() - 15);

  // Get current G-forces
  float lateral_g = frame_state.gforce_state.lateral_g;
  float longitudinal_g = frame_state.gforce_state.longitudinal_g;
  float total_g = std::sqrt(lateral_g * lateral_g + longitudinal_g * longitudinal_g);

  // Clamp values
  lateral_g = std::clamp(lateral_g, -1.5f, 1.5f);
  longitudinal_g = std::clamp(longitudinal_g, -1.5f, 1.5f);

  // Calculate dot position - match narrower grid area
  const float max_g = 1.5f;
  const float usable_width = meter_width - 220;  // 110px margins on each side
  const float usable_height = meter_height - 30;

  float dot_x = center.x() + (lateral_g / max_g) * (usable_width / 2);
  float dot_y = center.y() - (longitudinal_g / max_g) * (usable_height / 2);

  // Draw G-force dot
  QColor dot_color;
  if (total_g > 1.2f) dot_color = QColor(255, 34, 34);
  else if (total_g > 0.8f) dot_color = QColor(255, 136, 68);
  else if (total_g > 0.3f) dot_color = QColor(68, 255, 68);
  else dot_color = QColor(68, 136, 255);

  painter.setBrush(dot_color);
  painter.setPen(QPen(Qt::white, 2));
  painter.drawEllipse(QPointF(dot_x, dot_y), 6, 6);

  // Draw center "G" label
  painter.setBrush(QColor(100, 149, 237, 40));
  painter.setPen(QPen(QColor(100, 149, 237), 2));
  painter.drawEllipse(center, 12, 12);

  painter.setPen(Qt::white);
  QFont centerFont("Inter", 10, QFont::Bold);
  painter.setFont(centerFont);
  painter.drawText(QRectF(center.x() - 12, center.y() - 12, 24, 24), Qt::AlignCenter, "G");

  // Draw internal value displays with larger text and repositioned
  QFont valueFont("Inter", 32, QFont::Bold);  // Increased from 24
  QFont labelFont("Inter", 20);               // Increased from 16

  // LAT (bottom-left) - positioned further left for symmetry
  QRect latRect(meter_rect.left() + 15, meter_rect.bottom() - 75, 95, 67);  // Further from edge
  painter.setBrush(QColor(44, 62, 80, 240));
  painter.setPen(QPen(QColor(0, 255, 127, 120), 1));
  painter.drawRoundedRect(latRect, 4, 4);

  painter.setFont(labelFont);
  painter.setPen(QColor(170, 170, 170));
  painter.drawText(latRect.adjusted(0, 5, 0, -35), Qt::AlignTop | Qt::AlignHCenter, "LAT");

  painter.setFont(valueFont);
  painter.setPen(std::abs(lateral_g) > 0.5f ? QColor(255, 136, 68) : Qt::white);
  // Prevent -0.0g/0.0g flipping
  float display_lateral = (std::abs(lateral_g) < 0.05f) ? 0.0f : lateral_g;
  painter.drawText(latRect.adjusted(0, 30, 0, -5), Qt::AlignBottom | Qt::AlignHCenter,
                  QString::number(display_lateral, 'f', 1) + "g");

  // LONG (top-right) - positioned further right for symmetry
  QRect longRect(meter_rect.right() - 110, meter_rect.top() + 8, 95, 67);  // Further from edge
  painter.setBrush(QColor(44, 62, 80, 240));
  painter.setPen(QPen(QColor(100, 149, 237, 120), 1));
  painter.drawRoundedRect(longRect, 4, 4);

  painter.setFont(labelFont);
  painter.setPen(QColor(170, 170, 170));
  painter.drawText(longRect.adjusted(0, 5, 0, -35), Qt::AlignTop | Qt::AlignHCenter, "LONG");

  painter.setFont(valueFont);
  painter.setPen(std::abs(longitudinal_g) > 0.5f ? QColor(255, 136, 68) : Qt::white);
  // Prevent -0.0g/0.0g flipping
  float display_longitudinal = (std::abs(longitudinal_g) < 0.05f) ? 0.0f : longitudinal_g;
  painter.drawText(longRect.adjusted(0, 30, 0, -5), Qt::AlignBottom | Qt::AlignHCenter,
                  QString::number(display_longitudinal, 'f', 1) + "g");

  // TOTAL block removed completely

  painter.restore();
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
    // Not in standstill - reset immediately
    frame_state.standstill_elapsed = 0.0;
    frame_state.standstill_start_time = current_time;
    frame_state.standstill_exit_time = 0.0;
  }

  // Draw stand still timer if active
  if (combined_standstill && frame_state.standstill_elapsed > 0.1) {
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

  // DEBUG: Uncomment to show all radar detections as dots
  // drawAllRadarPoints(painter, rect, s);
}

// DEBUG: Simple function to draw all radar detections
void BluepilotRenderer::drawAllRadarPoints(QPainter &painter, const QRect &rect, const UIState &s) {
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
    if (mapToScreen(lead_one.getDRel(), lead_one.getYRel(), path_offset_z, &pt)) {
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
    if (mapToScreen(lead_two.getDRel(), lead_two.getYRel(), path_offset_z, &pt)) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(255, 255, 0, 200)); // Yellow dot
      painter.drawEllipse(pt, 8, 8);

      // Add distance text
      painter.setPen(Qt::white);
      painter.setFont(QFont("Inter", 10));
      painter.drawText(pt.x() + 15, pt.y(), QString("%1m").arg(lead_two.getDRel(), 0, 'f', 1));
    }
  }
}

void BluepilotRenderer::updateLeadTracking(const UIState &s) {
  const SubMaster &sm = *(s.sm);

  // FIXED: Validate required messages before accessing
  if (!sm.valid("radarState") || !sm.valid("modelV2")) {
    static int error_counter = 0;
    if (error_counter++ % 50 == 0) {
      std::cerr << "WARNING: BluePilot radarState or modelV2 not valid in updateLeadTracking" << std::endl;
    }
    // Set all leads to inactive
    for (int i = 0; i < 2; ++i) {
      frame_state.lead_state.virtual_active[i] = false;
      frame_state.lead_state.stable[i] = false;
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

      // FIXED: Use the same curvature calculation as model_old.cc
      float path_curvature = (idx > 1) ? fabs(line_y[idx] - line_y[idx - 1]) : 0.0f;

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
        if (fabs(raw_yRel - path_y) > 2.0f) should_track = false;
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
          // FIXED: Use exact approach from model_old.cc for better curve handling
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
          float lateral_diff = raw_yRel - frame_state.lead_state.smoothed_yRel[i];
          if (fabs(lateral_diff) > max_lateral_change) {
            // Limit lateral movement rate for stability
            raw_yRel = frame_state.lead_state.smoothed_yRel[i] + ((lateral_diff > 0) ? max_lateral_change : -max_lateral_change);
          }

          // First smooth the raw radar reading
          float smoothed_raw = alpha * raw_yRel + (1.0f - alpha) * frame_state.lead_state.smoothed_yRel[i];

          // Then blend with the path position using dynamic path weight
          frame_state.lead_state.smoothed_yRel[i] = path_weight * path_y + (1.0f - path_weight) * smoothed_raw;
        }

        // FIXED: Use exact same approach as model_old.cc - no Y sign flip, use path_offset_z
        float path_offset_z = 0.0f;
        if (sm.valid("liveCalibration")) {
          const auto &live_calib = sm["liveCalibration"].getLiveCalibration();
          const auto &height_list = live_calib.getHeight();
          if (height_list.size() > 0) {
            path_offset_z = height_list[0];
          }
        }

        QPointF current_pos;
        if (mapToScreen(d_rel, frame_state.lead_state.smoothed_yRel[i], path_z + path_offset_z, &current_pos)) {
          bool reasonable_position = true;

          if (is_radar_assisted) {
            // Check if radar detection is reasonable
            QRectF screen_bounds = frame_state.clip_region;
            float margin = 100.0f;
            QRectF extended_bounds = screen_bounds.adjusted(-margin, -margin, margin, margin);

            if (!extended_bounds.contains(current_pos) || fabs(frame_state.lead_state.smoothed_yRel[i]) > 8.0f) {
              reasonable_position = false;
            }

            if (fabs(frame_state.lead_state.smoothed_yRel[i]) > 5.0f) {
              frame_state.lead_state.active_counter[i] = std::max(frame_state.lead_state.active_counter[i] - 1, 0);
              if (fabs(frame_state.lead_state.smoothed_yRel[i]) > 6.5f) {
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

    // Store lead data for time-to-lead calculation
    frame_state.lead_state.d_rel[i] = lead_data.getDRel();
    frame_state.lead_state.v_lead[i] = lead_data.getVLead();
    frame_state.lead_state.v_rel[i] = lead_data.getVRel();

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

  // FIXED: Validate required messages before accessing
  if (!sm.valid("modelV2") || !sm.valid("radarState") || !sm.valid("carState")) {
    static int error_counter = 0;
    if (error_counter++ % 50 == 0) {
      std::cerr << "WARNING: BluePilot required messages not valid in updateStopDetection" << std::endl;
    }
    frame_state.stop_state.active = false;
    frame_state.stop_state.stability_counter = 0;
    frame_state.stop_state.fade_alpha = std::max(0.0f, frame_state.stop_state.fade_alpha - 0.1f);
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

        // FIXED: Use path_offset_z like in model_old.cc
        QPointF screen_point;
        if (mapToScreen(x, y, z + path_offset_z, &screen_point)) {
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

  // static int debug_counter = 0;
  // if (debug_counter++ % 100 == 0) {
  //   std::cout << "BluePilot radar - radarState alive: " << sm.alive("radarState")
  //             << " show_radar flag: " << frame_state.show_radar << std::endl;
  // }

  // Show radar overlay if enabled, regardless of experimental mode
  if (!frame_state.show_radar || !sm.alive("radarState") || !sm.valid("radarState")) {
    return;
  }

  if (!icons_initialized) {
    initializeStaticData();
  }

  const auto &radar_state = sm["radarState"].getRadarState();

  // Get radar overlay size scale
  int radar_scale = s.scene.radar_overlay_size;
  float scale_factor = 1.0f;

  if (radar_scale == 1) {
    scale_factor = 0.95f;  // Small (was 0.7f)
  } else if (radar_scale == 2) {
    scale_factor = 1.15f; // Medium (was 0.85f)
  } else if (radar_scale == 3) {
    scale_factor = 1.35f;  // Normal (was 1.0f)
  } else if (radar_scale == 4) {
    scale_factor = 1.55f; // Large (was 1.15f)
  } else {
    scale_factor = 1.35f;  // Default to normal (was 1.0f)
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

  // static int stop_debug_counter = 0;
  // if (frame_state.stop_state.active && stop_debug_counter++ % 50 == 0) {
  //   std::cout << "BluePilot stop sign - active: " << frame_state.stop_state.active
  //             << " fade_alpha: " << frame_state.stop_state.fade_alpha
  //             << " distance: " << frame_state.stop_state.display_distance
  //             << " show_stop flag: " << frame_state.show_stop << std::endl;
  // }

  if (frame_state.stop_state.active || frame_state.stop_state.fade_alpha > 0.0f) {
    // Get position for stop sign (use last valid if current is invalid)
    QPointF screen_point = frame_state.stop_state.last_valid_position;
    const int stop_sign_size = static_cast<int>(frame_state.stop_state.smoothed_size);

    // FIXED: Position relative to lane lines as in original code
    if (!frame_state.lane_line_vertices[2].isEmpty() && !screen_point.isNull()) {
      // Find the closest point on the right lane line to the stopping point
      int closest_idx = 0;
      float min_dist = std::numeric_limits<float>::max();
      for (int i = 0; i < frame_state.lane_line_vertices[2].size(); ++i) {
        float dist = std::hypot(screen_point.x() - frame_state.lane_line_vertices[2][i].x(),
                               screen_point.y() - frame_state.lane_line_vertices[2][i].y());
        if (dist < min_dist) {
          min_dist = dist;
          closest_idx = i;
        }
      }

      // Position the stop sign to the right of the closest lane line point
      QPointF lane_point = frame_state.lane_line_vertices[2][closest_idx];
      QPointF stop_point(lane_point.x() + stop_sign_size * 0.75, lane_point.y());

      // Ensure the stop sign stays within the clip region
      if (frame_state.clip_region.contains(stop_point)) {
        drawStopSignOverlay(painter, stop_point, stop_sign_size,
                          frame_state.stop_state.display_distance, v_ego, frame_state.stop_state.fade_alpha);
      } else {
        // Adjust if partially out of bounds
        float adjusted_x = std::clamp(stop_point.x(),
                                     frame_state.clip_region.left() + stop_sign_size / 2,
                                     frame_state.clip_region.right() - stop_sign_size / 2);
        stop_point.setX(adjusted_x);
        if (frame_state.clip_region.contains(stop_point)) {
          drawStopSignOverlay(painter, stop_point, stop_sign_size,
                            frame_state.stop_state.display_distance, v_ego, frame_state.stop_state.fade_alpha);
        }
      }
    } else {
      // Fallback: Use the original stopping point if no lane line data
      if (frame_state.clip_region.contains(screen_point)) {
        drawStopSignOverlay(painter, screen_point, stop_sign_size,
                          frame_state.stop_state.display_distance, v_ego, frame_state.stop_state.fade_alpha);
      }
    }
  }
}

bool BluepilotRenderer::mapToScreen(float in_x, float in_y, float in_z, QPointF *out) {
  if (frame_state.transform.isZero()) {
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
  for (int i = 1; i < static_cast<int>(line_x.size()) && line_x[i] <= path_height; ++i) {
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
  const float v_rel = lead_data.getVRel();
  const float v_ego = frame_state.vehicle_speed;

  // Calculate sizes based on distance for responsive design with scale factor
  float base_sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 3.0;  // Reduced from 5.0
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

  // ========== THREE SEPARATE INFO BOXES ==========

  // Convert measurements for display
  float distance_m = d_rel;
  float lead_speed_mph = v_lead * 2.237;

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
  QString speedText = QString("%1mph").arg(qRound(lead_speed_mph));
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
