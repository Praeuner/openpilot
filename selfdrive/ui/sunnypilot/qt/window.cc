/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/sunnypilot/qt/window.h"

#ifdef BLUEPILOT
#include "selfdrive/ui/bluepilot/qt/home.h"
#endif

MainWindowSP::MainWindowSP(QWidget *parent)
#ifdef BLUEPILOT
    : MainWindow(parent, new HomeWindowBP(parent), new SettingsWindowSP(parent)) {
#else
    : MainWindow(parent, new HomeWindowSP(parent), new SettingsWindowSP(parent)) {
#endif

  homeWindow = dynamic_cast<HomeWindowSP *>(MainWindow::homeWindow);
  settingsWindow = dynamic_cast<SettingsWindowSP *>(MainWindow::settingsWindow);

  // Install event filter for brightness control
  installEventFilter(this);
}

void MainWindowSP::closeSettings() {
  MainWindow::closeSettings();
}

bool MainWindowSP::eventFilter(QObject* obj, QEvent* event) {
  bool ignore = false;
  switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove: {
      // Handle onroad screen off control
      if (uiStateSP()->scene.started && uiStateSP()->scene.onroadScreenOffControl) {
        uiStateSP()->reset_onroad_sleep_timer();
      }

      // Handle standard interactive timeout (essential for keeping display awake)
      ignore = !device()->isAwake();
      device()->resetInteractiveTimeout();
      break;
    }
    default:
      break;
  }
  return ignore;
}
