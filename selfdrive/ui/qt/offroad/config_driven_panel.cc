// config_driven_panel.cc

#include <filesystem>
#include <iostream>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScreen>
#include <QTabWidget>
#include <QTabBar>

#include "selfdrive/ui/ui.h"
#include "selfdrive/ui/qt/offroad/config_driven_panel.h"
#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/offroad/settings.h"

ConfigDrivenPanel::ConfigDrivenPanel(SettingsWindow *parent, const QString &configPath)
    : ConfigDrivenListWidget(parent) {
    setSpacing(50);
    setMinimumWidth(1000);  // Minimum reasonable width
    setMaximumWidth(1920);  // Max width from original code
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    // unified style sheet at the panel level
    this->setStyleSheet(R"(
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

      QPushButton:disabled {
          background-color: #2a2a2a;
      }
      QAbstractButton:disabled {
          background-color: #2a2a2a;
      }
      ToggleControl:disabled {
          background-color: #2a2a2a;
      }
    )");

    // Initialize timers
    refreshTimer.setInterval(1000);
    refreshTimer.setSingleShot(false);
    connect(&refreshTimer, &QTimer::timeout, this, [this]() {
        if (!isRefreshing) {
            refreshPanel();
        }
    });

    activityTimer = new QTimer(this);
    activityTimer->setInterval(9000); // 9 seconds
    connect(activityTimer, &QTimer::timeout, this, &ConfigDrivenPanel::simulateActivity);

    // Set up the max duration timer
    QTimer::singleShot(270000, this, &ConfigDrivenPanel::stopActivitySimulation);

    // Enable mouse tracking to receive mouse move events
    setMouseTracking(true);

    // Load JSON config
    ConfigDrivenPanelConfig& config = ConfigDrivenPanelConfig::getInstance();
    QString actualConfigPath = getProjectRootPath() + configPath;
    if (!config.loadConfig(actualConfigPath)) {
        std::cerr << "Failed to load Custom Car panel configuration" << std::endl;
        return;
    }

    // Process JSON configuration
    const QJsonObject& jsonConfig = config.getConfig();
    QJsonArray groupsArray = jsonConfig["groups"].toArray();
    for (const auto& groupValue : groupsArray) {
        createGroup(groupValue.toObject());
    }
}

ConfigDrivenPanel::~ConfigDrivenPanel() {
    std::cout << "Stopping activity timer" << std::endl;
    activityTimer->stop();
}

QString ConfigDrivenPanel::getBaseGroupBoxStyle() {
    return R"(
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
        QGroupBox:disabled {
            border: 1px solid #555555;
        }
        QGroupBox::title:disabled {
            color: #777777;
        }
    )";
}

QGroupBox *ConfigDrivenPanel::createStyledGroupBox(const QString &title) {
    QGroupBox *groupBox = new QGroupBox(title);
    groupBox->setStyleSheet(getBaseGroupBoxStyle());
    return groupBox;
}

QPushButton* ConfigDrivenPanel::createResetButton() {
    QPushButton* resetButton = new QPushButton();
    resetButton->setObjectName("resetButton");
    resetButton->setText(tr(" Reset"));

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
    resetButton->setLayoutDirection(Qt::LeftToRight);
    resetButton->setFixedSize(200, 60);

    std::cout << "Reset button created with icon" << std::endl;
    return resetButton;
}

void ConfigDrivenPanel::createGroup(const QJsonObject& group) {
    QString type = group["type"].toString();
    bool hidden = group["hidden"].toBool();
    if (hidden) return;
    // std::cout << "Group type: " << type.toStdString() << std::endl;
    if (type == "tabPanel") {
        createTabPanel(group);
        return;
    }

    QString groupName = group["groupName"].toString();
    QString title = group["title"].toString();
    bool enableReset = group["enableResetButton"].toBool();

    QGroupBox* groupBox = createStyledGroupBox(title);
    QVBoxLayout* layout = new QVBoxLayout(groupBox);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);

    GroupData groupData;
    groupData.groupBox = groupBox;

    if (enableReset) {
        QHBoxLayout* titleLayout = new QHBoxLayout();
        titleLayout->addStretch();
        QPushButton* resetButton = createResetButton();
        connect(resetButton, &QPushButton::clicked, this, [this, groupName]() {
            this->handleGroupReset(groupName);
        });
        titleLayout->addWidget(resetButton);
        titleLayout->setContentsMargins(0, 0, 0, 20);
        layout->addLayout(titleLayout);
    }

    const QJsonArray& controls = group["controls"].toArray();
    bool hasVisibleControls = false;

    for (const auto& controlValue : controls) {
        QWidget* widget = createControl(controlValue.toObject());
        if (widget) {
            hasVisibleControls = true;
            if (auto* control = qobject_cast<AbstractControl*>(widget)) {
                QList<QLabel*> labels = control->findChildren<QLabel*>();
                for (QLabel* label : labels) {
                    if (label->text() == control->getDescription()) {
                        label->setContentsMargins(0, 0, 0, 10);
                        label->setStyleSheet(R"(
                            font-size: 40px;
                            color: yellow;
                            padding-left: 0px;
                            background-color: transparent;
                            min-height: 40px;
                        )");
                        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
                        label->setWordWrap(true);
                        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                        label->setMaximumWidth(1300);
                        break;
                    }
                }

                if (QHBoxLayout* mainLayout = control->findChild<QHBoxLayout*>()) {
                    mainLayout->setStretch(0, 1);
                    mainLayout->setSpacing(50);
                }
            }
            layout->addWidget(widget);
            groupData.controls.push_back(widget);
        }
    }

    if (hasVisibleControls) {
        groups[groupName] = groupData;
        addItem(groupBox);
    } else {
        delete groupBox;
    }
}

void ConfigDrivenPanel::updateGroupVisibility() {
    for (auto& [groupName, groupData] : groups) {
        bool hasVisibleControls = false;
        for (QWidget* control : groupData.controls) {
            if (control && control->isVisible()) {
                hasVisibleControls = true;
                break;
            }
        }
        groupData.groupBox->setVisible(hasVisibleControls);
    }
}

QWidget* ConfigDrivenPanel::createControl(const QJsonObject& control) {
    if (!validateControlBasics(control)) {
        return nullptr;
    }

    QString type = control["type"].toString();
    QString param = control["param"].toString();
    QString title = control["title"].toString();
    QString desc = control["desc"].toString();
    bool hidden = control["hidden"].toBool();
    if (hidden) {
        std::cout << param.toStdString() << " Control is hidden" << std::endl;
        return nullptr;
    }

    if (type == "toggle") {
        auto toggle = ConfigDrivenControlFactory::createToggleControl(param, title, desc, "");
        toggle->setObjectName(param);

        toggles[param.toStdString()] = toggle;
        QObject::connect(toggle, &ToggleControl::toggleFlipped, [this]() {
            onControlValueChanged();
        });

        if (control.contains("conditions")) {
            ControlConditions conditions;
            conditions.conditions = control["conditions"].toObject();
            conditions.hasConditions = true;
            controlConditions[toggle] = conditions;
            // Evaluate conditions immediately
            // bool shouldBeEnabled = validateCompositeConditions(conditions.conditions);
            toggle->setEnabled(true);
            toggle->update();
        }
        return toggle;
    }
    else if (type == "segmented_control") {
        QJsonArray options = control["options"].toArray();
        QVector<QPair<QString, QString>> optionPairs;
        QString defaultValue;

        // Look for option with default: true
        for (const auto& opt : options) {
            QJsonObject option = opt.toObject();
            optionPairs.append({
                option["name"].toString(),
                option["value"].toString()
            });

            if (option.contains("default") && option["default"].toBool()) {
                defaultValue = option["value"].toString();
            }
        }

        auto ctrl = new ConfigDrivenSegmentedControl(
            param, title, desc, "",
            optionPairs, defaultValue, nullptr
        );

        ctrl->setObjectName(param);
        return ctrl;
    }
    else if (type == "float") {
        auto ctrl = ConfigDrivenControlFactory::createFloatControl(
            param, title, desc,
            control["min"].toDouble(),
            control["max"].toDouble(),
            control["increment"].toDouble(),
            false, "", {},
            control["division"].toDouble());
        ctrl->setObjectName(param);
        QObject::connect(ctrl, &ConfigDrivenParamValueControlFloat::valueChanged, [this](float) {
            onControlValueChanged();
        });

        if (control.contains("conditions")) {
            ControlConditions conditions;
            conditions.conditions = control["conditions"].toObject();
            conditions.hasConditions = true;
            controlConditions[ctrl] = conditions;
            ctrl->setEnabled(true);
        }
        return ctrl;
    }
    else if (type == "integer") {
        auto ctrl = ConfigDrivenControlFactory::createIntegerControl(
            param, title, desc,
            control["min"].toInt(),
            control["max"].toInt(),
            control["increment"].toInt(),
            false);
        ctrl->setObjectName(param);
        QObject::connect(ctrl, &ConfigDrivenParamValueControl::valueChanged, [this](int) {
            onControlValueChanged();
        });

        if (control.contains("conditions")) {
            ControlConditions conditions;
            conditions.conditions = control["conditions"].toObject();
            conditions.hasConditions = true;
            controlConditions[ctrl] = conditions;
            ctrl->setEnabled(true);
        }
        return ctrl;
    }
    else if (type == "selection") {
        auto button = new ButtonControl(title, tr("SELECT"), desc);
        button->setObjectName(param);
        button->setStyleSheet(R"(
            QPushButton {
                border-radius: 10px;
                font-size: 40px;
                font-weight: 500;
                color: #E4E4E4;
            }
            QPushButton:pressed {
                background-color: #4a4a4a;
            }
            QPushButton:disabled {
                color: #777777;
            }
            QFrame:disabled {
                opacity: 0.5;
            }
            QLabel:disabled {
                color: #777777;
            }
        )");

        QJsonArray options = control["options"].toArray();
        QStringList items;
        std::map<QString, QString> selections;

        for (const auto& option : options) {
            QJsonObject opt = option.toObject();
            QString name = opt["name"].toString();
            QString value = opt["value"].toString();
            items.append(name);
            selections[name] = value;
        }

        QObject::connect(button, &ButtonControl::clicked, [=]() {
            QString cur = QString::fromStdString(params.get(param.toStdString()));
            for (const auto& [name, value] : selections) {
                if (value == cur) {
                    cur = name;
                    break;
                }
            }

            QString selection = MultiOptionDialog::getSelection(title, items, cur, this);
            if (!selection.isEmpty()) {
                auto it = selections.find(selection);
                if (it != selections.end()) {
                    params.put(param.toStdString(), it->second.toStdString());
                    button->setValue(selection);

                    if (ConfirmationDialog::confirm(
                        tr("Reboot required for changes to take effect. Would you like to reboot now?"),
                        tr("Reboot"), this)) {
                        params.putBool("DoReboot", true);
                    }
                }
            }
        });

        connect(button, &ButtonControl::clicked, this, &ConfigDrivenPanel::onControlValueChanged);

        QString currentValue = QString::fromStdString(params.get(param.toStdString()));
        for (const auto& [name, value] : selections) {
            if (value == currentValue) {
                button->setValue(name);
                break;
            }
        }

        if (control.contains("conditions")) {
            ControlConditions conditions;
            conditions.conditions = control["conditions"].toObject();
            conditions.hasConditions = true;
            controlConditions[button] = conditions;
            button->setEnabled(true);
        }
        return button;
    }
    else if (type == "param_viewer") {
        auto dataBtn = new ButtonControl(title, tr("VIEW"), desc);

        QObject::connect(dataBtn, &ButtonControl::clicked, [this, param, title]() {
            auto dialog = new ConfigDrivenParamViewerDialog(this);
            dialog->setupParamViewer(title, param);
            dialog->setupFullscreen();
            dialog->exec();
        });

        connect(dataBtn, &ButtonControl::clicked, this, &ConfigDrivenPanel::onControlValueChanged);

        if (control.contains("conditions")) {
            ControlConditions conditions;
            conditions.conditions = control["conditions"].toObject();
            conditions.hasConditions = true;
            controlConditions[dataBtn] = conditions;
            dataBtn->setEnabled(true);
        }
        return dataBtn;
    }
    else if (type == "param_list_viewer") {
        auto listBtn = new ButtonControl(title, tr("VIEW ALL"), desc);

        QObject::connect(listBtn, &ButtonControl::clicked, [this]() {
            std::vector<std::string> allParams = Params().allKeys();

            QVBoxLayout* paramLayout = new QVBoxLayout();
            QWidget* paramWidget = new QWidget();
            paramWidget->setLayout(paramLayout);

            for (const auto& param : allParams) {
                QString paramStr = QString::fromStdString(param);
                auto paramBtn = new ButtonControl(paramStr, tr("VIEW"), "");

                QObject::connect(paramBtn, &ButtonControl::clicked, [this, paramStr]() {
                    auto dialog = new ConfigDrivenParamViewerDialog(this);
                    dialog->setupParamViewer(tr("Parameter Value"), paramStr);
                    dialog->setupFullscreen();
                    dialog->exec();
                });

                paramLayout->addWidget(paramBtn);
            }

            ScrollView* scroll = new ScrollView(paramWidget, this);

            auto dialog = new ConfigDrivenFullScreenDialog(this);
            dialog->setupContent(tr("Available Parameters"), "");
            dialog->main_layout->insertWidget(2, scroll);
            dialog->setupFullscreen();
            dialog->exec();
        });

        return listBtn;
    }
    else if (type == "file_viewer") {
        auto dataBtn = new ButtonControl(title, tr("VIEW"), desc);

        QString relativePath = control["path"].toString();
        QString header = control["header"].toString();

        QObject::connect(dataBtn, &ButtonControl::clicked, [this, relativePath, header, title]() {
            QString rootPath = getProjectRootPath();
            QString fullPath = QDir(rootPath).filePath(relativePath);

            QFile file(fullPath);
            QString content;

            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                content = tr("<b>Error:</b><br>Could not open file: ") + fullPath;
            } else {
                QString fileContent = QString::fromUtf8(file.readAll());
                if (fileContent.isEmpty()) {
                    content = tr("File is empty");
                } else {
                    content = "<pre style='white-space: pre-wrap; margin: 0; padding: 0; background-color: transparent;'>" +
                            fileContent.toHtmlEscaped() +
                            "</pre>";
                }
            }

            QString dialogTitle = header.isEmpty() ? title : header;
            showFullScreenDialog(dialogTitle, content);
        });

        connect(dataBtn, &ButtonControl::clicked, this, &ConfigDrivenPanel::onControlValueChanged);

        if (control.contains("conditions")) {
            ControlConditions conditions;
            conditions.conditions = control["conditions"].toObject();
            conditions.hasConditions = true;
            controlConditions[dataBtn] = conditions;
            dataBtn->setEnabled(true);
        }
        return dataBtn;
    }
    else if (type == "command_button") {
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

        auto cmdBtn = new ButtonControl(title, buttonText, desc);

        QObject::connect(cmdBtn, &ButtonControl::clicked, [this, command, title, workingDir,
                        confirmText, confirmYesText, confirmNoText, requireConfirm, actionButtons]() {
            if (requireConfirm) {
                auto confirm = new ConfirmationDialog(confirmText, confirmYesText, confirmNoText, false, this);
                bool confirmed = confirm->exec();
                delete confirm;
                if (!confirmed) {
                    return;
                }
            }

            executeCommand(command, title, workingDir, actionButtons);
        });

        connect(cmdBtn, &ButtonControl::clicked, this, &ConfigDrivenPanel::onControlValueChanged);

        if (control.contains("conditions")) {
            ControlConditions conditions;
            conditions.conditions = control["conditions"].toObject();
            conditions.hasConditions = true;
            controlConditions[cmdBtn] = conditions;
            cmdBtn->setEnabled(true);
        }
        return cmdBtn;
    }

    return nullptr;
}

void ConfigDrivenPanel::createTabPanel(const QJsonObject& group) {
    QTabWidget* tabWidget = new QTabWidget(this);
    tabWidget->setStyleSheet(R"(
    QTabWidget::pane {
        border-top: none;
        background: rgba(80, 80, 80, 0.7);
        border-top-left-radius: 0;
        border-top-right-radius: 0;
        border-bottom-left-radius: 20px;
        border-bottom-right-radius: 20px;
        margin-top: 0;  /* Remove margin to connect with tab */
    }
    QTabWidget {
        border-top-left-radius: 0;
        border-top-right-radius: 0;
        border-bottom-left-radius: 20px;
        border-bottom-right-radius: 20px;

        background: rgba(80, 80, 80, 0.7);
    }
    QWidget#qt_tabwidget_stackedwidget {
        border-top-left-radius: 0;
        border-top-right-radius: 0;
        border-bottom-left-radius: 20px;
        border-bottom-right-radius: 20px;
        border-left: 2px solid rgba(255, 255, 255, 0.2);
        border-right: 2px solid rgba(255, 255, 255, 0.2);
        border-bottom: 2px solid rgba(255, 255, 255, 0.2);
        background: rgba(80, 80, 80, 0.7);
    }
    QTabBar {
        background: transparent;
        border: none;
    }
    QTabBar::tab {
        background: rgba(50, 50, 50, 1.0);
        border: none;
        border-top-left-radius: 20px;
        border-top-right-radius: 20px;
        padding: 12px 30px;
        margin: 0px 2px;
        color: #E4E4E4;
        font-size: 40px;
        font-weight: 500;
        min-width: 120px;
    }
    QTabBar::tab:selected {
        background: rgba(80, 80, 80, 0.7);
        color: white;
        border: 2px solid rgba(255, 255, 255, 0.2);
        border-bottom: none;  /* Remove bottom border to connect with panel */
        padding: 12px 30px;
        margin-bottom: -2px;  /* Offset to overlap with panel border */
    }
    QTabBar::tab:!selected {
        margin-top: 3px;
    }
    QTabBar::tab:hover {
        background: rgba(70, 70, 70, 1.0);
    }
    QTabWidget::tab-bar {
        alignment: center;
    }
)");

    tabWidget->setTabPosition(QTabWidget::TabPosition::North);
    tabWidget->setTabShape(QTabWidget::TabShape::Rounded);
    tabWidget->tabBar()->setExpanding(false);
    tabWidget->tabBar()->setDocumentMode(true);
    tabWidget->tabBar()->setUsesScrollButtons(false);

    QWidget* container = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->addWidget(tabWidget);

    const QJsonArray& tabs = group["tabs"].toArray();
    for (const auto& tabRef : tabs) {
        QJsonObject tab = tabRef.toObject();
        QString name = tab["name"].toString();
        QWidget* content = createTabContent(tab["groups"].toArray());
        tabWidget->addTab(content, name);
    }

    addItem(container);
}

QWidget* ConfigDrivenPanel::createTabContent(const QJsonArray& tabGroups) {
    QWidget* content = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setSpacing(25);

    for (const auto& groupRef : tabGroups) {
        QJsonObject groupObj = groupRef.toObject();
        QGroupBox* group = createStyledGroupBox(groupObj["title"].toString());
        group->setObjectName("tabPanelGroupBox");
        QString combinedStyle = getBaseGroupBoxStyle() + R"(
            QGroupBox#tabPanelGroupBox {
                padding: 30px !important;
            }
        )";
        group->setStyleSheet(combinedStyle);

        QVBoxLayout* groupLayout = new QVBoxLayout(group);

        const QJsonArray& controls = groupObj["controls"].toArray();
        for (const auto& controlRef : controls) {
            if (QWidget* control = createControl(controlRef.toObject())) {
                groupLayout->addWidget(control);
            }
        }

        groupLayout->setContentsMargins(20, 20, 20, 20);

        layout->addWidget(group);
    }

    layout->addStretch();
    return content;
}

void ConfigDrivenPanel::updateControlWithDefault(QWidget* ctrl) {
    if (!ctrl) return;

    QString paramName = ctrl->objectName();
    ConfigDrivenDefaultParams& defaults = ConfigDrivenDefaultParams::getInstance();
    QString defaultValue = defaults.getDefault(paramName);

    if (!defaultValue.isEmpty()) {
        if (auto* valueControl = qobject_cast<ConfigDrivenParamValueControl*>(ctrl)) {
            valueControl->setDefaultValue(defaultValue);
        } else if (auto* floatControl = qobject_cast<ConfigDrivenParamValueControlFloat*>(ctrl)) {
            floatControl->setDefaultValue(defaultValue);
        }
    } else {
        resetControlTitle(ctrl);
    }
}

void ConfigDrivenPanel::updateResetButtonVisibility(QGroupBox* group) {
    if (!group) return;

    QPushButton* resetButton = group->findChild<QPushButton*>("resetButton");
    if (resetButton) {
        bool hasDefaults = false;
        for (const auto& [groupName, groupData] : groups) {
            if (groupData.groupBox == group) {
                for (QWidget* ctrl : groupData.controls) {
                    QString paramName = ctrl->objectName();
                    if (!ConfigDrivenDefaultParams::getInstance().getDefault(paramName).isEmpty()) {
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

void ConfigDrivenPanel::handleGroupReset(const QString& groupName) {
    if (groups.find(groupName) == groups.end()) return;

    QString groupTitle = groups[groupName].groupBox->title();
    if (!showResetConfirmation(groupTitle)) {
        return;
    }

    resetGroupControls(groups[groupName].controls);
}

void ConfigDrivenPanel::simulateActivity() {
    QPoint globalPos = QCursor::pos();
    QPoint localPos = this->mapFromGlobal(globalPos);

    localPos += QPoint(rand() % 5 - 2, rand() % 5 - 2);
    globalPos += QPoint(rand() % 5 - 2, rand() % 5 - 2);

    QMouseEvent mouseEvent(QEvent::MouseMove, localPos, globalPos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);

    std::cout << "Simulating activity" << std::endl;
    if (!isCommaDevice()) {
        QCoreApplication::sendEvent(this, &mouseEvent);
    }
}

void ConfigDrivenPanel::stopActivitySimulation() {
    std::cout << "Stopping activity simulation | max duration timer stopped" << std::endl;
    activityTimer->stop();
}

void ConfigDrivenPanel::resetMaxDurationTimer() {
    QTimer::singleShot(270000, this, &ConfigDrivenPanel::stopActivitySimulation);
}

void ConfigDrivenPanel::refreshPanel() {
    if (isRefreshing) return;
    isRefreshing = true;

    try {
        for (auto& [groupName, groupData] : groups) {
            bool hasVisibleControls = false;

            for (QWidget* ctrl : groupData.controls) {
                if (auto* valueControl = qobject_cast<ConfigDrivenParamValueControl*>(ctrl)) {
                    valueControl->refresh();
                    updateControlWithDefault(ctrl);
                } else if (auto* floatControl = qobject_cast<ConfigDrivenParamValueControlFloat*>(ctrl)) {
                    floatControl->refresh();
                    updateControlWithDefault(ctrl);
                } else if (auto* toggle = qobject_cast<ParamControl*>(ctrl)) {
                    toggle->refresh();
                }

                auto conditionIt = controlConditions.find(ctrl);
                if (conditionIt != controlConditions.end() && conditionIt->second.hasConditions) {
                    bool shouldBeEnabled = validateCompositeConditions(conditionIt->second.conditions);
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

            groupData.groupBox->style()->unpolish(groupData.groupBox);
            groupData.groupBox->style()->polish(groupData.groupBox);
            groupData.groupBox->update();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error during refresh: " << e.what() << std::endl;
    }

    isRefreshing = false;
}

void ConfigDrivenPanel::updateConditionsForAllControls() {
    for (const auto& [ctrl, conditions] : controlConditions) {
        if (conditions.hasConditions && ctrl) {
            bool shouldBeEnabled = validateCompositeConditions(conditions.conditions);
            bool currentlyEnabled = ctrl->isEnabled();

            if (currentlyEnabled != shouldBeEnabled) {
                std::cout << "Control " << ctrl->objectName().toStdString()
                         << " enabled: " << currentlyEnabled
                         << " -> " << shouldBeEnabled << std::endl;

                ctrl->setEnabled(shouldBeEnabled);
                ctrl->setProperty("enabled", QVariant(shouldBeEnabled));
                ctrl->update();

                ctrl->style()->unpolish(ctrl);
                ctrl->style()->polish(ctrl);
                ctrl->update();

                QList<QWidget*> children = ctrl->findChildren<QWidget*>();
                for (QWidget* child : children) {
                    child->update();
                    child->style()->unpolish(child);
                    child->style()->polish(child);
                    child->update();
                }
            }
        }
    }
    updateGroupVisibility();
}

void ConfigDrivenPanel::showEvent(QShowEvent *event) {
    std::cout << "Showing ConfigDrivenPanel" << std::endl;
    QWidget::showEvent(event);

    refreshPanel();
    updateToggles();
    updateConditionsForAllControls();
    updateGroupVisibility();

    for (const auto& [groupName, groupData] : groups) {
        updateResetButtonVisibility(groupData.groupBox);
    }

    refreshTimer.start();
    activityTimer->start();
    resetMaxDurationTimer();
}

void ConfigDrivenPanel::hideEvent(QHideEvent *event) {
    std::cout << "Hiding ConfigDrivenPanel" << std::endl;
    refreshTimer.stop();
    activityTimer->stop();
    QWidget::hideEvent(event);
}

void ConfigDrivenPanel::onControlValueChanged() {
    std::cout << "Control value changed" << std::endl;
    refreshPanel();
    updateConditionsForAllControls();
    updateGroupVisibility();
}

void ConfigDrivenPanel::updateToggles() {
    for (auto &[param, toggle] : toggles) {
        toggle->refresh();
    }

    for (auto &[groupName, groupData] : groups) {
        for (QWidget* ctrl : groupData.controls) {
            if (auto* valueControl = qobject_cast<ConfigDrivenParamValueControl*>(ctrl)) {
                valueControl->refresh();
                updateControlWithDefault(ctrl);
            } else if (auto* floatControl = qobject_cast<ConfigDrivenParamValueControlFloat*>(ctrl)) {
                floatControl->refresh();
                updateControlWithDefault(ctrl);
            }
        }
        updateResetButtonVisibility(groupData.groupBox);
    }
}

bool ConfigDrivenPanel::showResetConfirmation(const QString& tuningType) {
    QString msg = tr("Are you sure you want to reset %1 to default values?").arg(tuningType);
    auto confirm = new ConfirmationDialog(msg, tr("Yes"), tr("No"), false, this);
    bool ret = confirm->exec();
    delete confirm;
    return ret;
}

bool ConfigDrivenPanel::validateControlBasics(const QJsonObject& control) {
    if (control.contains("OnlyOnCommaDevice") && control["OnlyOnCommaDevice"].toBool()) {
        if (!isCommaDevice()) {
            std::cout << "Control is only available on Comma devices" <<  " | Type: " << control["type"].toString().toStdString() << " | Param:" << control["param"].toString().toStdString() << std::endl;
            return false;
        }
    }

    if (!control.contains("type") || !control.contains("title")) {
        std::cerr << "Control missing required type or title field" << " | Type: " << control["type"].toString().toStdString() << " | Param:" << control["param"].toString().toStdString() << std::endl;
        return false;
    }

    QString type = control["type"].toString();
    if (type != "file_viewer" && type != "command_button" && type != "param_list_viewer" && !control.contains("param")) {
        std::cerr << "Control missing required param field for type: " << type.toStdString() << " | Param:" << control["param"].toString().toStdString() << std::endl;
        return false;
    }

    QStringList supportedTypes = {
        "toggle", "float", "integer", "selection",
        "param_viewer", "file_viewer", "command_button",
        "param_list_viewer", "segmented_control"
    };
    if (!supportedTypes.contains(type)) {
        std::cerr << "Unsupported control type: " << type.toStdString() << " | Param:" << control["param"].toString().toStdString() << std::endl;
        return false;
    }

    if (type == "float" || type == "integer") {
        if (!control.contains("min") || !control.contains("max")) {
            std::cerr << "Numeric control missing min/max values" << " | Type: " << control["type"].toString().toStdString() << " | Param:" << control["param"].toString().toStdString() << std::endl;
            return false;
        }
        if (type == "float" && (!control.contains("increment") || !control.contains("division"))) {
            std::cerr << "Float control missing increment or division values" << " | Type: " << control["type"].toString().toStdString() << " | Param:" << control["param"].toString().toStdString() << std::endl;
            return false;
        }
        if (type == "integer" && !control.contains("increment")) {
            std::cerr << "Integer control missing increment value | Param: " << control["param"].toString().toStdString() << std::endl;
            return false;
        }
    } else if (type == "selection") {
        if (!control.contains("options") || !control["options"].isArray()) {
            std::cerr << "Selection control missing options array | Param: " << control["param"].toString().toStdString() << std::endl;
            return false;
        }
    } else if (type == "file_viewer") {
        if (!control.contains("path")) {
            std::cerr << "File viewer control missing path" << " | Type: " << control["type"].toString().toStdString() << " | Param:" << control["param"].toString().toStdString() << std::endl;
            return false;
        }
    } else if (type == "command_button") {
        if (!control.contains("command")) {
            std::cerr << "Command button control missing command" << " | Type: " << control["type"].toString().toStdString() << " | Param:" << control["param"].toString().toStdString() << std::endl;
            return false;
        }
    }

    return true;
}

bool ConfigDrivenPanel::validateSingleCondition(const QString& conditionType, const QJsonValue& condition) {
    if (conditionType == "paramValueEquals") {
        QJsonObject equals = condition.toObject();
        for (auto it = equals.begin(); it != equals.end(); ++it) {
            std::string paramVal = params.get(it.key().toStdString());
            if (paramVal.empty()) return false;

            QString expected = it.value().toString();
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
    }
    else if (conditionType == "paramValueGreaterThan") {
        QJsonObject greaterThan = condition.toObject();
        for (auto it = greaterThan.begin(); it != greaterThan.end(); ++it) {
            std::string paramVal = params.get(it.key().toStdString());
            if (paramVal.empty()) return false;
            double paramNum = std::stod(paramVal);
            double compareNum = it.value().toDouble();
            if (paramNum <= compareNum) return false;
        }
    }
    else if (conditionType == "paramValueLessThan") {
        QJsonObject lessThan = condition.toObject();
        for (auto it = lessThan.begin(); it != lessThan.end(); ++it) {
            std::string paramVal = params.get(it.key().toStdString());
            if (paramVal.empty()) return false;
            double paramNum = std::stod(paramVal);
            double compareNum = it.value().toDouble();
            if (paramNum >= compareNum) return false;
        }
    }
    else if (conditionType == "paramValueInRange") {
        QJsonObject range = condition.toObject();
        for (auto it = range.begin(); it != range.end(); ++it) {
            std::string paramVal = params.get(it.key().toStdString());
            if (paramVal.empty()) return false;

            double paramNum = std::stod(paramVal);
            QJsonObject rangeValues = it.value().toObject();
            double min = rangeValues["min"].toDouble();
            double max = rangeValues["max"].toDouble();

            if (paramNum < min || paramNum > max) return false;
        }
    }
    else if (conditionType == "git_remote") {
        QJsonArray remotes = condition.toArray();
        std::vector<std::string> searchStrs;
        for (const auto& remote : remotes) {
            searchStrs.push_back(remote.toString().toStdString());
        }
        return isGitRemoteValid(searchStrs, {});
    }
    else if (conditionType == "onlyWhenTheseParams") {
        QJsonArray requiredParams = condition.toArray();
        for (const auto& param : requiredParams) {
            if (!params.getBool(param.toString().toStdString())) {
                return false;
            }
        }
    }
    return true;
}

bool ConfigDrivenPanel::validateConditionObject(const QJsonObject& conditionObj) {
    for (auto it = conditionObj.begin(); it != conditionObj.end(); ++it) {
        if (!validateSingleCondition(it.key(), it.value())) {
            return false;
        }
    }
    return true;
}

bool ConfigDrivenPanel::validateCompositeConditions(const QJsonObject& conditions) {
    bool result = true;

    if (conditions.contains("anyConditionsTrue")) {
        QJsonArray anyConditions = conditions["anyConditionsTrue"].toArray();
        if (!anyConditions.empty()) {
            bool anyTrue = false;
            for (const auto& condition : anyConditions) {
                if (validateConditionObject(condition.toObject())) {
                    anyTrue = true;
                    break;
                }
            }
            result &= anyTrue;
        }
    }

    if (conditions.contains("allConditionsTrue")) {
        QJsonArray allConditions = conditions["allConditionsTrue"].toArray();
        for (const auto& condition : allConditions) {
            if (!validateConditionObject(condition.toObject())) {
                result = false;
                break;
            }
        }
    }

    return result;
}

bool ConfigDrivenPanel::isGitRemoteValid(const std::vector<std::string>& searchStrs,
                                       const std::vector<std::string>& branchNames) {
    std::string gitRemote = params.get("GitRemote");
    std::string gitBranch = params.get("GitBranch");

    bool debugMode = gitRemote.empty();

    if (debugMode || searchStrs.empty() ||
        std::find(searchStrs.begin(), searchStrs.end(), "any") != searchStrs.end()) {
        return true;
    }

    if (gitRemote.empty()) {
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
            return false;
        }
    }

    return true;
}

void ConfigDrivenPanel::resetGroupControls(const std::vector<QWidget*>& controls) {
    ConfigDrivenDefaultParams& defaults = ConfigDrivenDefaultParams::getInstance();
    for (QWidget* ctrl : controls) {
        QString paramName = ctrl->objectName();
        QString defaultValue = defaults.getDefault(paramName);
        if (!defaultValue.isEmpty()) {
            params.put(paramName.toStdString(), defaultValue.toStdString());
        }
    }
    updateToggles();
}

void ConfigDrivenPanel::resetControlTitle(QWidget* control) {
    if (!control) return;
    if (auto* valueControl = qobject_cast<ConfigDrivenParamValueControl*>(control)) {
        valueControl->setDefaultValue("");
    } else if (auto* floatControl = qobject_cast<ConfigDrivenParamValueControlFloat*>(control)) {
        floatControl->setDefaultValue("");
    }
}

QString ConfigDrivenPanel::getProjectRootPath() {
    QString appPath = QCoreApplication::applicationDirPath();
    QDir dir(appPath);
    dir.cdUp();
    dir.cdUp();
    return dir.absolutePath();
}

void ConfigDrivenPanel::showFullScreenDialog(const QString& title, const QString& content) {
    auto dialog = new ConfigDrivenFullScreenDialog(this);
    dialog->setupContent(title, content);
    dialog->setupFullscreen();
    dialog->exec();
}

void ConfigDrivenPanel::executeCommand(const QString& command, const QString& title,
                                     const QString& workingDir, const QJsonArray& actionButtons) {
    auto dialog = new ConfigDrivenCommandDialog(this);
    dialog->executeCommand(command, title, workingDir, actionButtons);
}
