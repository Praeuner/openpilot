// selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_conditions.h

#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QWidget>
#include <QStyle>
#include <QVariant>
#include <set>
#include <vector>
#include <string>
#include <functional>
#include <map>

#include "common/params.h"
#include "common/util.h"
#include "cereal/gen/cpp/car.capnp.h"
#include "cereal/gen/cpp/custom.capnp.h"

struct ControlConditions {
  // Legacy support: "conditions" maps to both enabled and visible
  QJsonObject conditions;
  bool hasConditions;

  // New granular control
  QJsonObject enableConditions;   // Controls whether widget is enabled/disabled (grayed out)
  bool hasEnableConditions = false;
  QJsonObject visibleConditions;   // Controls whether widget is shown/hidden
  bool hasVisibleConditions = false;

  bool hasAutoReset = false;
  QString autoResetValue;
  QString paramName;
  // Dynamic descriptions
  QMap<QString, QString> descriptions;      // key -> description text
  QMap<QString, QJsonObject> descConditions; // key -> condition object
  QString defaultDescription;
  bool hasDynamicDescriptions = false;
  // Parameter substitution for descriptions
  QJsonObject paramSubstitutions;           // Defines runtime params to substitute in descriptions
  bool hasParamSubstitutions = false;
  // Note: Disabled reasons are now embedded in condition objects as "reason" fields

  // Performance optimization: cache last validated state to avoid redundant checks
  mutable bool lastEnabledState = true;
  mutable bool lastVisibleState = true;
  mutable bool cachedStateValid = false;
};

class PanelConditions {
public:
  static PanelConditions &getInstance() {
    static PanelConditions instance;
    return instance;
  }

  std::map<QWidget *, ControlConditions> controlConditions;

  bool validateCompositeConditions(const QJsonObject &conditions);
  bool validateSingleCondition(const QString &conditionType, const QJsonValue &condition);
  bool validateConditionObject(const QJsonObject &conditionObj);
  bool isGitRemoteValid(const std::vector<std::string> &searchStrs, const std::vector<std::string> &branchNames);
  void updateConditionsForAllControls(std::function<void()> updateGroupVisibility);
  bool updateConditionsForWidget(QWidget *widget, const ControlConditions &conditions);
  void logConditionCheck(const QString &controlName, const std::function<void()> &logFunc);
  QString evaluateDescription(const ControlConditions &conditions);
  QString performParameterSubstitution(const QString &text, const QJsonObject &substitutions);
  QString getDisabledReason(QWidget *widget);
  QStringList getFailedConditionReasons(const QJsonObject &conditions, const QJsonValue &disabledReasonConfig);

  // Performance: invalidate cached states when params change
  void invalidateCache() {
    for (auto &pair : controlConditions) {
      pair.second.cachedStateValid = false;
    }
  }

private:
  PanelConditions() {}
  Params params;
  QString readCarParamValue(cereal::CarParams::Reader &CP, const QString &path);
  QString readCarParamSPValue(cereal::CarParamsSP::Reader &CP_SP, const QString &path);
};
