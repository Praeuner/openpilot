// selfdrive/ui/BP/qt/offroad/panels/bp_panel_conditions.cc

#include "bp_panel_conditions.h"
#include <iostream>

bool PanelConditions::validateSingleCondition(const QString &conditionType, const QJsonValue &condition) {
  if (conditionType == "paramValueEquals") {
    QJsonObject equals = condition.toObject();
    for (auto it = equals.begin(); it != equals.end(); ++it) {
      std::string paramName = it.key().toStdString();
      std::string paramVal = params.get(paramName);
      QString expected = it.value().toString();

      if (paramVal.empty()) {
        return false;
      }

      bool isNumeric;
      double expectedNum = expected.toDouble(&isNumeric);

      if (isNumeric) {
        double actualNum = QString::fromStdString(paramVal).toDouble(&isNumeric);
        if (!isNumeric || std::abs(actualNum - expectedNum) > 1e-6) {
          return false;
        }
      } else if (paramVal != expected.toStdString()) {
        return false;
      }
    }
  } else if (conditionType == "paramValueGreaterThan") {
    QJsonObject greaterThan = condition.toObject();
    for (auto it = greaterThan.begin(); it != greaterThan.end(); ++it) {
      std::string paramName = it.key().toStdString();
      std::string paramVal = params.get(paramName);
      double compareNum = it.value().toDouble();

      if (paramVal.empty()) {
        return false;
      }

      double paramNum = std::stod(paramVal);
      if (paramNum <= compareNum) {
        return false;
      }
    }
  } else if (conditionType == "paramValueLessThan") {
    QJsonObject lessThan = condition.toObject();
    for (auto it = lessThan.begin(); it != lessThan.end(); ++it) {
      std::string paramName = it.key().toStdString();
      std::string paramVal = params.get(paramName);
      double compareNum = it.value().toDouble();

      if (paramVal.empty()) {
        return false;
      }

      double paramNum = std::stod(paramVal);
      if (paramNum >= compareNum) {
        return false;
      }
    }
  } else if (conditionType == "paramValueInRange") {
    QJsonObject range = condition.toObject();
    for (auto it = range.begin(); it != range.end(); ++it) {
      std::string paramName = it.key().toStdString();
      std::string paramVal = params.get(paramName);
      QJsonObject rangeValues = it.value().toObject();
      double min = rangeValues["min"].toDouble();
      double max = rangeValues["max"].toDouble();

      if (paramVal.empty()) {
        return false;
      }

      double paramNum = std::stod(paramVal);
      if (paramNum < min || paramNum > max) {
        return false;
      }
    }
  } else if (conditionType == "git_remote") {
    QJsonArray remotes = condition.toArray();
    std::vector<std::string> searchStrs;
    for (const auto &remote : remotes) {
      searchStrs.push_back(remote.toString().toStdString());
    }
    return isGitRemoteValid(searchStrs, {});
  } else if (conditionType == "git_branch") {
    QJsonArray branches = condition.toArray();
    std::vector<std::string> branchStrs;
    for (const auto &branch : branches) {
      branchStrs.push_back(branch.toString().toStdString());
    }
    return isGitRemoteValid({}, branchStrs);
  } else if (conditionType == "onlyWhenTheseParams") {
    QJsonArray requiredParams = condition.toArray();
    for (const auto &param : requiredParams) {
      std::string paramName = param.toString().toStdString();
      if (!params.getBool(paramName)) {
        return false;
      }
    }
  }

  return true;
}

bool PanelConditions::validateConditionObject(const QJsonObject &conditionObj) {
  if (conditionObj.contains("allConditionsTrue") || conditionObj.contains("anyConditionsTrue")) {
    return validateCompositeConditions(conditionObj);
  }

  for (auto it = conditionObj.begin(); it != conditionObj.end(); ++it) {
    if (!PanelConditions::validateSingleCondition(it.key(), it.value())) {
      return false;
    }
  }

  return true;
}

bool PanelConditions::validateCompositeConditions(const QJsonObject &conditions) {
  bool result = true;

  if (conditions.contains("anyConditionsTrue")) {
    QJsonArray anyConditions = conditions["anyConditionsTrue"].toArray();
    if (!anyConditions.empty()) {
      bool anyTrue = false;
      for (const auto &condition : anyConditions) {
        if (condition.isObject()) {
          QJsonObject condObj = condition.toObject();
          bool conditionResult;

          if (condObj.contains("allConditionsTrue") || condObj.contains("anyConditionsTrue")) {
            conditionResult = validateCompositeConditions(condObj);
          } else {
            conditionResult = validateConditionObject(condObj);
          }

          if (conditionResult) {
            anyTrue = true;
            break;
          }
        }
      }
      result &= anyTrue;
    }
  }

  if (conditions.contains("allConditionsTrue")) {
    QJsonArray allConditions = conditions["allConditionsTrue"].toArray();
    for (const auto &condition : allConditions) {
      if (condition.isObject()) {
        QJsonObject condObj = condition.toObject();
        bool conditionResult;

        if (condObj.contains("allConditionsTrue") || condObj.contains("anyConditionsTrue")) {
          conditionResult = validateCompositeConditions(condObj);
        } else {
          conditionResult = validateConditionObject(condObj);
        }

        if (!conditionResult) {
          result = false;
          break;
        }
      }
    }
  }

  return result;
}

void PanelConditions::updateConditionsForAllControls(std::function<void()> updateGroupVisibility) {
  static std::set<std::string> processedControls;
  processedControls.clear();

  // Create a copy since we might modify during iteration
  auto conditions = controlConditions;
  for (const auto &pair : conditions) {
    QWidget *ctrl = pair.first;

    // Skip if control was deleted
    if (!ctrl || ctrl->parent() == nullptr) {
      controlConditions.erase(ctrl);
      continue;
    }

    const ControlConditions &conds = pair.second;

    if (conds.hasConditions && ctrl) {
      std::string controlName = ctrl->objectName().toStdString();

      if (processedControls.find(controlName) != processedControls.end()) {
        continue;
      }
      processedControls.insert(controlName);

      bool currentlyEnabled = ctrl->isEnabled();
      bool shouldBeEnabled = validateCompositeConditions(conds.conditions);

      if (currentlyEnabled != shouldBeEnabled) {
        ctrl->setEnabled(shouldBeEnabled);
        ctrl->setProperty("enabled", QVariant(shouldBeEnabled));
        ctrl->update();
        ctrl->style()->unpolish(ctrl);
        ctrl->style()->polish(ctrl);
        ctrl->update();
      }
    }
  }

  updateGroupVisibility();
}

bool PanelConditions::isGitRemoteValid(const std::vector<std::string> &searchStrs, const std::vector<std::string> &branchNames) {
  std::string gitRemote = params.get("GitRemote");
  std::string gitBranch = params.get("GitBranch");

  bool debugMode = gitRemote.empty();

  if (debugMode || searchStrs.empty() || std::find(searchStrs.begin(), searchStrs.end(), "any") != searchStrs.end()) {
    return true;
  }

  if (gitRemote.empty()) {
    return false;
  }

  bool searchStrFound = false;
  for (const auto &searchStr : searchStrs) {
    if (!searchStr.empty() && gitRemote.find(searchStr) != std::string::npos) {
      searchStrFound = true;
      break;
    }
  }

  if (!searchStrFound) {
    return false;
  }

  if (!branchNames.empty()) {
    bool branchFound = false;
    for (const auto &branchName : branchNames) {
      if (!branchName.empty() && gitBranch == branchName) {
        branchFound = true;
        break;
      }
    }
    if (!branchFound) {
      return false;
    }
  }

  return true;
}

void PanelConditions::logConditionCheck(const QString &controlName, const std::function<void()> &logFunc) {
  const int width = 80;
  std::string separator(width, '=');
  std::string controlNameStr = controlName.toStdString();
  int padding = (width - controlNameStr.length() - 2) / 2;
  std::string centeredName = std::string(padding, ' ') + controlNameStr + std::string(padding, ' ');

  std::cout << "\n" << separator << std::endl;
  std::cout << centeredName << std::endl;
  std::cout << separator << std::endl;

  logFunc();

  std::cout << separator << "\n" << std::endl;
}

bool PanelConditions::updateConditionsForWidget(QWidget *widget, const ControlConditions &conditions) {
  if (!widget || !conditions.hasConditions) {
    return true;
  }

  bool currentlyEnabled = widget->isEnabled();
  bool shouldBeEnabled = validateCompositeConditions(conditions.conditions);

  if (currentlyEnabled != shouldBeEnabled) {
    widget->setEnabled(shouldBeEnabled);
    widget->setProperty("enabled", QVariant(shouldBeEnabled));
    widget->update();
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
  }

  return shouldBeEnabled;
}
