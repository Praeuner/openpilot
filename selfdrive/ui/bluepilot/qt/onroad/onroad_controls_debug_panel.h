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
  void updateState(const UIState &s);
  bool gestureEvent(QGestureEvent *event);
  void toggleVisibility();

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool event(QEvent *event) override;

private slots:
  void tabSelected(int index);

private:
  int m_xPosition;
  int m_panelWidth;
  bool m_visible = false;
  QPropertyAnimation *m_animation;
  QTimer *m_timer;

  // UI Components
  QVBoxLayout *m_navLayout;
  std::vector<ControlNavButton *> m_navButtons;
  LateralDebugPanel *m_lateralPanel;
  LongDebugPanel *m_longPanel;
  OtherDebugPanel *m_otherPanel;

  int m_currentTabIndex = 0;

  // Constants
  static constexpr float PANEL_RATIO = 0.85f; // 85% of screen width for right side panel
  static constexpr int BORDER_RADIUS = 0;

  void setupTabs();
  void drawBackground(QPainter &p);
  void updatePosition(); // New method to handle positioning

  QPushButton *m_closeButton;
  void setupCloseButton();
};
