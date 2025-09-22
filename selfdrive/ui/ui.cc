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

  // Developer UI settings (0=off, 1=right panel only, 2=right+bottom panels)
  s->scene.dev_ui_info = params.getInt("DevUIInfo");

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


#ifndef SUNNYPILOT
  QObject::connect(uiState(), &UIState::uiUpdate, this, &Device::update);
#endif
}

void Device::update(const UIState &s) {
  updateBrightness(s);
  updateWakefulness(s);
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
}

void Device::updateBrightness(const UIState &s) {

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

  // Onroad Brightness Control
#ifdef SUNNYPILOT
  if (awake && s.scene.started && s.scene.onroadScreenOffTimer == 0 && s.scene.onroadScreenOffControl) {
    brightness = s.scene.onroadScreenOffBrightness * 0.01 * brightness;
  }
#endif

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

void Device::resetOnroadDisplayTimer() {
  // Add throttling to prevent spam and identify caller
  static auto last_call = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_call);

  if (elapsed.count() < 100) {  // Less than 100ms since last call
    return;  // Ignore rapid-fire calls
  }

  last_call = now;
  resetInteractiveTimeout();
}

void Device::onUserInteraction() {
  static auto last_interaction = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_interaction).count();

  if (elapsed < 100) {
    return;
  }
  last_interaction = now;

  // Note: resetInteractiveTimeout() is called by the caller, so we don't need to call it again
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
