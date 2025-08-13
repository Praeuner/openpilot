#pragma once

#include <QPainter>
#include <QString>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui.h"
#else
#include "selfdrive/ui/ui.h"
#endif

class HudRenderer : public QObject {
  Q_OBJECT

public:
  HudRenderer();
  virtual ~HudRenderer() = default;
  virtual void updateState(const UIState &s);
  virtual void draw(QPainter &p, const QRect &surface_rect);

protected:
  void drawSetSpeed(QPainter &p, const QRect &surface_rect);
  void drawCurrentSpeed(QPainter &p, const QRect &surface_rect);
  void drawText(QPainter &p, int x, int y, const QString &text, int alpha = 255);
  void drawText(QPainter &p, int x, int y, const QString &text, const QColor &color);
  QColor getSpeedColor(int alpha = 255);
  void drawRoadName(QPainter &p, const QRect &surface_rect);

  float speed = 0;
  float set_speed = 0;
  bool is_cruise_set = false;
  bool is_cruise_available = true;
  bool is_metric = false;
  bool v_ego_cluster_seen = false;
  bool brake_pressed = false;
  bool show_brake_status = false;
  int status = STATUS_DISENGAGED;

  // Road name (from liveMapDataSP)
  QString road_name;
};
