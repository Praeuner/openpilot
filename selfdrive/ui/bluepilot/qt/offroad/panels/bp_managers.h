// bp_managers.h
#pragma once

#include <QWidget>
#include <QJsonObject>
#include "bp_base_view.h"
#include "bp_nav_bar_view.h"
#include "bp_nested_view.h"

class BPViewFactory {
public:
  static QWidget *createView(const QString &configPath);
  static QWidget *createViewFromConfig(const QJsonObject &config);
};

class BPDialogManager {
public:
  static BPNestedView *showNestedView(const QString &title, const QJsonObject &config, QWidget *parent = nullptr);
  static BPNestedView *showNestedView(const QString &title, const QString &configPath, QWidget *parent = nullptr);
};
