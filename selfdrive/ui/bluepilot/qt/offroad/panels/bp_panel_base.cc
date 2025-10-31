// bp_panel_base.cc

#include "bp_panel_base.h"
#include "bp_recent_changes.h"
#include "bp_panel_actions.h"
#include "selfdrive/ui/bluepilot/bp_logging.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/qt/widgets/input.h"
#include "common/watchdog.h"
#include "system/hardware/hw.h"
#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/sunnypilot/ui.h"

BPPanelBase::BPPanelBase(QWidget *parent) : BPPanelListWidget(parent) {
  setMouseTracking(true);
  setSpacing(30);
  setMinimumWidth(1000);
  setMaximumWidth(1920);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

  // Register signal connectors and list generators
  registerSignalConnectors();
  registerListGenerators();

  setStyleSheet(QString(R"(
        * {
            background: none;
        }
        AbstractControl {
            min-width: 0;
            max-width: 1920px;
        }
        QPushButton {
            border-radius: 10px;
            font-size: 40px;
            font-weight: 500;
            color: #E4E4E4;
            background-color: #393939;
            padding: 10px;
        }
        QPushButton:pressed {
            background-color: #4a4a4a;
        }
        QPushButton:disabled {
            background-color: #2a2a2a;
            color: #777777;
        }
        QLabel {
            color: white;
            font-size: 40px;
            min-width: 100px;
            background: none;
        }
        QLabel:disabled {
            color: #777777;
        }
        QFrame {
            padding: 0px;
            margin: 0px;
            background: none;
            border: none;
        }
        QFrame:disabled {
            opacity: 0.5;
        }
        QWidget {
            background: none;
        }
        QWidget:disabled {
            opacity: 0.5;
        }
        QAbstractButton:disabled {
            background-color: #2a2a2a;
        }
    )"));
}

BPPanelBase::~BPPanelBase() {
  // Stop any activity simulation
  ActivitySimulator::getInstance().stopSimulation(this);

  // Additional cleanup of conditions for this panel and its controls
  QList<QWidget *> controls = findChildren<QWidget *>();
  for (QWidget *control : controls) {
    PanelConditions::getInstance().controlConditions.erase(control);
  }
  // Also clean up conditions for the panel itself
  PanelConditions::getInstance().controlConditions.erase(this);
}

void BPPanelBase::updateActivitySimulation() {
  bool shouldSimulate = keepScreenAwake && isVisible();
  // std::cout << "BPPanelBase: Updating activity simulation for '" << objectName().toStdString() << "' - keepScreenAwake=" << keepScreenAwake << ", isVisible=" << isVisible()
  //           << std::endl;

  if (shouldSimulate) {
    // Find the topmost parent that has keepScreenAwake=true
    QWidget *parent = parentWidget();
    BPPanelBase *activeParent = nullptr;

    while (parent) {
      if (auto panelParent = qobject_cast<BPPanelBase *>(parent)) {
        if (panelParent->keepScreenAwake) {
          activeParent = panelParent;
          // std::cout << "BPPanelBase: Found parent with keepScreenAwake: '" << panelParent->objectName().toStdString() << "'" << std::endl;
        }
      }
      parent = parent->parentWidget();
    }

    if (activeParent) {
      // std::cout << "BPPanelBase: Parent panel '" << activeParent->objectName().toStdString() << "' is handling activity simulation" << std::endl;
      return;
    }

    // std::cout << "BPPanelBase: Requesting activity simulation as top-level panel '" << objectName().toStdString() << "'" << std::endl;
    ActivitySimulator::getInstance().requestSimulation(this);
  } else {
    if (ActivitySimulator::getInstance().isSimulatingFor(this)) {
      // std::cout << "BPPanelBase: Stopping activity simulation for '" << objectName().toStdString() << "'" << std::endl;
      ActivitySimulator::getInstance().stopSimulation(this);
    }
  }
}

void BPPanelBase::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  // Invalidate condition cache when panel becomes visible (params may have changed while hidden)
  PanelConditions::getInstance().invalidateCache();

  // Batch all updates into a single pass to reduce redundant iterations
  // Note: refresh() internally calls updateConditionsForAllControls, so we don't need both
  refresh();
  updateActivitySimulation();
}

void BPPanelBase::hideEvent(QHideEvent *event) {
  updateActivitySimulation();
  QWidget::hideEvent(event);
}

bool BPPanelBase::loadConfig(const QString &configPath) {
  if (configPath.isEmpty()) {
    return false;
  }

  ConfigManager &config = ConfigManager::getInstance();
  QString actualConfigPath = FileUtils::getProjectRootPath() + configPath;
  if (!config.loadConfig(actualConfigPath)) {
    BPLog::bpError() << "[bp.panel.base] loadConfig | Failed to load configuration" << std::endl;
    return false;
  }

  configJson = config.getConfig();
  QString panelName = configJson.contains("menuName") && !configJson["menuName"].isNull() ? configJson["menuName"].toString() : "Unnamed Panel";
  setObjectName(panelName);

  QJsonArray groupsArray = configJson["groups"].toArray();
  for (const auto &groupValue : groupsArray) {
    createGroup(groupValue.toObject());
  }

  return true;
}

void BPPanelBase::createGroup(const QJsonObject &group) {
  QString type = group["type"].toString();
  bool hidden = group["hidden"].toBool();
  bool hideDividers = group["hideDividers"].toBool();
  if (hidden)
    return;
  // std::cout << "Group type: " << type.toStdString() << std::endl;
  if (type == "tabPanel") {
    createTabPanel(group);
    return;
  }

  if (group.contains("keepScreenAwake") && group["keepScreenAwake"].toBool()) {
    BPLog::bpDebugGeneral() << "[bp.panel.base] createGroup | Group has keepScreenAwake=true, enabling activity simulation" << std::endl;
    keepScreenAwake = true;
    updateActivitySimulation();
  }

  QString groupName = group["groupName"].toString();
  QString title = group["title"].toString();
  bool enableReset = group["enableResetButton"].toBool();

  QGroupBox *groupBox = createStyledGroupBox(title);
  QVBoxLayout *layout = new QVBoxLayout(groupBox);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(10);

  groupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  GroupData groupData;
  groupData.groupBox = groupBox;

  if (enableReset) {
    QHBoxLayout *titleLayout = new QHBoxLayout();
    titleLayout->addStretch();
    QPushButton *resetButton = createResetButton();
    connect(resetButton, &QPushButton::clicked, this, [this, groupName]() { this->handleGroupReset(groupName); });
    titleLayout->addWidget(resetButton);
    layout->addLayout(titleLayout);
  }

  const QJsonArray &controls = group["controls"].toArray();
  bool hasVisibleControls = false;

  for (int i = 0; i < controls.size(); i++) {
    QWidget *widget = processControlCreation(controls[i].toObject());
    if (widget) {
      hasVisibleControls = true;
      layout->addWidget(widget);
      groupData.controls.push_back(widget);

      // Add divider line after each control except the last one
      if (i < controls.size() - 1 && !hideDividers) {
        QWidget *lineContainer = new QWidget();
        QVBoxLayout *containerLayout = new QVBoxLayout(lineContainer);
        containerLayout->setContentsMargins(5, 5, 5, 5);

        QFrame *line = new QFrame();
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Plain);
        line->setStyleSheet(R"(
                    border: none;
                    background-color: rgba(255, 255, 255, 0.5);
                    min-height: 1px;
                    max-height: 1px;
                )");

        containerLayout->addWidget(line);
        layout->addWidget(lineContainer);
        // Store dividers separately for visibility management
        groupData.dividers.push_back(lineContainer);
      }
    }
  }

  if (hasVisibleControls) {
    groups[groupName] = groupData;
    addItem(groupBox);

    for (QWidget *control : groupData.controls) {
      if (control) {
        control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
      }
    }
  } else {
    // Use deleteLater for widgets to ensure proper cleanup
    groupBox->deleteLater();
  }
}

void BPPanelBase::createTabPanel(const QJsonObject &group) {
  QTabWidget *tabWidget = new QTabWidget(this);

  tabWidget->setStyleSheet(R"(
        QTabWidget::pane {
            border: none;
            background: transparent;
            margin-top: 20px;
        }
        QTabWidget::tab-bar {
            alignment: left;
        }
        QTabBar::tab {
            background: #303030;
            color: #FFFFFF;
            min-width: 200px;
            padding: 15px 30px;
            font-size: 40px;
            font-weight: 500;
            margin-right: 5px;
            border: none;
            border-radius: 10px;
        }
        QTabBar::tab:hover {
            background: #404040;
        }
        QTabBar::tab:selected {
            background: #2196F3;
            color: white;
        }
        QTabBar::tab:disabled {
            background: #202020;
            color: #666666;
        }
    )");

  tabWidget->setTabPosition(QTabWidget::TabPosition::North);
  tabWidget->setTabShape(QTabWidget::TabShape::Rounded);
  tabWidget->tabBar()->setExpanding(false);
  tabWidget->tabBar()->setDocumentMode(true);
  tabWidget->tabBar()->setUsesScrollButtons(false);

  QWidget *container = new QWidget();
  QVBoxLayout *containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(0, 20, 0, 0);
  containerLayout->addWidget(tabWidget);

  const QJsonArray &tabs = group["tabs"].toArray();
  for (const auto &tabRef : tabs) {
    QJsonObject tab = tabRef.toObject();
    QString name = tab["name"].toString();
    QWidget *content = createTabContent(tab["groups"].toArray());
    tabWidget->addTab(content, name);
  }

  addItem(container);
}

QWidget *BPPanelBase::createTabContent(const QJsonArray &tabGroups) {
  QWidget *content = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(content);
  layout->setSpacing(25);

  for (const auto &groupRef : tabGroups) {
    QJsonObject groupObj = groupRef.toObject();
    QGroupBox *group = createStyledGroupBox(groupObj["title"].toString());

    QVBoxLayout *groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(10, 10, 10, 10);
    groupLayout->setSpacing(10);

    const QJsonArray &controls = groupObj["controls"].toArray();
    bool hasVisibleControls = false;
    for (const auto &controlRef : controls) {
      if (QWidget *control = processControlCreation(controlRef.toObject())) {
        groupLayout->addWidget(control);
        hasVisibleControls = true;
      }
    }

    // For single-control groups, prevent vertical expansion by setting Minimum size policy
    if (hasVisibleControls) {
      if (controls.size() == 1) {
        group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
      }
      layout->addWidget(group);
    } else {
      group->deleteLater();
    }
  }

  layout->addStretch();
  return content;
}

QGroupBox *BPPanelBase::createStyledGroupBox(const QString &title) {
  QGroupBox *groupBox = new QGroupBox(title);
  groupBox->setStyleSheet(R"(
        QGroupBox {
            background-color: #242424;
            border: none;
            border-radius: 15px;
            margin-top: 50px;
            padding: 5px;
            font-size: 40px;
            font-weight: 500;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 5px 15px;
            border-top-left-radius: 15px;
            border-top-right-radius: 15px;
            border-bottom: none;
            margin-left: 35px;
            margin-top: 0px;
            background-color: #242424;
            color: #2196F3;
        }
        QGroupBox > QWidget {
            background-color: transparent;
        }
        QGroupBox::indicator {
            width: 0px;
        }
    )");

  groupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  return groupBox;
}

QPushButton *BPPanelBase::createResetButton() {
  QPushButton *resetButton = new QPushButton();
  resetButton->setObjectName("resetButton");
  resetButton->setText(tr(" Reset"));

  QIcon icon = QIcon("../assets/offroad/icon_reset.png");
  icon.addPixmap(QPixmap("../assets/offroad/icon_reset.png"), QIcon::Normal, QIcon::On);
  icon.addPixmap(QPixmap("../assets/offroad/icon_reset.png"), QIcon::Disabled);

  resetButton->setIcon(icon);
  resetButton->setStyleSheet(R"(
        QPushButton {
            background-color: #303030;
            border: none;
            border-radius: 30px;
            color: #E4E4E4;
            font-size: 40px;
            font-weight: 500;
            padding: 0px 25px;
            text-align: center;
        }
        QPushButton:hover {
            background-color: #404040;
        }
        QPushButton:pressed {
            background-color: #505050;
        }
        QPushButton:disabled {
            background-color: #202020;
            color: #666666;
        }
    )");

  resetButton->setIconSize(QSize(40, 40));
  resetButton->setCursor(Qt::PointingHandCursor);
  resetButton->setLayoutDirection(Qt::LeftToRight);
  resetButton->setFixedSize(200, 60);

  QGraphicsDropShadowEffect *shadowEffect = new QGraphicsDropShadowEffect;
  shadowEffect->setBlurRadius(15);
  shadowEffect->setColor(QColor(0, 0, 0, 80));
  shadowEffect->setOffset(0, 2);
  resetButton->setGraphicsEffect(shadowEffect);

  return resetButton;
}

QWidget *BPPanelBase::processControlCreation(const QJsonObject &control) {
  if (!validateControlBasics(control))
    return nullptr;
  if (control["hidden"].toBool()) {
    BPLog::bpDebugGeneral() << "[bp.panel.base] processControlCreation | " << control["param"].toString().toStdString() << " Control is hidden" << std::endl;
    return nullptr;
  }

  QString type = control["type"].toString();
  QWidget *widget = nullptr;

  if (type == "toggle") {
    widget = createToggleControl(control);
  } else if (type == "param_toggle_button") {
    widget = createParamToggleButton(control);
  } else if (type == "segmented_control") {
    widget = createSegmentedControl(control);
  } else if (type == "float") {
    widget = createNumericControl(control, true);
  } else if (type == "integer") {
    widget = createNumericControl(control, false);
  } else if (type == "selection") {
    widget = createSelectionControl(control);
  } else if (type == "param_viewer") {
    widget = createParamViewerControl(control);
  } else if (type == "param_list_viewer") {
    widget = createParamListViewerControl(control);
  } else if (type == "static_param_display") {
    widget = createStaticParamDisplayControl(control);
  } else if (type == "file_param_display") {
    widget = createFileParamDisplayControl(control);
  } else if (type == "text_input") {
    widget = createTextInputControl(control);
  } else if (type == "html_viewer") {
    widget = createHtmlViewerControl(control);
  } else if (type == "file_viewer") {
    widget = createFileViewerControl(control);
  } else if (type == "recent_changes") {
    widget = createRecentChangesControl(control);
  } else if (type == "command_button") {
    widget = createCommandButtonControl(control);
  } else if (type == "nested_controls_button") {
    widget = createNestedControlsButton(control);
  } else if (type == "restart_ui") {
    widget = createRestartUIControl(control);
  } else if (type == "static_text") {
    widget = createStaticTextControl(control);
  } else if (type == "platform_display") {
    widget = createPlatformDisplayControl(control);
  } else {
    BPLog::bpError() << "[bp.panel.base] processControlCreation | Unsupported control type: " << type.toStdString() << " | Param:" << control["param"].toString().toStdString() << std::endl;
    return nullptr;
  }

  // Parse all condition types (legacy conditions, enableConditions, visibleConditions)
  if (widget && (control.contains("conditions") || control.contains("enableConditions") || control.contains("visibleConditions"))) {
    ControlConditions conditions;

    // Legacy "conditions" - controls both enabled state (backward compatibility)
    if (control.contains("conditions")) {
      conditions.conditions = control["conditions"].toObject();
      conditions.hasConditions = true;
    }

    // New granular control: enableConditions (controls whether widget is enabled/disabled)
    if (control.contains("enableConditions")) {
      conditions.enableConditions = control["enableConditions"].toObject();
      conditions.hasEnableConditions = true;
    }

    // New granular control: visibleConditions (controls whether widget is shown/hidden)
    if (control.contains("visibleConditions")) {
      conditions.visibleConditions = control["visibleConditions"].toObject();
      conditions.hasVisibleConditions = true;
    }

    // Store auto-reset info if present
    if (control.contains("auto_reset_value")) {
      conditions.autoResetValue = control["auto_reset_value"].toString();
      conditions.hasAutoReset = true;
      conditions.paramName = control["param"].toString();
    }

    // Parse dynamic descriptions if present
    if (control.contains("descriptions") && control.contains("description_conditions")) {
      conditions.hasDynamicDescriptions = true;
      conditions.defaultDescription = control["desc"].toString();

      QJsonObject descriptions = control["descriptions"].toObject();
      QJsonObject descConditions = control["description_conditions"].toObject();

      // Build the description map and condition map
      for (auto it = descriptions.begin(); it != descriptions.end(); ++it) {
        QString key = it.key();
        QString desc = it.value().toString();
        conditions.descriptions[key] = desc;

        // Get the corresponding condition
        if (descConditions.contains(key)) {
          conditions.descConditions[key] = descConditions[key].toObject();
        }
      }
    }

    // Parse parameter substitutions if present
    if (control.contains("param_substitutions")) {
      conditions.hasParamSubstitutions = true;
      conditions.paramSubstitutions = control["param_substitutions"].toObject();
    }

    PanelConditions::getInstance().controlConditions[widget] = conditions;
    widget->setEnabled(true);
    widget->update();
  }

  return widget;
}

QWidget *BPPanelBase::createParamToggleButton(const QJsonObject &control) {
  QString param = control["param"].toString();
  QString title = control["title"].toString();
  QString desc = control["desc"].toString();
  QString buttonText = control["button_text"].toString();

  auto paramToggleBtn = new BPParamToggleButton(param, title, desc, buttonText);
  paramToggleBtn->setObjectName(param);

  // Handle dynamic button text
  if (control.value("dynamic_button_text").toBool(false) && control.contains("button_texts")) {
    QJsonObject buttonTexts = control["button_texts"].toObject();
    QString enabledText = buttonTexts["enabled"].toString();
    QString disabledText = buttonTexts["disabled"].toString();
    if (!enabledText.isEmpty() && !disabledText.isEmpty()) {
      paramToggleBtn->enableDynamicButtonText(enabledText, disabledText);
    }
  }

  // Handle dynamic styling
  if (control.value("dynamic_styling").toBool(false) && control.contains("styles")) {
    QJsonObject styles = control["styles"].toObject();
    QJsonObject enabledStyle = styles["enabled"].toObject();
    QJsonObject disabledStyle = styles["disabled"].toObject();

    QString bgColorEnabled = enabledStyle["background_color"].toString();
    QString bgColorDisabled = disabledStyle["background_color"].toString();
    QString bgColorEnabledPressed = enabledStyle["background_color_pressed"].toString();
    QString bgColorDisabledPressed = disabledStyle["background_color_pressed"].toString();

    QString textColor = "#FFFFFF";
    if (enabledStyle.contains("text_color")) {
      textColor = enabledStyle["text_color"].toString();
    } else if (disabledStyle.contains("text_color")) {
      textColor = disabledStyle["text_color"].toString();
    }

    if (!bgColorEnabled.isEmpty() && !bgColorDisabled.isEmpty()) {
      paramToggleBtn->enableDynamicStyling(bgColorEnabled, bgColorDisabled, bgColorEnabledPressed, bgColorDisabledPressed, textColor);
    }
  }

  // Handle confirmation texts
  if (control.value("confirm").toBool(false)) {
    QString confirmOn = control["confirm_text_on"].toString();
    QString confirmOff = control["confirm_text_off"].toString();
    QString confirmYes = control.contains("confirm_yes_text") ? control["confirm_yes_text"].toString() : "Confirm";
    QString confirmNo = control.contains("confirm_no_text") ? control["confirm_no_text"].toString() : "Cancel";

    if (!confirmOn.isEmpty() && !confirmOff.isEmpty()) {
      paramToggleBtn->setConfirmationTexts(confirmOn, confirmOff, confirmYes, confirmNo);
    }
  }

  connect(paramToggleBtn, &BPParamToggleButton::valueChanged, this, &BPPanelBase::onControlValueChanged);

  return paramToggleBtn;
}

QWidget *BPPanelBase::createToggleControl(const QJsonObject &control) {
  QString param = control["param"].toString();
  QString title = control["title"].toString();
  QString desc = control["desc"].toString();
  bool needsRestart = control.value("needs_restart").toBool(false);

  // Append restart warning to description if needed
  QString finalDesc = desc;
  if (needsRestart && !params.getBool((param.toStdString() + "Lock"))) {
    finalDesc += tr(" Changing this setting will restart openpilot if the car is powered on.");
  }

  auto toggle = new BPToggleControl(param, title, finalDesc);
  toggle->setObjectName(param);
  toggles[param.toStdString()] = toggle;

  // Handle dynamic title
  if (control.value("dynamic_title").toBool(false) && control.contains("titles")) {
    QJsonObject titles = control["titles"].toObject();
    QString enabledTitle = titles["enabled"].toString();
    QString disabledTitle = titles["disabled"].toString();
    if (!enabledTitle.isEmpty() && !disabledTitle.isEmpty()) {
      toggle->enableDynamicTitle(enabledTitle, disabledTitle);
    }
  }

  // Handle dynamic styling
  if (control.value("dynamic_styling").toBool(false) && control.contains("styles")) {
    QJsonObject styles = control["styles"].toObject();
    QJsonObject enabledStyle = styles["enabled"].toObject();
    QJsonObject disabledStyle = styles["disabled"].toObject();

    QString bgColorEnabled = enabledStyle["background_color"].toString();
    QString bgColorDisabled = disabledStyle["background_color"].toString();
    QString bgColorEnabledPressed = enabledStyle["background_color_pressed"].toString();
    QString bgColorDisabledPressed = disabledStyle["background_color_pressed"].toString();

    // Get text color, prefer from enabled style, fallback to disabled, then default
    QString textColor = "#FFFFFF";
    if (enabledStyle.contains("text_color")) {
      textColor = enabledStyle["text_color"].toString();
    } else if (disabledStyle.contains("text_color")) {
      textColor = disabledStyle["text_color"].toString();
    }

    if (!bgColorEnabled.isEmpty() && !bgColorDisabled.isEmpty()) {
      toggle->enableDynamicStyling(bgColorEnabled, bgColorDisabled, bgColorEnabledPressed, bgColorDisabledPressed, textColor);
    }
  }

  // Handle mutual exclusion
  QJsonArray mutuallyExclusive = control["mutually_exclusive"].toArray();

  // Handle confirmation
  bool requireConfirm = control.value("confirm").toBool(false);
  bool useDynamicConfirmText = control.value("use_dynamic_confirm_text").toBool(false);
  QString staticConfirmText = control.contains("confirm_text") ? control["confirm_text"].toString() : "";
  QString confirmYes = control.contains("confirm_yes_text") ? control["confirm_yes_text"].toString() : "Enable";
  QString confirmNo = control.contains("confirm_no_text") ? control["confirm_no_text"].toString() : "Cancel";

  connect(toggle, &BPToggleControl::toggleFlipped, this, [this, param, mutuallyExclusive, needsRestart, requireConfirm, useDynamicConfirmText, staticConfirmText, confirmYes, confirmNo, toggle](bool state) {
    // If confirmation required and toggling ON, show dialog
    if (requireConfirm && state) {
      // Revert the toggle and param immediately since we need confirmation
      params.putBool(param.toStdString(), false);
      toggle->blockSignals(true);
      toggle->refresh();  // This will reset it to the current param value (now off)
      toggle->blockSignals(false);

      // Get the confirmation text - use current description if dynamic, otherwise use static confirm_text
      QString confirmText;
      if (useDynamicConfirmText) {
        confirmText = toggle->getDescription();  // Use the currently displayed dynamic description
      } else if (!staticConfirmText.isEmpty()) {
        confirmText = staticConfirmText;
      } else {
        confirmText = toggle->getDescription();  // Fallback to current description
      }

      BPConfirmationDialog::ConfirmConfig config;
      config.title = tr("Confirmation Required");
      config.prompt = confirmText;
      config.confirmText = confirmYes;
      config.cancelText = confirmNo;

      auto *dialog = BPConfirmationDialog::showConfirmation(config, this);
      connect(dialog, &BPConfirmationDialog::confirmed, this, [=](bool accepted) {
        if (accepted) {
          // User confirmed, now actually toggle it on
          params.putBool(param.toStdString(), true);
          toggle->refresh();

          // Handle mutual exclusion
          if (!mutuallyExclusive.isEmpty()) {
            for (const auto &excludedParam : mutuallyExclusive) {
              QString excludedParamName = excludedParam.toString();
              params.putBool(excludedParamName.toStdString(), false);
              BPLog::bpInfo() << "[bp.panel.base] createToggleControl | Mutually exclusive param disabled - " << excludedParamName.toStdString() << std::endl;

              // Refresh the excluded toggle if it exists
              if (toggles.find(excludedParamName.toStdString()) != toggles.end()) {
                toggles[excludedParamName.toStdString()]->refresh();
              }
            }
          }

          // Request onroad cycle if needs_restart
          if (needsRestart) {
            params.putBool("OnroadCycleRequested", true);
          }

          onControlValueChanged();
          BPLog::bpInfo() << "[bp.panel.base] createToggleControl | Parameter confirmed and enabled - " << param.toStdString() << std::endl;
        } else {
          BPLog::bpInfo() << "[bp.panel.base] createToggleControl | Parameter change cancelled - " << param.toStdString() << std::endl;
        }
      });
    } else {
      // No confirmation needed or toggling off
      std::string currentValue = params.get(param.toStdString());
      BPLog::bpInfo() << "[bp.panel.base] createToggleControl | Parameter changed - " << param.toStdString() << ": " << (currentValue == "1" ? "On" : "Off") << " -> " << (state ? "On" : "Off") << std::endl;

      // If toggled on, disable mutually exclusive params
      if (state && !mutuallyExclusive.isEmpty()) {
        for (const auto &excludedParam : mutuallyExclusive) {
          QString excludedParamName = excludedParam.toString();
          params.putBool(excludedParamName.toStdString(), false);
          BPLog::bpInfo() << "[bp.panel.base] createToggleControl | Mutually exclusive param disabled - " << excludedParamName.toStdString() << std::endl;

          // Refresh the excluded toggle if it exists
          if (toggles.find(excludedParamName.toStdString()) != toggles.end()) {
            toggles[excludedParamName.toStdString()]->refresh();
          }
        }
      }

      // Request onroad cycle if needs_restart
      if (needsRestart) {
        params.putBool("OnroadCycleRequested", true);
      }

      onControlValueChanged();
    }
  });

  // Disable when engaged if needs_restart
  if (needsRestart) {
    QObject::connect(uiState(), &UIState::engagedChanged, toggle, [toggle](bool engaged) {
      // Invalidate condition cache on engagement state change
      PanelConditions::getInstance().invalidateCache();

      if (engaged) {
        toggle->setEnabled(false);
      } else {
        // Re-enable when not engaged (conditions will be re-evaluated)
        toggle->setEnabled(true);
      }
    });
  }

  return toggle;
}

QWidget *BPPanelBase::createSegmentedControl(const QJsonObject &control) {
  QJsonArray options = control["options"].toArray();
  QVector<QPair<QString, QString>> optionPairs;
  QVector<QString> descList;
  QVector<QJsonObject> optionConditions;
  QString defaultValue;

  for (const auto &opt : options) {
    QJsonObject option = opt.toObject();
    optionPairs.append({option["name"].toString(), option["value"].toString()});
    if (option.contains("default") && option["default"].toBool()) {
      defaultValue = option["value"].toString();
    }
    // Collect descriptions for bullet point list if present
    if (option.contains("desc")) {
      descList.append(option["desc"].toString());
    }
    // Collect conditions for each option if present
    if (option.contains("conditions")) {
      optionConditions.append(option["conditions"].toObject());
    } else {
      optionConditions.append(QJsonObject()); // Empty conditions = always enabled
    }
  }

  // Check if showDescBottom is specified in JSON
  bool showDescBottom = control.contains("showDescBottom") && control["showDescBottom"].toBool();

  auto segmented = new BPSegmentedControl(control["param"].toString(), control["title"].toString(), control["desc"].toString(), optionPairs, defaultValue, nullptr, descList, showDescBottom);
  segmented->setObjectName(control["param"].toString());
  connect(segmented, &BPSegmentedControl::valueChanged, this, &BPPanelBase::onControlValueChanged);

  // Special handling for BPUiTextSize parameter - prompt for UI restart
  QString paramName = control["param"].toString();
  if (paramName == "BPUiTextSize") {
    connect(segmented, &BPSegmentedControl::valueChanged, this, [this]() {
      BPConfirmationDialog::ConfirmConfig config;
      config.title = tr("Restart UI");
      config.prompt = tr("UI text size has been changed.\n\nRestart the UI now to apply the new text size?");
      config.confirmText = tr("Restart Now");
      config.cancelText = tr("Later");

      auto *dialog = BPConfirmationDialog::showConfirmation(config, this);
      connect(dialog, &BPConfirmationDialog::confirmed, this, [=](bool accepted) {
        if (accepted) {
          BPLog::bpInfo() << "[bp.panel.base] Restarting UI due to text size change..." << std::endl;
          qApp->exit(18); // Exit code 18 triggers UI restart
        }
      });
    });
  }

  // Set up per-option conditional visibility
  if (!optionConditions.isEmpty()) {
    // Store option conditions for this control
    auto updateButtonStates = [segmented, optionConditions]() {
      // Invalidate cache when conditions need to be re-evaluated
      PanelConditions::getInstance().invalidateCache();

      QVector<bool> enabledStates;
      auto &panelConditions = PanelConditions::getInstance();
      for (const auto &optionCond : optionConditions) {
        bool enabled = true;
        if (!optionCond.isEmpty()) {
          enabled = panelConditions.validateConditionObject(optionCond);
        }
        enabledStates.append(enabled);
      }
      segmented->updateButtonStates(enabledStates);
    };

    // Initial update
    updateButtonStates();

    // Connect to condition change signals
    connect(uiState(), &UIState::offroadTransition, segmented, updateButtonStates);
    connect(uiState(), &UIState::engagedChanged, segmented, updateButtonStates);

    // Also update when CarParams change
    QTimer *timer = new QTimer(segmented);
    connect(timer, &QTimer::timeout, updateButtonStates);
    timer->start(5000); // Check every 5 seconds for CarParams changes
  }

  return segmented;
}

QWidget *BPPanelBase::createNumericControl(const QJsonObject &control, bool isFloat) {
  QString param = control["param"].toString();
  QString title = control["title"].toString();
  QString desc = control["desc"].toString();
  double min = control["min"].toDouble();
  double max = control["max"].toDouble();
  double increment = control["increment"].toDouble();
  BPNumericControl *numeric = nullptr;

  if (isFloat) {
    double division = control["division"].toDouble();
    numeric = new BPNumericControl(param, title, desc, min, max, increment, true, division);
  } else {
    numeric = new BPNumericControl(param, title, desc, min, max, increment, false);
  }

  numeric->setObjectName(param);
  connect(numeric, &BPNumericControl::valueChanged, this, &BPPanelBase::onControlValueChanged);
  return numeric;
}

QWidget *BPPanelBase::createSelectionControl(const QJsonObject &control) {
  bool hideDesc = control.contains("hideDesc") && control["hideDesc"].toBool();
  auto selectionControl = new BPSelectionControl(control["param"].toString(), control["title"].toString(), control["desc"].toString(), nullptr, hideDesc);
  selectionControl->setObjectName(control["param"].toString());

  QVector<BPSelectionDialog::Option> options;
  QVector<QPair<QString, QString>> optionPairs; // For the selection control mapping

  // Check for unit/unitMetric for dynamic replacement
  QString unitToUse;
  if (control.contains("unit") && control.contains("unitMetric")) {
    bool isMetric = params.getBool("IsMetric");
    unitToUse = isMetric ? control["unitMetric"].toString() : control["unit"].toString();
  }

  // Check if there's a list generator function
  QJsonArray optArray;
  if (control.contains("list_generator")) {
    QString generatorName = control["list_generator"].toString();
    auto it = listGenerators.find(generatorName);
    if (it != listGenerators.end()) {
      QMap<QString, QString> generatedList = it->second();
      for (auto mapIt = generatedList.begin(); mapIt != generatedList.end(); ++mapIt) {
        QString displayName = mapIt.key();
        // Replace {unit} placeholder with appropriate unit
        if (!unitToUse.isEmpty()) {
          displayName.replace("{unit}", unitToUse);
        }
        options.append({displayName, mapIt.value()});
        optionPairs.append({displayName, mapIt.value()});
      }
      BPLog::bpInfo() << "[bp.panel.base] createSelectionControl | Using list generator: " << generatorName.toStdString() << std::endl;
    } else {
      BPLog::bpWarn() << "[bp.panel.base] createSelectionControl | List generator not found: " << generatorName.toStdString() << std::endl;
    }
  } else {
    // Use static options from JSON
    optArray = control["options"].toArray();
    for (const auto &opt : optArray) {
      QJsonObject optObj = opt.toObject();
      QString displayName = optObj["name"].toString();
      QString value = optObj["value"].toString();
      // Replace {unit} placeholder with appropriate unit
      if (!unitToUse.isEmpty()) {
        displayName.replace("{unit}", unitToUse);
      }
      options.append({displayName, value});
      optionPairs.append({displayName, value}); // display name -> value
    }
  }

  // Set the options for value-to-display mapping
  selectionControl->setOptions(optionPairs);

  // Initialize parameter with default value if it doesn't exist
  QString paramName = control["param"].toString();
  std::string paramNameStd = paramName.toStdString();
  std::string currentValue = params.get(paramNameStd);

  if (currentValue.empty() && !optArray.isEmpty()) {
    // Find the default option and set it
    for (const auto &opt : optArray) {
      QJsonObject optObj = opt.toObject();
      if (optObj.contains("default") && optObj["default"].toBool()) {
        QString defaultValue = optObj["value"].toString();
        params.put(paramNameStd, defaultValue.toStdString());
        currentValue = defaultValue.toStdString();
        BPLog::bpInfo() << "[bp.panel.base] createSelectionControl | Parameter initialized with default - " << paramNameStd << ": " << defaultValue.toStdString() << std::endl;
        break;
      }
    }
  }

  selectionControl->setSelectedValue(QString::fromStdString(currentValue));

  connect(selectionControl, &BPSelectionControl::clicked, [=]() {
    QString currentValue = QString::fromStdString(params.get(control["param"].toString().toStdString()));
    QString newValue = BPSelectionDialog::getValue(control["title"].toString(), options, currentValue, this);

    if (!newValue.isEmpty() && newValue != currentValue) {
      BPLog::bpInfo() << "[bp.panel.base] createSelectionControl | Selection Control - New value: " << newValue.toStdString() << std::endl;
      params.put(control["param"].toString().toStdString(), newValue.toStdString());
      selectionControl->setSelectedValue(newValue);
      onControlValueChanged();

      // Check if reboot is required for this parameter
      bool requiresReboot = control.contains("requiresReboot") && control["requiresReboot"].toBool();

      if (requiresReboot) {
        // Show confirmation dialog for reboot if necessary.
        BPConfirmationDialog::ConfirmConfig config;
        config.title = tr("Device Reboot Required");
        config.prompt = tr("Reboot required for changes to take effect. Would you like to reboot now?");
        config.confirmText = tr("Reboot");
        config.cancelText = tr("Cancel");
        auto *dialog = BPConfirmationDialog::showConfirmation(config, this);
        connect(dialog, &BPConfirmationDialog::confirmed, this, [this](bool accepted) {
          if (accepted) {
            params.putBool("DoReboot", true);
          }
        });
      }
    }
  });
  return selectionControl;
}

QWidget *BPPanelBase::createParamViewerControl(const QJsonObject &control) {
  auto paramViewer = new BPParamViewerControl(control["param"].toString(), control["title"].toString(), control["desc"].toString());

  connect(paramViewer, &BPParamViewerControl::viewClicked, [=]() {
    auto dialog = new BPParamViewerDialog(this);
    dialog->setupParamViewer(control["title"].toString(), control["param"].toString());
    dialog->setupFullscreen();
  });

  return paramViewer;
}

QWidget *BPPanelBase::createParamListViewerControl(const QJsonObject &control) {
  auto listViewer = new BPParamListViewerControl(control["title"].toString(), control["desc"].toString());

  connect(listViewer, &BPParamListViewerControl::viewClicked, [=]() {
    auto listDialog = new BPParamListDialog(this);
    connect(listDialog, &BPParamListDialog::paramViewRequested, [=](const QString &param) {
      auto paramDialog = new BPParamViewerDialog(this);
      paramDialog->setupParamViewer(tr("Parameter Value"), param);
      paramDialog->setupFullscreen();
    });
    listDialog->setupParamList();
    listDialog->setupFullscreen();
  });

  return listViewer;
}

QWidget *BPPanelBase::createStaticParamDisplayControl(const QJsonObject &control) {
  QString valueProcessor = control.value("value_processor").toString("");
  auto staticDisplay = new BPStaticParamDisplay(control["param"].toString(), control["title"].toString(), control["desc"].toString(), valueProcessor);
  staticDisplay->setObjectName(control["param"].toString());
  return staticDisplay;
}

QWidget *BPPanelBase::createFileParamDisplayControl(const QJsonObject &control) {
  QString fileName = control["file"].toString();
  QString title = control["title"].toString();
  QString desc = control["desc"].toString();
  QString prefix = control.value("prefix").toString("");
  QString suffix = control.value("suffix").toString("");

  auto fileDisplay = new BPFileParamDisplay(fileName, title, desc, prefix, suffix);
  fileDisplay->setObjectName(fileName);
  return fileDisplay;
}

QWidget *BPPanelBase::createTextInputControl(const QJsonObject &control) {
  QString param = control["param"].toString();
  QString title = control["title"].toString();
  QString desc = control["desc"].toString();
  QString buttonText = control.value("button_text").toString("ADD");
  QString placeholder = control.value("placeholder").toString("");

  auto textCtrl = new BPTextInputControl(param, title, desc, buttonText, placeholder);

  connect(textCtrl, &BPTextInputControl::showTextInputDialog, this, [=](const QString &paramName, const QString &dialogTitle, const QString &placeholderText) {
    QString currentValue = QString::fromStdString(params.get(paramName.toStdString()));
    QString newText = InputDialog::getText(dialogTitle, this, placeholderText, false, -1, currentValue);
    if (!newText.isEmpty()) {
      params.put(paramName.toStdString(), newText.toStdString());
      textCtrl->refresh();
      emit controlValueChanged();
    }
  });

  connect(textCtrl, &BPTextInputControl::textRemoved, this, [=]() {
    emit controlValueChanged();
  });

  return textCtrl;
}

QWidget *BPPanelBase::createHtmlViewerControl(const QJsonObject &control) {
  QString title = control["title"].toString();
  QString desc = control["desc"].toString();
  QString path = control["path"].toString();
  QString header = control.value("header").toString(title);

  auto htmlCtrl = new BPHtmlViewerControl(title, desc, path, header);

  connect(htmlCtrl, &BPHtmlViewerControl::htmlViewRequested, this, [=](const QString &htmlPath, const QString &hdr) {
    QString rootPath = FileUtils::getProjectRootPath();
    QString fullPath = QDir(rootPath).filePath(htmlPath);
    QString htmlContent = QString::fromStdString(util::read_file(fullPath.toStdString()));
    ConfirmationDialog::rich(htmlContent, this);
  });

  return htmlCtrl;
}

QWidget *BPPanelBase::createFileViewerControl(const QJsonObject &control) {
  QString title = control["title"].toString();
  QString desc = control["desc"].toString();
  QString path = control["path"].toString();
  QString header = control["header"].toString();

  auto fileCtrl = new BPFileViewerControl(title, desc, path, header);

  connect(fileCtrl, &BPFileViewerControl::fileViewRequested, this, [=](const QString &relPath, const QString &hdr, const QString &fallbackTitle) {
    QString rootPath = FileUtils::getProjectRootPath();
    QString fullPath = QDir(rootPath).filePath(relPath);
    auto dlg = new BPFileViewerDialog(this);
    dlg->loadFileAndShow(fullPath, hdr, fallbackTitle);
  });

  return fileCtrl;
}

QWidget *BPPanelBase::createRecentChangesControl(const QJsonObject &control) {
  QString title = control["title"].toString();
  QString desc = control["desc"].toString();

  auto recentChangesCtrl = new BPRecentChangesControl(title, desc);

  connect(recentChangesCtrl, &BPRecentChangesControl::recentChangesRequested, this, [=]() {
    auto dlg = new BPRecentChangesDialog(this);
    dlg->loadAndDisplayChanges(BPRecentChangesDialog::getCurrentVersion());
    dlg->setupFullscreen();
  });

  return recentChangesCtrl;
}

QWidget *BPPanelBase::createCommandButtonControl(const QJsonObject &control) {
  QString command = control["command"].toString();
  QString action = control["action"].toString();
  QString workingDir = control["working_dir"].toString();
  QString buttonText = control["button_text"].toString();
  QString confirmText = control["confirm_text"].toString();
  QString confirmYesText = control["confirm_yes_text"].toString();
  QString confirmNoText = control["confirm_no_text"].toString();
  bool requireConfirm = control["confirm"].toBool();
  int timeoutMs = control["command_timeout_ms"].toInt(120000); // Default 2 minutes
  bool showRetry = control["showRetry"].toBool(true); // Default to true
  QJsonArray actionButtons;

  if (control.contains("actionButtons")) {
    actionButtons = control["actionButtons"].toArray();
  }

  if (buttonText.isEmpty()) {
    buttonText = tr("EXECUTE");
  }
  if (requireConfirm) {
    if (confirmText.isEmpty()) {
      confirmText = tr("Are you sure you want to execute this command?");
    }
    if (confirmYesText.isEmpty()) {
      confirmYesText = tr("Yes");
    }
    if (confirmNoText.isEmpty()) {
      confirmNoText = tr("No");
    }
  }

  auto cmdCtrl = new BPCommandControl(control["title"].toString(), control["desc"].toString(), buttonText, command, action, control, workingDir, requireConfirm, confirmText, confirmYesText,
                                      confirmNoText, actionButtons);

  // Apply custom button styling if provided
  if (control.contains("button_style")) {
    QJsonObject buttonStyle = control["button_style"].toObject();
    QString bgColor = buttonStyle["background_color"].toString();
    QString bgColorPressed = buttonStyle["background_color_pressed"].toString();
    QString textColor = buttonStyle["text_color"].toString();

    if (!bgColor.isEmpty() && !bgColorPressed.isEmpty() && !textColor.isEmpty()) {
      cmdCtrl->setButtonStyle(bgColor, bgColorPressed, textColor);
    }
  }

  // Connect command handler (for shell commands)
  connect(cmdCtrl, &BPCommandControl::commandRequested, this,
          [this, timeoutMs, showRetry](const QString &cmd, const QString &dialogTitle, const QString &dir, const QJsonArray &buttons, bool confirmRequired, const QString &confText,
                 const QString &yesText, const QString &noText) {
            if (confirmRequired) {
              BPConfirmationDialog::ConfirmConfig config;
              config.title = tr("Confirmation Required");
              config.prompt = confText.isEmpty() ? tr("Are you sure?") : confText;
              config.confirmText = yesText.isEmpty() ? tr("Yes") : yesText;
              config.cancelText = noText.isEmpty() ? tr("No") : noText;

              auto *dialog = BPConfirmationDialog::showConfirmation(config, this);
              connect(dialog, &BPConfirmationDialog::confirmed, this, [=](bool accepted) {
                if (accepted) {
                  BPCommandDialog *commandDialog = new BPCommandDialog(this);
                  commandDialog->executeCommand(cmd, dialogTitle, dir, buttons, timeoutMs, showRetry);
                }
              });
            } else {
              BPCommandDialog *commandDialog = new BPCommandDialog(this);
              commandDialog->executeCommand(cmd, dialogTitle, dir, buttons, timeoutMs, showRetry);
            }
          });

  // Connect action handler (for action system)
  connect(cmdCtrl, &BPCommandControl::actionRequested, this,
          [this](const QString &action, const QJsonObject &actionData) {
            if (!actionHandler) {
              actionHandler = new BPActionHandler(this, this);
              // Forward action handler signals to this widget's signals
              connect(actionHandler, &BPActionHandler::showDriverView, this, &BPPanelBase::showDriverView);
              connect(actionHandler, &BPActionHandler::reviewTrainingGuide, this, &BPPanelBase::reviewTrainingGuide);
              connect(actionHandler, &BPActionHandler::showLanguageSelector, this, &BPPanelBase::showLanguageSelector);
              connect(actionHandler, &BPActionHandler::showRegulatory, this, &BPPanelBase::showRegulatory);
              connect(actionHandler, &BPActionHandler::openNestedPanel, this, [this](const QString &configPath, const QString &title) {
                auto *nestedView = new BPNestedView(this);
                connect(nestedView, &BPNestedView::finished, [nestedView]() {
                  nestedView->disconnect();
                  nestedView->setParent(nullptr);
                  nestedView->deleteLater();
                });
                nestedView->setupView(title, configPath);
                nestedView->show();
              });
            }
            actionHandler->handleAction(action, actionData);
          });

  // Handle connect_signal if present
  if (control.contains("connect_signal")) {
    QString signalName = control["connect_signal"].toString();
    connectSignal(signalName, cmdCtrl, this);
  }

  connect(cmdCtrl, &BPCommandControl::commandRequested, this, &BPPanelBase::onControlValueChanged);
  return cmdCtrl;
}

QWidget *BPPanelBase::createRestartUIControl(const QJsonObject &control) {
  QString buttonText = control["button_text"].toString();
  QString confirmText = control["confirm_text"].toString();
  QString confirmYesText = control["confirm_yes_text"].toString();
  QString confirmNoText = control["confirm_no_text"].toString();
  bool requireConfirm = control.value("confirm").toBool(true); // Default to requiring confirmation

  if (buttonText.isEmpty()) {
    buttonText = tr("RESTART UI");
  }
  if (confirmText.isEmpty()) {
    confirmText = tr("Are you sure you want to restart the user interface?");
  }
  if (confirmYesText.isEmpty()) {
    confirmYesText = tr("Restart");
  }
  if (confirmNoText.isEmpty()) {
    confirmNoText = tr("Cancel");
  }

  auto restartCtrl = new BPCommandControl(
    control["title"].toString(),
    control["desc"].toString(),
    buttonText,
    "", // no shell command
    "", // no action
    control,
    "", // no working dir
    requireConfirm,
    confirmText,
    confirmYesText,
    confirmNoText,
    QJsonArray() // no action buttons
  );

  // Connect restart UI handler
  connect(restartCtrl, &BPCommandControl::commandRequested, this,
          [=](const QString &cmd, const QString &dialogTitle, const QString &dir, const QJsonArray &buttons, bool confirmRequired, const QString &confText,
              const QString &yesText, const QString &noText) {
            if (confirmRequired) {
              BPConfirmationDialog::ConfirmConfig config;
              config.title = dialogTitle.isEmpty() ? tr("Restart UI") : dialogTitle;
              config.prompt = confText;
              config.confirmText = yesText;
              config.cancelText = noText;

              auto *dialog = BPConfirmationDialog::showConfirmation(config, this);
              connect(dialog, &BPConfirmationDialog::confirmed, this, [=](bool accepted) {
                if (accepted) {
                  BPLog::bpInfo() << "[bp.panel.base] Restarting UI..." << std::endl;
                  qApp->exit(18); // Exit code 18 triggers UI restart
                }
              });
            } else {
              BPLog::bpInfo() << "[bp.panel.base] Restarting UI..." << std::endl;
              qApp->exit(18);
            }
          });

  return restartCtrl;
}

QWidget *BPPanelBase::createStaticTextControl(const QJsonObject &control) {
  // Create a simple label-based control for static informational text
  QWidget *container = new QWidget(this);
  QVBoxLayout *layout = new QVBoxLayout(container);
  layout->setContentsMargins(40, 20, 40, 20);
  layout->setSpacing(10);

  // Title label
  QLabel *titleLabel = new QLabel(control["title"].toString(), container);
  titleLabel->setStyleSheet("font-size: 50px; font-weight: 500; color: #E4E4E4;");
  layout->addWidget(titleLabel);

  // Description label
  QLabel *descLabel = new QLabel(control["desc"].toString(), container);
  descLabel->setWordWrap(true);
  descLabel->setStyleSheet("font-size: 40px; color: #AAAAAA; padding-top: 10px;");
  layout->addWidget(descLabel);

  container->setStyleSheet("QWidget { background-color: #1C1C1C; border-radius: 10px; }");
  return container;
}

QWidget *BPPanelBase::createPlatformDisplayControl(const QJsonObject &control) {
  QString title = control["title"].toString();
  QString desc = control["desc"].toString();
  QString valueColor = control.value("value_color").toString("#0086E9");

  auto platformDisplay = new BPPlatformDisplayControl(title, desc, valueColor);
  platformDisplay->setObjectName("platform_display");
  return platformDisplay;
}

QWidget *BPPanelBase::createNestedControlsButton(const QJsonObject &control) {
  QString buttonIcon = control["button_icon"].toString();
  QString buttonText = control["button_text"].toString().isEmpty() ? tr("OPEN") : control["button_text"].toString();
  QString panelTitle = control["panel_title"].toString().isEmpty() ? control["title"].toString() : control["panel_title"].toString();

  auto nestedBtn = new BPNestedControlsButton(control["title"].toString(), control["desc"].toString(), buttonText, buttonIcon);

  QJsonObject nestedConfig;
  nestedConfig["menuName"] = panelTitle;
  nestedConfig["groups"] = control["groups"].toArray();

  connect(nestedBtn, &BPNestedControlsButton::clicked, [=]() {
    auto *nestedView = new BPNestedView(this);
    // Ensure proper cleanup when dialog is finished
    connect(nestedView, &BPNestedView::finished, [nestedView]() {
      nestedView->disconnect();
      nestedView->setParent(nullptr);
      nestedView->deleteLater();
    });
    nestedView->setupView(panelTitle, nestedConfig);
    nestedView->show();
  });

  return nestedBtn;
}

void BPPanelBase::updateConditionsForAllControls() {
  PanelConditions::getInstance().updateConditionsForAllControls([this]() { updateGroupVisibility(); });
}

void BPPanelBase::updateGroupVisibility() {
  for (auto &[groupName, groupData] : groups) {
    // Check if any controls are visible
    bool hasVisibleControls = false;
    for (QWidget *control : groupData.controls) {
      if (control && !control->testAttribute(Qt::WA_WState_Hidden)) {
        hasVisibleControls = true;
        break;
      }
    }

    // Update group visibility
    groupData.groupBox->setVisible(hasVisibleControls);

    // Update divider visibility based on adjacent controls
    // Each divider corresponds to the space between controls[i] and controls[i+1]
    for (size_t i = 0; i < groupData.dividers.size(); i++) {
      QWidget *divider = groupData.dividers[i];
      if (!divider) continue;

      // A divider should be visible only if both adjacent controls are visible
      // Use explicit visibility state (not dependent on parent visibility)
      bool prevControlVisible = (i < groupData.controls.size() && groupData.controls[i] && !groupData.controls[i]->testAttribute(Qt::WA_WState_Hidden));
      bool nextControlVisible = ((i + 1) < groupData.controls.size() && groupData.controls[i + 1] && !groupData.controls[i + 1]->testAttribute(Qt::WA_WState_Hidden));

      divider->setVisible(prevControlVisible && nextControlVisible);
    }
  }
}

void BPPanelBase::updateToggles() {
  for (auto &[param, toggle] : toggles) {
    toggle->refresh();
  }

  for (auto &[groupName, groupData] : groups) {
    for (QWidget *ctrl : groupData.controls) {
      if (auto toggle = qobject_cast<BPToggleControl *>(ctrl)) {
        toggle->refresh();
      } else if (auto numeric = qobject_cast<BPNumericControl *>(ctrl)) {
        numeric->refresh();
      } else if (auto segmented = qobject_cast<BPSegmentedControl *>(ctrl)) {
        segmented->refresh();
      } else if (auto selection = qobject_cast<BPSelectionControl *>(ctrl)) {
        // Refresh selection control by updating its selected value
        QString currentValue = QString::fromStdString(params.get(ctrl->objectName().toStdString()));
        selection->setSelectedValue(currentValue);
      } else if (auto staticDisplay = qobject_cast<BPStaticParamDisplay *>(ctrl)) {
        staticDisplay->refresh();
      }
    }
    updateResetButtonVisibility(groupData.groupBox);
  }
}

void BPPanelBase::updateResetButtonVisibility(QGroupBox *group) {
  if (!group)
    return;

  QPushButton *resetButton = group->findChild<QPushButton *>("resetButton");
  if (resetButton) {
    bool hasDefaults = false;
    for (const auto &[groupName, groupData] : groups) {
      if (groupData.groupBox == group) {
        for (QWidget *ctrl : groupData.controls) {
          QString paramName = ctrl->objectName();
          if (!DefaultParams::getInstance().getDefault(paramName).isEmpty()) {
            hasDefaults = true;
            break;
          }
        }
        break;
      }
    }
    resetButton->setVisible(hasDefaults);
  }
}

void BPPanelBase::handleGroupReset(const QString &groupName) {
  if (groups.find(groupName) == groups.end())
    return;

  QString groupTitle = groups[groupName].groupBox->title();
  QString msg = tr("Are you sure you want to reset %1 to default values?").arg(groupTitle);

  BPConfirmationDialog::ConfirmConfig config;
  config.title = tr("Reset Group Values");
  config.prompt = msg;
  config.confirmText = tr("Yes");
  config.cancelText = tr("No");

  auto *dialog = BPConfirmationDialog::showConfirmation(config, this);
  connect(dialog, &BPConfirmationDialog::confirmed, this, [this, groupName](bool accepted) {
    if (accepted) {
      resetGroupControls(groups[groupName].controls);
    }
  });
}

void BPPanelBase::resetGroupControls(const std::vector<QWidget *> &controls) {
  DefaultParams &defaults = DefaultParams::getInstance();

  for (QWidget *ctrl : controls) {
    const QString paramName = ctrl->objectName();
    if (paramName.isEmpty()) {
      continue;
    }

    QString defaultValue = defaults.getDefault(paramName);
    if (defaultValue.isEmpty()) {
      continue;
    }

    params.put(paramName.toStdString(), defaultValue.toStdString());
  }

  refresh();
}

bool BPPanelBase::validateControlBasics(const QJsonObject &control) {
  // Check if it's a Comma device restriction
  if (control.contains("OnlyOnCommaDevice") && control["OnlyOnCommaDevice"].toBool() && !CommaTools::isCommaDevice()) {
    QString logMsg = "[bp.panel.base] validateControlBasics | OnlyShownOnCommaDevice | Type: " + control["type"].toString();

    // Add title if available
    if (control.contains("title") && !control["title"].toString().isEmpty()) {
      logMsg += " | Title: " + control["title"].toString();
    }

    // Add param only if it exists and is not empty
    if (control.contains("param") && !control["param"].toString().isEmpty()) {
      logMsg += " | Param: " + control["param"].toString();
    }

    BPLog::bpInfo() << logMsg.toStdString() << std::endl;
    return false;
  }

  // Ensure type field exists
  if (!control.contains("type")) {
    BPLog::bpError() << "[bp.panel.base] validateControlBasics | Control missing required 'type' field | Param: " << control["param"].toString().toStdString() << std::endl;
    return false;
  }

  // Some control types don't require a title (e.g., button_grid has titles in buttons array)
  static const QSet<QString> typesNotRequiringTitle{"button_grid"};

  if (!typesNotRequiringTitle.contains(control["type"].toString()) && !control.contains("title")) {
    BPLog::bpError() << "[bp.panel.base] validateControlBasics | Control missing required 'title' field | Type: " << control["type"].toString().toStdString()
              << " | Param: " << control["param"].toString().toStdString() << std::endl;
    return false;
  }

  const QString &type = control["type"].toString();
  const QString &param = control["param"].toString();

  // Set of supported control types
  static const QSet<QString> supportedTypes{"toggle",      "float",          "integer",           "selection",         "param_viewer",
                                            "file_viewer", "recent_changes", "command_button", "param_list_viewer", "segmented_control",
                                            "nested_controls_button", "static_param_display", "param_toggle_button", "html_viewer", "text_input", "restart_ui", "file_param_display", "static_text", "platform_display"};

  // Ensure the type is supported
  if (!supportedTypes.contains(type)) {
    BPLog::bpError() << "[bp.panel.base] validateControlBasics | Unsupported control type: " << type.toStdString() << " | Param: " << param.toStdString() << std::endl;
    return false;
  }

  static const QSet<QString> typesNotRequiringParam{
      "param_viewer", "param_list_viewer", "nested_controls_button", "command_button", "file_viewer", "recent_changes", "html_viewer", "restart_ui", "file_param_display", "static_text", "platform_display"
  };

  // Ensure param is present for necessary types
  if (!typesNotRequiringParam.contains(type) && !control.contains("param")) {
    BPLog::bpError() << "[bp.panel.base] validateControlBasics | Missing 'param' field for control type: " << type.toStdString() << " | Param: " << param.toStdString() << std::endl;
    return false;
  }

  // Specific checks for control types
  if (type == "float" || type == "integer") {
    if (!control.contains("min") || !control.contains("max") || !control.contains("increment")) {
      BPLog::bpError() << "[bp.panel.base] validateControlBasics | Numeric control missing min/max/increment values | Type: " << type.toStdString() << " | Param: " << param.toStdString() << std::endl;
      return false;
    }
    if (type == "float" && !control.contains("division")) {
      BPLog::bpError() << "[bp.panel.base] validateControlBasics | Float control missing 'division' value | Param: " << param.toStdString() << std::endl;
      return false;
    }
  } else if (type == "selection") {
    if (!control.contains("options") || !control["options"].isArray()) {
      BPLog::bpError() << "[bp.panel.base] validateControlBasics | Selection control missing 'options' array | Param: " << param.toStdString() << std::endl;
      return false;
    }
  } else if (type == "file_viewer" && !control.contains("path")) {
    BPLog::bpError() << "[bp.panel.base] validateControlBasics | File viewer control missing 'path' field | Param: " << param.toStdString() << std::endl;
    return false;
  } else if (type == "command_button" && !control.contains("command") && !control.contains("action") && !control.contains("connect_signal")) {
    BPLog::bpError() << "[bp.panel.base] validateControlBasics | Command button control missing 'command', 'action', or 'connect_signal' field | Param: " << param.toStdString() << std::endl;
    return false;
  }

  return true;
}

void BPPanelBase::onControlValueChanged() {
  // Invalidate condition cache since a param was changed
  PanelConditions::getInstance().invalidateCache();

  // Refresh UI with new param values and re-validate conditions
  refresh();
  emit controlValueChanged();
}

void BPPanelBase::refresh() {
  if (isRefreshing)
    return;
  isRefreshing = true;

  try {
    // First pass: refresh all controls (read params, update UI)
    for (auto &[groupName, groupData] : groups) {
      for (QWidget *ctrl : groupData.controls) {
        // Refresh different control types
        if (auto toggle = qobject_cast<BPToggleControl *>(ctrl)) {
          toggle->refresh();
        } else if (auto numeric = qobject_cast<BPNumericControl *>(ctrl)) {
          numeric->refresh();
        } else if (auto segmented = qobject_cast<BPSegmentedControl *>(ctrl)) {
          segmented->refresh();
        } else if (auto selection = qobject_cast<BPSelectionControl *>(ctrl)) {
          // Refresh selection control by updating its selected value
          QString currentValue = QString::fromStdString(params.get(ctrl->objectName().toStdString()));
          selection->setSelectedValue(currentValue);
        } else if (auto staticDisplay = qobject_cast<BPStaticParamDisplay *>(ctrl)) {
          staticDisplay->refresh();
        }
      }
    }

    // Second pass: update conditions for all controls in one batch
    // This is more efficient than inline validation during refresh
    updateConditionsForAllControls();

    // Third pass: update group visibility based on final control states
    updateGroupVisibility();

  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.panel.base] refresh | Error during refresh: " << e.what() << std::endl;
  }

  isRefreshing = false;
}

// Registry implementations
void BPPanelBase::registerSignalConnectors() {
  // Register showLanguageSelector signal connector
  signalConnectors["showLanguageSelector"] = [this](QWidget *widget, QObject *target) {
    QObject::connect(this, &BPPanelBase::showLanguageSelector, target, [target]() {
      Params params;
      QMap<QString, QString> langs = getSupportedLanguages();
      QString currentLang = langs.key(QString::fromStdString(params.get("LanguageSetting")));
      QString selection = MultiOptionDialog::getSelection(QObject::tr("Select a language"), langs.keys(), currentLang, qobject_cast<QWidget*>(target));
      if (!selection.isEmpty()) {
        params.put("LanguageSetting", langs[selection].toStdString());
        qApp->exit(18);
        watchdog_kick(0);
      }
    });
  };

  // Register showRegulatory signal connector
  signalConnectors["showRegulatory"] = [this](QWidget *widget, QObject *target) {
    QObject::connect(this, &BPPanelBase::showRegulatory, target, [target]() {
      if (Hardware::TICI()) {
        const std::string txt = util::read_file("../assets/offroad/fcc.html");
        ConfirmationDialog::rich(QString::fromStdString(txt), qobject_cast<QWidget*>(target));
      }
    });
  };

  // Register showDriverView signal connector
  signalConnectors["showDriverView"] = [this](QWidget *widget, QObject *target) {
    QObject::connect(this, &BPPanelBase::showDriverView, [target]() {
      if (QWidget *w = qobject_cast<QWidget*>(target)) {
        w->show();
      }
    });
  };

  // Register reviewTrainingGuide signal connector
  signalConnectors["reviewTrainingGuide"] = [this](QWidget *widget, QObject *target) {
    QObject::connect(this, &BPPanelBase::reviewTrainingGuide, [target]() {
      if (QWidget *w = qobject_cast<QWidget*>(target)) {
        w->show();
      }
    });
  };
}

void BPPanelBase::registerListGenerators() {
  // Register language list generator
  listGenerators["getSupportedLanguages"] = []() {
    return getSupportedLanguages();
  };
}

void BPPanelBase::connectSignal(const QString &signalName, QWidget *widget, QObject *target) {
  auto it = signalConnectors.find(signalName);
  if (it != signalConnectors.end()) {
    it->second(widget, target);
    // BPLog::bpInfo() << "[bp.panel.base] Connected signal: " << signalName.toStdString() << std::endl;
  } else {
    BPLog::bpWarn() << "[bp.panel.base] Signal connector not found: " << signalName.toStdString() << std::endl;
  }
}
