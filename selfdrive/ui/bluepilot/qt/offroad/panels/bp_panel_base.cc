// bp_panel_base.cc

#include "bp_panel_base.h"
#include "bp_recent_changes.h"
#include <iostream>

BPPanelBase::BPPanelBase(QWidget *parent) : BPPanelListWidget(parent) {
  setMouseTracking(true);
  setSpacing(30);
  setMinimumWidth(1000);
  setMaximumWidth(1920);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

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
  refresh();
  updateToggles();
  updateConditionsForAllControls();
  updateGroupVisibility();
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
    std::cerr << "Failed to load configuration" << std::endl;
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
    std::cout << "BPPanelBase: Group has keepScreenAwake=true, enabling activity simulation" << std::endl;
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
        // Store the container in groupData to handle visibility
        groupData.controls.push_back(lineContainer);
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
    delete groupBox;
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
    for (const auto &controlRef : controls) {
      if (QWidget *control = processControlCreation(controlRef.toObject())) {
        groupLayout->addWidget(control);
      }
    }

    layout->addWidget(group);
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
    std::cout << control["param"].toString().toStdString() << " Control is hidden" << std::endl;
    return nullptr;
  }

  QString type = control["type"].toString();
  QWidget *widget = nullptr;

  if (type == "toggle") {
    widget = createToggleControl(control);
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
  } else if (type == "file_viewer") {
    widget = createFileViewerControl(control);
  } else if (type == "recent_changes") {
    widget = createRecentChangesControl(control);
  } else if (type == "command_button") {
    widget = createCommandButtonControl(control);
  } else if (type == "nested_controls_button") {
    widget = createNestedControlsButton(control);
  } else {
    std::cerr << "Unsupported control type: " << type.toStdString() << " | Param:" << control["param"].toString().toStdString() << std::endl;
    return nullptr;
  }

  if (widget && control.contains("conditions")) {
    ControlConditions conditions;
    conditions.conditions = control["conditions"].toObject();
    conditions.hasConditions = true;
    PanelConditions::getInstance().controlConditions[widget] = conditions;
    widget->setEnabled(true);
    widget->update();
  }

  return widget;
}

QWidget *BPPanelBase::createToggleControl(const QJsonObject &control) {
  QString param = control["param"].toString();
  QString title = control["title"].toString();
  QString desc = control["desc"].toString();
  auto toggle = new BPToggleControl(param, title, desc);
  toggle->setObjectName(param);
  toggles[param.toStdString()] = toggle;
  connect(toggle, &BPToggleControl::toggleFlipped, this, [this, param](bool state) {
    std::string currentValue = params.get(param.toStdString());
    std::cout << "Parameter changed - " << param.toStdString() << ": " << (currentValue == "1" ? "On" : "Off") << " -> " << (state ? "On" : "Off") << std::endl;
    onControlValueChanged();
  });
  return toggle;
}

QWidget *BPPanelBase::createSegmentedControl(const QJsonObject &control) {
  QJsonArray options = control["options"].toArray();
  QVector<QPair<QString, QString>> optionPairs;
  QString defaultValue;
  for (const auto &opt : options) {
    QJsonObject option = opt.toObject();
    optionPairs.append({option["name"].toString(), option["value"].toString()});
    if (option.contains("default") && option["default"].toBool()) {
      defaultValue = option["value"].toString();
    }
  }
  auto segmented = new BPSegmentedControl(control["param"].toString(), control["title"].toString(), control["desc"].toString(), optionPairs, defaultValue);
  segmented->setObjectName(control["param"].toString());
  connect(segmented, &BPSegmentedControl::valueChanged, this, &BPPanelBase::onControlValueChanged);
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
  auto selectionControl = new BPSelectionControl(control["param"].toString(), control["title"].toString(), control["desc"].toString());
  selectionControl->setObjectName(control["param"].toString());

  QVector<BPSelectionDialog::Option> options;
  QJsonArray optArray = control["options"].toArray();
  for (const auto &opt : optArray) {
    QJsonObject optObj = opt.toObject();
    options.append({optObj["name"].toString(), optObj["value"].toString()});
  }

  QString currentValue = QString::fromStdString(params.get(control["param"].toString().toStdString()));
  selectionControl->setSelectedValue(currentValue);

  connect(selectionControl, &BPSelectionControl::clicked, [=]() {
    QString currentValue = QString::fromStdString(params.get(control["param"].toString().toStdString()));
    QString newValue = BPSelectionDialog::getValue(control["title"].toString(), options, currentValue, this);

    if (!newValue.isEmpty() && newValue != currentValue) {
      std::cout << "Selection Control - New value: " << newValue.toStdString() << std::endl;
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
  QString workingDir = control["working_dir"].toString();
  QString buttonText = control["button_text"].toString();
  QString confirmText = control["confirm_text"].toString();
  QString confirmYesText = control["confirm_yes_text"].toString();
  QString confirmNoText = control["confirm_no_text"].toString();
  bool requireConfirm = control["confirm"].toBool();
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

  auto cmdCtrl = new BPCommandControl(control["title"].toString(), control["desc"].toString(), buttonText, command, workingDir, requireConfirm, confirmText, confirmYesText,
                                      confirmNoText, actionButtons);

  connect(cmdCtrl, &BPCommandControl::commandRequested, this,
          [this](const QString &cmd, const QString &dialogTitle, const QString &dir, const QJsonArray &buttons, bool confirmRequired, const QString &confText,
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
                  commandDialog->executeCommand(cmd, dialogTitle, dir, buttons);
                }
              });
            } else {
              BPCommandDialog *commandDialog = new BPCommandDialog(this);
              commandDialog->executeCommand(cmd, dialogTitle, dir, buttons);
            }
          });

  connect(cmdCtrl, &BPCommandControl::commandRequested, this, &BPPanelBase::onControlValueChanged);
  return cmdCtrl;
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
    bool hasVisibleControls = false;
    for (QWidget *control : groupData.controls) {
      if (control && control->isVisible()) {
        hasVisibleControls = true;
        break;
      }
    }
    groupData.groupBox->setVisible(hasVisibleControls);
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
    std::cout << "Control is only available on Comma devices | Type: " << control["type"].toString().toStdString() << " | Param: " << control["param"].toString().toStdString()
              << std::endl;
    return false;
  }

  // Ensure required fields exist
  if (!control.contains("type") || !control.contains("title")) {
    std::cerr << "Control missing required 'type' or 'title' field | Type: " << control["type"].toString().toStdString()
              << " | Param: " << control["param"].toString().toStdString() << std::endl;
    return false;
  }

  const QString &type = control["type"].toString();
  const QString &param = control["param"].toString();

  // Set of supported control types
  static const QSet<QString> supportedTypes{"toggle",      "float",          "integer",           "selection",         "param_viewer",
                                            "file_viewer", "recent_changes", "command_button", "param_list_viewer", "segmented_control", "nested_controls_button"};

  // Ensure the type is supported
  if (!supportedTypes.contains(type)) {
    std::cerr << "Unsupported control type: " << type.toStdString() << " | Param: " << param.toStdString() << std::endl;
    return false;
  }

  static const QSet<QString> typesNotRequiringParam{
      "param_viewer", "param_list_viewer", "nested_controls_button", "command_button", "file_viewer", "recent_changes",
  };

  // Ensure param is present for necessary types
  if (!typesNotRequiringParam.contains(type) && !control.contains("param")) {
    std::cerr << "Missing 'param' field for control type: " << type.toStdString() << " | Param: " << param.toStdString() << std::endl;
    return false;
  }

  // Specific checks for control types
  if (type == "float" || type == "integer") {
    if (!control.contains("min") || !control.contains("max") || !control.contains("increment")) {
      std::cerr << "Numeric control missing min/max/increment values | Type: " << type.toStdString() << " | Param: " << param.toStdString() << std::endl;
      return false;
    }
    if (type == "float" && !control.contains("division")) {
      std::cerr << "Float control missing 'division' value | Param: " << param.toStdString() << std::endl;
      return false;
    }
  } else if (type == "selection") {
    if (!control.contains("options") || !control["options"].isArray()) {
      std::cerr << "Selection control missing 'options' array | Param: " << param.toStdString() << std::endl;
      return false;
    }
  } else if (type == "file_viewer" && !control.contains("path")) {
    std::cerr << "File viewer control missing 'path' field | Param: " << param.toStdString() << std::endl;
    return false;
  } else if (type == "command_button" && !control.contains("command")) {
    std::cerr << "Command button control missing 'command' field | Param: " << param.toStdString() << std::endl;
    return false;
  }

  return true;
}

void BPPanelBase::onControlValueChanged() {
  refresh();
  updateConditionsForAllControls();
  updateGroupVisibility();
  emit controlValueChanged();
}

void BPPanelBase::refresh() {
  if (isRefreshing)
    return;
  isRefreshing = true;

  try {
    for (auto &[groupName, groupData] : groups) {
      bool hasVisibleControls = false;

      for (QWidget *ctrl : groupData.controls) {
        // Refresh different control types
        if (auto toggle = qobject_cast<BPToggleControl *>(ctrl)) {
          toggle->refresh();
        } else if (auto numeric = qobject_cast<BPNumericControl *>(ctrl)) {
          numeric->refresh();
        } else if (auto segmented = qobject_cast<BPSegmentedControl *>(ctrl)) {
          segmented->refresh();
        }

        // Update control conditions
        auto conditionIt = PanelConditions::getInstance().controlConditions.find(ctrl);
        if (conditionIt != PanelConditions::getInstance().controlConditions.end() && conditionIt->second.hasConditions) {
          bool shouldBeEnabled = PanelConditions::getInstance().validateCompositeConditions(conditionIt->second.conditions);
          if (ctrl->isEnabled() != shouldBeEnabled) {
            ctrl->setEnabled(shouldBeEnabled);
            ctrl->update();
          }
        }

        if (ctrl->isEnabled() && ctrl->isVisible()) {
          hasVisibleControls = true;
        }
      }

      groupData.groupBox->setVisible(hasVisibleControls);
    }
  } catch (const std::exception &e) {
    std::cerr << "Error during refresh: " << e.what() << std::endl;
  }

  isRefreshing = false;
}
