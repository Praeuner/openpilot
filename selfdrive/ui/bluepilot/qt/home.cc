// selfdrive/ui/bluepilot/qt/home.cc

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include "selfdrive/ui/bluepilot/qt/home.h"

HomeWindowBP::HomeWindowBP(QWidget *parent) : HomeWindow(parent) {
  // CRITICAL FIX: Disconnect all signals from old sidebar before deletion
  // Parent class HomeWindow connected offroadTransition signal to the sidebar at line 40
  // If we delete without disconnecting, the signal will try to deliver to deleted memory -> CRASH
  if (sidebar) {
    QObject::disconnect(uiState(), &UIState::offroadTransition, sidebar, nullptr);
    QObject::disconnect(sidebar, nullptr, nullptr, nullptr);
    delete sidebar;
    sidebar = nullptr;
  }

  // Replace stock sidebar with BluePilot sidebar
  sidebar = new SidebarBP(this);

  // Connect the debug panel signal
  QObject::connect(sidebar, SIGNAL(debugPanelRequested()), this, SLOT(showDebugPanel()));
  QObject::connect(sidebar, &Sidebar::openSettings, this, &HomeWindow::openSettings);
  // Note: offroadTransition is now handled internally by SidebarBP

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
  BPLog::bpDebugGeneral() << "[bp.home] showDebugPanel | showDebugPanel called" << std::endl;

  // Create debug panel on first use
  if (!debug_panel) {
    BPLog::bpDebugGeneral() << "[bp.home] showDebugPanel | Creating debug panel on-demand" << std::endl;
    debug_panel = new OnroadControlsDebugPanel(this);
  }

  if (!debug_panel->isVisible()) {
    BPLog::bpDebugGeneral() << "[bp.home] showDebugPanel | Setting debug panel height to: " << height() << std::endl;
    debug_panel->setFixedHeight(height());
    // Width is now calculated dynamically by the debug panel
  }

  debug_panel->toggleVisibility();
  debug_panel->raise();
  debug_panel->activateWindow();

  BPLog::bpDebugGeneral() << "[bp.home] showDebugPane | Debug panel toggled, new visibility: " << debug_panel->isVisible() << std::endl;
}

void HomeWindowBP::updateState(const UIState &s) {
  HomeWindow::updateState(s);

  // Update sidebar visibility in UI state for hybrid overlay scaling
  if (sidebar && sidebar->isVisible() != s.scene.sidebar_visible) {
    // Update the UI state with current sidebar visibility
    const_cast<UIState&>(s).scene.sidebar_visible = sidebar->isVisible();
  }

  if (debug_panel && debug_panel->isVisible()) {
    debug_panel->updateState(s);
    // Keep debug panel on top during state updates
    debug_panel->raise();
  }
}

void HomeWindowBP::resizeEvent(QResizeEvent *event) {
  BPLog::bpDebugGeneral() << "[bp.home] resizeEvent | new size: " << event->size().width() << "x" << event->size().height() << std::endl;

  // Call parent implementation first
  HomeWindow::resizeEvent(event);

  // Update debug panel size if it exists and is visible
  if (debug_panel && debug_panel->isVisible()) {
    BPLog::bpDebugGeneral() << "[bp.home] resizeEvent | Updating debug panel for resize event" << std::endl;
    debug_panel->setFixedHeight(height());
    // The debug panel will recalculate its width in its own resizeEvent
  } else {
    BPLog::bpDebugGeneral() << "[bp.home] resizeEvent | Debug panel not visible, skipping resize update" << std::endl;
  }
}


void HomeWindowBP::forceDebugPanelRefresh() {
  BPLog::bpDebugGeneral() << "[bp.home] forceDebugPanelRefresh | forceDebugPanelRefresh called" << std::endl;

  if (debug_panel && debug_panel->isVisible()) {
    BPLog::bpDebugGeneral() << "[bp.home] forceDebugPanelRefresh | Forcing debug panel refresh" << std::endl;
    debug_panel->forceRefresh();
  } else {
    BPLog::bpDebugGeneral() << "[bp.home] forceDebugPanelRefresh | Debug panel not visible, cannot force refresh" << std::endl;
  }
}

