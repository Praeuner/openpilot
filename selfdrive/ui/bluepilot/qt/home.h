#pragma once

#include "selfdrive/ui/qt/home.h"
#include "selfdrive/ui/bluepilot/qt/onroad/onroad_controls_debug_panel.h"
#include "selfdrive/ui/bluepilot/qt/sidebar.h"

class HomeWindowBP : public HomeWindow {
  Q_OBJECT

public:
  explicit HomeWindowBP(QWidget *parent = 0);
  ~HomeWindowBP();
  void forceDebugPanelRefresh(); // Force refresh of debug panel

public slots:
  void showDebugPanel();

protected:
  void updateState(const UIState &s) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  OnroadControlsDebugPanel *debug_panel;
};
