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

struct ControlConditions {
  QJsonObject conditions;
  bool hasConditions;
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

private:
  PanelConditions() {}
  Params params;
};