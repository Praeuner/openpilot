#pragma once

#include <QPainter>
#include <QRect>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <eigen3/Eigen/Dense>
#include "selfdrive/ui/ui.h"
#include "cereal/messaging/messaging.h"

class RadarOverlay {
public:
  static void render(QPainter &painter, const QRect &rect, const UIState &s,
                    const QPointF vertices[2], const bool radar_assisted[2],
                    const bool virtual_active[2], const int active_counter[2],
                    float scale_factor, const Eigen::Matrix3f &transform, const QRectF &clip_region);

  static void drawEnhancedLead(QPainter &painter, const cereal::RadarState::LeadData::Reader &lead_data,
                              const QPointF &vd, const QRect &rect, bool radar_assisted,
                              float alpha, float scale_factor, const UIState &s);

  static void drawAllRadarPoints(QPainter &painter, const QRect &rect, const UIState &s,
                                const Eigen::Matrix3f &transform, const QRectF &clip_region);

  static void initializeIcons();
  static void cleanup();

private:
  static QPixmap* radar_icon;
  static QPixmap* vision_icon;
  static bool icons_initialized;

  static QPixmap createFallbackIcon(const QString &text, const QColor &color);
  static bool mapToScreen(float in_x, float in_y, float in_z, QPointF *out,
                         const Eigen::Matrix3f &transform, const QRectF &clip_region);
};
