#pragma once

#include <QWidget>
#include <QPropertyAnimation>
#include <QRegularExpression>

#include "selfdrive/ui/ui.h"

class OnroadAlertsBP : public QWidget {
  Q_OBJECT
  Q_PROPERTY(qreal alertOpacity MEMBER alert_opacity NOTIFY valueChanged);
  Q_PROPERTY(qreal progressValue MEMBER progress_value NOTIFY valueChanged);

public:
  OnroadAlertsBP(QWidget *parent = 0);
  ~OnroadAlertsBP();
  void updateState(const UIState &s);
  void clear();

signals:
  void valueChanged();

protected:
  struct Alert {
    QString text1;
    QString text2;
    QString type;
    cereal::SelfdriveState::AlertSize size;
    cereal::SelfdriveState::AlertStatus status;

    bool equal(const Alert &other) const {
      return text1 == other.text1 && text2 == other.text2 && type == other.type;
    }
  };

  // Modern color scheme inspired by sidebar
  const QColor good_color = QColor(42, 199, 122);
  const QColor warning_color = QColor(255, 195, 0);
  const QColor danger_color = QColor(242, 72, 85);
  const QColor background_color = QColor(32, 33, 35);
  const QColor card_background = QColor(48, 49, 51);
  const QColor accent_color = QColor(24, 144, 255);
  const QColor progress_color = QColor(3, 132, 252);

  // Alert status colors with modern touch
  const QMap<cereal::SelfdriveState::AlertStatus, QColor> alert_colors = {
    {cereal::SelfdriveState::AlertStatus::NORMAL, QColor(35, 36, 38, 241)},      // Darker background
    {cereal::SelfdriveState::AlertStatus::USER_PROMPT, QColor(220, 100, 20, 241)}, // Less yellow orange warning
    {cereal::SelfdriveState::AlertStatus::CRITICAL, QColor(242, 72, 85, 241)},    // Danger
  };

  // Status accent colors for borders
  const QMap<cereal::SelfdriveState::AlertStatus, QColor> accent_colors = {
    {cereal::SelfdriveState::AlertStatus::NORMAL, accent_color},
    {cereal::SelfdriveState::AlertStatus::USER_PROMPT, QColor(200, 80, 15)}, // Less yellow orange border
    {cereal::SelfdriveState::AlertStatus::CRITICAL, danger_color},
  };

  void paintEvent(QPaintEvent*) override;
  OnroadAlertsBP::Alert getAlert(const SubMaster &sm, uint64_t started_frame);

  // Helper methods
  void drawModernCard(QPainter &p, const QRect &rect, bool isFullscreen);
  void drawRadialProgress(QPainter &p, const QRect &rect, float percentage);
  float extractPercentage(const QString &text);

  QColor bg;
  Alert alert = {};

  // Animation properties
  QPropertyAnimation *opacity_animation = nullptr;
  QPropertyAnimation *progress_animation = nullptr;
  qreal alert_opacity = 0.0;
  qreal progress_value = 0.0;
  float target_progress = 0.0;
};
