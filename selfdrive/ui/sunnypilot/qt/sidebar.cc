/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/sunnypilot/qt/sidebar.h"

#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/sunnypilot/qt/util.h"
#include "common/params.h"

SidebarSP::SidebarSP(QWidget *parent) : Sidebar(parent) {
  // Redirect uiUpdate signal to SidebarSP::updateState instead of Sidebar::updateState
  QObject::disconnect(uiState(), &UIState::uiUpdate, this, &Sidebar::updateState);
  QObject::connect(uiStateSP(), &UIStateSP::uiUpdate, this, &SidebarSP::updateState);
}

void SidebarSP::updateState(const UIStateSP &s) {
  if (!isVisible()) return;
  Sidebar::updateState(s);

  auto &sm = *(s.sm);
  auto deviceState = sm["deviceState"].getDeviceState();

  // Update CPU temperature display for SunnySP - match the refresh rate from parent class
  if (metrics_refresh_counter == 0) {
    float max_temp = deviceState.getMaxTempC();
    sidebar_temp_str = QString("%1°C").arg(QString::number(max_temp, 'f', 1));
    setProperty("sidebarTemp", sidebar_temp_str);
  }

  ItemStatus sunnylinkStatus;
  auto sl_dongle_id = getSunnylinkDongleId();
  auto last_sunnylink_ping_str = params.get("LastSunnylinkPingTime");
  auto last_sunnylink_ping = std::stoull(last_sunnylink_ping_str.empty() ? "0" : last_sunnylink_ping_str);
  auto elapsed_sunnylink_ping = nanos_since_boot() - last_sunnylink_ping;
  auto sunnylink_enabled = params.getBool("SunnylinkEnabled");

  QString status = tr("DISABLED");
  QColor color = disabled_color;

  if (sunnylink_enabled && last_sunnylink_ping == 0) {
    // If sunnylink is enabled, but we don't have a dongle id, and we haven't received a ping yet, we are registering
    status = sl_dongle_id.has_value() ? tr("OFFLINE") : tr("REGISTERED");
    color = sl_dongle_id.has_value() ? warning_color : progress_color;
  } else if (sunnylink_enabled) {
    // If sunnylink is enabled, we are considered online if we have received a ping in the last 80 seconds, else error.
    status = elapsed_sunnylink_ping < 80000000000ULL ? tr("ONLINE") : tr("ERROR");
    color = elapsed_sunnylink_ping < 80000000000ULL ? good_color : danger_color;
  }
  sunnylinkStatus = ItemStatus{{tr("SUNNYLINK"), status}, color};
  setProperty("sunnylinkStatus", QVariant::fromValue(sunnylinkStatus));
}

void SidebarSP::drawSidebar(QPainter &p) {
  // First draw the base sidebar
  Sidebar::drawSidebar(p);

  // SunnyLink card is now handled in the base Sidebar class
}
