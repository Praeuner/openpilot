#include "selfdrive/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <chrono>

#include <QtConcurrent>
#include <QThreadPool>

#include "common/transformations/orientation.hpp"
#include "common/swaglog.h"
#include "common/util.h"
#include "common/watchdog.h"
#include "qt/util.h"
#include <iostream>
#include "system/hardware/hw.h"
#include "bluepilot/qt/offroad/panels/bp_recent_changes.h"
#include <limits>

#define BACKLIGHT_DT 0.05
#define BACKLIGHT_TS 10.00

void update_sockets(UIState *s) {
  s->sm->update(0);
}

void update_state(UIState *s) {
  SubMaster &sm = *(s->sm);
  UIScene &scene = s->scene;

  if (sm.updated("liveCalibration")) {
    auto list2rot = [](const capnp::List<float>::Reader &rpy_list) ->Eigen::Matrix3f {
      return euler2rot({rpy_list[0], rpy_list[1], rpy_list[2]}).cast<float>();
    };

    auto live_calib = sm["liveCalibration"].getLiveCalibration();
    if (live_calib.getCalStatus() == cereal::LiveCalibrationData::Status::CALIBRATED) {
      auto device_from_calib = list2rot(live_calib.getRpyCalib());
      auto wide_from_device = list2rot(live_calib.getWideFromDeviceEuler());
      s->scene.view_from_calib = VIEW_FROM_DEVICE * device_from_calib;
      s->scene.view_from_wide_calib = VIEW_FROM_DEVICE * wide_from_device * device_from_calib;
    } else {
      s->scene.view_from_calib = s->scene.view_from_wide_calib = VIEW_FROM_DEVICE;
    }
  }
  if (sm.updated("pandaStates")) {
    auto pandaStates = sm["pandaStates"].getPandaStates();
    if (pandaStates.size() > 0) {
      scene.pandaType = pandaStates[0].getPandaType();

      if (scene.pandaType != cereal::PandaState::PandaType::UNKNOWN) {
        scene.ignition = false;
        for (const auto& pandaState : pandaStates) {
          scene.ignition |= pandaState.getIgnitionLine() || pandaState.getIgnitionCan();
        }
      }
    }
  } else if ((s->sm->frame - s->sm->rcv_frame("pandaStates")) > 5*UI_FREQ) {
    scene.pandaType = cereal::PandaState::PandaType::UNKNOWN;
  }
  if (sm.updated("wideRoadCameraState")) {
    auto cam_state = sm["wideRoadCameraState"].getWideRoadCameraState();
    float scale = (cam_state.getSensor() == cereal::FrameData::ImageSensor::AR0231) ? 6.0f : 1.0f;
    scene.light_sensor = std::max(100.0f - scale * cam_state.getExposureValPercent(), 0.0f);
  } else if (!sm.allAliveAndValid({"wideRoadCameraState"})) {
    scene.light_sensor = -1;
  }
  scene.started = sm["deviceState"].getDeviceState().getStarted() && scene.ignition;

  auto params = Params();
  scene.recording_audio = params.getBool("RecordAudio") && scene.started;
}

void ui_update_params(UIState *s) {
  auto params = Params();
  s->scene.is_metric = params.getBool("IsMetric");
  s->scene.show_hybrid_drive_overlay = params.getBool("FordPrefHybridDriveOverlay"); // && params.getBool("FordPrefHevDataAvailable");
  s->scene.hybrid_drive_gauge_size = params.getInt("FordPrefHybridDriveGaugeSize");
  s->scene.show_hybrid_battery_overlay = params.getBool("FordPrefHybridBatteryOverlay"); // && params.getBool("FordPrefHevBattDataAvailable");
  s->scene.show_animated_wheel_angle = params.getBool("FordPrefShowAnimatedWheelAngle");
  s->scene.stand_still_timer = params.getBool("StandstillTimer");
  s->scene.show_bp_radar_overlay = params.getBool("FordPrefShowRadarLeadOverlay");
  s->scene.radar_overlay_size = params.getInt("FordPrefRadarOverlaySize");
  s->scene.show_blindspot_indicators = params.getBool("ShowBlindspotIndicators");
  s->scene.show_stop_indicator_overlay = params.getBool("ShowStopIndicatorOverlay");
  s->scene.show_gforce_meter = params.getBool("ShowGForceMeter");  // New parameter for G-force meter
  s->scene.show_brake_status = params.getBool("ShowBrakeStatus");

  s->scene.wide_camera_low_speed = params.getBool("ShowWideCameraAtLowSpeed");

  // std::cout << "hybrid_drive_gauge_size: " << s->scene.hybrid_drive_gauge_size << std::endl;
  // std::cout << "StandstillTimer: " << s->scene.stand_still_timer << std::endl;
}

void UIState::updateStatus() {
  if (scene.started && (sm->updated("selfdriveState") || sm->updated("selfdriveStateSP"))) {
    auto ss = (*sm)["selfdriveState"].getSelfdriveState();
    auto mads = (*sm)["selfdriveStateSP"].getSelfdriveStateSP().getMads();
    auto state = ss.getState();
    auto state_mads = mads.getState();
    if (state == cereal::SelfdriveState::OpenpilotState::PRE_ENABLED || state == cereal::SelfdriveState::OpenpilotState::OVERRIDING ||
        state_mads == cereal::ModularAssistiveDrivingSystem::ModularAssistiveDrivingSystemState::PAUSED ||
        state_mads == cereal::ModularAssistiveDrivingSystem::ModularAssistiveDrivingSystemState::OVERRIDING) {
      status = STATUS_OVERRIDE;
    } else {
      if (mads.getAvailable()) {
        if (mads.getEnabled() && ss.getEnabled()) {
          status = STATUS_ENGAGED;
        } else if (mads.getEnabled()) {
          status = STATUS_LAT_ONLY;
        } else if (ss.getEnabled()) {
          status = STATUS_LONG_ONLY;
        } else {
          status = STATUS_DISENGAGED;
        }
      } else {
        status = ss.getEnabled() ? STATUS_ENGAGED : STATUS_DISENGAGED;
      }
    }
  }

  if (engaged() != engaged_prev) {
    engaged_prev = engaged();
    emit engagedChanged(engaged());
  }

  // Handle onroad/offroad transition
  if (scene.started != started_prev || sm->frame == 1) {
    if (scene.started) {
      status = STATUS_DISENGAGED;
      scene.started_frame = sm->frame;
    }
    started_prev = scene.started;
    emit offroadTransition(!scene.started);

    // Check and show recent changes when going offroad
    if (!scene.started && started_prev) {
      QTimer::singleShot(2000, []() {
        RecentChangesManager::getInstance().showChangesDialog(nullptr);
      });
    }
  }
}

UIState::UIState(QObject *parent) : QObject(parent) {
  sm = std::make_unique<SubMaster>(std::vector<const char*>{
    "modelV2", "controlsState", "liveCalibration", "radarState", "deviceState",
    "pandaStates", "carParams", "driverMonitoringState", "carState", "driverStateV2",
    "wideRoadCameraState", "managerState", "selfdriveState", "longitudinalPlan",
    // BluePilot custom state with extended signals (e.g., brake light status)
    "carStateBP",
  });
  prime_state = new PrimeState(this);
  language = QString::fromStdString(Params().get("LanguageSetting"));

#ifndef SUNNYPILOT
  // update timer
  timer = new QTimer(this);
  QObject::connect(timer, &QTimer::timeout, this, &UIState::update);
  timer->start(1000 / UI_FREQ);
#endif

  // Check for recent changes after a short delay to allow UI to initialize
  QTimer::singleShot(3000, []() {
    if (RecentChangesManager::getInstance().shouldShowChanges()) {
      std::cout << "New version detected, showing recent changes dialog" << std::endl;
      RecentChangesManager::getInstance().showChangesDialog(nullptr);
    }
  });
}

void UIState::update() {
#ifndef SUNNYPILOT
  update_sockets(this);
  update_state(this);
  updateStatus();

  if (sm->frame % UI_FREQ == 0) {
    watchdog_kick(nanos_since_boot());
  }
  emit uiUpdate(*this);
#endif
}

Device::Device(QObject *parent) : brightness_filter(BACKLIGHT_OFFROAD, BACKLIGHT_TS, BACKLIGHT_DT), QObject(parent) {
  setAwake(true);
  resetInteractiveTimeout();

  // Initialize BluePilot brightness control parameters
  bp_brightness_mode = QString::fromStdString(Params().get("BpDisplayBrightnessMode")).toInt();
  bp_dim_level = QString::fromStdString(Params().get("BpDisplayBrightnessDimLevel")).toInt();
  bp_timeout = QString::fromStdString(Params().get("BpDisplayBrightnessTimeout")).toInt();
  bp_brightness_timeout = 0;
  bp_auto_brightness_override = false;
  bp_alert_active = false;
  bp_saved_brightness = -1;
  bp_params_changed = false;
  bp_brightness_failure_count = 0;
  bp_last_brightness_attempt = std::chrono::steady_clock::now();

  // Validate initial parameter values
  if (!validateBrightnessValue(bp_dim_level)) {
    bp_dim_level = 70; // fallback to default
  }
  if (bp_timeout < 10 || bp_timeout > 120) {
    bp_timeout = 30; // fallback to default
  }


#ifndef SUNNYPILOT
  QObject::connect(uiState(), &UIState::uiUpdate, this, &Device::update);
#endif
}

void Device::update(const UIState &s) {
  updateBrightness(s);
  updateWakefulness(s);
  updateBpBrightnessControl(s);
}

void Device::setAwake(bool on) {
  if (on != awake) {
    awake = on;
    Hardware::set_display_power(awake);
    LOGD("setting display power %d", awake);
    emit displayPowerChanged(awake);
  }
}

void Device::resetInteractiveTimeout(int timeout) {
  int customTimeout = QString::fromStdString(Params().get("InteractivityTimeout")).toInt();
  if (timeout == -1) {
    timeout = customTimeout == 0 ? (ignition_on ? 10 : 30) : customTimeout;
  }
  interactive_timeout = timeout * UI_FREQ;

  // Reset BluePilot brightness timeout on touch events
  resetBpBrightnessTimeout();
}

void Device::updateBrightness(const UIState &s) {
  int brightness;
  int brightness_override = QString::fromStdString(Params().get("Brightness")).toInt();
  float clipped_brightness = offroad_brightness;

  // If BluePilot brightness control is overriding auto brightness, skip normal brightness logic
  if (bp_auto_brightness_override) {
    return; // Brightness is handled by updateBpBrightnessControl
  }

  // Save current brightness for potential BluePilot restoration
  if (bp_saved_brightness == -1 && s.scene.started) {
    bp_saved_brightness = last_brightness;
  }

  if (s.scene.started && s.scene.light_sensor >= 0) {
    clipped_brightness = s.scene.light_sensor;

    // CIE 1931 - https://www.photonstophotos.net/GeneralTopics/Exposure/Psychometric_Lightness_and_Gamma.htm
    if (clipped_brightness <= 8) {
      clipped_brightness = (clipped_brightness / 903.3);
    } else {
      clipped_brightness = std::pow((clipped_brightness + 16.0) / 116.0, 3.0);
    }

    if (brightness_override == 1) {
      clipped_brightness = std::clamp(100.0f * clipped_brightness, 1.0f, 100.0f);  // Scale back to 1% to 100%
    } else if (brightness_override == 0) {
      clipped_brightness = std::clamp(100.0f * clipped_brightness, 10.0f, 100.0f);  // Scale back to 10% to 100%
    }
  }

  if (brightness_override == 0 || brightness_override == 1) {
    brightness = brightness_filter.update(clipped_brightness);
  } else {
    brightness = brightness_override;
  }

  if (!awake) {
    brightness = 0;
  }

  setBrightnessSafe(brightness);
}

void Device::updateWakefulness(const UIState &s) {
  bool ignition_just_turned_off = !s.scene.ignition && ignition_on;
  ignition_on = s.scene.ignition;

  if (ignition_just_turned_off) {
    resetInteractiveTimeout();
  } else if (interactive_timeout > 0 && --interactive_timeout == 0) {
    emit interactiveTimeout();
  }

  setAwake(s.scene.ignition || interactive_timeout > 0);
}

void Device::updateBpBrightnessControl(const UIState &s) {
  // Only apply BluePilot brightness control when onroad
  if (!s.scene.started) {
    // Reset override when going offroad
    if (bp_auto_brightness_override) {
      bp_auto_brightness_override = false;
      bp_saved_brightness = -1;
    }
    return;
  }

  // Read BluePilot brightness control parameters only once per second to reduce overhead
  static int param_update_counter = 0;
  if (param_update_counter++ % UI_FREQ == 0) {
    int new_mode = QString::fromStdString(Params().get("BpDisplayBrightnessMode")).toInt();
    int new_dim_level = QString::fromStdString(Params().get("BpDisplayBrightnessDimLevel")).toInt();
    int new_timeout = QString::fromStdString(Params().get("BpDisplayBrightnessTimeout")).toInt();

    // Check for parameter changes
    if (new_mode != bp_brightness_mode || new_dim_level != bp_dim_level || new_timeout != bp_timeout) {
      bp_params_changed = true;
      bp_brightness_mode = new_mode;
      bp_dim_level = validateBrightnessValue(new_dim_level) ? new_dim_level : 70;
      bp_timeout = (new_timeout >= 10 && new_timeout <= 120) ? new_timeout : 30;

      // Reset timeout when parameters change
      resetBpBrightnessTimeout();
    }
  }

  // Check if alert is active using the existing events system
  bool current_alert_active = isAlertActive(s);

  // Log state changes and reset timeout when alert becomes active
  if (current_alert_active != bp_alert_active) {
    if (current_alert_active) {
      std::cout << "[BP_BRIGHTNESS] Alert detected, resetting timeout" << std::endl;
      resetBpBrightnessTimeout();
    } else {
      std::cout << "[BP_BRIGHTNESS] Alert cleared" << std::endl;
    }
  }
  bp_alert_active = current_alert_active;

  // If always on mode (0), don't override auto brightness
  if (bp_brightness_mode == 0) {
    if (bp_auto_brightness_override) {
      std::cout << "[BP_BRIGHTNESS] Switching to always-on mode, restoring auto brightness" << std::endl;
      restoreAutoBrightness(s);
    }
    return;
  }

  // For dim/off modes, we need to override auto brightness
  if (!bp_auto_brightness_override) {
    std::cout << "[BP_BRIGHTNESS] Switching to dim/off mode, overriding auto brightness" << std::endl;
    // Save current brightness before overriding
    if (bp_saved_brightness == -1 && last_brightness > 0) {
      bp_saved_brightness = last_brightness;
    }
    bp_auto_brightness_override = true;
  }

  // Decrement timeout counter
  if (bp_brightness_timeout > 0) {
    bp_brightness_timeout--;
    // Only show debug countdown at specific intervals to reduce spam
    int remaining_seconds = bp_brightness_timeout / UI_FREQ;
    if (bp_brightness_timeout % (UI_FREQ * 10) == 0 && remaining_seconds > 0) { // Debug every 10 seconds
      std::cout << "[BP_BRIGHTNESS] Timeout countdown: " << remaining_seconds << "s remaining" << std::endl;
    }
  }

  // Apply brightness control based on mode and timeout
  if (bp_brightness_timeout == 0) {
    if (bp_brightness_mode == 1) {
      // Dim mode - set to dim level
      if (last_brightness != bp_dim_level) {
        std::cout << "[BP_BRIGHTNESS] Dimming display to " << bp_dim_level << "%" << std::endl;
        setBrightnessSafe(bp_dim_level);
      }
    } else if (bp_brightness_mode == 2) {
      // Off mode - turn off display
      if (awake) {
        std::cout << "[BP_BRIGHTNESS] Turning off display" << std::endl;
        setAwake(false);
      }
    }
  }
}

void Device::resetBpBrightnessTimeout() {
  bp_brightness_timeout = bp_timeout * UI_FREQ;

  // If we're in dim/off mode and timeout was reset, restore normal brightness
  if (bp_brightness_mode == 1 && bp_auto_brightness_override) {
    std::cout << "[BP_BRIGHTNESS] Restoring brightness after timeout reset" << std::endl;
    bp_auto_brightness_override = false;
    // Restore saved brightness if available
    if (bp_saved_brightness > 0) {
      setBrightnessSafe(bp_saved_brightness);
    }
  } else if (bp_brightness_mode == 2 && !awake) {
    std::cout << "[BP_BRIGHTNESS] Waking up display after timeout reset" << std::endl;
    setAwake(true);
    bp_auto_brightness_override = false;
    // Restore saved brightness if available
    if (bp_saved_brightness > 0) {
      setBrightnessSafe(bp_saved_brightness);
    }
  }
}

bool Device::isAlertActive(const UIState &s) {
  // Use the existing events system - only check for alerts that require user attention
  if (s.sm && s.sm->rcv_frame("selfdriveState") > 0) {
    const auto& ss = (*s.sm)["selfdriveState"].getSelfdriveState();
    // Only reset brightness timeout for alerts that require user attention
    return ss.getAlertStatus() == cereal::SelfdriveState::AlertStatus::USER_PROMPT ||
           ss.getAlertStatus() == cereal::SelfdriveState::AlertStatus::CRITICAL;
  }
  return false;
}

void Device::resetOnroadDisplayTimer() {
  // Reset both interactive timeout and BluePilot brightness timeout on touch
  resetInteractiveTimeout();
}

void Device::setBrightnessSafe(int brightness) {
  // Allow 0 for display off
  if (brightness != 0 && !validateBrightnessValue(brightness)) {
    std::cout << "[BP_BRIGHTNESS] Invalid brightness value: " << brightness << ", using fallback" << std::endl;
    brightness = std::clamp(brightness, 1, 100);
  }

  if (brightness != last_brightness) {
    if (!brightness_future.isRunning()) {
      auto now = std::chrono::steady_clock::now();

      // Rate limiting: don't attempt brightness changes too frequently after failures
      if (bp_brightness_failure_count > 3) {
        auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - bp_last_brightness_attempt);
        if (time_since_last.count() < 1000) { // Wait 1 second between attempts after failures
          return;
        }
      }

      bp_last_brightness_attempt = now;

      // Direct synchronous call - safer than threading
      try {
        Hardware::set_brightness(brightness);
        bp_brightness_failure_count = 0;
        last_brightness = brightness;
      } catch (const std::exception& e) {
        bp_brightness_failure_count++;
        std::cout << "[BP_BRIGHTNESS] Hardware brightness failure: " << e.what() << std::endl;

        // After multiple failures, try fallback brightness
        if (bp_brightness_failure_count >= 5) {
          try {
            std::cout << "[BP_BRIGHTNESS] Attempting fallback brightness of 50%" << std::endl;
            Hardware::set_brightness(50);
            bp_brightness_failure_count = 0;
            last_brightness = 50;
          } catch (...) {
            std::cout << "[BP_BRIGHTNESS] Fallback brightness also failed" << std::endl;
          }
        }
      } catch (...) {
        std::cout << "[BP_BRIGHTNESS] Unknown error setting brightness" << std::endl;
      }
    }
  }
}

void Device::restoreAutoBrightness(const UIState &s) {
  bp_auto_brightness_override = false;

  // Restore saved brightness or trigger auto brightness update
  if (bp_saved_brightness > 0 && s.scene.started) {
    setBrightnessSafe(bp_saved_brightness);
    bp_saved_brightness = -1;
  }
}

bool Device::validateBrightnessValue(int brightness) {
  return brightness == 0 || (brightness >= 1 && brightness <= 100);
}

#ifndef SUNNYPILOT
UIState *uiState() {
  static UIState ui_state;
  return &ui_state;
}

Device *device() {
  static Device _device;
  return &_device;
}
#endif
