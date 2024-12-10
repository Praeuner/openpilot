#include <filesystem>
#include <iostream>
#include <QTimer>
#include <QMouseEvent>
#include <QCoreApplication>

#include "selfdrive/ui/ui.h"
#include "selfdrive/ui/qt/offroad/ford_panel.h"
#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/widgets/scrollview.h"
#include "common/params.h"

FordSettingsPanel::FordSettingsPanel(SettingsWindow *parent) : FordSettingsListWidget(parent) {
  setSpacing(50);

  // Initialize the timer
  activityTimer = new QTimer(this);
  activityTimer->setInterval(9000); // 9 seconds
  connect(activityTimer, &QTimer::timeout, this, &FordSettingsPanel::simulateActivity);

  // Set up the max duration timer
  QTimer::singleShot(27000, this, &FordSettingsPanel::stopActivitySimulation); // 4 minutes and 30 seconds

  // Enable mouse tracking to receive mouse move events
  setMouseTracking(true);

  // Vehicle Model Selector
  addVehicleSelector();

  // Preferences
  addPreferences();

  // Lateral Tuning
  // addLateralTuning();

  // Brake Tuning
  // addBrakeTuning();

  // Limits Tuning
  // addLimitsTuning();

  // Parameter Viewer
  addParameterButtons();
}

FordSettingsPanel::~FordSettingsPanel() {
  // Ensure the timer is stopped
  activityTimer->stop();
}

QGroupBox *FordSettingsPanel::createStyledGroupBox(const QString &title) {
  QGroupBox *groupBox = new QGroupBox(title);
  groupBox->setStyleSheet(R"(
    QGroupBox {
      border: 1px solid #cccccc;
      border-radius: 10px;
      margin-top: 30px;
      font-weight: bold;
      padding: 15px;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      left: 20px;
      padding: 0 10px;
      color: #666666;
    }
  )");
  return groupBox;
}

QPushButton* FordSettingsPanel::createResetButton() {
    QPushButton* resetButton = new QPushButton();
    resetButton->setObjectName("resetButton");  // Add this line
    resetButton->setText(tr(" Reset"));  // Space before "Reset" for icon spacing

    // Create a custom icon with styling
    QIcon icon = QIcon("../assets/offroad/icon_reset.png");
    icon.addPixmap(QPixmap("../assets/offroad/icon_reset.png"), QIcon::Normal, QIcon::On);
    icon.addPixmap(QPixmap("../assets/offroad/icon_reset.png"), QIcon::Disabled);

    resetButton->setIcon(icon);
    resetButton->setStyleSheet(R"(
        QPushButton {
            border: none;
            border-radius: 25px;
            background-color: #4a4a4a;
            color: white;
            padding: 8px 8px;
            font-size: 30px;
            font-weight: 500;
            min-width: 120px;
            min-height: 50px;
        }
        QPushButton:pressed {
            background-color: #3a3a3a;
        }
        QPushButton::icon {
            width: 40px;
            height: 40px;
        }
        QPushButton::icon:disabled {
            opacity: 0.5;
        }
    )");

    resetButton->setIconSize(QSize(40, 40));
    resetButton->setCursor(Qt::PointingHandCursor);

    // Ensure icon is on the left and text is on the right
    resetButton->setLayoutDirection(Qt::LeftToRight);

    // Set a fixed size to ensure consistent appearance
    resetButton->setFixedSize(200, 60);

    std::cout << "Reset button created with icon" << std::endl;
    return resetButton;
}

void FordSettingsPanel::addVehicleSelector() {
  bool anyCtrlAdded = false;
  QGroupBox *vehicleGroup = createStyledGroupBox(tr("Vehicle Selection: (Tap for more info)"));
  QVBoxLayout *vehicleLayout = new QVBoxLayout(vehicleGroup);

  const std::map<QString, QString> vehicleModels = {
    {"F-150 2021-2023", "FORD_F_150_MK14"},
    {"F-150 Lightning", "FORD_F_150_LIGHTNING"},
    {"Mustang Mach-E", "FORD_MUSTANG_MACH_E_MK1"}
  };

  ButtonControl* btn = new ButtonControl(tr("Vehicle Model"), tr("SELECT"), tr("Select your vehicle model"));

  QString currentModel = QString::fromStdString(params.get("FordSelectedVehicleModel"));
  if (!currentModel.isEmpty()) {
    for (const auto &[desc, value] : vehicleModels) {
      if (value == currentModel) {
        btn->setValue(desc);
        break;
      }
    }
  }

  QObject::connect(btn, &ButtonControl::clicked, [=]() {
    QStringList items;
    for (const auto &[desc, _] : vehicleModels) {
      items.append(desc);
    }

    QString cur = QString::fromStdString(params.get("FordSelectedVehicleModel"));
    for (const auto &[desc, value] : vehicleModels) {
      if (value == cur) {
        cur = desc;
        break;
      }
    }

    QString selection = MultiOptionDialog::getSelection(tr("Select your vehicle model"), items, cur, this);

    if (!selection.isEmpty()) {
      QString modelValue = vehicleModels.at(selection);
      params.put("FordSelectedVehicleModel", modelValue.toStdString());
      btn->setValue(selection);
      // setenv("FINGERPRINT", modelValue.toStdString().c_str(), 1);

      if (ConfirmationDialog::confirm(tr("Reboot required for changes to take effect. Would you like to reboot now?"), tr("Reboot"), this)) {
        params.putBool("DoReboot", true);
      }
    }
  });

  anyCtrlAdded = true;
  vehicleLayout->addWidget(btn);

  if (!anyCtrlAdded) {
    std::cout << "No vehicle selector added" << std::endl;
    return;
  }
  addItem(vehicleGroup);
}

void FordSettingsPanel::addPreferences() {
  bool anyToggleAdded = false;
  QGroupBox *preferencesGroup = createStyledGroupBox(tr("Preferences: (Tap for more info)"));
  QVBoxLayout *preferencesLayout = new QVBoxLayout(preferencesGroup);

  std::vector<std::tuple<QString, QString, QString, std::vector<std::string>, std::vector<std::string>>> preference_toggles = {
    {"FordPrefSendHandsFreeCanMsg", tr("Show Hands-Free UI (On Supported Vehicles)"), tr("This will send the necessary messages to allow the Hands-Free interface in the cluster on supported Ford/Lincoln vehicles with BlueCruise enabled."), {"any"}, {}},
    {"FordPrefLaneDepartCanMsg", tr("Send Lane Departure Signals to Vehicle"), tr("When enabled, this will attempt to send Openpilot lane departure signals to the vehicle for a native response in the cluster and alerts"), { "any"}, {}},
    {"FordPrefDriverMonitorCanMsg", tr("Send Driver Monitor Signals to Vehicle"), tr("When enabled, this will attempt to send Openpilot driver monitor notice signals to the vehicle for a native response in the cluster and alerts"), { "any"}, {}},
    {"FordPrefHumanTurnDetectionEnable", tr("Enable Human Turn Detection"), tr("When enabled, this will reset the steering so you don't fight the wheel when making manual turns"), { "any"}, {}},
    {"FordPrefQuietDrive", tr("Quiet Drive 🤫", tr("BluePilot will display alerts but only play the most important warning sounds.")), { "any"}, {}},
    {"FordPrefEnableDebugLogs", tr("Enable Debug Logging"), tr("Enables outputting debug in the logs and console"), { "ford-op/sp-dev-c3"}, {}},
  };

  for (const auto &[param, title, desc, git_remote_allowed, git_branch_allowed] : preference_toggles) {
    bool ctrlAllowed = isGitRemoteValid(git_remote_allowed, git_branch_allowed);
    if (ctrlAllowed) {
      anyToggleAdded = true;
      auto toggle = new ParamControl(param, title, desc, "../assets/offroad/icon_blank.png");
      preferencesLayout->addWidget(toggle);
      toggles[param.toStdString()] = toggle;
    }
  }
  if (!anyToggleAdded) {
    std::cout << "No preferences toggles added" << std::endl;
    return;
  }
  addItem(preferencesGroup);
}

void FordSettingsPanel::addLateralTuning() {
  bool anyCtrlsAdded = false;
  lateralTuningGroup = createStyledGroupBox(tr("Lateral Tuning: (Tap on control for more info)"));
  QVBoxLayout *lateralTuningLayout = new QVBoxLayout(lateralTuningGroup);

  QHBoxLayout *titleLayout = new QHBoxLayout();
  titleLayout->addStretch();
  // Always create the reset button, but set it invisible initially
  QPushButton *resetButton = createResetButton();
  connect(resetButton, &QPushButton::clicked, this, &FordSettingsPanel::resetLateralTuning);
  titleLayout->addWidget(resetButton);

  titleLayout->setContentsMargins(0, 0, 0, 20);
  lateralTuningLayout->addLayout(titleLayout);

  // Add lateral tuning controls
  std::vector<std::tuple<QString, QString, QString, std::vector<std::string>, std::vector<std::string>, FordSettings::ControlType, float, float, float, float>> lateralTuningDefs = {
      {"FordLatTuningLaneChgModifier", tr("Lane Change Modifier"), tr("Adjust the lane change curvature agrresivness (lower = slower)."), { "any" }, {}, FordSettings::ControlType::Float, 0.0f, 1.0f, 0.05f, 100.0f},
      {"FordLatTuningCustomPathOffset", tr("In lane offset"), tr("Positions car further left (negative) or right (positive)"), { "any" }, {}, FordSettings::ControlType::Float, -0.5f, 0.5f, 0.05f, 100.0f},
  };

  for (const auto &[param, title, desc, git_remote_allowed, git_branch_allowed, controlType, min, max, increment, division] : lateralTuningDefs) {
    bool ctrlAllowed = isGitRemoteValid(git_remote_allowed, git_branch_allowed);
    if (ctrlAllowed) {
      anyCtrlsAdded = true;
      QWidget *ctrl;
      if (controlType == FordSettings::ControlType::Integer) {
          ctrl = FordSettingsControlFactory::createIntegerControl(
              param, title, desc,
              static_cast<int>(min), static_cast<int>(max),
              static_cast<int>(increment), false);
      } else { // FordSettings::ControlType::Float
          ctrl = FordSettingsControlFactory::createFloatControl(
              param, title, desc,
              min, max, increment, false, "", {}, division);
      }
      ctrl->setObjectName(param);
      lateralTuningLayout->addWidget(ctrl);
      lateralTuningControls.push_back(ctrl);
    }
  }

  if (!anyCtrlsAdded) {
    std::cout << "No lateral tuning toggles added" << std::endl;
    return;
  }
  addItem(lateralTuningGroup);
}

void FordSettingsPanel::addBrakeTuning() {
  bool anyCtrlsAdded = false;
  brakeTuningGroup = createStyledGroupBox(tr("Brake Tuning: (Tap on control for more info)"));
  QVBoxLayout *brakeTuningLayout = new QVBoxLayout(brakeTuningGroup);

  QHBoxLayout *titleLayout = new QHBoxLayout();
  titleLayout->addStretch();
  QPushButton *resetButton = createResetButton();
  connect(resetButton, &QPushButton::clicked, this, &FordSettingsPanel::resetBrakeTuning);
  titleLayout->addWidget(resetButton);

  titleLayout->setContentsMargins(0, 0, 0, 20);
  brakeTuningLayout->addLayout(titleLayout);

  std::vector<std::tuple<QString, QString, QString, std::vector<std::string>, std::vector<std::string>, FordSettings::ControlType, float, float, float, float>> brakeTuningDefs = {
      {"FordLongTuningBrakeActuatorActivate", tr("Brake Actuator Activate"), tr("Acceleration setpoint for which the brake actuator is activated during the braking phase."), { "ford-op/sp-dev-c3"}, {}, FordSettings::ControlType::Float, -1.0f, 0.0f, 0.01f, 100.0f},
      {"FordLongTuningBrakeActuatorReleaseDelta", tr("Brake Actuator Release Delta"), tr("Sets the acceleration gap between the activation and release of the brake actuator during the braking phase."), { "ford-op/sp-dev-c3"}, {}, FordSettings::ControlType::Float, 0.0f, 10.0f, 0.01f, 100.0f},
  };

  for (const auto &[param, title, desc, git_remote_allowed, git_branch_allowed, controlType, min, max, increment, division] : brakeTuningDefs) {
    bool ctrlAllowed = isGitRemoteValid(git_remote_allowed, git_branch_allowed);
    if (ctrlAllowed) {
      anyCtrlsAdded = true;
      QWidget *ctrl;
      if (controlType == FordSettings::ControlType::Integer) {
          ctrl = FordSettingsControlFactory::createIntegerControl(
              param, title, desc,
              static_cast<int>(min), static_cast<int>(max),
              static_cast<int>(increment), false);
      } else { // FordSettings::ControlType::Float
          ctrl = FordSettingsControlFactory::createFloatControl(
              param, title, desc,
              min, max, increment, false, "", {}, division);
      }
      ctrl->setObjectName(param);  // Set the object name to the parameter name
      brakeTuningLayout->addWidget(ctrl);
      brakeTuningControls.push_back(ctrl);
    }
  }
  if (!anyCtrlsAdded) {
    std::cout << "No brake tuning toggles added" << std::endl;
    return;
  }
  addItem(brakeTuningGroup);
}

void FordSettingsPanel::addLimitsTuning() {
  bool anyCtrlsAdded = false;
  limitsTuningGroup = createStyledGroupBox(tr("Limits Tuning: (Tap on control for more info)"));
  QVBoxLayout *limitsTuningLayout = new QVBoxLayout(limitsTuningGroup);

  QHBoxLayout *titleLayout = new QHBoxLayout();
  titleLayout->addStretch();
  QPushButton *resetButton = createResetButton();
  connect(resetButton, &QPushButton::clicked, this, &FordSettingsPanel::resetLimitsTuning);
  titleLayout->addWidget(resetButton);

  titleLayout->setContentsMargins(0, 0, 0, 20);
  limitsTuningLayout->addLayout(titleLayout);

  std::vector<std::tuple<QString, QString, QString, std::vector<std::string>, std::vector<std::string>, FordSettings::ControlType, float, float, float, float>> limitsTuningDefs = {
      {"FordLimitsCurvatureMax", tr("Curvature Max"), tr("Defined the max curvature allowed."), { "ford-op/sp-dev-c3"}, {}, FordSettings::ControlType::Float, 0.0f, 0.2f, 0.005f, 100.0f},
      {"FordLimitsCurvatureError", tr("Curvature Error"), tr("Defined the max curvature error allowed."), { "ford-op/sp-dev-c3"}, {}, FordSettings::ControlType::Float, 0.0f, 0.02f, 0.001f, 1000.0f},
  };

  for (const auto &[param, title, desc, git_remote_allowed, git_branch_allowed, controlType, min, max, increment, division] : limitsTuningDefs) {
    bool ctrlAllowed = isGitRemoteValid(git_remote_allowed, git_branch_allowed);
    if (ctrlAllowed) {
      anyCtrlsAdded = true;
      QWidget *ctrl;
      if (controlType == FordSettings::ControlType::Integer) {
          ctrl = FordSettingsControlFactory::createIntegerControl(
              param, title, desc,
              static_cast<int>(min), static_cast<int>(max),
              static_cast<int>(increment), false);
      } else { // FordSettings::ControlType::Float
          ctrl = FordSettingsControlFactory::createFloatControl(
              param, title, desc,
              min, max, increment, false, "", {}, division);
      }
      ctrl->setObjectName(param);  // Set the object name to the parameter name
      limitsTuningLayout->addWidget(ctrl);
      limitsTuningControls.push_back(ctrl);
    }
  }
  if (!anyCtrlsAdded) {
    std::cout << "No brake tuning toggles added" << std::endl;
    return;
  }
  addItem(limitsTuningGroup);
}

void FordSettingsPanel::addParameterButtons() {
  QGroupBox *groupBox = createStyledGroupBox(tr("Parameter Viewer"));
  QVBoxLayout *layout = new QVBoxLayout(groupBox);

  std::vector<std::tuple<QString, QString>> paramViewerDefs = {
    {"LiveParameters", tr("Live Parameters")},
    {"LiveTorque", tr("Live Torque")},
    {"FingerPrintData", tr("Finger Print Data")},
    {"CalibrationParams", tr("Calibration Parameters")},
    {"CarModel", tr("Car Model")},
    {"CarModelText", tr("Car Model Text")},
    {"CarParamsPersistent", tr("Car Params Persistent")},
    {"ChevronInfo", tr("Chevron Info")},
    {"CustomDrivingModel", tr("Custom Driving Model")},
    {"CustomTorqueLateral", tr("Custom Torque Lateral")},
    {"CustomOffsets", tr("Custom Offsetts")},
    {"DevUIInfo", tr("Dev UI Info")},
    {"FeatureStatus", tr("Feature Status")},
    {"PandaSignatures", tr("Panda Signatures")},
    {"TorqueDeadzoneDeg", tr("Torque Deadzone Deg")},
    {"TorqueFriction", tr("Torque Friction")},
    {"TorqueLateralJerk", tr("Torque Lateral Jerk")},
    {"TorqueMaxLatAccel", tr("Torque Max Lat Accell")}
  };

  for (const auto &paramDef : paramViewerDefs) {
    QString param = std::get<0>(paramDef);
    QString title = std::get<1>(paramDef);
    auto dataBtn = new ButtonControl(title, tr("VIEW"), tr("Display current value of %1").arg(param));

    QObject::connect(dataBtn, &ButtonControl::clicked, [this, param, title]() {
      Params localParams;
      QString rawValue = QString::fromStdString(localParams.get(param.toStdString()));
      QString data = "<b>" + param + ":</b><br>" + rawValue;
      ConfirmationDialog::rich(data, this);
    });

    layout->addWidget(dataBtn);
  }

  addItem(groupBox);
}

void FordSettingsPanel::updateControlWithDefault(QWidget* ctrl) {
  if (!ctrl) return;

  QString paramName = ctrl->objectName();
  FordDefaultParams& defaults = FordDefaultParams::getInstance();
  QString defaultValue = defaults.getDefault(paramName);

  if (!defaultValue.isEmpty()) {
    if (auto* valueControl = qobject_cast<FordSettingsParamValueControl*>(ctrl)) {
      valueControl->setDefaultValue(defaultValue);
    } else if (auto* floatControl = qobject_cast<FordSettingsParamValueControlFloat*>(ctrl)) {
      floatControl->setDefaultValue(defaultValue);
    }
  } else {
    resetControlTitle(ctrl);
  }
}

void FordSettingsPanel::updateResetButtonVisibility(QGroupBox* group) {
  if (!group) return;

  QPushButton* resetButton = group->findChild<QPushButton*>("resetButton");
  if (resetButton) {
    bool hasDefaults = false;
    for (QWidget* ctrl : group->findChildren<QWidget*>()) {
      QString paramName = ctrl->objectName();
      if (!FordDefaultParams::getInstance().getDefault(paramName).isEmpty()) {
        hasDefaults = true;
        break;
      }
    }
    resetButton->setVisible(hasDefaults);
  }
}

void FordSettingsPanel::simulateActivity() {
    // Create a mouse move event at the current cursor position
    QPoint globalPos = QCursor::pos();
    QPoint localPos = this->mapFromGlobal(globalPos);
    QMouseEvent mouseEvent(QEvent::MouseMove, localPos, globalPos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);

    // Send the event to this widget
    QCoreApplication::sendEvent(this, &mouseEvent);
  }

  void FordSettingsPanel::stopActivitySimulation() {
    activityTimer->stop();
  }

  void FordSettingsPanel::resetMaxDurationTimer() {
    // Reset the max duration timer
    QTimer::singleShot(270000, this, &FordSettingsPanel::stopActivitySimulation); // 4 minutes and 30 seconds
  }

void FordSettingsPanel::showEvent(QShowEvent *event) {
  updateToggles();

  // Update reset buttons visibility
  updateResetButtonVisibility(lateralTuningGroup);
  updateResetButtonVisibility(brakeTuningGroup);
  updateResetButtonVisibility(limitsTuningGroup);

  // Start the timer when the panel is shown
  activityTimer->start();
  resetMaxDurationTimer();

  QWidget::showEvent(event);
}

void FordSettingsPanel::hideEvent(QHideEvent *event) {
  // Stop the timer when the panel is hidden
  activityTimer->stop();

  QWidget::hideEvent(event);
}

void FordSettingsPanel::updateToggles() {
  for (auto &[param, toggle] : toggles) {
    toggle->refresh();
  }

  auto updateControls = [this](const std::vector<QWidget*>& controls) {
    for (QWidget* ctrl : controls) {
      if (auto* valueControl = qobject_cast<FordSettingsParamValueControl*>(ctrl)) {
        valueControl->refresh();
        updateControlWithDefault(ctrl);
      } else if (auto* floatControl = qobject_cast<FordSettingsParamValueControlFloat*>(ctrl)) {
        floatControl->refresh();
        updateControlWithDefault(ctrl);
      }
    }
  };

  updateControls(lateralTuningControls);
  updateControls(brakeTuningControls);
  updateControls(limitsTuningControls);
}

bool FordSettingsConfirmationDialog::toggle(const QString &prompt_text, const QString &confirm_text, QWidget *parent) {
  ConfirmationDialog d = ConfirmationDialog(prompt_text, confirm_text, tr("Reboot Later"), false, parent);
  return d.exec();
}

bool FordSettingsConfirmationDialog::toggleAlert(const QString &prompt_text, const QString &button_text, QWidget *parent) {
  ConfirmationDialog d = ConfirmationDialog(prompt_text, button_text, "", false, parent);
  return d.exec();
}

bool FordSettingsConfirmationDialog::yesorno(const QString &prompt_text, QWidget *parent) {
  ConfirmationDialog d = ConfirmationDialog(prompt_text, tr("Yes"), tr("No"), false, parent);
  return d.exec();
}

FordSettingsButtonIconControl::FordSettingsButtonIconControl(const QString &title, const QString &text, const QString &desc, const QString &icon, QWidget *parent) : AbstractControl(title, desc, icon, parent) {
  btn.setText(text);
  btn.setStyleSheet(R"(
    QPushButton {
      padding: 0;
      border-radius: 50px;
      font-size: 35px;
      font-weight: 500;
      color: #E4E4E4;
      background-color: #393939;
    }
    QPushButton:pressed {
      background-color: #4a4a4a;
    }
    QPushButton:disabled {
      color: #33E4E4E4;
    }
  )");
  btn.setFixedSize(250, 100);
  QObject::connect(&btn, &QPushButton::clicked, this, &FordSettingsButtonIconControl::clicked);
  hlayout->addWidget(&btn);
}

FordSettingsParamValueControl* FordSettingsControlFactory::createIntegerControl(
    const QString &param, const QString &title, const QString &desc,
    int minValue, int maxValue, int increment, bool loop,
    const QString &label, const std::map<int, QString> &valueLabels) {
  return new FordSettingsParamValueControl(param, title, desc, "", minValue, maxValue,
                                           valueLabels, nullptr, loop, label, increment);
}

FordSettingsParamValueControlFloat* FordSettingsControlFactory::createFloatControl(
    const QString &param, const QString &title, const QString &desc,
    float minValue, float maxValue, float increment, bool loop,
    const QString &label, const std::map<int, QString> &valueLabels, float division) {
  return new FordSettingsParamValueControlFloat(param, title, desc, "", minValue, maxValue,
                                                valueLabels, nullptr, loop, label, division);
}

bool FordSettingsPanel::showResetConfirmation(const QString& tuningType) {
    QString msg = tr("Are you sure you want to reset %1 to default values?").arg(tuningType);
    auto confirm = new ConfirmationDialog(msg, tr("Yes"), tr("No"), false, this);
    bool ret = confirm->exec();
    delete confirm;
    return ret;
}

bool FordSettingsPanel::isGitRemoteValid(const std::vector<std::string>& searchStrs, const std::vector<std::string>& branchNames) {
  // std::cout << "Entering isGitRemoteValid" << std::endl;;
  std::string gitRemote = params.get("GitRemote");
  std::string gitBranch = params.get("GitBranch");

  // std::cout << "GitRemote: " << gitRemote << std::endl;
  // std::cout << "GitBranch: " << gitBranch << std::endl;

  // Set debugMode to true to allow all controls to be shown if the GitRemote is empty
  bool debugMode = gitRemote.empty();

  if (debugMode || searchStrs.empty() || std::find(searchStrs.begin(), searchStrs.end(), "any") != searchStrs.end()) {
    // std::cout << "Returning true (searchStrs is empty or contains 'any')" << std::endl;
    return true;
  }

  if (gitRemote.empty()) {
    // std::cout << "Returning false (GitRemote is empty)" << std::endl;
    return false;
  }

  bool searchStrFound = false;
  for (const auto& searchStr : searchStrs) {
    if (!searchStr.empty() && gitRemote.find(searchStr) != std::string::npos) {
      searchStrFound = true;
      break;
    }
  }

  if (!searchStrFound) {
    // std::cout << "Returning false (no searchStr found in GitRemote)" << std::endl;
    return false;
  }

  if (!branchNames.empty()) {
    bool branchFound = false;
    for (const auto& branchName : branchNames) {
      if (!branchName.empty() && gitBranch == branchName) {
        branchFound = true;
        break;
      }
    }
    if (!branchFound) {
      // std::cout << "Returning false (no matching branchName)" << std::endl;
      return false;
    }
  }

  // std::cout << "Returning true (all checks passed)" << std::endl;
  return true;
}




void FordSettingsPanel::resetLateralTuning() {
  if (!showResetConfirmation(tr("Lateral Tuning"))) {
    return;
  }
  resetGroupControls(lateralTuningControls);
}

void FordSettingsPanel::resetBrakeTuning() {
  if (!showResetConfirmation(tr("Brake Tuning"))) {
    return;
  }
  resetGroupControls(brakeTuningControls);
}

void FordSettingsPanel::resetLimitsTuning() {
  if (!showResetConfirmation(tr("Limits Tuning"))) {
    return;
  }
  resetGroupControls(limitsTuningControls);
}

void FordSettingsPanel::resetGroupControls(const std::vector<QWidget*>& controls) {
  FordDefaultParams& defaults = FordDefaultParams::getInstance();
  for (QWidget* ctrl : controls) {
    QString paramName = ctrl->objectName();
    QString defaultValue = defaults.getDefault(paramName);
    if (!defaultValue.isEmpty()) {
      params.put(paramName.toStdString(), defaultValue.toStdString());
    }
  }
  updateToggles();
}




void FordSettingsPanel::resetControlTitle(QWidget* control) {
  if (!control) return;
  if (auto* valueControl = qobject_cast<FordSettingsParamValueControl*>(control)) {
    valueControl->setDefaultValue("");
  } else if (auto* floatControl = qobject_cast<FordSettingsParamValueControlFloat*>(control)) {
    floatControl->setDefaultValue("");
  }
}
