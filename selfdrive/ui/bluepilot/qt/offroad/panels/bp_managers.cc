// bp_managers.cc
#include "bp_managers.h"

QWidget *BPViewFactory::createView(const QString &configPath) {
  ConfigManager &config = ConfigManager::getInstance();
  if (!config.loadConfig(configPath))
    return nullptr;

  QWidget *view = createViewFromConfig(config.getConfig());
  if (view) {
    // Ensure proper cleanup when parent is destroyed
    view->setAttribute(Qt::WA_DeleteOnClose);
  }
  return view;
}

QWidget *BPViewFactory::createViewFromConfig(const QJsonObject &config) {
  QString viewType = config["type"].toString();

  if (viewType == "navigation") {
    auto *navView = new BPNavBarView();
    navView->initialize(config); // Now works with QJsonObject overload
    return navView;
  } else {
    auto *baseView = new BPBaseView();
    baseView->initialize(config); // Now works with QJsonObject overload
    return baseView;
  }
}

BPNestedView *BPDialogManager::showNestedView(const QString &title, const QJsonObject &config, QWidget *parent) {
  auto *view = new BPNestedView(parent);
  view->setupView(title, config);
  view->show();
  return view;
}

BPNestedView *BPDialogManager::showNestedView(const QString &title, const QString &configPath, QWidget *parent) {
  ConfigManager &config = ConfigManager::getInstance();
  if (!config.loadConfig(configPath))
    return nullptr;
  return showNestedView(title, config.getConfig(), parent);
}
