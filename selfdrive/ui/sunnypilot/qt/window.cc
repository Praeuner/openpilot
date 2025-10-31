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
    : MainWindow(parent, new HomeWindowBP(parent), new BPSettingsWindow(parent)) {
#else
    : MainWindow(parent, new HomeWindowSP(parent), new SettingsWindowSP(parent)) {
#endif

  homeWindow = dynamic_cast<HomeWindowSP *>(MainWindow::homeWindow);
#ifdef BLUEPILOT
  settingsWindow = dynamic_cast<BPSettingsWindow *>(MainWindow::settingsWindow);
#else
  settingsWindow = dynamic_cast<SettingsWindowSP *>(MainWindow::settingsWindow);
#endif
}

void MainWindowSP::closeSettings() {
  MainWindow::closeSettings();
}
