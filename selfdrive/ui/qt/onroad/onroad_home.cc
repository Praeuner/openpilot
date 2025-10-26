#include "selfdrive/ui/qt/onroad/onroad_home.h"

#include <QPainter>
#include <QStackedLayout>

#include "selfdrive/ui/qt/util.h"
#include "common/params.h"

OnroadWindow::OnroadWindow(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *main_layout  = new QVBoxLayout(this);
  main_layout->setMargin(UI_BORDER_SIZE);
  QStackedLayout *stacked_layout = new QStackedLayout;
  stacked_layout->setStackingMode(QStackedLayout::StackAll);
  main_layout->addLayout(stacked_layout);

  nvg = new AnnotatedCameraWidget(VISION_STREAM_ROAD, this);

  QWidget * split_wrapper = new QWidget;
  split = new QHBoxLayout(split_wrapper);
  split->setContentsMargins(0, 0, 0, 0);
  split->setSpacing(0);
  split->addWidget(nvg);

  if (getenv("DUAL_CAMERA_VIEW")) {
    CameraWidget *arCam = new CameraWidget("camerad", VISION_STREAM_ROAD, this);
    split->insertWidget(0, arCam);
  }

  stacked_layout->addWidget(split_wrapper);

#ifdef BLUEPILOT
  // Stock alerts are part of the stacked layout
  stock_alerts = new OnroadAlerts(this);
  stock_alerts->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  stacked_layout->addWidget(stock_alerts);

  // BluePilot alerts render as a manual overlay (kept outside stacked layout)
  bp_alerts = new OnroadAlertsBP(this);
  // bp_alerts manages its own geometry to cover the parent window

  // Set initial visibility based on parameter
  use_bp_alerts = Params().getBool("BPUseBluepilotAlerts");
  if (use_bp_alerts) {
    stock_alerts->hide();
    bp_alerts->show();
  } else {
    bp_alerts->hide();
    stock_alerts->show();
  }
#else
  alerts = new OnroadAlerts(this);
  alerts->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  stacked_layout->addWidget(alerts);
#endif

  setAttribute(Qt::WA_OpaquePaintEvent);

  // We handle the connection of the signals on the derived class
#ifndef SUNNYPILOT
  QObject::connect(uiState(), &UIState::uiUpdate, this, &OnroadWindow::updateState);
  QObject::connect(uiState(), &UIState::offroadTransition, this, &OnroadWindow::offroadTransition);
#endif
}

void OnroadWindow::updateState(const UIState &s) {
  if (!s.scene.started) {
    return;
  }

#ifdef BLUEPILOT
  // Check if we need to switch alert widgets
  bool should_use_bp = Params().getBool("BPUseBluepilotAlerts");
  if (should_use_bp != use_bp_alerts) {
    // Switch widgets (no need to raise - stacking order keeps split_wrapper on top)
    use_bp_alerts = should_use_bp;
    if (use_bp_alerts) {
      stock_alerts->hide();
      bp_alerts->show();
    } else {
      bp_alerts->hide();
      stock_alerts->show();
    }
  }

  // Update the active alert widget
  if (use_bp_alerts) {
    bp_alerts->updateState(s);
  } else {
    stock_alerts->updateState(s);
  }
#else
  alerts->updateState(s);
#endif
  nvg->updateState(s);

  QColor bgColor = bg_colors[s.status];
  if (bg != bgColor) {
    // repaint border
    bg = bgColor;
    update();
  }
}

void OnroadWindow::offroadTransition(bool offroad) {
#ifdef BLUEPILOT
  // Clear both alert widgets
  stock_alerts->clear();
  bp_alerts->clear();
#else
  alerts->clear();
#endif
}

void OnroadWindow::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.fillRect(rect(), QColor(bg.red(), bg.green(), bg.blue(), 255));
}
