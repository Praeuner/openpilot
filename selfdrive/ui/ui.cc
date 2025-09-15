#include "selfdrive/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <chrono>

#include <QtConcurrent>
#include <QThreadPool>
#include <QApplication>
#include <QWidget>

#include "common/transformations/orientation.hpp"
#include "common/swaglog.h"
#include "common/util.h"
#include "common/watchdog.h"
#include "qt/util.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include "system/hardware/hw.h"

// Helper function to generate timestamp for BP_BRIGHTNESS logging
std::string bp_timestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

  std::stringstream ss;
  ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
  ss << "." << std::setfill('0') << std::setw(3) << ms.count();
  return ss.str();
}

#define BP_LOG(msg) std::cout << "[BP_BRIGHTNESS " << bp_timestamp() << "] " << msg << std::endl
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

  BP_LOG("Initializing BluePilot brightness control");

  // Initialize BluePilot brightness control timer
  bp_brightness_timer = new QTimer(this);
  bp_brightness_timer->setSingleShot(true);
  connect(bp_brightness_timer, &QTimer::timeout, this, &Device::onBpBrightnessTimeout);
  BP_LOG("Timer created and connected");

  alert_reset_timer = new QTimer(this);
  alert_reset_timer->setInterval(5000);
  alert_reset_timer->setSingleShot(false);
  connect(alert_reset_timer, &QTimer::timeout, this, &Device::onAlertReset);

  // Read and validate initial parameters
  auto params = Params();
  bp_brightness_mode = QString::fromStdString(params.get("BpDisplayBrightnessMode")).toInt();
  bp_dim_level = QString::fromStdString(params.get("BpDisplayBrightnessDimLevel")).toInt();
  bp_timeout = QString::fromStdString(params.get("BpDisplayBrightnessTimeout")).toInt();

  // Validate parameter values with fallbacks
  if (bp_dim_level < 20 || bp_dim_level > 90) {
    BP_LOG("Invalid dim level " << bp_dim_level << ", using default 70%");
    bp_dim_level = 70;
  }
  if (bp_timeout < 10 || bp_timeout > 120) {
    BP_LOG("Invalid timeout " << bp_timeout << ", using default 30s");
    bp_timeout = 30;
  }
  if (bp_brightness_mode < 0 || bp_brightness_mode > 2) {
    BP_LOG("Invalid mode " << bp_brightness_mode << ", using default 0 (always on)");
    bp_brightness_mode = 0;
  }

  BP_LOG("Initialized with mode=" << bp_brightness_mode
            << ", dim_level=" << bp_dim_level << "%, timeout=" << bp_timeout << "s");

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
    BP_LOG("Display power changed to " << (awake ? "ON" : "OFF"));
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

  // Also trigger user interaction for BluePilot brightness
  onUserInteraction();
}

void Device::updateBrightness(const UIState &s) {
  // --- BluePilot Dim/Off Hold (drop-in guard) ---
  static Params params;
  static bool dim_session_active = false;
  static int dim_start_pct = 0;
  static int dim_target_pct = 8;
  static int64_t dim_begin_ms = 0;

  static bool off_session_active = false;
  static int off_start_pct = 0;
  static int64_t off_begin_ms = 0;
  static bool panel_off_applied = false;

  const int64_t now_ms = nanos_since_boot() / 1000000LL;

  // 1) OFF has highest priority
  if (params.getBool("BPDisplayOffActive")) {
    if (!off_session_active) {
      off_start_pct = last_brightness;
      off_begin_ms = now_ms;
      off_session_active = true;
      panel_off_applied = false;
    }
    // Fade to 0, then power off panel/backlight once
    const int duration_ms = 300;
    const float t = std::clamp((now_ms - off_begin_ms) / float(duration_ms), 0.0f, 1.0f);
    const int eased = int(off_start_pct + (0 - off_start_pct) * t);
    if (eased != last_brightness && !brightness_future.isRunning()) {
      brightness_future = QtConcurrent::run(Hardware::set_brightness, eased);
      last_brightness = eased;
    }
    if (t >= 1.0f && !panel_off_applied) {
      Hardware::set_display_power(false);
      panel_off_applied = true;
    }
    return; // skip auto while off
  } else if (off_session_active) {
    // Waking up: turn panel back on before auto resumes
    if (panel_off_applied) {
      Hardware::set_display_power(true);
      panel_off_applied = false;
    }
    off_session_active = false;
  }

  // 2) DIM mode
  if (params.getBool("BPDimActive")) {
    if (!dim_session_active) {
      dim_start_pct = last_brightness;
      try {
        std::string v = params.get("BPDimLevel");
        dim_target_pct = v.empty() ? bp_dim_level : std::clamp(std::stoi(v), 1, 100);
      } catch (...) { dim_target_pct = bp_dim_level; }
      dim_begin_ms = now_ms;
      dim_session_active = true;
    }
    const int duration_ms = 350;
    const float t = std::clamp((now_ms - dim_begin_ms) / float(duration_ms), 0.0f, 1.0f);
    const int eased = int(dim_start_pct + (dim_target_pct - dim_start_pct) * t);
    int target = (t >= 1.0f) ? dim_target_pct : eased;
    if (target != last_brightness && !brightness_future.isRunning()) {
      brightness_future = QtConcurrent::run(Hardware::set_brightness, target);
      last_brightness = target;
    }
    return; // skip auto while dimmed
  }

  if (dim_session_active) dim_session_active = false;
  // --- end BluePilot Dim/Off Hold ---

  // When BluePilot is controlling brightness, skip auto brightness logic
  if (bp_auto_brightness_override) {
    // Don't update brightness - let updateBpBrightnessControl handle it
    return;
  }

  int brightness;
  int brightness_override = QString::fromStdString(Params().get("Brightness")).toInt();
  float clipped_brightness = offroad_brightness;

  // Normal auto-brightness logic when not overridden by BluePilot
  if (s.scene.started && s.scene.light_sensor >= 0) {
    clipped_brightness = s.scene.light_sensor;

    // CIE 1931 - https://www.photonstophotos.net/GeneralTopics/Exposure/Psychometric_Lightness_and_Gamma.htm
    if (clipped_brightness <= 8) {
      clipped_brightness = (clipped_brightness / 903.3);
    } else {
      clipped_brightness = std::pow((clipped_brightness + 16.0) / 116.0, 3.0);
    }

    if (brightness_override == 1) {
      clipped_brightness = std::clamp(100.0f * clipped_brightness, 1.0f, 100.0f);
    } else if (brightness_override == 0) {
      clipped_brightness = std::clamp(100.0f * clipped_brightness, 10.0f, 100.0f);
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

  if (brightness != last_brightness) {
    if (!brightness_future.isRunning()) {
      brightness_future = QtConcurrent::run(Hardware::set_brightness, brightness);
      last_brightness = brightness;
    }
  }
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
    // Reset everything when going offroad
    if (bp_state != BP_NORMAL) {
      BP_LOG("Going offroad, resetting BP control from "
                << getBpStateString() << " to BP_NORMAL");
      restoreFromBpControl();
    }
    Params().putBool("BPDimActive", false);
    Params().putBool("BPDisplayOffActive", false);
    updateBpStatusText(s);
    return;
  }

  // Auto-start timer on onroad transition with active mode
  static bool was_started = false;
  if (!was_started && s.scene.started && bp_brightness_mode > 0) {
    was_started = true;
    bp_state = BP_COUNTDOWN;
    bp_brightness_timer->start(bp_timeout * 1000);
    BP_LOG("Auto-starting timer on onroad transition: "
              << bp_timeout << "s (mode=" << bp_brightness_mode << ")");
  } else if (!s.scene.started) {
    was_started = false;
  }

  // Read parameters periodically (every 1 second)
  static int param_update_counter = 0;
  if (param_update_counter++ % UI_FREQ == 0) {
    int new_mode = QString::fromStdString(Params().get("BpDisplayBrightnessMode")).toInt();
    int new_dim_level = QString::fromStdString(Params().get("BpDisplayBrightnessDimLevel")).toInt();
    int new_timeout = QString::fromStdString(Params().get("BpDisplayBrightnessTimeout")).toInt();

    // Validate new parameters
    new_dim_level = (new_dim_level >= 20 && new_dim_level <= 90) ? new_dim_level : 70;
    new_timeout = (new_timeout >= 10 && new_timeout <= 120) ? new_timeout : 30;
    new_mode = (new_mode >= 0 && new_mode <= 2) ? new_mode : 0;

    if (new_mode != bp_brightness_mode || new_dim_level != bp_dim_level || new_timeout != bp_timeout) {
      BP_LOG("Parameter change detected: mode " << bp_brightness_mode
                << "→" << new_mode << ", dim " << bp_dim_level << "→" << new_dim_level
                << "%, timeout " << bp_timeout << "→" << new_timeout << "s");

      bp_brightness_mode = new_mode;
      bp_dim_level = new_dim_level;
      bp_timeout = new_timeout;

      // Handle immediate mode changes
      if (new_mode == 0) {
        // Switching to always-on mode
        BP_LOG("Switching to always-on mode");
        restoreFromBpControl();
        return;
      } else {
        // Restart timer with new timeout
        resetBpBrightnessTimeout();
      }
    }
  }

  // Check for alerts
  bool current_alert_active = isAlertActive(s);
  if (current_alert_active != bp_alert_active) {
    if (current_alert_active) {
      BP_LOG("Alert detected, resetting timeout");
      onUserInteraction();
      if (!alert_reset_timer->isActive()) {
        alert_reset_timer->start();
      }
    } else {
      BP_LOG("Alert cleared, restarting full timeout");
      onUserInteraction();
      if (alert_reset_timer->isActive()) {
        alert_reset_timer->stop();
      }
    }
    bp_alert_active = current_alert_active;
  }

  // Mode 0 = always on (pure stock behavior)
  if (bp_brightness_mode == 0) {
    if (bp_state != BP_NORMAL) {
      BP_LOG("Always-on mode active, restoring stock behavior");
      restoreFromBpControl();
    }
    updateBpStatusText(s);
    return;
  }

  // Start countdown timer if not already active and we're in normal state
  if (bp_state == BP_NORMAL && !bp_brightness_timer->isActive()) {
    bp_state = BP_COUNTDOWN;
    bp_brightness_timer->start(bp_timeout * 1000);
    BP_LOG("Starting countdown timer: " << bp_timeout
              << "s (state: " << getBpStateString() << ")");
  }

  updateBpStatusText(s);

  // Handle dimmed state - continuously apply dim brightness
  if (bp_state == BP_DIMMED && bp_brightness_mode == 1) {
    int filtered_brightness = brightness_filter.update(bp_dim_level);
    if (!brightness_future.isRunning() && filtered_brightness != last_brightness) {
      brightness_future = QtConcurrent::run(Hardware::set_brightness, filtered_brightness);
      last_brightness = filtered_brightness;
      // Only log occasionally to avoid spam
      static int dim_log_counter = 0;
      if (dim_log_counter++ % (UI_FREQ * 10) == 0) { // Every 10 seconds
        BP_LOG("Maintaining dim brightness at " << filtered_brightness << "%");
      }
    }
  }
}

void Device::resetBpBrightnessTimeout() {
  if (bp_brightness_mode == 0) {
    // No need to set timeout in always-on mode
    return;
  }

  // Stop any existing timer
  if (bp_brightness_timer->isActive()) {
    bp_brightness_timer->stop();
    BP_LOG("Stopped existing timer");
  }

  // Only start timer if we're onroad and not in always-on mode
  if (bp_brightness_mode > 0) {
    bp_brightness_timer->start(bp_timeout * 1000);
    BP_LOG("Reset timeout to " << bp_timeout << "s (timer active: "
              << (bp_brightness_timer->isActive() ? "YES" : "NO") << ")");

    // Update state
    if (bp_state == BP_DIMMED || bp_state == BP_OFF) {
      BP_LOG("Restoring from " << getBpStateString());
      restoreFromBpControl();
    }
    bp_state = BP_COUNTDOWN;
  }
}

void Device::restoreFromBpControl() {
  BP_LOG("Restoring from BP control, current state: " << getBpStateString());

  // Clear the dim/off active flags
  Params().putBool("BPDimActive", false);
  Params().putBool("BPDisplayOffActive", false);

  // Stop timer
  if (bp_brightness_timer->isActive()) {
    bp_brightness_timer->stop();
    BP_LOG("Timer stopped");
  }

  if (alert_reset_timer->isActive()) {
    alert_reset_timer->stop();
  }

  // Restore display power if it was turned off
  if (bp_state == BP_OFF && !awake) {
    BP_LOG("Waking display from OFF state");
    setAwake(true);
  }

  // Restore brightness control to auto
  if (bp_auto_brightness_override) {
    BP_LOG("Restoring auto brightness control (was overridden)");
    bp_auto_brightness_override = false;

    // Reset brightness filter for smooth transition
    if (bp_saved_brightness > 0) {
      brightness_filter.reset(bp_saved_brightness);
      BP_LOG("Restored brightness filter to saved value: " << bp_saved_brightness << "%");
    }
    bp_saved_brightness = -1;
  }

  // Reset state
  bp_state = BP_NORMAL;
  BP_LOG("State restored to " << getBpStateString());
}

bool Device::isAlertActive(const UIState &s) {
  // Check for alerts that require user attention
  if (s.sm && s.sm->rcv_frame("selfdriveState") > 0) {
    const auto& ss = (*s.sm)["selfdriveState"].getSelfdriveState();
    bool alert_active = ss.getAlertStatus() == cereal::SelfdriveState::AlertStatus::USER_PROMPT ||
                       ss.getAlertStatus() == cereal::SelfdriveState::AlertStatus::CRITICAL;

    // Log alert state changes
    static bool last_alert_state = false;
    if (alert_active != last_alert_state) {
      BP_LOG("Alert state changed: "
                << (alert_active ? "ACTIVE" : "INACTIVE")
                << " (type: " << static_cast<int>(ss.getAlertStatus()) << ")");
      last_alert_state = alert_active;
    }

    return alert_active;
  }
  return false;
}

void Device::setBrightnessSafe(int brightness) {
  // Validate brightness value
  if (brightness != 0 && (brightness < 1 || brightness > 100)) {
    BP_LOG("Invalid brightness value: " << brightness << ", clamping");
    brightness = std::clamp(brightness, 1, 100);
  }

  if (brightness != last_brightness) {
    // Wait for any running brightness operation to complete
    if (brightness_future.isRunning()) {
      brightness_future.waitForFinished();
    }

    // Set brightness asynchronously
    brightness_future = QtConcurrent::run(Hardware::set_brightness, brightness);
    last_brightness = brightness;
    BP_LOG("Setting hardware brightness to " << brightness << "%");
  }
}

const char* Device::getBpStateString() const {
  switch (bp_state) {
    case BP_NORMAL: return "BP_NORMAL";
    case BP_COUNTDOWN: return "BP_COUNTDOWN";
    case BP_DIMMED: return "BP_DIMMED";
    case BP_OFF: return "BP_OFF";
    default: return "BP_UNKNOWN";
  }
}

void Device::resetOnroadDisplayTimer() {
  // Add throttling to prevent spam and identify caller
  static auto last_call = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_call);

  if (elapsed.count() < 100) {  // Less than 100ms since last call
    static int spam_count = 0;
    if (++spam_count % 50 == 0) {  // Log every 50th spam call
      BP_LOG("WARNING: resetOnroadDisplayTimer() called "
                << spam_count << " times rapidly! Check Qt connections.");
    }
    return;  // Ignore rapid-fire calls
  }

  BP_LOG("Display timer reset via external call (elapsed: "
            << elapsed.count() << "ms)");
  last_call = now;
  resetInteractiveTimeout();
}

void Device::onUserInteraction() {
  static auto last_interaction = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_interaction).count();

  if (elapsed < 100) {
    static int spam_count = 0;
    if (++spam_count % 50 == 0) {
      BP_LOG("WARNING: onUserInteraction() called " << spam_count << " times rapidly!");
    }
    return;
  }
  last_interaction = now;

  BP_LOG("User interaction detected, current state: " << getBpStateString());

  // Clear both dim and off flags
  Params().putBool("BPDimActive", false);
  Params().putBool("BPDisplayOffActive", false);

  // Reset BluePilot brightness timeout
  resetBpBrightnessTimeout();

  // Note: resetInteractiveTimeout() is called by the caller, so we don't need to call it again
}

void Device::onAlertReset() {
  if (bp_alert_active) {
    BP_LOG("Periodic alert reset triggered");
    onUserInteraction();
  }
}

void Device::onBpBrightnessTimeout() {
  BP_LOG("Timeout triggered! Current state: " << getBpStateString()
            << ", mode: " << bp_brightness_mode);

  Params p;

  if (bp_brightness_mode == 1) {
    // Mode 1: Dim the display using the new params approach
    BP_LOG("Setting dim params: level=" << bp_dim_level << "%");

    // Set the dim level and activate dimming
    p.put("BPDimLevel", std::to_string(bp_dim_level));
    p.putBool("BPDimActive", true);
    bp_state = BP_DIMMED;
  } else if (bp_brightness_mode == 2) {
    // Mode 2: Turn off display with fade-to-black
    BP_LOG("Turning off display with fade-to-black");

    // Save current brightness and override auto
    if (!bp_auto_brightness_override) {
      bp_saved_brightness = last_brightness;
      brightness_filter.reset(bp_saved_brightness);
      bp_auto_brightness_override = true;
      BP_LOG("Saved current brightness: " << bp_saved_brightness << "% and overridden auto");
    }

    // Trigger the fade-to-black and panel power off
    p.putBool("BPDisplayOffActive", true);
    bp_state = BP_OFF;
  }

  BP_LOG("Timeout handling complete, new state: " << getBpStateString());
}

void Device::updateBpStatusText(const UIState &s) {
  static QString prev_status;
  static int touch_counter = 0;
  static auto last_touch = std::chrono::steady_clock::now();
  static bool debug_logged = false;

  if (!debug_logged) {
    std::cout << "[STATUS_DEBUG] updateBpStatusText called for first time" << std::endl;
    debug_logged = true;
  }

  QString new_status;
  if (!s.scene.started) {
    new_status = "Offroad";
  } else if (bp_brightness_mode == 0) {
    new_status = "Auto";
  } else if (bp_alert_active) {
    new_status = "Alert Active";
  } else if (bp_state == BP_COUNTDOWN) {
    int remaining = (bp_brightness_timer->remainingTime() + 999) / 1000;
    if (bp_brightness_mode == 1) {
      new_status = QString("Dim to %1%: %2s").arg(bp_dim_level).arg(remaining);
    } else {
      new_status = QString("Off in %1s").arg(remaining);
    }
  } else if (bp_state == BP_DIMMED) {
    new_status = QString("Dimmed to %1%").arg(bp_dim_level);
  } else if (bp_state == BP_OFF) {
    new_status = "Display Off";
  } else {
    new_status = "Normal";
  }

  // Check for recent touch events
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_touch).count();
  if (elapsed < 2000 && touch_counter > 0) {
    new_status = "Touch Event";
    touch_counter--;
  }

  if (new_status != prev_status) {
    prev_status = new_status;
    const_cast<UIScene&>(s.scene).bp_status_text = new_status;
    BP_LOG("Status updated: " << new_status.toStdString());
    std::cout << "[STATUS_DEBUG] Setting s.scene.bp_status_text to: '" << new_status.toStdString() << "'" << std::endl;

    // Render BP status text in bottom right - temporary for testing
    QApplication *app = qApp;
    if (app) {
      QWidget *main_window = app->activeWindow();
      if (main_window) {
        // Create a temporary overlay to show the status text
        static QLabel *status_label = nullptr;
        if (!status_label) {
          status_label = new QLabel(main_window);
          status_label->setStyleSheet("QLabel { color: white; background-color: rgba(0,0,0,128); padding: 5px; }");
          status_label->setFont(QFont("Inter", 24, QFont::Bold));
        }

        status_label->setText(new_status);
        status_label->adjustSize();

        // Position in bottom right corner
        int x = main_window->width() - status_label->width() - 20;
        int y = main_window->height() - status_label->height() - 20;
        status_label->move(x, y);
        status_label->show();
        status_label->raise();

        // Set a very high z-order to keep it on top
        status_label->setParent(main_window);
        status_label->setAttribute(Qt::WA_AlwaysShowToolTips, true);

        main_window->update();
      }
    }
  }
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
