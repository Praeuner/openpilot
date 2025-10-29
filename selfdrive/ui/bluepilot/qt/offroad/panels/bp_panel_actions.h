// bp_panel_actions.h
// Action handling system for BluePilot panel controls

#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <map>

#include "common/params.h"
#include "selfdrive/ui/bluepilot/bp_logging.h"

// Forward declarations
class QWidget;
class BPPanelBase;

/**
 * BPActionHandler - Centralized action handling system for BluePilot controls
 *
 * This class handles all actions triggered by controls in the BluePilot UI,
 * including parameter toggles, panel navigation, system commands, and custom actions.
 */
class BPActionHandler : public QObject {
  Q_OBJECT

public:
  explicit BPActionHandler(QWidget *parentWidget, QObject *parent = nullptr);
  virtual ~BPActionHandler() = default;

  // Main action handler - routes actions to appropriate handlers
  void handleAction(const QString &action, const QJsonObject &actionData = QJsonObject());

  // Register custom action handlers
  using ActionCallback = std::function<void(const QJsonObject&)>;
  void registerCustomAction(const QString &actionName, ActionCallback callback);

signals:
  // Emitted when a panel should be shown
  void showDriverView();
  void reviewTrainingGuide();
  void showLanguageSelector();
  void showRegulatory();

  // Emitted when nested panel should be opened
  void openNestedPanel(const QString &configPath, const QString &title);

private:
  // Built-in action handlers
  void handleToggleParam(const QJsonObject &data);
  void handleSetParam(const QJsonObject &data);
  void handleRemoveParams(const QJsonObject &data);
  void handleShowPanel(const QJsonObject &data);
  void handleShowDriverCamera(const QJsonObject &data);
  void handleShowSignal(const QJsonObject &data, const QString &defaultSignal);
  void handleShowTrainingGuide(const QJsonObject &data);
  void handleShowLanguageSelector(const QJsonObject &data);
  void handleShowRegulatory(const QJsonObject &data);
  void handleResetSettings(const QJsonObject &data);
  void handleRebootDevice(const QJsonObject &data);
  void handlePowerOffDevice(const QJsonObject &data);
  void handlePairDevice(const QJsonObject &data);
  void handleResetCalibration(const QJsonObject &data);
  void handleToggleOffroadMode(const QJsonObject &data);
  void handleViewErrorLog(const QJsonObject &data);
  void handleManageSshKeys(const QJsonObject &data);
  void handleSetCopypartyPassword(const QJsonObject &data);
  void handleSearchPlatform(const QJsonObject &data);
  void handleRemovePlatform(const QJsonObject &data);

  // Helper methods
  void emitSignalByName(const QString &signalName);
  void getUserSshKeys(const QString &username);
  void searchPlatforms(const QString &query);
  void setPlatform(const QString &platform, bool offroad);

  QWidget *widget;
  Params params;
  std::map<QString, ActionCallback> customActions;
};
