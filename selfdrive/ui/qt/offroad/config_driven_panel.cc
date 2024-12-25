// selfdrive/ui/qt/offroad/config_driven_panel.cc
#include <filesystem>
#include <iostream>
#include <QTimer>
#include <QMouseEvent>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScreen>
#include <QGuiApplication>
#include <QTextList>
#include <QTextEdit>
#include <QFile>
#include <iostream>

#include "selfdrive/ui/ui.h"
#include "selfdrive/ui/qt/offroad/config_driven_panel.h"
#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/widgets/scrollview.h"
#include "common/params.h"

ConfigDrivenPanel::ConfigDrivenPanel(SettingsWindow *parent, const QString &configPath) : ConfigDrivenListWidget(parent) {
    setSpacing(50);

    // Initialize timers
    refreshTimer.setSingleShot(true);
    refreshTimer.setInterval(100);
    connect(&refreshTimer, &QTimer::timeout, this, [this]() {
        if (!isRefreshing) {
            refreshPanel();
        }
    });

    activityTimer = new QTimer(this);
    activityTimer->setInterval(9000); // 9 seconds
    connect(activityTimer, &QTimer::timeout, this, &ConfigDrivenPanel::simulateActivity);

    // Set up the max duration timer
    QTimer::singleShot(270000, this, &ConfigDrivenPanel::stopActivitySimulation); // 4 minutes and 30 seconds

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

QGroupBox *ConfigDrivenPanel::createStyledGroupBox(const QString &title) {
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

QPushButton* ConfigDrivenPanel::createResetButton() {
    QPushButton* resetButton = new QPushButton();
    resetButton->setObjectName("resetButton");
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
    resetButton->setLayoutDirection(Qt::LeftToRight);
    resetButton->setFixedSize(200, 60);

    std::cout << "Reset button created with icon" << std::endl;
    return resetButton;
}

void ConfigDrivenPanel::createGroup(const QJsonObject& group) {
    QString groupName = group["groupName"].toString();
    QString title = group["title"].toString();
    bool enableReset = group["enableResetButton"].toBool();

    QGroupBox* groupBox = createStyledGroupBox(title);
    QVBoxLayout* layout = new QVBoxLayout(groupBox);

    // Initialize group data
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
                        label->setStyleSheet("font-size: 40px; color: yellow; padding-left: 0px;");
                        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
                        break;
                    }
                }
            }
            layout->addWidget(widget);
            groupData.controls.push_back(widget);
        }
    }

    // Only add the group if it has visible controls
    if (hasVisibleControls) {
        groups[groupName] = groupData;
        addItem(groupBox);
    } else {
        delete groupBox;  // Clean up if no visible controls
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
        std::cout << "Control failed basic validation" << std::endl;
        return nullptr;
    }

    bool hidden = control["hidden"].toBool();
    if (hidden) {
        std::cout << "Control is hidden" << std::endl;
        return nullptr;
    }

    QString type = control["type"].toString();
    QString param = control["param"].toString();
    QString title = control["title"].toString();
    QString desc = control["desc"].toString();

    // Create the widget based on type
    QWidget* widget = nullptr;

    if (type == "toggle") {
        auto toggle = ConfigDrivenControlFactory::createToggleControl(param, title, desc, "");
        toggle->setObjectName(param);
        toggle->setVisible(!hidden);
        toggles[param.toStdString()] = toggle;
        widget = toggle;
    }
    else if (type == "float") {
        float min = control["min"].toDouble();
        float max = control["max"].toDouble();
        float increment = control["increment"].toDouble();
        float division = control["division"].toDouble();

        auto ctrl = ConfigDrivenControlFactory::createFloatControl(
            param, title, desc,
            min, max, increment, false, "", {}, division);
        ctrl->setObjectName(param);
        ctrl->setVisible(!hidden);
        widget = ctrl;
    }
    else if (type == "integer") {
        int min = control["min"].toInt();
        int max = control["max"].toInt();
        int increment = control["increment"].toInt();

        auto ctrl = ConfigDrivenControlFactory::createIntegerControl(
            param, title, desc,
            min, max, increment, false);
        ctrl->setObjectName(param);
        ctrl->setVisible(!hidden);
        widget = ctrl;
    }
    else if (type == "selection") {
        auto button = new ButtonControl(title, tr("SELECT"), desc);
        button->setObjectName(param);
        button->setVisible(!hidden);
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

            // Convert value back to display name
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

        // Set initial value
        QString currentValue = QString::fromStdString(params.get(param.toStdString()));
        for (const auto& [name, value] : selections) {
            if (value == currentValue) {
                button->setValue(name);
                break;
            }
        }

        widget = button;
    }
    else if (type == "param_viewer") {
        auto dataBtn = new ButtonControl(title, tr("VIEW"), desc);
        dataBtn->setVisible(!hidden);

        QObject::connect(dataBtn, &ButtonControl::clicked, [this, param, title]() {
            QString rawValue = QString::fromStdString(params.get(param.toStdString()));
            QString content;

            if (rawValue.isEmpty()) {
                content = "<pre style='white-space: pre-wrap; margin: 0; padding: 0; background-color: transparent;'>" +
                        tr("No data available for this parameter.") +
                        "</pre>";
            } else {
                content = "<pre style='white-space: pre-wrap; margin: 0; padding: 0; background-color: transparent;'>" +
                        rawValue.toHtmlEscaped() +
                        "</pre>";
            }

            QString dialogTitle = title + " | " + param;
            showFullScreenDialog(dialogTitle, content);
        });

        widget = dataBtn;
    }
    else if (type == "file_viewer") {
        QString relativePath = control["path"].toString();
        QString header = control["header"].toString();
        auto dataBtn = new ButtonControl(title, tr("VIEW"), desc);
        dataBtn->setVisible(!hidden);

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

        widget = dataBtn;
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
        cmdBtn->setVisible(!hidden);

        QObject::connect(cmdBtn, &ButtonControl::clicked, [this, command, title, workingDir, confirmText, confirmYesText, confirmNoText, requireConfirm, actionButtons]() {
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

        widget = cmdBtn;
    }

    // If widget was created successfully, cache its conditions
    if (widget && control.contains("conditions")) {
        ControlConditions conditions;
        conditions.conditions = control["conditions"].toObject();
        conditions.hasConditions = true;
        controlConditions[widget] = conditions;
        widget->setObjectName(param); // Ensure the widget has its param as object name

        // Log the control being created
        // std::cout << "Created control with param: " << param.toStdString()
        //          << " and conditions: " << conditions.conditions.isEmpty() << std::endl;
    }

    // Check initial visibility based on conditions
    if (widget && control.contains("conditions")) {
        QJsonObject conditions = control["conditions"].toObject();
        bool shouldBeVisible = validateCompositeConditions(conditions);
        widget->setVisible(shouldBeVisible);

        // std::cout << "Initial visibility for " << param.toStdString()
        //          << ": " << shouldBeVisible << std::endl;
    }

    // Connect value change signals
    if (widget) {
        if (auto* paramControl = qobject_cast<ParamControl*>(widget)) {
            connect(paramControl, &ParamControl::toggleFlipped,
                    this, &ConfigDrivenPanel::onControlValueChanged);
        }
        else if (auto* valueControl = qobject_cast<ConfigDrivenParamValueControl*>(widget)) {
            connect(valueControl, &ConfigDrivenParamValueControl::valueChanged,
                    this, &ConfigDrivenPanel::onControlValueChanged);
        }
        else if (auto* floatControl = qobject_cast<ConfigDrivenParamValueControlFloat*>(widget)) {
            connect(floatControl, &ConfigDrivenParamValueControlFloat::valueChanged,
                    this, &ConfigDrivenPanel::onControlValueChanged);
        }
        else if (auto* buttonControl = qobject_cast<ButtonControl*>(widget)) {
            connect(buttonControl, &ButtonControl::clicked,
                    this, &ConfigDrivenPanel::onControlValueChanged);
        }
    }

    return widget;
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
        // Find the corresponding GroupData for this QGroupBox
        for (const auto& [groupName, groupData] : groups) {
            if (groupData.groupBox == group) {
                // Check controls in this group for defaults
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
    // Create a mouse move event at the current cursor position
    QPoint globalPos = QCursor::pos();
    QPoint localPos = this->mapFromGlobal(globalPos);

    // Add small random movement to simulate real activity
    localPos += QPoint(rand() % 5 - 2, rand() % 5 - 2);
    globalPos += QPoint(rand() % 5 - 2, rand() % 5 - 2);

    QMouseEvent mouseEvent(QEvent::MouseMove, localPos, globalPos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);

    std::cout << "Simulating activity" << std::endl;
    // Send the event to this widget
    QCoreApplication::sendEvent(this, &mouseEvent);
}

void ConfigDrivenPanel::stopActivitySimulation() {
    std::cout << "Stopping activity simulation | max duration timer stopped" << std::endl;
    activityTimer->stop();
}

void ConfigDrivenPanel::resetMaxDurationTimer() {
    // Reset the max duration timer
    QTimer::singleShot(270000, this, &ConfigDrivenPanel::stopActivitySimulation); // 4 minutes and 30 seconds
}

void ConfigDrivenPanel::refreshPanel() {
    if (isRefreshing) return;
    isRefreshing = true;

    try {
        // Update all controls and evaluate their conditions
        for (auto& [groupName, groupData] : groups) {
            bool hasVisibleControls = false;

            for (QWidget* ctrl : groupData.controls) {
                // First refresh the control's value
                if (auto* valueControl = qobject_cast<ConfigDrivenParamValueControl*>(ctrl)) {
                    valueControl->refresh();
                    updateControlWithDefault(ctrl);
                } else if (auto* floatControl = qobject_cast<ConfigDrivenParamValueControlFloat*>(ctrl)) {
                    floatControl->refresh();
                    updateControlWithDefault(ctrl);
                } else if (auto* toggle = qobject_cast<ParamControl*>(ctrl)) {
                    toggle->refresh();
                }

                // Check visibility conditions
                bool shouldBeVisible = true;
                auto conditionIt = controlConditions.find(ctrl);
                if (conditionIt != controlConditions.end() && conditionIt->second.hasConditions) {
                    shouldBeVisible = validateCompositeConditions(conditionIt->second.conditions);
                }

                ctrl->setVisible(shouldBeVisible);
                if (shouldBeVisible) {
                    hasVisibleControls = true;
                }
            }

            // Update group visibility based on visible controls
            groupData.groupBox->setVisible(hasVisibleControls);

            if (groupData.groupBox->isVisible()) {
                updateResetButtonVisibility(groupData.groupBox);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error during refresh: " << e.what() << std::endl;
    }

    isRefreshing = false;
}

void ConfigDrivenPanel::onControlValueChanged() {
    if (!refreshTimer.isActive()) {
        refreshTimer.start();
    }
}

void ConfigDrivenPanel::showEvent(QShowEvent *event) {
    std::cout << "Showing ConfigDrivenPanel" << std::endl;
    updateToggles();
    updateGroupVisibility();

    // Update reset buttons visibility for all groups
    for (const auto& [groupName, groupData] : groups) {
        updateResetButtonVisibility(groupData.groupBox);
    }

    refreshPanel();
    activityTimer->start();
    resetMaxDurationTimer();
    QWidget::showEvent(event);
}

void ConfigDrivenPanel::hideEvent(QHideEvent *event) {
    // Stop the timer when the panel is hidden
    std::cout << "Hiding ConfigDrivenPanel" << std::endl;
    activityTimer->stop();
    std::cout << "Activity timer stopped" << std::endl;
    QWidget::hideEvent(event);
}

void ConfigDrivenPanel::updateToggles() {
    // Update toggle controls
    for (auto &[param, toggle] : toggles) {
        toggle->refresh();
    }

    // Update all groups' controls
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

bool ConfigDrivenConfirmationDialog::toggle(const QString &prompt_text, const QString &confirm_text, QWidget *parent) {
    ConfirmationDialog d = ConfirmationDialog(prompt_text, confirm_text, tr("Reboot Later"), false, parent);
    return d.exec();
}

bool ConfigDrivenConfirmationDialog::toggleAlert(const QString &prompt_text, const QString &button_text, QWidget *parent) {
    ConfirmationDialog d = ConfirmationDialog(prompt_text, button_text, "", false, parent);
    return d.exec();
}

bool ConfigDrivenConfirmationDialog::yesorno(const QString &prompt_text, QWidget *parent) {
    ConfirmationDialog d = ConfirmationDialog(prompt_text, tr("Yes"), tr("No"), false, parent);
    return d.exec();
}

ConfigDrivenButtonIconControl::ConfigDrivenButtonIconControl(const QString &title, const QString &text, const QString &desc, const QString &icon, QWidget *parent) : AbstractControl(title, desc, icon, parent) {
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
    QObject::connect(&btn, &QPushButton::clicked, this, &ConfigDrivenButtonIconControl::clicked);
    hlayout->addWidget(&btn);
}

ConfigDrivenParamValueControl* ConfigDrivenControlFactory::createIntegerControl(
    const QString &param, const QString &title, const QString &desc,
    int minValue, int maxValue, int increment, bool loop,
    const QString &label, const std::map<int, QString> &valueLabels) {
    return new ConfigDrivenParamValueControl(param, title, desc, "", minValue, maxValue, valueLabels, nullptr, loop, label, increment);
}

ConfigDrivenParamValueControlFloat* ConfigDrivenControlFactory::createFloatControl(
    const QString &param, const QString &title, const QString &desc,
    float minValue, float maxValue, float increment, bool loop,
    const QString &label, const std::map<int, QString> &valueLabels, float division) {
    return new ConfigDrivenParamValueControlFloat(param, title, desc, "", minValue, maxValue,
                                                valueLabels, nullptr, loop, label, division);
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
            std::cout << "Control is only available on Comma devices" << std::endl;
            return false;
        }
    }

    // Check if required basic fields are present
    if (!control.contains("type") || !control.contains("title")) {
        std::cerr << "Control missing required type or title field" << std::endl;
        return false;
    }

    // Param is required for most control types except file_viewer and command_button
    QString type = control["type"].toString();
    if (type != "file_viewer" && type != "command_button" && !control.contains("param")) {
        std::cerr << "Control missing required param field for type: " << type.toStdString() << std::endl;
        return false;
    }

    // Verify type is supported
    QStringList supportedTypes = {
        "toggle", "float", "integer", "selection",
        "param_viewer", "file_viewer", "command_button"
    };
    if (!supportedTypes.contains(type)) {
        std::cerr << "Unsupported control type: " << type.toStdString() << std::endl;
        return false;
    }

    // Type-specific validation
    if (type == "float" || type == "integer") {
        if (!control.contains("min") || !control.contains("max")) {
            std::cerr << "Numeric control missing min/max values" << std::endl;
            return false;
        }
        if (type == "float" && (!control.contains("increment") || !control.contains("division"))) {
            std::cerr << "Float control missing increment or division values" << std::endl;
            return false;
        }
        if (type == "integer" && !control.contains("increment")) {
            std::cerr << "Integer control missing increment value" << std::endl;
            return false;
        }
    } else if (type == "selection") {
        if (!control.contains("options") || !control["options"].isArray()) {
            std::cerr << "Selection control missing options array" << std::endl;
            return false;
        }
    } else if (type == "file_viewer") {
        if (!control.contains("path")) {
            std::cerr << "File viewer control missing path" << std::endl;
            return false;
        }
    } else if (type == "command_button") {
        if (!control.contains("command")) {
            std::cerr << "Command button control missing command" << std::endl;
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
            if (paramVal != it.value().toString().toStdString()) {
                return false;
            }
        }
    } else if (conditionType == "paramValueGreaterThan") {
        QJsonObject greaterThan = condition.toObject();
        for (auto it = greaterThan.begin(); it != greaterThan.end(); ++it) {
            std::string paramVal = params.get(it.key().toStdString());
            if (paramVal.empty()) return false;
            double paramNum = std::stod(paramVal);
            double compareNum = it.value().toDouble();
            if (paramNum <= compareNum) return false;
        }
    } else if (conditionType == "paramValueLessThan") {
        QJsonObject lessThan = condition.toObject();
        for (auto it = lessThan.begin(); it != lessThan.end(); ++it) {
            std::string paramVal = params.get(it.key().toStdString());
            if (paramVal.empty()) return false;
            double paramNum = std::stod(paramVal);
            double compareNum = it.value().toDouble();
            if (paramNum >= compareNum) return false;
        }
    } else if (conditionType == "git_remote") {
        QJsonArray remotes = condition.toArray();
        std::vector<std::string> searchStrs;
        for (const auto& remote : remotes) {
            searchStrs.push_back(remote.toString().toStdString());
        }
        return isGitRemoteValid(searchStrs, {});
    } else if (conditionType == "onlyWhenTheseParams") {
        QJsonArray requiredParams = condition.toArray();
        for (const auto& param : requiredParams) {
            if (!params.getBool(param.toString().toStdString())) {
                return false;
            }
        }
    }
    // Add other condition type checks as needed

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
    // Check anyConditionsTrue
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
            if (!anyTrue) return false;
        }
    }

    // Check allConditionsTrue
    if (conditions.contains("allConditionsTrue")) {
        QJsonArray allConditions = conditions["allConditionsTrue"].toArray();
        for (const auto& condition : allConditions) {
            if (!validateConditionObject(condition.toObject())) {
                return false;
            }
        }
    }

    return true;
}


bool ConfigDrivenPanel::isGitRemoteValid(const std::vector<std::string>& searchStrs, const std::vector<std::string>& branchNames) {
    std::string gitRemote = params.get("GitRemote");
    std::string gitBranch = params.get("GitBranch");

    // Set debugMode to true to allow all controls to be shown if the GitRemote is empty
    bool debugMode = gitRemote.empty();

    if (debugMode || searchStrs.empty() || std::find(searchStrs.begin(), searchStrs.end(), "any") != searchStrs.end()) {
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
    // Move up two directories from selfdrive/ui
    dir.cdUp();
    dir.cdUp();
    return dir.absolutePath();
}

void ConfigDrivenPanel::showFullScreenDialog(const QString& title, const QString& content) {
    QDialog* dialog = new QDialog(this);
    dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    dialog->setStyleSheet(getDialogStyle());

    // Create main layout first
    QVBoxLayout* main_layout = new QVBoxLayout(dialog);
    main_layout->setContentsMargins(45, 35, 45, 45);
    main_layout->setSpacing(0);

    // Add title
    QLabel* title_label = new QLabel(title);
    title_label->setStyleSheet("font-size: 90px; font-weight: 600; background-color: black;");
    main_layout->addWidget(title_label);
    main_layout->addSpacing(30);

    // Create content widget with its layout
    QWidget* contentWidget = new QWidget(dialog);
    contentWidget->setStyleSheet("background-color: black;");
    QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* text = new QLabel(contentWidget);
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(true);
    text->setText(content);
    text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    text->setStyleSheet("font-size: 35px; font-weight: 200; color: #C9C9C9; background-color:#1B1B1B; padding: 50px 50px;");
    text->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    contentLayout->addWidget(text);
    contentLayout->addStretch();

    // Create scroll view
    ScrollView* scroll = new ScrollView(contentWidget, dialog);
    main_layout->addWidget(scroll);
    main_layout->addSpacing(50);

    // Create close button
    QPushButton* close_btn = new QPushButton(tr("Close"));
    close_btn->setFixedHeight(160); // Ensure button remains visible
    close_btn->setStyleSheet(R"(
        QPushButton {
            background-color: #465BEA;
            font-size: 55px;
            font-weight: 400;
            border-radius: 10px;
            color: white;
        }
        QPushButton:pressed {
            background-color: #3049F4;
        }
        QPushButton:disabled {
            background-color: #4F4F4F;
            color: #888888;
        }
    )");
    main_layout->addWidget(close_btn);

    // Connect close button before showing dialog
    QObject::connect(close_btn, &QPushButton::clicked, dialog, &QDialog::accept);

    // Apply fullscreen settings
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        dialog->setFixedSize(2160, 1080);
    }

    // Show dialog and apply rotation
    dialog->show();
    setupFullscreenDialog(dialog);

    dialog->exec();
}

void ConfigDrivenPanel::executeCommand(const QString& command, const QString& title, const QString& workingDir, const QJsonArray& actionButtons) {
    QProcess* process = new QProcess(this);

    if (!workingDir.isEmpty()) {
        QDir dir(workingDir);
        if (dir.exists()) {
            process->setWorkingDirectory(workingDir);
        } else {
            std::cout << "Warning: Working directory does not exist: " << workingDir.toStdString() << std::endl;
        }
    }

    // Create dialog with proper flags
    QDialog* dialog = new QDialog(this);
    dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    dialog->setStyleSheet(getDialogStyle());

    // Create main layout
    QVBoxLayout* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(45, 35, 45, 45);
    layout->setSpacing(0);

    // Add title
    QLabel* titleLabel = new QLabel(title + " - " + tr("Output"));
    titleLabel->setStyleSheet("font-size: 90px; font-weight: 600; background-color: black;");
    layout->addWidget(titleLabel);
    layout->addSpacing(30);

    // Create output text area
    QTextEdit* outputText = new QTextEdit(dialog);
    outputText->setReadOnly(true);
    outputText->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::LinksAccessibleByMouse);
    outputText->setStyleSheet(R"(
        QTextEdit {
            font-family: monospace;
            font-size: 35px;
            font-weight: 200;
            color: #C9C9C9;
            background-color: #1B1B1B;
            padding: 50px;
            border: none;
        }
        QTextEdit QScrollBar:vertical {
            width: 20px;
            background: #1B1B1B;
            margin: 0px;
        }
        QTextEdit QScrollBar::handle:vertical {
            background-color: white;
            min-height: 30px;
            border-radius: 5px;
            margin: 2px;
            width: 16px;
        }
        QTextEdit QScrollBar::add-line:vertical,
        QTextEdit QScrollBar::sub-line:vertical {
            height: 0px;
            background: none;
        }
        QTextEdit QScrollBar::add-page:vertical,
        QTextEdit QScrollBar::sub-page:vertical {
            background: none;
        }
    )");
    outputText->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outputText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    outputText->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
    layout->addWidget(outputText);

    // Create button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(20);

    // Add kill button
    QPushButton* killButton = new QPushButton(tr("Stop Command"), dialog);
    killButton->setFixedHeight(160);
    killButton->setStyleSheet(R"(
        QPushButton {
            background-color: #EA4646;
            font-size: 55px;
            font-weight: 400;
            border-radius: 10px;
            color: white;
        }
        QPushButton:pressed {
            background-color: #F43030;
        }
    )");
    buttonLayout->addWidget(killButton);

    // Add action buttons if any, limiting to maximum of 3
    int maxActionButtons = 3;
    int addedButtons = 0;

    for (const auto& buttonValue : actionButtons) {
        if (addedButtons >= maxActionButtons) {
            std::cout << "Warning: Exceeded maximum of " << maxActionButtons << " action buttons. Skipping remaining buttons." << std::endl;
            break;
        }
        QJsonObject buttonObj = buttonValue.toObject();
        QString buttonText = buttonObj["text"].toString();
        QString buttonCommand = buttonObj["command"].toString();
        bool buttonConfirm = buttonObj["confirm"].toBool();
        QString buttonConfirmText = buttonObj["confirm_text"].toString();
        QString buttonConfirmYesText = buttonObj["confirm_yes_text"].toString();
        QString buttonConfirmNoText = buttonObj["confirm_no_text"].toString();

        QPushButton* actionButton = new QPushButton(buttonText, dialog);
        actionButton->setFixedHeight(160);

        // Apply custom style if provided
        if (buttonObj.contains("style")) {
            QJsonObject style = buttonObj["style"].toObject();
            QString backgroundColor = style["background_color"].toString().isEmpty() ? "#465BEA" : style["background_color"].toString();
            QString pressedColor = style["pressed_color"].toString().isEmpty() ? "#3049F4" : style["pressed_color"].toString();
            QString disabledColor = style["disabled_color"].toString().isEmpty() ? "#4F4F4F" : style["disabled_color"].toString();
            QString textColor = style["text_color"].toString().isEmpty() ? "white" : style["text_color"].toString();

            QString buttonStyle = QString(R"(
                QPushButton {
                    background-color: %1;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 10px;
                    color: %2;
                }
                QPushButton:pressed {
                    background-color: %3;
                }
                QPushButton:disabled {
                    background-color: %4;
                    color: %5;
                }
            )").arg(backgroundColor, textColor, pressedColor, disabledColor, textColor);

            actionButton->setStyleSheet(buttonStyle);
        }

        connect(actionButton, &QPushButton::clicked, [=]() {
            if (buttonConfirm) {
                auto confirm = new ConfirmationDialog(
                    buttonConfirmText.isEmpty() ? tr("Are you sure?") : buttonConfirmText,
                    buttonConfirmYesText.isEmpty() ? tr("Yes") : buttonConfirmYesText,
                    buttonConfirmNoText.isEmpty() ? tr("No") : buttonConfirmNoText,
                    false, dialog);
                bool confirmed = confirm->exec();
                delete confirm;
                if (!confirmed) {
                    return;
                }
            }
            QProcess::execute("/bin/bash", QStringList() << "-c" << buttonCommand);
        });

        buttonLayout->addWidget(actionButton);
    }

    // Add close button
    QPushButton* closeButton = new QPushButton(tr("Command is Running..."), dialog);
    closeButton->setEnabled(false);
    closeButton->setFixedHeight(160);
    closeButton->setStyleSheet(R"(
        QPushButton {
            background-color: #465BEA;
            font-size: 55px;
            font-weight: 400;
            border-radius: 10px;
            color: white;
        }
        QPushButton:pressed {
            background-color: #3049F4;
        }
        QPushButton:disabled {
            background-color: #4F4F4F;
            color: #888888;
        }
    )");
    buttonLayout->addWidget(closeButton);

    layout->addSpacing(50);
    layout->addLayout(buttonLayout);

    // Connect kill button
    connect(killButton, &QPushButton::clicked, [=]() {
        if (process->state() != QProcess::NotRunning) {
            outputText->append("\n<span style='color: #ff7c30;'>Process terminated by user</span>");
            process->kill();
            killButton->hide();
            closeButton->setEnabled(true);
            closeButton->setText(tr("Close (Terminated)"));
            closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #EA4646;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 10px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #F43030;
                }
            )");
        }
    });

    // Connect process signals
    connect(process, &QProcess::readyReadStandardOutput, [=]() {
        QString output = QString::fromUtf8(process->readAllStandardOutput());
        outputText->append(output);
    });

    connect(process, &QProcess::readyReadStandardError, [=]() {
        QString error = QString::fromUtf8(process->readAllStandardError());
        outputText->append("<span style='color: #ff7c30;'>" + error.toHtmlEscaped() + "</span>");
    });

    // Handle process completion
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [=](int exitCode, QProcess::ExitStatus exitStatus) {
        closeButton->setEnabled(true);
        killButton->hide();

        if (exitStatus == QProcess::CrashExit) {
            // Do nothing as the kill button handler already set the styling
        } else if (exitCode != 0) {
            outputText->append(QString("\n<span style='color: #ff7c30;'>Command failed with exit code: %1</span>").arg(exitCode));
            closeButton->setText(tr("Close (Command Failed | Exit code: %1)").arg(exitCode));
            closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #EA4646;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 10px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #F43030;
                }
            )");
        } else {
            outputText->append("\n<span style='color: #50d332;'>Command completed successfully</span>");
            closeButton->setText(tr("Close (Completed Successfully)"));
            closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #33Ab4C;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 10px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #2A9040;
                }
            )");
        }

        connect(dialog, &QDialog::finished, process, &QProcess::deleteLater);
    });

    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);

    // Set dialog size and show
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        dialog->setFixedSize(2160, 1080);
    }
    dialog->show();
    setupFullscreenDialog(dialog);

    // Start the process
    std::cout << "Executing command: " << command.toStdString() << std::endl;
    process->start("/bin/bash", QStringList() << "-c" << command);

    dialog->exec();
}
