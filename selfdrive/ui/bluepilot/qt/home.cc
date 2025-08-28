#include "selfdrive/ui/bluepilot/qt/home.h"

#include <iostream>

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

  // Debug panel will be created on-demand when first requested
  debug_panel = nullptr;
}

HomeWindowBP::~HomeWindowBP() {
  // Clean up debug panel if it was created
  if (debug_panel) {
    delete debug_panel;
    debug_panel = nullptr;
  }
}

void HomeWindowBP::showDebugPanel() {
  std::cout << "HomeWindowBP: showDebugPanel called" << std::endl;

  // Create debug panel on first use
  if (!debug_panel) {
    std::cout << "HomeWindowBP: Creating debug panel on-demand" << std::endl;
    debug_panel = new OnroadControlsDebugPanel(this);
  }

  if (!debug_panel->isVisible()) {
    std::cout << "HomeWindowBP: Setting debug panel height to:" << height() << std::endl;
    debug_panel->setFixedHeight(height());
    // Width is now calculated dynamically by the debug panel
  }

  debug_panel->toggleVisibility();
  debug_panel->raise();
  debug_panel->activateWindow();

  std::cout << "HomeWindowBP: Debug panel toggled, new visibility:" << debug_panel->isVisible() << std::endl;
}

void HomeWindowBP::updateState(const UIState &s) {
  HomeWindow::updateState(s);

  if (debug_panel && debug_panel->isVisible()) {
    debug_panel->updateState(s);
    // Keep debug panel on top during state updates
    debug_panel->raise();
  }
}

void HomeWindowBP::resizeEvent(QResizeEvent *event) {
  std::cout << "HomeWindowBP: resizeEvent - new size:" << event->size().width() << "x" << event->size().height() << std::endl;

  // Call parent implementation first
  HomeWindow::resizeEvent(event);

  // Update debug panel size if it exists and is visible
  if (debug_panel && debug_panel->isVisible()) {
    std::cout << "HomeWindowBP: Updating debug panel for resize event" << std::endl;
    debug_panel->setFixedHeight(height());
    // The debug panel will recalculate its width in its own resizeEvent
  } else {
    std::cout << "HomeWindowBP: Debug panel not visible, skipping resize update" << std::endl;
  }
}


void HomeWindowBP::forceDebugPanelRefresh() {
  std::cout << "HomeWindowBP: forceDebugPanelRefresh called" << std::endl;

  if (debug_panel && debug_panel->isVisible()) {
    std::cout << "HomeWindowBP: Forcing debug panel refresh" << std::endl;
    debug_panel->forceRefresh();
  } else {
    std::cout << "HomeWindowBP: Debug panel not visible, cannot force refresh" << std::endl;
  }
}

