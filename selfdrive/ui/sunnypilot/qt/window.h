/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include "selfdrive/ui/qt/window.h"
#include "selfdrive/ui/sunnypilot/qt/home.h"

#ifdef BLUEPILOT
#include "selfdrive/ui/bluepilot/qt/offroad/settings.h"
#else
#include "selfdrive/ui/sunnypilot/qt/offroad/settings/settings.h"
#endif

class MainWindowSP : public MainWindow {
  Q_OBJECT

public:
  explicit MainWindowSP(QWidget *parent = 0);

private:
  HomeWindowSP *homeWindow;
#ifdef BLUEPILOT
  BPSettingsWindow *settingsWindow;
#else
  SettingsWindowSP *settingsWindow;
#endif
  void closeSettings() override;
};
