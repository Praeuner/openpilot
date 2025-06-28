#pragma once

#include "selfdrive/ui/qt/sidebar.h"
#include "selfdrive/ui/bluepilot/qt/onroad/onroad_controls_debug_panel.h"
#include <QPropertyAnimation>
#include <memory>
#include "cereal/messaging/messaging.h"

class SidebarBP : public Sidebar {
  Q_OBJECT
  Q_PROPERTY(qreal hover_opacity MEMBER hover_opacity NOTIFY valueChanged);

public:
  explicit SidebarBP(QWidget *parent = 0);
  ~SidebarBP();

signals:
  void debugPanelRequested();

protected:
  void paintEvent(QPaintEvent *event);
  void mousePressEvent(QMouseEvent *event);
  void mouseReleaseEvent(QMouseEvent *event);
  void enterEvent(QEvent *event);
  void leaveEvent(QEvent *event);
  void drawSidebar(QPainter &p);
  void drawMetric(QPainter &p, const QString &label, const QString &mainValue, const QString &leftValue, const QString &rightValue, QColor c, int y, bool compactMode);
  void drawProgressBar(QPainter &p, int x, int y, int width, int height, float percentage, QColor color);

public slots:
  void updateState(const UIState &s);

private:
  // Button images
  QPixmap home_img, flag_img, settings_img;

  // Modern sidebar styling
  const QColor good_color = QColor(42, 199, 122);
  const QColor warning_color = QColor(255, 195, 0);
  const QColor danger_color = QColor(242, 72, 85);
  const QColor background_color = QColor(32, 33, 35);
  const QColor card_background = QColor(48, 49, 51);
  const QColor accent_color = QColor(24, 144, 255);

  // CPU card area for debug panel trigger
  const QRect cpu_card_area = QRect(30, 140, 240, 110);
  QRect memory_fan_btn = QRect(30, 360, 240, 100);

  // Modern metrics
  QString cpu_temp = "0°C";
  QString cpu_usage = "0%";
  QString gpu_temp = "0°C";
  QString gpu_usage = "0%";
  QString memory_usage = "0%";
  QString fan_demand = "0%";
  bool show_fan_instead_memory = false;

  // Hover animation
  bool is_hovering = false;
  QPropertyAnimation *hover_animation = nullptr;
  qreal hover_opacity = 0.0;

  // Performance metrics refresh control
  int metrics_refresh_counter = 0;
  const int METRICS_REFRESH_INTERVAL = 20;

  ItemStatus connect_status, panda_status, temp_status, gpu_status, memory_status, network_status;

  // BluePilot PubMaster for userFlag
  std::unique_ptr<PubMaster> bp_pm;

  // Local button rectangles (since base class ones are const)
  QRect bp_settings_btn;
  QRect bp_home_btn;
};