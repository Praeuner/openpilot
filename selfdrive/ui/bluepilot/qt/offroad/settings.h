/**
 * BluePilot Settings Window
 * Modern, streamlined settings interface with consistent BP styling
 */

#pragma once

#include "selfdrive/ui/qt/offroad/settings.h"

class BPSettingsWindow : public SettingsWindow {
  Q_OBJECT

public:
  explicit BPSettingsWindow(QWidget *parent = nullptr);

protected:
  struct PanelInfo {
    QString name;
    QWidget *widget;
    QString icon;

    PanelInfo(const QString &name, QWidget *widget, const QString &icon)
      : name(name), widget(widget), icon(icon) {}
  };

  // Modern BP color scheme
  const QColor bp_background = QColor(32, 33, 35);
  const QColor bp_card_background = QColor(48, 49, 51);
  const QColor bp_accent = QColor(24, 144, 255);
  const QColor bp_text_primary = QColor(255, 255, 255);
  const QColor bp_text_secondary = QColor(189, 189, 189);
  const QColor bp_button_hover = QColor(60, 61, 63);
  const QColor bp_button_pressed = QColor(70, 71, 73);
};
