#pragma once

#include <QWidget>
#include <QTimer>
#include <QPainter>
#include <QGestureEvent>
#include <QPropertyAnimation>
#include <QSwipeGesture>
#include <QTabBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <vector>
#include <deque>
#include <utility>
#include <atomic>
#include <QMutex>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui.h"
#define UIState UIStateSP
#else
#include "selfdrive/ui/ui.h"
#endif

// Forward declarations
class LateralDebugPanel;
class LongDebugPanel;
class OtherDebugPanel;

// Custom navigation button - MUST be defined before OnroadControlsDebugPanel
class ControlNavButton : public QPushButton {
  Q_OBJECT
public:
  ControlNavButton(const QIcon &icon, QWidget *parent = nullptr);
  void setSelected(bool selected);

private:
  QLabel *iconLabel;
};

class OnroadControlsDebugPanel : public QWidget {
  Q_OBJECT

public:
  OnroadControlsDebugPanel(QWidget *parent = nullptr);
  ~OnroadControlsDebugPanel();
  void updateState(const UIState &s);
  bool gestureEvent(QGestureEvent *event);
  void toggleVisibility();
  void forceRefresh(); // Force complete refresh of size and position
  bool needsSizeUpdate() const; // Check if current size needs updating

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool event(QEvent *event) override;

private slots:
  void tabSelected(int index);
  void updatePanels();

private:
  int m_xPosition;
  int m_panelWidth;
  bool m_visible = false;
  QPropertyAnimation *m_animation;
  QTimer *m_updateTimer;
  QMutex m_updateMutex;
  std::atomic<bool> m_updatePending;
  const UIState *m_lastState = nullptr; // Store pointer instead of copy

  // UI Components
  QVBoxLayout *m_navLayout;
  std::vector<ControlNavButton *> m_navButtons;
  LateralDebugPanel *m_lateralPanel;
  LongDebugPanel *m_longPanel;
  OtherDebugPanel *m_otherPanel;

  int m_currentTabIndex = 0;

  // Constants
  static constexpr int SIDEBAR_WIDTH = 460; // Width of the sidebar
  static constexpr int NAV_BUTTONS_WIDTH = 160; // Width reserved for navigation buttons
  static constexpr int BORDER_RADIUS = 0;
  static constexpr int UPDATE_INTERVAL_MS = 50; // 20 Hz update rate

  void setupTabs();
  void drawBackground(QPainter &p);
  void updatePosition(); // Method to handle positioning
  void scheduleUpdate(const UIState &s);
  int calculatePanelWidth() const; // Method to calculate dynamic panel width
  void handleParentResize(); // Method to handle parent resize events

  QPushButton *m_closeButton;
  void setupCloseButton();

  // Cached drawing components
  QLinearGradient m_mainGradient;
  QLinearGradient m_navGradient;
  bool m_gradientsInitialized = false;
};
