// config_driven_controls.cc
#include "config_driven_panel_controls.h"
#include "common/params.h"
#include <QTimer>

ConfigDrivenButtonIconControl::ConfigDrivenButtonIconControl(const QString &title, const QString &text, const QString &desc, const QString &icon, QWidget *parent) : AbstractControl(title, desc, icon, parent) {
    btn.setText(text);
    btn.setFixedSize(250, 100);
    QObject::connect(&btn, &QPushButton::clicked, this, &ConfigDrivenButtonIconControl::clicked);
    hlayout->addWidget(&btn);
}

ConfigDrivenParamManageControl::ConfigDrivenParamManageControl(const QString &param, const QString &title, const QString &desc, const QString &icon, QWidget *parent)
    : ParamControl(param, title, desc, icon, parent),
      key(param.toStdString()),
      manageButton(new ButtonControl(tr(""), tr("MANAGE"), tr(""))) {
    hlayout->insertWidget(hlayout->indexOf(&toggle) - 1, manageButton);

    connect(this, &ToggleControl::toggleFlipped, this, [this](bool state) {
      refresh();
    });

    connect(manageButton, &ButtonControl::clicked, this, &ConfigDrivenParamManageControl::manageButtonClicked);
}

void ConfigDrivenParamManageControl::refresh() {
    ParamControl::refresh();
    manageButton->setVisible(params.getBool(key));
}

void ConfigDrivenParamManageControl::showEvent(QShowEvent *event) {
    ParamControl::showEvent(event);
    refresh();
}

ConfigDrivenParamValueControl::ConfigDrivenParamValueControl(const QString &param, const QString &title, const QString &desc, const QString &icon,
                    const int &minValue, const int &maxValue, const std::map<int, QString> &valueLabels,
                    QWidget *parent, const bool &loop, const QString &label, const int &division)
    : ParamControl(param, title, desc, icon, parent),
      minValue(minValue), maxValue(maxValue), valueLabelMappings(valueLabels), loop(loop), labelText(label), division(division) {
        key = param.toStdString();

        defaultValueLabel = new QLabel(this);
        defaultValueLabel->setAlignment(Qt::AlignLeft);
        hlayout->addWidget(defaultValueLabel);

        QLabel *minLabel = new QLabel("Min: " + QString::number(minValue), this);
        QLabel *maxLabel = new QLabel("Max: " + QString::number(maxValue), this);
        QString minMaxLabelStyle = R"(
          QLabel {
            color: %1;
            font-size: 30px;
          }
          QLabel:disabled {
            color: #777777;
          }
        )";
        minLabel->setStyleSheet(minMaxLabelStyle.arg("#ff7c30"));
        maxLabel->setStyleSheet(minMaxLabelStyle.arg("#50d332"));
        minLabel->setAlignment(Qt::AlignCenter);
        maxLabel->setAlignment(Qt::AlignCenter);

        valueLabel = new QLabel(this);
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setFixedSize(190, 130);
        valueLabel->setStyleSheet(R"(
          QLabel {
            color: #3f9fff;
            font-size: 50px;
            background-color: #393939;
            border: none;
          }
          QLabel:disabled {
            color: #777777;
            background-color: #2a2a2a;
          }
        )");

        QPushButton *decrementButton = createButton("-", this, true);
        QPushButton *incrementButton = createButton("+", this, false);

        QVBoxLayout *decrementLayout = new QVBoxLayout();
        decrementLayout->addWidget(decrementButton);
        decrementLayout->addWidget(minLabel);
        decrementLayout->setSpacing(5);
        decrementLayout->setContentsMargins(0, 0, 0, 0);

        QVBoxLayout *incrementLayout = new QVBoxLayout();
        incrementLayout->addWidget(incrementButton);
        incrementLayout->addWidget(maxLabel);
        incrementLayout->setSpacing(5);
        incrementLayout->setContentsMargins(0, 0, 0, 0);

        QHBoxLayout *controlLayout = new QHBoxLayout;
        controlLayout->setSpacing(0);
        controlLayout->setContentsMargins(0, 0, 0, 0);
        controlLayout->addLayout(decrementLayout);
        controlLayout->addWidget(valueLabel);
        controlLayout->addLayout(incrementLayout);

        QWidget *controlWidget = new QWidget(this);
        controlWidget->setLayout(controlLayout);
        controlWidget->setStyleSheet("QWidget { background-color: #393939; border-radius: 10px; padding: 0 0 0 0; }");
        controlWidget->setFixedSize(524, 130);

        hlayout->addWidget(controlWidget);
        hlayout->setContentsMargins(0, 10, 0, 10);

        connect(decrementButton, &QPushButton::clicked, this, [=]() {
            updateValue(-1);
        });

        connect(incrementButton, &QPushButton::clicked, this, [=]() {
            updateValue(1);
        });

        toggle.hide();
}

void ConfigDrivenParamValueControl::updateValue(int increment) {
    value = value + increment;

    if (loop) {
        if (value < minValue) value = maxValue;
        else if (value > maxValue) value = minValue;
    } else {
        value = std::max(minValue, std::min(maxValue, value));
    }

    params.putInt(key, value);
    emit buttonPressed();
    emit valueChanged(value);
}

void ConfigDrivenParamValueControl::refresh() {
    value = params.getInt(key);

    QString text;
    auto it = valueLabelMappings.find(value);
    if (division > 1) {
        text = QString::number(value / (division * 1.0), 'g');
    } else {
        text = it != valueLabelMappings.end() ? it->second : QString::number(value);
    }
    if (!labelText.isEmpty()) {
        text += labelText;
    }
    valueLabel->setText(text);
    valueLabel->setStyleSheet("QLabel { color: #3f9fff; font-size: 50px; background-color: #393939; border: none; }");
}

void ConfigDrivenParamValueControl::updateControl(int newMinValue, int newMaxValue, const QString &newLabel, int newDivision) {
    minValue = newMinValue;
    maxValue = newMaxValue;
    labelText = newLabel;
    division = newDivision;
}

void ConfigDrivenParamValueControl::setDefaultValue(const QString &defaultValue) {
    if (defaultValue.isEmpty()) {
        defaultValueLabel->clear();
        defaultValueLabel->hide();
    } else {
        defaultValueLabel->setText(tr("Default: %1").arg(defaultValue));
        defaultValueLabel->show();
    }
}

void ConfigDrivenParamValueControl::setEnabled(bool isEnabled) {
    ParamControl::setEnabled(isEnabled);
    valueLabel->setEnabled(isEnabled);
    defaultValueLabel->setEnabled(isEnabled);
    for (QPushButton* btn : findChildren<QPushButton*>()) {
        btn->setEnabled(isEnabled);
    }
    for (QLabel* lbl : findChildren<QLabel*>()) {
        lbl->setEnabled(isEnabled);
    }
}

void ConfigDrivenParamValueControl::showEvent(QShowEvent *event) {
    refresh();
}

QPushButton* ConfigDrivenParamValueControl::createButton(const QString &text, QWidget *parent, bool isLeftButton) {
    QPushButton *button = new QPushButton(text, parent);
    button->setFixedSize(166, 110);
    button->setAutoRepeat(true);
    button->setAutoRepeatInterval(100);

    QString buttonStyle = R"(
      QPushButton {
        border: none;
        font-size: 50px;
        font-weight: 500;
        color: #E4E4E4;
        background-color: #393939;
      }
      QPushButton:pressed {
        background-color: #4a4a4a;
      }
      QPushButton:disabled {
        color: #777777;
        background-color: #2a2a2a;
      }
    )";

    if (isLeftButton) {
        buttonStyle += "QPushButton { border-top-left-radius: 10px; border-bottom-left-radius: 10px; }";
    } else {
        buttonStyle += "QPushButton { border-top-right-radius: 10px; border-bottom-right-radius: 10px; }";
    }

    button->setStyleSheet(buttonStyle);
    return button;
}

ConfigDrivenParamValueControlFloat::ConfigDrivenParamValueControlFloat(const QString &param, const QString &title, const QString &desc, const QString &icon,
                    const float &minValue, const float &maxValue, const std::map<int, QString> &valueLabels,
                    QWidget *parent, const bool &loop, const QString &label, const float &division)
    : ParamControl(param, title, desc, icon, parent),
      minValue(minValue), maxValue(maxValue), valueLabelMappings(valueLabels), loop(loop), labelText(label), division(division) {
        key = param.toStdString();

        defaultValueLabel = new QLabel(this);
        defaultValueLabel->setAlignment(Qt::AlignLeft);
        defaultValueLabel->setWordWrap(true);
        defaultValueLabel->setMaximumWidth(800);  // Adjust width as needed
        defaultValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        hlayout->addWidget(defaultValueLabel);

        for (QLabel* title_label : findChildren<QLabel*>()) {
            if (title_label->text() == title) {
                title_label->setWordWrap(true);
                title_label->setMaximumWidth(800);  // Adjust width as needed
                title_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                break;
            }
        }

        QLabel *minLabel = new QLabel("Min: " + QString::number(minValue, 'f', 2), this);
        QLabel *maxLabel = new QLabel("Max: " + QString::number(maxValue, 'f', 2), this);
        QString minMaxLabelStyle = R"(
          QLabel {
            color: %1;
            font-size: 30px;
          }
          QLabel:disabled {
            color: #777777;
          }
        )";
        minLabel->setStyleSheet(minMaxLabelStyle.arg("#ff7c30"));
        maxLabel->setStyleSheet(minMaxLabelStyle.arg("#50d332"));
        minLabel->setAlignment(Qt::AlignCenter);
        maxLabel->setAlignment(Qt::AlignCenter);

        valueLabel = new QLabel(this);
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setFixedSize(190, 130);
        valueLabel->setStyleSheet(R"(
          QLabel {
            color: #3f9fff;
            font-size: 50px;
            background-color: #393939;
            border: none;
          }
          QLabel:disabled {
            color: #777777;
            background-color: #2a2a2a;
          }
        )");

        QPushButton *decrementButton = createButton("-", this, true);
        QPushButton *incrementButton = createButton("+", this, false);

        QVBoxLayout *decrementLayout = new QVBoxLayout();
        decrementLayout->addWidget(decrementButton);
        decrementLayout->addWidget(minLabel);
        decrementLayout->setSpacing(5);
        decrementLayout->setContentsMargins(0, 0, 0, 0);

        QVBoxLayout *incrementLayout = new QVBoxLayout();
        incrementLayout->addWidget(incrementButton);
        incrementLayout->addWidget(maxLabel);
        incrementLayout->setSpacing(5);
        incrementLayout->setContentsMargins(0, 0, 0, 0);

        QHBoxLayout *controlLayout = new QHBoxLayout;
        controlLayout->setSpacing(0);
        controlLayout->setContentsMargins(0, 0, 0, 0);
        controlLayout->addLayout(decrementLayout);
        controlLayout->addWidget(valueLabel);
        controlLayout->addLayout(incrementLayout);

        QWidget *controlWidget = new QWidget(this);
        controlWidget->setLayout(controlLayout);
        controlWidget->setStyleSheet("QWidget { background-color: #393939; border-radius: 10px; padding: 0 0 0 0; }");
        controlWidget->setFixedSize(524, 130);

        hlayout->addWidget(controlWidget);
        hlayout->setContentsMargins(0, 10, 0, 10);

        connect(decrementButton, &QPushButton::clicked, this, [=]() {
            updateValue(-1.0f);
        });

        connect(incrementButton, &QPushButton::clicked, this, [=]() {
            updateValue(1.0f);
        });

        toggle.hide();
}

void ConfigDrivenParamValueControlFloat::refresh() {
    value = params.getFloat(key);

    QString text;
    auto it = valueLabelMappings.find(static_cast<int>(value * division));
    int decimals = calculateDecimalPlaces(division);
    if (it != valueLabelMappings.end()) {
        text = it->second;
    } else {
        text = QString::number(value, 'f', decimals);
    }
    if (!labelText.isEmpty()) {
        text += labelText;
    }
    valueLabel->setText(text);
    valueLabel->setStyleSheet("QLabel { color: #3f9fff; background-color: #393939; font-size: 50px; border: none; }");
}

void ConfigDrivenParamValueControlFloat::updateValue(float increment) {
    value += increment / division;

    if (loop) {
        if (value < minValue) value = maxValue;
        else if (value > maxValue) value = minValue;
    } else {
        value = std::max(minValue, std::min(maxValue, value));
    }

    int decimals = calculateDecimalPlaces(division);
    value = std::round(value * std::pow(10, decimals)) / std::pow(10, decimals);

    params.putFloat(key, value);
    refresh();
    emit buttonPressed();
    emit valueChanged(value);
}

void ConfigDrivenParamValueControlFloat::updateControl(float newMinValue, float newMaxValue, const QString &newLabel, float newDivision) {
    minValue = newMinValue;
    maxValue = newMaxValue;
    labelText = newLabel;
    division = newDivision;
}

void ConfigDrivenParamValueControlFloat::setDefaultValue(const QString &defaultValue) {
    if (defaultValue.isEmpty()) {
        defaultValueLabel->clear();
        defaultValueLabel->hide();
        // defaultValueLabel->setText(tr("Default: %1").arg("10.0"));
        // defaultValueLabel->show();
    } else {
        defaultValueLabel->clear();
        defaultValueLabel->hide();
        // defaultValueLabel->setText(tr("Default: %1").arg(defaultValue));
        // defaultValueLabel->show();
    }
}

void ConfigDrivenParamValueControlFloat::setEnabled(bool isEnabled) {
    ParamControl::setEnabled(isEnabled);
    valueLabel->setEnabled(isEnabled);
    defaultValueLabel->setEnabled(isEnabled);
    for (QPushButton* btn : findChildren<QPushButton*>()) {
        btn->setEnabled(isEnabled);
    }
    for (QLabel* lbl : findChildren<QLabel*>()) {
        lbl->setEnabled(isEnabled);
    }
}

void ConfigDrivenParamValueControlFloat::showEvent(QShowEvent *event) {
    refresh();
}

int ConfigDrivenParamValueControlFloat::calculateDecimalPlaces(float divisor) {
    if (divisor == 0) return 0;
    return std::max(0, -static_cast<int>(std::floor(std::log10(1.0 / divisor))));
}

QPushButton* ConfigDrivenParamValueControlFloat::createButton(const QString &text, QWidget *parent, bool isLeftButton) {
    QPushButton *button = new QPushButton(text, parent);
    button->setFixedSize(166, 110);
    button->setAutoRepeat(true);
    button->setAutoRepeatInterval(100);

    QString buttonStyle = R"(
      QPushButton {
        border: none;
        font-size: 50px;
        font-weight: 500;
        color: #E4E4E4;
        background-color: #393939;
      }
      QPushButton:pressed {
        background-color: #4a4a4a;
      }
    )";

    if (isLeftButton) {
        buttonStyle += "QPushButton { border-top-left-radius: 10px; border-bottom-left-radius: 10px; }";
    } else {
        buttonStyle += "QPushButton { border-top-right-radius: 10px; border-bottom-right-radius: 10px; }";
    }

    button->setStyleSheet(buttonStyle);
    return button;
}

ConfigDrivenDualParamControl::ConfigDrivenDualParamControl(ParamControl *control1, ParamControl *control2, QWidget *parent, bool split)
    : QFrame(parent) {
    QHBoxLayout *hlayout = new QHBoxLayout(this);

    control1->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    control1->setMaximumWidth(split ? 800 : 700);

    control2->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    control2->setMaximumWidth(800);

    hlayout->addWidget(control1);
    hlayout->addWidget(control2);
}

// Factory Methods Implementation
ConfigDrivenParamValueControl* ConfigDrivenControlFactory::createIntegerControl(
    const QString &param, const QString &title, const QString &desc,
    int minValue, int maxValue, int increment, bool loop,
    const QString &label, const std::map<int, QString> &valueLabels) {

    auto control = new ConfigDrivenParamValueControl(param, title, desc, "", minValue, maxValue, valueLabels, nullptr, loop, label, increment);

    if (QHBoxLayout* mainLayout = control->findChild<QHBoxLayout*>()) {
        mainLayout->setStretch(0, 1);
        mainLayout->setSpacing(50);
    }

    return control;
}

ConfigDrivenParamValueControlFloat* ConfigDrivenControlFactory::createFloatControl(
    const QString &param, const QString &title, const QString &desc,
    float minValue, float maxValue, float increment, bool loop,
    const QString &label, const std::map<int, QString> &valueLabels, float division) {

    return new ConfigDrivenParamValueControlFloat(param, title, desc, "", minValue, maxValue,
                                                valueLabels, nullptr, loop, label, division);
}

ParamControl* ConfigDrivenControlFactory::createToggleControl(const QString &param, const QString &title,
                                                            const QString &desc, const QString &icon) {
    auto toggle = new ParamControl(param, title, desc, icon);

    toggle->setStyleSheet(R"(
        ParamControl {
            background: transparent;
        }
        QFrame {
            background: transparent;
        }
        QHBoxLayout {
            background: transparent;
        }
        QWidget {
            background: transparent;
        }
    )");

    QHBoxLayout* layout = toggle->findChild<QHBoxLayout*>();
    if (layout) {
        layout->setContentsMargins(0, 10, 0, 10);
        layout->setSpacing(50);
        layout->setStretch(0, 1);
    }

    QList<QLabel*> labels = toggle->findChildren<QLabel*>();
    for (QLabel* label : labels) {
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    return toggle;
}

ConfigDrivenParamToggleControl::ConfigDrivenParamToggleControl(const QString &param, const QString &title,
    const QString &desc, const QString &icon, const std::vector<QString> &button_params,
    const std::vector<QString> &button_texts, QWidget *parent, const int minimum_button_width)
    : ParamControl(param, title, desc, icon, parent) {

    connect(this, &ToggleControl::toggleFlipped, this, [this](bool state) {
        refreshButtons(state);
    });

    button_group = new QButtonGroup(this);
    button_group->setExclusive(false);

    std::map<QString, bool> paramState;
    for (const QString &button_param : button_params) {
        paramState[button_param] = params.getBool(button_param.toStdString());
    }

    for (int i = 0; i < button_texts.size(); ++i) {
        QPushButton *button = new QPushButton(button_texts[i], this);
        button->setCheckable(true);
        button->setChecked(paramState[button_params[i]]);
        button->setMinimumWidth(minimum_button_width);
        button_group->addButton(button, i);

        connect(button, &QPushButton::clicked, [this, button_params, i](bool checked) {
            params.putBool(button_params[i].toStdString(), checked);
            button_group->button(i)->setChecked(checked);
            emit buttonClicked(checked);
        });

        hlayout->insertWidget(hlayout->indexOf(&toggle), button);
    }
}

void ConfigDrivenParamToggleControl::refreshButtons(bool state) {
    for (QAbstractButton *button : button_group->buttons()) {
        button->setVisible(state);
    }
}

ConfigDrivenParamValueToggleControl::ConfigDrivenParamValueToggleControl(const QString &param, const QString &title,
    const QString &desc, const QString &icon, const int &minValue, const int &maxValue,
    const std::map<int, QString> &valueLabels, QWidget *parent, const bool &loop, const QString &label,
    const int &division, const std::vector<QString> &button_params, const std::vector<QString> &button_texts,
    const int minimum_button_width)
    : ParamControl(param, title, desc, icon, parent),
      minValue(minValue), maxValue(maxValue), valueLabelMappings(valueLabels), loop(loop),
      labelText(label), division(division) {

    key = param.toStdString();

    const QString style = R"(
        QPushButton {
            border-radius: 10px;
            font-size: 40px;
            font-weight: 500;
            height:100px;
            padding: 0 25 0 25;
            color: #E4E4E4;
            background-color: #393939;
        }
        QPushButton:pressed {
            background-color: #4a4a4a;
        }
        QPushButton:checked:enabled {
            background-color: #33Ab4C;
        }
        QPushButton:disabled {
            color: #33E4E4E4;
        }
    )";

    button_group = new QButtonGroup(this);
    button_group->setExclusive(false);

    std::map<QString, bool> paramState;
    for (const QString &button_param : button_params) {
        paramState[button_param] = params.getBool(button_param.toStdString());
    }

    for (int i = 0; i < button_texts.size(); ++i) {
        QPushButton *button = new QPushButton(button_texts[i], this);
        button->setCheckable(true);
        button->setChecked(paramState[button_params[i]]);
        button->setStyleSheet(style);
        button->setMinimumWidth(minimum_button_width);
        button_group->addButton(button, i);

        connect(button, &QPushButton::clicked, [this, button_params, i](bool checked) {
            params.putBool(button_params[i].toStdString(), checked);
            button_group->button(i)->setChecked(checked);
        });

        hlayout->addWidget(button);
    }

    valueLabel = new QLabel(this);
    hlayout->addWidget(valueLabel);

    QPushButton *decrementButton = createButton("-", this);
    QPushButton *incrementButton = createButton("+", this);

    hlayout->addWidget(decrementButton);
    hlayout->addWidget(incrementButton);

    connect(decrementButton, &QPushButton::clicked, this, [=]() {
        updateValue(-1);
    });

    connect(incrementButton, &QPushButton::clicked, this, [=]() {
        updateValue(1);
    });

    toggle.hide();
}

void ConfigDrivenParamValueToggleControl::updateValue(int increment) {
    value = value + increment;

    if (loop) {
        if (value < minValue) value = maxValue;
        else if (value > maxValue) value = minValue;
    } else {
        value = std::max(minValue, std::min(maxValue, value));
    }

    params.putInt(key, value);
    refresh();
    emit buttonPressed();
    emit valueChanged(value);
}

void ConfigDrivenParamValueToggleControl::refresh() {
    value = params.getInt(key);

    QString text;
    auto it = valueLabelMappings.find(value);
    if (division > 1) {
        text = QString::number(value / (division * 1.0), 'g');
    } else {
        text = it != valueLabelMappings.end() ? it->second : QString::number(value);
    }
    if (!labelText.isEmpty()) {
        text += labelText;
    }
    valueLabel->setText(text);
    valueLabel->setStyleSheet("QLabel { color: #E0E879; }");
}

void ConfigDrivenParamValueToggleControl::updateControl(int newMinValue, int newMaxValue, const QString &newLabel, int newDivision) {
    minValue = newMinValue;
    maxValue = newMaxValue;
    labelText = newLabel;
    division = newDivision;
}

void ConfigDrivenParamValueToggleControl::showEvent(QShowEvent *event) {
    refresh();
}

QPushButton* ConfigDrivenParamValueToggleControl::createButton(const QString &text, QWidget *parent) {
    QPushButton *button = new QPushButton(text, parent);
    button->setFixedSize(150, 100);
    button->setAutoRepeat(true);
    button->setAutoRepeatInterval(150);
    button->setStyleSheet(R"(
        QPushButton {
            border-radius: 10px;
            font-size: 50px;
            font-weight: 500;
            height: 100px;
            padding: 0 25 0 25;
            color: #E4E4E4;
            background-color: #393939;
        }
        QPushButton:pressed {
            background-color: #4a4a4a;
        }
    )");
    return button;
}

ConfigDrivenSegmentedControl::ConfigDrivenSegmentedControl(
    const QString &param, const QString &title, const QString &desc,
    const QString &icon, const QVector<QPair<QString, QString>> &options,
    QWidget *parent)
  : ParamControl(param, title, desc, icon, parent)
  , paramName(param)
  , optionsList(options)
{
  buttonGroup = new QButtonGroup(this);
  buttonGroup->setExclusive(true);

  QWidget *pillWidget = new QWidget(this);
  pillWidget->setObjectName("pillWidget");
  QHBoxLayout *pillLayout = new QHBoxLayout(pillWidget);
  pillLayout->setContentsMargins(0, 0, 0, 0);
  pillLayout->setSpacing(0);

  QString style = R"(
    QWidget#pillWidget {
      background-color: rgba(57, 57, 57, 0.3);
      border-radius: 50px;
      padding: 5px;
      max-height: 90px;
    }
    QPushButton {
      border: none;
      padding: 8px 24px;
      margin: 0;
      background: transparent;
      color: #ddd;
      font-size: 32px;
      min-height: 80px;
      max-height: 80px;
    }
    QPushButton:checked {
      background: rgba(85, 85, 85, 0.9);
      color: white;
      border-radius: 45px;
    }
  )";

  pillWidget->setStyleSheet(style);

  int maxOptions = std::min(optionsList.size(), 4);
  for (int i = 0; i < maxOptions; i++) {
    QPushButton *btn = new QPushButton(optionsList[i].first, pillWidget);
    btn->setCheckable(true);
    buttonGroup->addButton(btn, i);
    pillLayout->addWidget(btn);

    connect(btn, &QPushButton::clicked, this, [this, i]() {
      Params().put(paramName.toStdString(), optionsList[i].second.toStdString());
      updateSelection();
    });
  }

  hlayout->addWidget(pillWidget);
  toggle.hide();
  updateSelection();
}

void ConfigDrivenSegmentedControl::updateSelection() {
  QString storedVal = QString::fromStdString(Params().get(paramName.toStdString()));
  for (QAbstractButton *btn : buttonGroup->buttons()) {
    int idx = buttonGroup->id(btn);
    bool match = (storedVal == optionsList[idx].second);
    btn->setChecked(match);
  }
}

void ConfigDrivenSegmentedControl::refresh() {
  updateSelection();
}
