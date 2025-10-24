#pragma once

#include <QWidget>
#include <QPropertyAnimation>

#include "selfdrive/ui/ui.h"

class OnroadAlertsBP : public QWidget {
  Q_OBJECT
  Q_PROPERTY(qreal alertOpacity MEMBER alert_opacity NOTIFY valueChanged);

public:
  OnroadAlertsBP(QWidget *parent = 0);
  ~OnroadAlertsBP();
  void updateState(const UIState &s);
  void clear();

private:
  bool is_destroying = false;

signals:
  void valueChanged();

protected:
  void resizeEvent(QResizeEvent *event) override;

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

  // Alert status colors with modern touch
  const QMap<cereal::SelfdriveState::AlertStatus, QColor> alert_colors = {
    {cereal::SelfdriveState::AlertStatus::NORMAL, QColor(35, 36, 38, 241)},      // Darker background
    {cereal::SelfdriveState::AlertStatus::USER_PROMPT, QColor(220, 100, 20, 241)}, // Less yellow orange warning
    {cereal::SelfdriveState::AlertStatus::CRITICAL, QColor(242, 72, 85, 241)},    // Danger
  };


  void paintEvent(QPaintEvent*) override;
  OnroadAlertsBP::Alert getAlert(const SubMaster &sm, uint64_t started_frame);

  // Helper methods
  void drawModernCard(QPainter &p, const QRect &rect, bool isFullscreen);
  void drawBrowserTabCard(QPainter &p, const QRect &rect);
  void drawScreenBorder(QPainter &p, const QColor &borderColor);
  void drawBlurryBorder(QPainter &p, const QColor &borderColor);  // Legacy - redirects to drawScreenBorder

  QColor bg;
  Alert alert = {};

  // Animation properties
  QPropertyAnimation *opacity_animation = nullptr;
  qreal alert_opacity = 0.0;
  int dev_ui_info = 0;  // Store developer UI state for positioning
};
