#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/bp_media_control_button.h"

#include <QFont>
#include <QPainter>
#include <QPolygon>

MediaControlButton::MediaControlButton(IconType type, QWidget *parent)
    : QPushButton(parent), iconType(type) {
  setStyleSheet(R"(
      QPushButton {
        background: rgba(0, 0, 0, 180);
        border: none;
        border-radius: 75px;
      }
      QPushButton:pressed {
        background: rgba(0, 0, 0, 240);
      }
    )");
}

void MediaControlButton::setIconType(IconType type) {
  iconType = type;
  update();
}

void MediaControlButton::paintEvent(QPaintEvent *event) {
  QPushButton::paintEvent(event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(255, 255, 255));

  QRect rect = this->rect();
  int centerX = rect.width() / 2;
  int centerY = rect.height() / 2;

  switch (iconType) {
    case Play: {
      int size = rect.width() / 4;
      QPolygon triangle;
      triangle << QPoint(centerX - size / 2, centerY - size)
               << QPoint(centerX - size / 2, centerY + size)
               << QPoint(centerX + size, centerY);
      painter.drawPolygon(triangle);
      break;
    }
    case Pause: {
      int barWidth = rect.width() / 10;
      int barHeight = rect.height() / 3;
      int spacing = barWidth;

      QRect leftBar(centerX - spacing / 2 - barWidth, centerY - barHeight / 2, barWidth, barHeight);
      QRect rightBar(centerX + spacing / 2, centerY - barHeight / 2, barWidth, barHeight);

      painter.drawRect(leftBar);
      painter.drawRect(rightBar);
      break;
    }
    case RewindArrow: {
      painter.setPen(QPen(QColor(255, 255, 255), 6, Qt::SolidLine, Qt::RoundCap));
      painter.setBrush(Qt::NoBrush);

      int radius = rect.width() / 4;
      QRect arcRect(centerX - radius, centerY - radius, radius * 2, radius * 2);
      painter.drawArc(arcRect, 135 * 16, -270 * 16);

      QPolygon arrowHead;
      int arrowSize = 12;
      arrowHead << QPoint(centerX - radius, centerY - radius / 4)
                << QPoint(centerX - radius - arrowSize, centerY)
                << QPoint(centerX - radius, centerY + radius / 4);
      painter.setBrush(QColor(255, 255, 255));
      painter.drawPolygon(arrowHead);

      painter.setPen(QColor(255, 255, 255));
      painter.setFont(QFont("Arial", 24, QFont::Bold));
      painter.drawText(rect, Qt::AlignCenter, "10");
      break;
    }
    case ForwardArrow: {
      painter.setPen(QPen(QColor(255, 255, 255), 6, Qt::SolidLine, Qt::RoundCap));
      painter.setBrush(Qt::NoBrush);

      int radius = rect.width() / 4;
      QRect arcRect(centerX - radius, centerY - radius, radius * 2, radius * 2);
      painter.drawArc(arcRect, 315 * 16, -270 * 16);

      QPolygon arrowHead;
      int arrowSize = 12;
      arrowHead << QPoint(centerX + radius, centerY - radius / 4)
                << QPoint(centerX + radius + arrowSize, centerY)
                << QPoint(centerX + radius, centerY + radius / 4);
      painter.setBrush(QColor(255, 255, 255));
      painter.drawPolygon(arrowHead);

      painter.setPen(QColor(255, 255, 255));
      painter.setFont(QFont("Arial", 24, QFont::Bold));
      painter.drawText(rect, Qt::AlignCenter, "10");
      break;
    }
  }
}
