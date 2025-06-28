#include "selfdrive/ui/bluepilot/qt/home.h"

HomeWindowBP::HomeWindowBP(QWidget *parent) : HomeWindow(parent) {
  // Replace stock sidebar with BluePilot sidebar
  delete sidebar;
  sidebar = new SidebarBP(this);

  // Connect the debug panel signal
  QObject::connect(sidebar, SIGNAL(debugPanelRequested()), this, SLOT(showDebugPanel()));
  QObject::connect(sidebar, &Sidebar::openSettings, this, &HomeWindow::openSettings);
  QObject::connect(uiState(), &UIState::offroadTransition, sidebar, &Sidebar::offroadTransition);

  // Re-add sidebar to layout (it was removed when we deleted the old one)
  QHBoxLayout *main_layout = qobject_cast<QHBoxLayout*>(layout());
  if (main_layout) {
    main_layout->insertWidget(0, sidebar);
  }

  // Initialize debug panel
  debug_panel = new OnroadControlsDebugPanel(this);
  debug_panel->hide();
}

void HomeWindowBP::showDebugPanel() {
  if (!debug_panel->isVisible()) {
    debug_panel->setFixedHeight(height());
    debug_panel->setFixedWidth(width() * 0.8);
  }

  debug_panel->toggleVisibility();
  debug_panel->raise();
}

void HomeWindowBP::updateState(const UIState &s) {
  HomeWindow::updateState(s);

  if (debug_panel && debug_panel->isVisible()) {
    debug_panel->updateState(s);
  }
}

