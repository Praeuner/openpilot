// bp_panel_actions.cc

#include "bp_panel_actions.h"
#include "bp_panel_dialogs.h"

#include <QWidget>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/qt/widgets/input.h"
#include "selfdrive/ui/qt/api.h"
#include "selfdrive/ui/sunnypilot/ui.h"
#include "selfdrive/ui/sunnypilot/qt/util.h"
#include "common/watchdog.h"
#include "system/hardware/hw.h"

BPActionHandler::BPActionHandler(QWidget *parentWidget, QObject *parent)
    : QObject(parent), widget(parentWidget) {
  BPLog::bpInfo() << "[bp.action.handler] BPActionHandler initialized" << std::endl;
}

void BPActionHandler::handleAction(const QString &action, const QJsonObject &actionData) {
  BPLog::bpDebugGeneral() << "[bp.action.handler] Handling action: " << action.toStdString() << std::endl;

  // Check for custom actions first
  if (customActions.find(action) != customActions.end()) {
    customActions[action](actionData);
    return;
  }

  // Built-in actions
  if (action == "toggle_param") {
    handleToggleParam(actionData);
  } else if (action == "set_param") {
    handleSetParam(actionData);
  } else if (action == "remove_params") {
    handleRemoveParams(actionData);
  } else if (action == "show_panel") {
    handleShowPanel(actionData);
  } else if (action == "show_driver_camera") {
    handleShowDriverCamera(actionData);
  } else if (action == "show_training_guide") {
    handleShowTrainingGuide(actionData);
  } else if (action == "show_language_selector") {
    handleShowLanguageSelector(actionData);
  } else if (action == "show_regulatory") {
    handleShowRegulatory(actionData);
  } else if (action == "reset_settings") {
    handleResetSettings(actionData);
  } else if (action == "reboot_device") {
    handleRebootDevice(actionData);
  } else if (action == "poweroff_device") {
    handlePowerOffDevice(actionData);
  } else if (action == "pair_device") {
    handlePairDevice(actionData);
  } else if (action == "reset_calibration") {
    handleResetCalibration(actionData);
  } else if (action == "toggle_offroad_mode") {
    handleToggleOffroadMode(actionData);
  } else if (action == "view_error_log") {
    handleViewErrorLog(actionData);
  } else if (action == "search_platform") {
    handleSearchPlatform(actionData);
  } else if (action == "remove_platform") {
    handleRemovePlatform(actionData);
  } else if (action == "manage_ssh_keys") {
    handleManageSshKeys(actionData);
  } else {
    BPLog::bpWarn() << "[bp.action.handler] Unknown action: " << action.toStdString() << std::endl;
  }
}

void BPActionHandler::registerCustomAction(const QString &actionName, ActionCallback callback) {
  customActions[actionName] = callback;
  BPLog::bpInfo() << "[bp.action.handler] Registered custom action: " << actionName.toStdString() << std::endl;
}

void BPActionHandler::handleToggleParam(const QJsonObject &data) {
  QString param = data["param"].toString();
  if (param.isEmpty()) {
    BPLog::bpError() << "[bp.action.handler] toggle_param: No param specified" << std::endl;
    return;
  }

  bool currentValue = params.getBool(param.toStdString());
  bool removeOnDisable = data["remove_on_disable"].toBool(false);

  // Check if confirmation is required
  if (data["confirm"].toBool(false)) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = data["title"].toString(param);

    // Use different confirm text based on current state if provided
    QString confirmText;
    if (data.contains("confirm_text_true") && data.contains("confirm_text_false")) {
      confirmText = currentValue
                    ? data["confirm_text_true"].toString()
                    : data["confirm_text_false"].toString();
    } else {
      confirmText = data["confirm_text"].toString(QObject::tr("Are you sure?"));
    }

    config.prompt = confirmText;
    config.confirmText = data["confirm_yes_text"].toString(QObject::tr("Confirm"));
    config.cancelText = data["confirm_no_text"].toString(QObject::tr("Cancel"));

    auto *dialog = BPConfirmationDialog::showConfirmation(config, widget);
    connect(dialog, &BPConfirmationDialog::confirmed, this, [this, param, currentValue, removeOnDisable](bool accepted) {
      if (accepted) {
        if (currentValue && removeOnDisable) {
          params.remove(param.toStdString());
          BPLog::bpInfo() << "[bp.action.handler] Toggled param (removed): " << param.toStdString() << std::endl;
        } else {
          params.putBool(param.toStdString(), !currentValue);
          BPLog::bpInfo() << "[bp.action.handler] Toggled param: " << param.toStdString()
                          << " from " << (currentValue ? "true" : "false")
                          << " to " << (!currentValue ? "true" : "false") << std::endl;
        }
      }
    });
  } else {
    // No confirmation, toggle directly
    if (currentValue && removeOnDisable) {
      params.remove(param.toStdString());
      BPLog::bpInfo() << "[bp.action.handler] Toggled param (removed): " << param.toStdString() << std::endl;
    } else {
      params.putBool(param.toStdString(), !currentValue);
      BPLog::bpInfo() << "[bp.action.handler] Toggled param: " << param.toStdString()
                      << " from " << (currentValue ? "true" : "false")
                      << " to " << (!currentValue ? "true" : "false") << std::endl;
    }
  }
}

void BPActionHandler::handleShowPanel(const QJsonObject &data) {
  QString target = data["target"].toString();
  QString title = data["title"].toString();

  if (target.isEmpty()) {
    BPLog::bpError() << "[bp.action.handler] show_panel: No target specified" << std::endl;
    return;
  }

  BPLog::bpInfo() << "[bp.action.handler] Opening nested panel: " << target.toStdString() << std::endl;
  emit openNestedPanel(target, title);
}

void BPActionHandler::handleShowDriverCamera(const QJsonObject &data) {
  BPLog::bpInfo() << "[bp.action.handler] Showing driver camera" << std::endl;
  emit showDriverView();
}

void BPActionHandler::handleShowSignal(const QJsonObject &data, const QString &defaultSignal) {
  QString signalName = data["connect_signal"].toString(defaultSignal);

  // Check if confirmation is required
  if (data["confirm"].toBool(false)) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = data["title"].toString();
    config.prompt = data["confirm_text"].toString(QObject::tr("Are you sure?"));
    config.confirmText = data["confirm_yes_text"].toString(QObject::tr("Confirm"));
    config.cancelText = data["confirm_no_text"].toString(QObject::tr("Cancel"));

    auto *dialog = BPConfirmationDialog::showConfirmation(config, widget);
    connect(dialog, &BPConfirmationDialog::confirmed, this, [this, signalName](bool accepted) {
      if (accepted) {
        BPLog::bpInfo() << "[bp.action.handler] Emitting signal: " << signalName.toStdString() << std::endl;
        emitSignalByName(signalName);
      }
    });
  } else {
    BPLog::bpInfo() << "[bp.action.handler] Emitting signal: " << signalName.toStdString() << std::endl;
    emitSignalByName(signalName);
  }
}

void BPActionHandler::emitSignalByName(const QString &signalName) {
  if (signalName == "reviewTrainingGuide") {
    emit reviewTrainingGuide();
  } else if (signalName == "showLanguageSelector") {
    emit showLanguageSelector();
  } else if (signalName == "showRegulatory") {
    emit showRegulatory();
  } else if (signalName == "showDriverView") {
    emit showDriverView();
  } else {
    BPLog::bpWarn() << "[bp.action.handler] Unknown signal: " << signalName.toStdString() << std::endl;
  }
}

void BPActionHandler::handleSetParam(const QJsonObject &data) {
  QString param = data["param"].toString();
  QString value = data["value"].toString();

  if (param.isEmpty()) {
    BPLog::bpError() << "[bp.action.handler] set_param: No param specified" << std::endl;
    return;
  }

  // Check if confirmation is required
  if (data["confirm"].toBool(false)) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = data["title"].toString(param);
    config.prompt = data["confirm_text"].toString(QObject::tr("Are you sure?"));
    config.confirmText = data["confirm_yes_text"].toString(QObject::tr("Confirm"));
    config.cancelText = data["confirm_no_text"].toString(QObject::tr("Cancel"));

    auto *dialog = BPConfirmationDialog::showConfirmation(config, widget);
    connect(dialog, &BPConfirmationDialog::confirmed, this, [this, param, value](bool accepted) {
      if (accepted) {
        BPLog::bpInfo() << "[bp.action.handler] Setting param: " << param.toStdString()
                        << " to value: " << value.toStdString() << std::endl;
        params.put(param.toStdString(), value.toStdString());
      }
    });
  } else {
    BPLog::bpInfo() << "[bp.action.handler] Setting param: " << param.toStdString()
                    << " to value: " << value.toStdString() << std::endl;
    params.put(param.toStdString(), value.toStdString());
  }
}

void BPActionHandler::handleRemoveParams(const QJsonObject &data) {
  QJsonArray paramsArray = data["params"].toArray();

  if (paramsArray.isEmpty()) {
    BPLog::bpError() << "[bp.action.handler] remove_params: No params specified" << std::endl;
    return;
  }

  // Check if confirmation is required
  if (data["confirm"].toBool(false)) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = data["title"].toString(QObject::tr("Remove Parameters"));
    config.prompt = data["confirm_text"].toString(QObject::tr("Are you sure?"));
    config.confirmText = data["confirm_yes_text"].toString(QObject::tr("Confirm"));
    config.cancelText = data["confirm_no_text"].toString(QObject::tr("Cancel"));

    auto *dialog = BPConfirmationDialog::showConfirmation(config, widget);
    connect(dialog, &BPConfirmationDialog::confirmed, this, [this, paramsArray](bool accepted) {
      if (accepted) {
        for (const auto &paramValue : paramsArray) {
          QString param = paramValue.toString();
          BPLog::bpInfo() << "[bp.action.handler] Removing param: " << param.toStdString() << std::endl;
          params.remove(param.toStdString());
        }
      }
    });
  } else {
    for (const auto &paramValue : paramsArray) {
      QString param = paramValue.toString();
      BPLog::bpInfo() << "[bp.action.handler] Removing param: " << param.toStdString() << std::endl;
      params.remove(param.toStdString());
    }
  }
}

void BPActionHandler::handleResetSettings(const QJsonObject &data) {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = QObject::tr("Reset Settings");
  config.prompt = QObject::tr("Are you sure you want to reset all settings? This cannot be undone.");
  config.confirmText = QObject::tr("Reset");
  config.cancelText = QObject::tr("Cancel");

  auto *dialog = BPConfirmationDialog::showConfirmation(config, widget);
  connect(dialog, &BPConfirmationDialog::confirmed, this, [this](bool accepted) {
    if (accepted) {
      // Second confirmation
      BPConfirmationDialog::ConfirmConfig config2;
      config2.title = QObject::tr("Final Warning");
      config2.prompt = QObject::tr("The reset cannot be undone. You have been warned.");
      config2.confirmText = QObject::tr("Confirm");
      config2.cancelText = QObject::tr("Cancel");

      auto *dialog2 = BPConfirmationDialog::showConfirmation(config2, widget);
      connect(dialog2, &BPConfirmationDialog::confirmed, this, [this](bool accepted2) {
        if (accepted2) {
          BPLog::bpInfo() << "[bp.action.handler] Resetting all settings" << std::endl;

          const std::vector<std::string> keys = params.allKeys();
          for (const auto &key : keys) {
            params.remove(key);
          }

          Hardware::reboot();
        }
      });
    }
  });
}

void BPActionHandler::handleRebootDevice(const QJsonObject &data) {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = data["title"].toString(QObject::tr("Reboot Device"));
  config.prompt = data["confirm_text"].toString(QObject::tr("Are you sure you want to reboot the device?"));
  config.confirmText = data["confirm_yes_text"].toString(QObject::tr("Reboot"));
  config.cancelText = data["confirm_no_text"].toString(QObject::tr("Cancel"));

  auto *dialog = BPConfirmationDialog::showConfirmation(config, widget);
  connect(dialog, &BPConfirmationDialog::confirmed, this, [](bool accepted) {
    if (accepted) {
      BPLog::bpInfo() << "[bp.action.handler] Rebooting device" << std::endl;
      Hardware::reboot();
    }
  });
}

void BPActionHandler::handlePowerOffDevice(const QJsonObject &data) {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = data["title"].toString(QObject::tr("Power Off Device"));
  config.prompt = data["confirm_text"].toString(QObject::tr("Are you sure you want to power off the device?"));
  config.confirmText = data["confirm_yes_text"].toString(QObject::tr("Power Off"));
  config.cancelText = data["confirm_no_text"].toString(QObject::tr("Cancel"));

  auto *dialog = BPConfirmationDialog::showConfirmation(config, widget);
  connect(dialog, &BPConfirmationDialog::confirmed, this, [](bool accepted) {
    if (accepted) {
      BPLog::bpInfo() << "[bp.action.handler] Powering off device" << std::endl;
      Hardware::poweroff();
    }
  });
}

void BPActionHandler::handlePairDevice(const QJsonObject &data) {
  BPLog::bpInfo() << "[bp.action.handler] Opening pairing dialog" << std::endl;
  // The pairing functionality will be handled by emitting a signal that the parent handles
  // This allows the settings window to open its existing PairingPopup
  if (widget) {
    // We'll need to add a signal for this
    BPLog::bpWarn() << "[bp.action.handler] Pair device action needs parent implementation" << std::endl;
  }
}

void BPActionHandler::handleResetCalibration(const QJsonObject &data) {
  // Check if engaged
  if (uiStateSP()->engaged()) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = QObject::tr("Cannot Reset");
    config.prompt = QObject::tr("Disengage to reset calibration.");
    config.confirmText = QObject::tr("OK");
    config.cancelText = QObject::tr("OK");

    BPConfirmationDialog::showConfirmation(config, widget);
    return;
  }

  BPConfirmationDialog::ConfirmConfig config;
  config.title = data["title"].toString(QObject::tr("Reset Calibration"));
  config.prompt = data["confirm_text"].toString(QObject::tr("Are you sure you want to reset calibration?"));
  config.confirmText = data["confirm_yes_text"].toString(QObject::tr("Reset"));
  config.cancelText = data["confirm_no_text"].toString(QObject::tr("Cancel"));

  auto *dialog = BPConfirmationDialog::showConfirmation(config, widget);
  connect(dialog, &BPConfirmationDialog::confirmed, this, [this](bool accepted) {
    if (accepted) {
      // Double-check engaged state before removing
      if (!uiStateSP()->engaged()) {
        BPLog::bpInfo() << "[bp.action.handler] Resetting calibration" << std::endl;
        params.remove("CalibrationParams");
        params.remove("LiveTorqueParameters");
        params.remove("LiveParameters");
        params.remove("LiveParametersV2");
        params.remove("LiveDelay");
        params.putBool("OnroadCycleRequested", true);
      }
    }
  });
}

void BPActionHandler::handleToggleOffroadMode(const QJsonObject &data) {
  // Check if engaged
  if (uiStateSP()->engaged()) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = QObject::tr("Cannot Toggle");
    config.prompt = QObject::tr("Disengage to enter Always Offroad mode.");
    config.confirmText = QObject::tr("OK");
    config.cancelText = QObject::tr("OK");

    BPConfirmationDialog::showConfirmation(config, widget);
    return;
  }

  bool currentlyOffroad = params.getBool("OffroadMode");

  BPConfirmationDialog::ConfirmConfig config;
  if (currentlyOffroad) {
    config.title = QObject::tr("Exit Always Offroad");
    config.prompt = QObject::tr("Are you sure you want to exit Always Offroad mode?");
    config.confirmText = QObject::tr("Exit");
  } else {
    config.title = QObject::tr("Enable Always Offroad");
    config.prompt = QObject::tr("Are you sure you want to enter Always Offroad mode?");
    config.confirmText = QObject::tr("Enable");
  }
  config.cancelText = QObject::tr("Cancel");

  auto *dialog = BPConfirmationDialog::showConfirmation(config, widget);
  connect(dialog, &BPConfirmationDialog::confirmed, this, [this, currentlyOffroad](bool accepted) {
    if (accepted) {
      // Double-check engaged state
      if (!uiStateSP()->engaged()) {
        if (currentlyOffroad) {
          BPLog::bpInfo() << "[bp.action.handler] Exiting Always Offroad mode" << std::endl;
          params.remove("OffroadMode");
        } else {
          BPLog::bpInfo() << "[bp.action.handler] Entering Always Offroad mode" << std::endl;
          params.putBool("OffroadMode", true);
        }
      }
    }
  });
}

void BPActionHandler::handleShowTrainingGuide(const QJsonObject &data) {
  // Check if confirmation is required
  if (data["confirm"].toBool(false)) {
    BPConfirmationDialog::ConfirmConfig config;
    config.title = data["title"].toString(QObject::tr("Training Guide"));
    config.prompt = data["confirm_text"].toString(QObject::tr("Are you sure you want to review the training guide?"));
    config.confirmText = data["confirm_yes_text"].toString(QObject::tr("Review"));
    config.cancelText = data["confirm_no_text"].toString(QObject::tr("Cancel"));

    auto *dialog = BPConfirmationDialog::showConfirmation(config, widget);
    connect(dialog, &BPConfirmationDialog::confirmed, this, [this](bool accepted) {
      if (accepted) {
        BPLog::bpInfo() << "[bp.action.handler] Showing training guide" << std::endl;
        emit reviewTrainingGuide();
      }
    });
  } else {
    BPLog::bpInfo() << "[bp.action.handler] Showing training guide" << std::endl;
    emit reviewTrainingGuide();
  }
}

void BPActionHandler::handleShowLanguageSelector(const QJsonObject &data) {
  BPLog::bpInfo() << "[bp.action.handler] Showing language selector" << std::endl;

  QMap<QString, QString> langs = getSupportedLanguages();
  QString selection = MultiOptionDialog::getSelection(QObject::tr("Select a language"), langs.keys(), langs.key(uiState()->language), widget);

  if (!selection.isEmpty()) {
    // put language setting, exit Qt UI, and trigger fast restart
    params.put("LanguageSetting", langs[selection].toStdString());
    qApp->exit(18);
    watchdog_kick(0);
  }
}

void BPActionHandler::handleShowRegulatory(const QJsonObject &data) {
  BPLog::bpInfo() << "[bp.action.handler] Showing regulatory info" << std::endl;

  const std::string txt = util::read_file("../assets/offroad/fcc.html");
  ConfirmationDialog::rich(QString::fromStdString(txt), widget);
}

void BPActionHandler::handleViewErrorLog(const QJsonObject &data) {
  BPLog::bpInfo() << "[bp.action.handler] Viewing error log" << std::endl;

  QFileInfo file("/data/community/crashes/error.log");
  QString text;
  if (file.exists()) {
    text = "<b>" + file.lastModified().toString("dd-MMM-yyyy hh:mm:ss ").toUpper() + "</b><br><br>";
  }
  text += QString::fromStdString(util::read_file("/data/community/crashes/error.log"));
  ConfirmationDialog::rich(text, widget);
}

void BPActionHandler::handleManageSshKeys(const QJsonObject &data) {
  BPLog::bpInfo() << "[bp.action.handler] Managing SSH keys" << std::endl;

  QString currentKeys = QString::fromStdString(params.get("GithubSshKeys"));

  if (!currentKeys.isEmpty()) {
    // Keys exist - show current username and option to remove
    QString currentUsername = QString::fromStdString(params.get("GithubUsername"));
    QString message = QObject::tr("Current GitHub username: <b>%1</b><br><br>"
                                  "Warning: This grants SSH access to all public keys in your GitHub settings. "
                                  "Never enter a GitHub username other than your own. "
                                  "A comma employee will NEVER ask you to add their GitHub username.<br><br>"
                                  "Do you want to remove these SSH keys?").arg(currentUsername);

    if (ConfirmationDialog::confirm(message, QObject::tr("Remove SSH Keys"), widget)) {
      params.remove("GithubUsername");
      params.remove("GithubSshKeys");
      BPLog::bpInfo() << "[bp.action.handler] SSH keys removed" << std::endl;
      ConfirmationDialog::alert(QObject::tr("SSH keys have been removed."), widget);
    }
  } else {
    // No keys - prompt for GitHub username
    QString username = InputDialog::getText(QObject::tr("Enter your GitHub username"), widget,
                                           QObject::tr("Warning: This grants SSH access to all public keys in your GitHub settings. "
                                                      "Never enter a GitHub username other than your own. "
                                                      "A comma employee will NEVER ask you to add their GitHub username."));
    if (!username.isEmpty()) {
      BPLog::bpInfo() << "[bp.action.handler] Fetching SSH keys for username: " << username.toStdString() << std::endl;
      getUserSshKeys(username);
    }
  }
}

void BPActionHandler::getUserSshKeys(const QString &username) {
  HttpRequest *request = new HttpRequest(widget, false);

  QObject::connect(request, &HttpRequest::requestDone, [=](const QString &resp, bool success) {
    if (success) {
      if (!resp.isEmpty()) {
        params.put("GithubUsername", username.toStdString());
        params.put("GithubSshKeys", resp.toStdString());
        BPLog::bpInfo() << "[bp.action.handler] SSH keys added successfully for: " << username.toStdString() << std::endl;
        ConfirmationDialog::alert(QObject::tr("SSH keys have been added for user: %1").arg(username), widget);
      } else {
        BPLog::bpWarn() << "[bp.action.handler] Username has no keys: " << username.toStdString() << std::endl;
        ConfirmationDialog::alert(QObject::tr("Username '%1' has no keys on GitHub").arg(username), widget);
      }
    } else {
      if (request->timeout()) {
        BPLog::bpError() << "[bp.action.handler] Request timed out for username: " << username.toStdString() << std::endl;
        ConfirmationDialog::alert(QObject::tr("Request timed out"), widget);
      } else {
        BPLog::bpError() << "[bp.action.handler] Username doesn't exist: " << username.toStdString() << std::endl;
        ConfirmationDialog::alert(QObject::tr("Username '%1' doesn't exist on GitHub").arg(username), widget);
      }
    }

    request->deleteLater();
  });

  request->sendRequest("https://github.com/" + username + ".keys");
}

void BPActionHandler::handleSearchPlatform(const QJsonObject &data) {
  BPLog::bpInfo() << "[bp.action.handler] Platform search requested" << std::endl;

  QString query = InputDialog::getText(QObject::tr("Search your vehicle"), widget,
                                       QObject::tr("Enter model year (e.g., 2021) and model name (Toyota Corolla):"), false);
  if (query.length() > 0) {
    searchPlatforms(query);
  }
}

void BPActionHandler::handleRemovePlatform(const QJsonObject &data) {
  BPLog::bpInfo() << "[bp.action.handler] Remove platform requested" << std::endl;

  if (ConfirmationDialog::confirm(QObject::tr("Remove manual vehicle selection and return to automatic fingerprinting?"),
                                  QObject::tr("Remove"), widget)) {
    params.remove("CarPlatformBundle");
    BPLog::bpInfo() << "[bp.action.handler] Platform bundle removed" << std::endl;
    ConfirmationDialog::alert(QObject::tr("Manual vehicle selection removed. The device will fingerprint automatically on next drive."), widget);
  }
}

void BPActionHandler::searchPlatforms(const QString &query) {
  if (query.isEmpty()) {
    return;
  }

  QMap<QString, QVariantMap> platforms = loadPlatformList();
  QSet<QString> matched_cars;

  QString normalized_query = query.simplified().toLower();
  QStringList tokens = normalized_query.split(" ", QString::SkipEmptyParts);

  int search_year = -1;
  QStringList search_terms;

  for (const QString &token : tokens) {
    bool ok;
    int year = token.toInt(&ok);
    if (ok && year >= 1900 && year <= 2100) {
      search_year = year;
    } else {
      search_terms << token;
    }
  }

  for (auto it = platforms.constBegin(); it != platforms.constEnd(); ++it) {
    QString platform_name = it.key();
    QVariantMap platform_data = it.value();

    if (search_year != -1) {
      QVariantList year_list = platform_data["year"].toList();
      bool year_match = false;
      for (const QVariant &year_var : year_list) {
        int year = year_var.toString().toInt();
        if (year == search_year) {
          year_match = true;
          break;
        }
      }
      if (!year_match) continue;
    }

    QString normalized_make = platform_data["make"].toString().normalized(QString::NormalizationForm_KD).toLower();
    QString normalized_model = platform_data["model"].toString().normalized(QString::NormalizationForm_KD).toLower();
    normalized_make.remove(QRegularExpression("[^a-zA-Z0-9\\s]"));
    normalized_model.remove(QRegularExpression("[^a-zA-Z0-9\\s]"));

    bool all_terms_match = true;
    for (const QString &term : search_terms) {
      QString normalized_term = term.normalized(QString::NormalizationForm_KD).toLower();
      normalized_term.remove(QRegularExpression("[^a-zA-Z0-9\\s]"));

      bool term_matched = false;

      if (normalized_make.contains(normalized_term, Qt::CaseInsensitive)) {
        term_matched = true;
      }

      if (!term_matched) {
        if (term.contains(QRegularExpression("[a-z]\\d|\\d[a-z]", QRegularExpression::CaseInsensitiveOption))) {
          QString clean_model = normalized_model;
          QString clean_term = normalized_term;
          clean_model.remove(" ");
          clean_term.remove(" ");
          if (clean_model.contains(clean_term, Qt::CaseInsensitive)) {
            term_matched = true;
          }
        } else {
          if (normalized_model.contains(normalized_term, Qt::CaseInsensitive)) {
            term_matched = true;
          }
        }
      }

      if (!term_matched) {
        all_terms_match = false;
        break;
      }
    }

    if (all_terms_match) {
      matched_cars.insert(platform_name);
    }
  }

  QStringList results = matched_cars.toList();
  results.sort();

  if (results.isEmpty()) {
    ConfirmationDialog::alert(QObject::tr("No vehicles found for query: %1").arg(query), widget);
    return;
  }

  QString selected_platform = MultiOptionDialog::getSelection(QObject::tr("Select a vehicle"), results, "", widget);

  if (!selected_platform.isEmpty()) {
    bool offroad = params.getBool("IsOffroad");
    setPlatform(selected_platform, offroad);
  }
}

void BPActionHandler::setPlatform(const QString &_platform, bool offroad) {
  QMap<QString, QVariantMap> platforms = loadPlatformList();
  QVariantMap platform_data = platforms[_platform];

  const QString offroad_msg = offroad ? QObject::tr("This setting will take effect immediately.") :
                                        QObject::tr("This setting will take effect once the device enters offroad state.");
  const QString msg = QString("<b>%1</b><br><br>%2")
                      .arg(_platform, offroad_msg);

  QString content("<body><h2 style=\"text-align: center;\">" + QObject::tr("Vehicle Selector") + "</h2><br>"
                  "<p style=\"text-align: center; margin: 0 128px; font-size: 50px;\">" + msg + "</p></body>");

  if (ConfirmationDialog(content, QObject::tr("Confirm"), QObject::tr("Cancel"), true, widget).exec()) {
    QJsonObject json_bundle;
    json_bundle["platform"] = platform_data["platform"].toString();
    json_bundle["name"] = _platform;
    json_bundle["make"] = platform_data["make"].toString();
    json_bundle["brand"] = platform_data["brand"].toString();
    json_bundle["model"] = platform_data["model"].toString();
    json_bundle["package"] = platform_data["package"].toString();

    QVariantList yearList = platform_data["year"].toList();
    QJsonArray yearArray;
    for (const QVariant &year : yearList) {
      yearArray.append(year.toString());
    }
    json_bundle["year"] = yearArray;

    QString json_bundle_str = QString::fromUtf8(QJsonDocument(json_bundle).toJson(QJsonDocument::Compact));

    params.put("CarPlatformBundle", json_bundle_str.toStdString());
    BPLog::bpInfo() << "[bp.action.handler] Platform set to: " << _platform.toStdString() << std::endl;
  }
}
