#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QString>
#include <QMouseEvent>
#include <QEvent>

#include "bp_route_manager.h"

class BPRouteCard : public QWidget {
  Q_OBJECT

public:
  explicit BPRouteCard(const RouteInfo &route, QWidget *parent = nullptr);
  ~BPRouteCard() = default;

  // Getters
  QString getRoutePath() const { return m_routePath; }
  QString getRouteId() const { return m_route.baseName; }
  bool isStarred() const { return m_route.isStarred; }

  // Update thumbnail
  void setThumbnail(const QPixmap &thumbnail);

  // Update star status
  void updateStarStatus(bool starred);

signals:
  void cardClicked(const QString &routePath);
  void starClicked(const QString &routePath);
  void deleteClicked(const QString &routePath);
  void thumbnailRequested(const QString &routePath, const QString &routeId);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void enterEvent(QEvent *event) override;
  void leaveEvent(QEvent *event) override;

private:
  void setupUI();
  void createThumbnailWidget();
  void createInfoWidget();
  void createStarButton();
  QString formatRoute() const;

  RouteInfo m_route;
  QString m_routePath;

  // UI elements
  QLabel *m_thumbnailLabel;
  QLabel *m_durationLabel;
  QLabel *m_sizeLabel;
  QLabel *m_dateLabel;
  QLabel *m_segmentsLabel;
  QPushButton *m_starButton;

  bool m_isHovered = false;

  // Style constants
  static constexpr int CARD_HEIGHT = 120;
  static constexpr int CARD_MARGIN = 10;
  static constexpr int THUMBNAIL_WIDTH = 160;
  static constexpr int THUMBNAIL_HEIGHT = 90;
};