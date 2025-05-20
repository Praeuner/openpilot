/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/sunnypilot/ui.h"

#include "common/watchdog.h"
#include <sys/resource.h>
#include <thread>
#include <sstream>

#ifdef SENTRY_ENABLED
#include "third_party/sentry/include/sentry.h"
#endif
#include <chrono>

void UIStateSP::updateStatus() { UIState::updateStatus(); }

UIStateSP::UIStateSP(QObject *parent) : UIState(parent) {
  sm = std::make_unique<SubMaster>(std::vector<const char *>{"modelV2",
                                                             "controlsState",
                                                             "liveCalibration",
                                                             "radarState",
                                                             "deviceState",
                                                             "pandaStates",
                                                             "carParams",
                                                             "driverMonitoringState",
                                                             "carState",
                                                             "driverStateV2",
                                                             "wideRoadCameraState",
                                                             "managerState",
                                                             "selfdriveState",
                                                             "longitudinalPlan",
                                                             "modelManagerSP",
                                                             "selfdriveStateSP",
                                                             "longitudinalPlanSP",
                                                             "backupManagerSP",
                                                             "carStateBP",
                                                             "carControl",
                                                             "carOutput",
                                                             "liveDelay"});

  // update timer
  timer = new QTimer(this);
  QObject::connect(timer, &QTimer::timeout, this, &UIStateSP::update);
  timer->start(1000 / UI_FREQ);
}

// This method overrides completely the update method from the parent class intentionally.
void UIStateSP::update() {
#ifdef SENTRY_ENABLED
  auto start_time = std::chrono::steady_clock::now();
#endif

  update_sockets(this);
#ifdef SENTRY_ENABLED
  auto after_sockets = std::chrono::steady_clock::now();
  auto sockets_duration = std::chrono::duration_cast<std::chrono::milliseconds>(after_sockets - start_time).count();
  if (sockets_duration > 40) {
    sentry_value_t event = sentry_value_new_event();
    sentry_value_set_by_key(event, "message", sentry_value_new_string("update_sockets took too long"));
    sentry_value_set_by_key(event, "extra.duration", sentry_value_new_int32(sockets_duration));
    sentry_capture_event(event);
  }
#endif

  update_state(this);
#ifdef SENTRY_ENABLED
  auto after_state = std::chrono::steady_clock::now();
  auto state_duration = std::chrono::duration_cast<std::chrono::milliseconds>(after_state - after_sockets).count();
  if (state_duration > 40) {
    sentry_value_t event = sentry_value_new_event();
    sentry_value_set_by_key(event, "message", sentry_value_new_string("update_state took too long"));
    sentry_value_set_by_key(event, "extra.duration", sentry_value_new_int32(state_duration));
    sentry_capture_event(event);
  }
#endif

  updateStatus();
#ifdef SENTRY_ENABLED
  auto after_status = std::chrono::steady_clock::now();
  auto status_duration = std::chrono::duration_cast<std::chrono::milliseconds>(after_status - after_state).count();
  if (status_duration > 40) {
    sentry_value_t event = sentry_value_new_event();
    sentry_value_set_by_key(event, "message", sentry_value_new_string("updateStatus took too long"));
    sentry_value_set_by_key(event, "extra.duration", sentry_value_new_int32(status_duration));

    // Add system context to help identify freeze causes
    sentry_value_set_by_key(event, "extra.frame_id", sentry_value_new_int32(sm->frame));
    sentry_value_set_by_key(event, "extra.started", sentry_value_new_bool(scene.started));

    // Add memory usage information
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    sentry_value_set_by_key(event, "extra.memory_kb", sentry_value_new_int32(usage.ru_maxrss));

    // Add camera performance data if available
    // if (sm->valid("wideRoadCameraState")) {
    //   const auto &camera_state = sm->getMsg("wideRoadCameraState").getWideRoadCameraState();
    //   sentry_value_set_by_key(event, "extra.camera_fps", sentry_value_new_double(camera_state.getFps()));
    // }

    // Convert thread ID to string instead of int32
    std::stringstream thread_id_ss;
    thread_id_ss << std::this_thread::get_id();
    sentry_value_set_by_key(event, "extra.thread_id", sentry_value_new_string(thread_id_ss.str().c_str()));

    sentry_capture_event(event);
  }

  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(after_status - start_time).count();
  if (total_duration > 100) {
    sentry_value_t event = sentry_value_new_event();
    sentry_value_set_by_key(event, "message", sentry_value_new_string("UI update exceeded time threshold"));
    sentry_value_set_by_key(event, "extra.total_duration", sentry_value_new_int32(total_duration));

    // Track component timings for pinpointing bottlenecks
    sentry_value_set_by_key(event, "extra.sockets_duration", sentry_value_new_int32(sockets_duration));
    sentry_value_set_by_key(event, "extra.state_duration", sentry_value_new_int32(state_duration));
    sentry_value_set_by_key(event, "extra.status_duration", sentry_value_new_int32(status_duration));

    sentry_capture_event(event);
  }
#endif

  if (sm->frame % UI_FREQ == 0) {
    watchdog_kick(nanos_since_boot());
  }
  emit uiUpdate(*this);
}

DeviceSP::DeviceSP(QObject *parent) : Device(parent) {
  QObject::connect(uiStateSP(), &UIStateSP::uiUpdate, this, &DeviceSP::update);
}

UIStateSP *uiStateSP() {
  static UIStateSP ui_state;
  return &ui_state;
}

void UIStateSP::setSunnylinkRoles(const std::vector<RoleModel>& roles) {
  sunnylinkRoles = roles;
  emit sunnylinkRolesChanged(roles);
}

void UIStateSP::setSunnylinkDeviceUsers(const std::vector<UserModel>& users) {
  sunnylinkUsers = users;
  emit sunnylinkDeviceUsersChanged(users);
}

DeviceSP *deviceSP() {
  static DeviceSP _device;
  return &_device;
}
