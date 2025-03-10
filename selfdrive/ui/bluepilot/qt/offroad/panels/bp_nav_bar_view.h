// selfdrive/ui/bluepilot/qt/offroad/panels/bp_nav_bar_view.h

#pragma once

#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QJsonObject>
#include <QJsonArray>
#include "bp_panel_base.h"

// Constants
constexpr int NAV_PANEL_WIDTH = 200;
constexpr int NAV_BUTTON_MARGIN = 20;
constexpr int NAV_ICON_SIZE = 80;

// Custom navigation button
class NavButton : public QPushButton {
  Q_OBJECT
public:
  NavButton(const QString &text, const QIcon &icon, QWidget *parent = nullptr);
  void setSelected(bool selected);
};

class BPNavBarView : public QWidget {
  Q_OBJECT

public:
  explicit BPNavBarView(QWidget *parent = nullptr);
  bool initialize(const QString &configPath);
  bool initialize(const QJsonObject &config);
  void refresh();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  struct NavItem {
    QString title;
    QString icon;
    NavButton *button = nullptr;
    BPPanelBase *panel = nullptr;
    QJsonObject config;
  };

  void setupLayout();
  void createNavItems(const QJsonArray &items);
  void setupNavItem(const QJsonObject &item, int index);
  void selectNavItem(int index);
  void updateActivityState();

  QHBoxLayout *main_layout;
  QWidget *nav_panel;
  QVBoxLayout *nav_layout;
  QStackedWidget *content_stack;
  std::vector<NavItem> nav_items;
};
