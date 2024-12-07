#pragma once

#include <QFrame>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QWidget>
#include <QStackedLayout>
#include <cmath>

#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/offroad/settings.h"

namespace FordSettings {
    enum class ControlType {
        Integer,
        Float
    };
}

class FordDefaultParams {
public:
    static FordDefaultParams& getInstance() {
        static FordDefaultParams instance;
        return instance;
    }

    QString getDefault(const QString& key) const {
        Params params;
        std::string defaultKey = key.toStdString() + "_default";
        return QString::fromStdString(params.get(defaultKey));
    }

private:
    FordDefaultParams() {}
};

class FordSettingsListWidget : public QWidget {
  Q_OBJECT
public:
  explicit FordSettingsListWidget(QWidget *parent = 0) : QWidget(parent), outer_layout(this) {
    outer_layout.setMargin(0);
    outer_layout.setSpacing(0);
    outer_layout.addLayout(&inner_layout);
    inner_layout.setMargin(0);
    inner_layout.setSpacing(25); // default spacing is 25
    outer_layout.addStretch();
  }
  inline void addItem(QWidget *w) { inner_layout.addWidget(w); }
  inline void addItem(QLayout *layout) { inner_layout.addLayout(layout); }
  inline void setSpacing(int spacing) { inner_layout.setSpacing(spacing); }

private:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setPen(Qt::gray);

    // int visibleWidgetCount = 0;
    std::vector<QRect> visibleRects;

    for (int i = 0; i < inner_layout.count(); ++i) {
      QWidget *widget = inner_layout.itemAt(i)->widget();
      if (widget && widget->isVisible()) {
        // visibleWidgetCount++;
        visibleRects.push_back(inner_layout.itemAt(i)->geometry());
      }
    }

    // for (int i = 0; i < visibleWidgetCount - 1; ++i) {
    //   int bottom = visibleRects[i].bottom() + inner_layout.spacing() / 2;
    //   p.drawLine(visibleRects[i].left() + 40, bottom, visibleRects[i].right() - 40, bottom);
    // }
  }
  QVBoxLayout outer_layout;
  QVBoxLayout inner_layout;
};

class FordSettingsPanel : public FordSettingsListWidget {
  Q_OBJECT

public:
  explicit FordSettingsPanel(SettingsWindow *parent);
  ~FordSettingsPanel();
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

protected:
  bool event(QEvent *event) override {
    switch (event->type()) {
      case QEvent::MouseMove:
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      case QEvent::KeyPress:
      case QEvent::KeyRelease:
      case QEvent::Wheel:
        resetMaxDurationTimer();
        break;
      default:
        break;
    }
    return QWidget::event(event);
  }

private:
  QGroupBox *createStyledGroupBox(const QString &title);
  QPushButton* createResetButton();

  std::vector<QWidget*> lateralTuningControls;
  std::vector<QWidget*> brakeTuningControls;
  std::vector<QWidget*> limitsTuningControls;
  void addParameterButtons();
  void addVehicleSelector();
  void addPreferences();
  void addLateralTuning();
  void addBrakeTuning();
  void addLimitsTuning();
  void updateToggles();
  void resetLateralTuning();
  void resetBrakeTuning();
  void resetLimitsTuning();
  bool showResetConfirmation(const QString& tuningType);

  void updateControlWithDefault(QWidget* ctrl);
  void updateResetButtonVisibility(QGroupBox* group);
  void resetControlTitle(QWidget* control);
  void resetGroupControls(const std::vector<QWidget*>& controls);

  bool isGitRemoteValid(const std::vector<std::string>& searchStrs, const std::vector<std::string>& branchNames);

  Params params;
  std::map<std::string, ParamControl*> toggles;
  QGroupBox *lateralTuningGroup;
  QGroupBox *brakeTuningGroup;
  QGroupBox *limitsTuningGroup;

  QTimer *activityTimer;

  void simulateActivity();
  void stopActivitySimulation();
  void resetMaxDurationTimer();

};

// Forward declarations
class FordSettingsParamValueControl;
class FordSettingsParamValueControlFloat;

class FordSettingsControlFactory {
public:
  static FordSettingsParamValueControl* createIntegerControl(
    const QString &param, const QString &title, const QString &desc,
    int minValue, int maxValue, int increment = 1, bool loop = false,
    const QString &label = "", const std::map<int, QString> &valueLabels = {});

  static FordSettingsParamValueControlFloat* createFloatControl(
    const QString &param, const QString &title, const QString &desc,
    float minValue, float maxValue, float increment = 0.1f, bool loop = false,
    const QString &label = "", const std::map<int, QString> &valueLabels = {}, float division = 1.0f);

};

class FordSettingsConfirmationDialog : public ConfirmationDialog {
  Q_OBJECT

public:
  explicit FordSettingsConfirmationDialog(const QString &prompt_text, const QString &confirm_text,
                              const QString &cancel_text, const bool rich, QWidget* parent);
  static bool toggle(const QString &prompt_text, const QString &confirm_text, QWidget *parent);
  static bool toggleAlert(const QString &prompt_text, const QString &button_text, QWidget *parent);
  static bool yesorno(const QString &prompt_text, QWidget *parent);
};

class FordSettingsButtonIconControl : public AbstractControl {
  Q_OBJECT

public:
  FordSettingsButtonIconControl(const QString &title, const QString &text, const QString &desc = "", const QString &icon = "", QWidget *parent = nullptr);
  inline void setText(const QString &text) { btn.setText(text); }
  inline QString text() const { return btn.text(); }

signals:
  void clicked();

public slots:
  void setEnabled(bool enabled) { btn.setEnabled(enabled); }

private:
  QPushButton btn;
};

class FordSettingsButtonParamControl : public ParamControl {
  Q_OBJECT
public:
  FordSettingsButtonParamControl(const QString &param, const QString &title, const QString &desc, const QString &icon,
                     const std::vector<QString> &button_texts, const int minimum_button_width = 225)
    : ParamControl(param, title, desc, icon) {
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

    key = param.toStdString();
    int value = atoi(params.get(key).c_str());

    button_group = new QButtonGroup(this);
    button_group->setExclusive(true);
    for (size_t i = 0; i < button_texts.size(); i++) {
      QPushButton *button = new QPushButton(button_texts[i], this);
      button->setCheckable(true);
      button->setChecked(i == value);
      button->setStyleSheet(style);
      button->setMinimumWidth(minimum_button_width);
      hlayout->addWidget(button);
      button_group->addButton(button, i);
    }

    QObject::connect(button_group, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked) {
      if (checked) {
        params.put(key, std::to_string(id));
        refresh();
        emit buttonClicked(id);
      }
    });

    toggle.hide();
  }

  void setEnabled(bool enable) {
    for (auto btn : button_group->buttons()) {
      btn->setEnabled(enable);
    }
  }

signals:
  void buttonClicked(int id);

private:
  std::string key;
  Params params;
  QButtonGroup *button_group;
};

class FordSettingsParamManageControl : public ParamControl {
  Q_OBJECT

public:
  FordSettingsParamManageControl(const QString &param, const QString &title, const QString &desc, const QString &icon, QWidget *parent = nullptr)
    : ParamControl(param, title, desc, icon, parent),
      key(param.toStdString()),
      manageButton(new ButtonControl(tr(""), tr("MANAGE"), tr(""))) {
    hlayout->insertWidget(hlayout->indexOf(&toggle) - 1, manageButton);

    connect(this, &ToggleControl::toggleFlipped, this, [this](bool state) {
      refresh();
    });

    connect(manageButton, &ButtonControl::clicked, this, &FordSettingsParamManageControl::manageButtonClicked);
  }

  void refresh() {
    ParamControl::refresh();
    manageButton->setVisible(params.getBool(key));
  }

  void showEvent(QShowEvent *event) override {
    ParamControl::showEvent(event);
    refresh();
  }

signals:
  void manageButtonClicked();

private:
  std::string key;
  Params params;
  ButtonControl *manageButton;
};

class FordSettingsParamToggleControl : public ParamControl {
  Q_OBJECT
public:
  FordSettingsParamToggleControl(const QString &param, const QString &title, const QString &desc,
                     const QString &icon, const std::vector<QString> &button_params,
                     const std::vector<QString> &button_texts, QWidget *parent = nullptr,
                     const int minimum_button_width = 225)
    : ParamControl(param, title, desc, icon, parent) {

    connect(this, &ToggleControl::toggleFlipped, this, [this](bool state) {
      refreshButtons(state);
    });

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
        emit buttonClicked(checked);
      });

      hlayout->insertWidget(hlayout->indexOf(&toggle), button);
    }
  }

  void refreshButtons(bool state) {
    for (QAbstractButton *button : button_group->buttons()) {
      button->setVisible(state);
    }
  }

signals:
  void buttonClicked(const bool checked);

private:
  Params params;
  QButtonGroup *button_group;
};

class FordSettingsParamValueControl : public ParamControl {
  Q_OBJECT

public:
  FordSettingsParamValueControl(const QString &param, const QString &title, const QString &desc, const QString &icon,
                    const int &minValue, const int &maxValue, const std::map<int, QString> &valueLabels,
                    QWidget *parent = nullptr, const bool &loop = true, const QString &label = "", const int &division = 1)
    : ParamControl(param, title, desc, icon, parent),
      minValue(minValue), maxValue(maxValue), valueLabelMappings(valueLabels), loop(loop), labelText(label), division(division) {
        key = param.toStdString();

         // Create a vertical layout for the title and default value
        QVBoxLayout *titleLayout = new QVBoxLayout();
        titleLayout->setSpacing(0);
        titleLayout->setContentsMargins(0, 0, 0, 0);

        // Move the title label to our new layout
        titleLayout->addWidget(this->title_label);

        // Create and add the default value label
        defaultValueLabel = new QLabel(this);
        defaultValueLabel->setStyleSheet("QLabel { color: #888888; font-size: 40px; }");
        defaultValueLabel->setAlignment(Qt::AlignLeft);
        titleLayout->addWidget(defaultValueLabel);

        // Replace the title label in the main layout with our new vertical layout
        hlayout->replaceWidget(this->title_label, new QWidget());
        hlayout->insertLayout(0, titleLayout);

        QLabel *minLabel = new QLabel("Min: " + QString::number(minValue), this);
        QLabel *maxLabel = new QLabel("Max: " + QString::number(maxValue), this);
        minLabel->setStyleSheet("QLabel { color: #ff7c30; font-size: 30px; }");
        maxLabel->setStyleSheet("QLabel { color: #50d332; font-size: 30px; }");
        minLabel->setAlignment(Qt::AlignCenter);
        maxLabel->setAlignment(Qt::AlignCenter);

        valueLabel = new QLabel(this);
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setFixedSize(190, 130);
        valueLabel->setStyleSheet("QLabel { color: #3f9fff; font-size: 50px; background-color: #393939; border: none; }");

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

  void updateValue(int increment) {
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

  void refresh() {
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

  void updateControl(int newMinValue, int newMaxValue, const QString &newLabel, int newDivision = 1) {
    minValue = newMinValue;
    maxValue = newMaxValue;
    labelText = newLabel;
    division = newDivision;
  }

  void showEvent(QShowEvent *event) override {
    refresh();
  }

  QString getLabel() const { return label; }
  void setLabel(const QString& newLabel) { label = newLabel; update(); }

  void setDefaultValue(const QString &defaultValue) {
    if (defaultValue.isEmpty()) {
      defaultValueLabel->clear();  // Clear the text if the default value is empty
      defaultValueLabel->hide();   // Optionally hide the label when there's no default value
    } else {
      defaultValueLabel->setText(tr("Default: %1").arg(defaultValue));
      defaultValueLabel->show();   // Ensure the label is visible when there's a default value
    }
  }

signals:
  void buttonPressed();
  void valueChanged(int value);

private:
  bool loop;
  int division;
  int maxValue;
  int minValue;
  int value;
  QLabel *valueLabel;
  QLabel *defaultValueLabel;
  QString labelText;
  std::map<int, QString> valueLabelMappings;
  std::string key;
  Params params;
  QString label;

  QPushButton *createButton(const QString &text, QWidget *parent, bool isLeftButton) {
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
};

class FordSettingsParamValueControlFloat : public ParamControl {
  Q_OBJECT

public:
  FordSettingsParamValueControlFloat(const QString &param, const QString &title, const QString &desc, const QString &icon,
                    const float &minValue, const float &maxValue, const std::map<int, QString> &valueLabels,
                    QWidget *parent = nullptr, const bool &loop = true, const QString &label = "", const float &division = 1.0f)
    : ParamControl(param, title, desc, icon, parent),
      minValue(minValue), maxValue(maxValue), valueLabelMappings(valueLabels), loop(loop), labelText(label), division(division) {
        key = param.toStdString();

         // Create a vertical layout for the title and default value
        QVBoxLayout *titleLayout = new QVBoxLayout();
        titleLayout->setSpacing(0);
        titleLayout->setContentsMargins(0, 0, 0, 0);

        // Move the title label to our new layout
        titleLayout->addWidget(this->title_label);

        // Create and add the default value label
        defaultValueLabel = new QLabel(this);
        defaultValueLabel->setStyleSheet("QLabel { color: #888888; font-size: 40px; }");
        defaultValueLabel->setAlignment(Qt::AlignLeft);
        titleLayout->addWidget(defaultValueLabel);

        // Replace the title label in the main layout with our new vertical layout
        hlayout->replaceWidget(this->title_label, new QWidget());  // Remove the old title label
        hlayout->insertLayout(0, titleLayout);  // Insert the new layout at the beginning

        QLabel *minLabel = new QLabel("Min: " + QString::number(minValue, 'f', 2), this);
        QLabel *maxLabel = new QLabel("Max: " + QString::number(maxValue, 'f', 2), this);
        // Orange color code: #
        minLabel->setStyleSheet("QLabel { color: #ff7c30; font-size: 30px; }");
        maxLabel->setStyleSheet("QLabel { color: #50d332; font-size: 30px; }");
        minLabel->setAlignment(Qt::AlignCenter);
        maxLabel->setAlignment(Qt::AlignCenter);

        valueLabel = new QLabel(this);
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setFixedSize(190, 130);
        valueLabel->setStyleSheet("QLabel { color: #3f9fff; font-size: 50px; background-color: #393939; border: none; }");

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

  void refresh() {
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

  void updateValue(float increment) {
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

  void updateControl(float newMinValue, float newMaxValue, const QString &newLabel, float newDivision = 1.0f) {
    minValue = newMinValue;
    maxValue = newMaxValue;
    labelText = newLabel;
    division = newDivision;
  }

  void showEvent(QShowEvent *event) override {
    refresh();
  }

  QString getLabel() const { return label; }
  void setLabel(const QString& newLabel) { label = newLabel; update(); }

  void setDefaultValue(const QString &defaultValue) {
    if (defaultValue.isEmpty()) {
      defaultValueLabel->clear();  // Clear the text if the default value is empty
      defaultValueLabel->hide();   // Optionally hide the label when there's no default value
    } else {
      defaultValueLabel->setText(tr("Default: %1").arg(defaultValue));
      defaultValueLabel->show();   // Ensure the label is visible when there's a default value
    }
  }

signals:
  void buttonPressed();
  void valueChanged(float value);

private:
  bool loop;
  float division;
  float maxValue;
  float minValue;
  float value;
  QLabel *valueLabel;
  QLabel *defaultValueLabel;
  QString labelText;
  std::map<int, QString> valueLabelMappings;
  std::string key;
  Params params;
  QString label;

  int calculateDecimalPlaces(float divisor) {
    if (divisor == 0) return 0;
    return std::max(0, -static_cast<int>(std::floor(std::log10(1.0 / divisor))));
  }

  QPushButton *createButton(const QString &text, QWidget *parent, bool isLeftButton) {
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
};

class FordSettingsDualParamControl : public QFrame {
  Q_OBJECT

public:
  FordSettingsDualParamControl(ParamControl *control1, ParamControl *control2, QWidget *parent = nullptr, bool split=false)
    : QFrame(parent) {
    QHBoxLayout *hlayout = new QHBoxLayout(this);

    control1->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    control1->setMaximumWidth(split ? 800 : 700);

    control2->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    control2->setMaximumWidth(800);

    hlayout->addWidget(control1);
    hlayout->addWidget(control2);
  }
};

class FordSettingsParamValueToggleControl : public ParamControl {
  Q_OBJECT

public:
  FordSettingsParamValueToggleControl(const QString &param, const QString &title, const QString &desc, const QString &icon,
                          const int &minValue, const int &maxValue, const std::map<int, QString> &valueLabels,
                          QWidget *parent = nullptr, const bool &loop = true, const QString &label = "", const int &division = 1,
                          const std::vector<QString> &button_params = std::vector<QString>(), const std::vector<QString> &button_texts = std::vector<QString>(),
                          const int minimum_button_width = 225)
    : ParamControl(param, title, desc, icon, parent),
      minValue(minValue), maxValue(maxValue), valueLabelMappings(valueLabels), loop(loop), labelText(label), division(division) {
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

  void updateValue(int increment) {
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

  void refresh() {
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

  void updateControl(int newMinValue, int newMaxValue, const QString &newLabel, int newDivision) {
    minValue = newMinValue;
    maxValue = newMaxValue;
    labelText = newLabel;
    division = newDivision;
  }

  void showEvent(QShowEvent *event) override {
    refresh();
  }

signals:
  void buttonPressed();
  void valueChanged(int value);

private:
  bool loop;
  int division;
  int maxValue;
  int minValue;
  int value;
  QButtonGroup *button_group;
  QLabel *valueLabel;
  QString labelText;
  std::map<int, QString> valueLabelMappings;
  std::string key;
  Params params;

  QPushButton *createButton(const QString &text, QWidget *parent) {
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
};
