// config_driven_panel_controls.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QStyle>
#include <cmath>
#include "selfdrive/ui/qt/widgets/controls.h"
#include "common/params.h"
#include "config_driven_panel_utils.h"

class ConfigDrivenButtonIconControl : public AbstractControl {
  Q_OBJECT

public:
  ConfigDrivenButtonIconControl(const QString &title, const QString &text, const QString &desc = "", const QString &icon = "", QWidget *parent = nullptr);
  inline void setText(const QString &text) { btn.setText(text); }
  inline QString text() const { return btn.text(); }

signals:
  void clicked();

public slots:
  void setEnabled(bool enabled) { btn.setEnabled(enabled); }

private:
  QPushButton btn;
};

class ConfigDrivenParamManageControl : public ParamControl {
  Q_OBJECT

public:
  ConfigDrivenParamManageControl(const QString &param, const QString &title, const QString &desc, const QString &icon, QWidget *parent = nullptr);
  void refresh();

protected:
  void showEvent(QShowEvent *event) override;

signals:
  void manageButtonClicked();

private:
  std::string key;
  Params params;
  ButtonControl *manageButton;
};

class ConfigDrivenParamValueControl : public ParamControl {
  Q_OBJECT

public:
  ConfigDrivenParamValueControl(const QString &param, const QString &title, const QString &desc, const QString &icon,
                    const int &minValue, const int &maxValue, const std::map<int, QString> &valueLabels,
                    QWidget *parent = nullptr, const bool &loop = true, const QString &label = "", const int &division = 1);

  void updateValue(int increment);
  void refresh();
  void updateControl(int newMinValue, int newMaxValue, const QString &newLabel, int newDivision = 1);
  QString getLabel() const { return label; }
  void setLabel(const QString& newLabel) { label = newLabel; update(); }
  void setDefaultValue(const QString &defaultValue);
  void setEnabled(bool isEnabled);

protected:
  void showEvent(QShowEvent *event) override;

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
  QPushButton *createButton(const QString &text, QWidget *parent, bool isLeftButton);
};

class ConfigDrivenParamValueControlFloat : public ParamControl {
  Q_OBJECT

public:
  ConfigDrivenParamValueControlFloat(const QString &param, const QString &title, const QString &desc, const QString &icon,
                    const float &minValue, const float &maxValue, const std::map<int, QString> &valueLabels,
                    QWidget *parent = nullptr, const bool &loop = true, const QString &label = "", const float &division = 1.0f);

  void refresh();
  void updateValue(float increment);
  void updateControl(float newMinValue, float newMaxValue, const QString &newLabel, float newDivision = 1.0f);
  QString getLabel() const { return label; }
  void setLabel(const QString& newLabel) { label = newLabel; update(); }
  void setDefaultValue(const QString &defaultValue);
  void setEnabled(bool isEnabled);

protected:
  void showEvent(QShowEvent *event) override;

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
  int calculateDecimalPlaces(float divisor);
  QPushButton *createButton(const QString &text, QWidget *parent, bool isLeftButton);
};

class ConfigDrivenControlFactory {
public:
  static ConfigDrivenParamValueControl* createIntegerControl(
    const QString &param, const QString &title, const QString &desc,
    int minValue, int maxValue, int increment = 1, bool loop = false,
    const QString &label = "", const std::map<int, QString> &valueLabels = {});

  static ConfigDrivenParamValueControlFloat* createFloatControl(
    const QString &param, const QString &title, const QString &desc,
    float minValue, float maxValue, float increment = 0.1f, bool loop = false,
    const QString &label = "", const std::map<int, QString> &valueLabels = {}, float division = 1.0f);

  static ParamControl* createToggleControl(const QString &param, const QString &title,
                                         const QString &desc, const QString &icon);
};

class ConfigDrivenDualParamControl : public QFrame {
  Q_OBJECT

public:
  ConfigDrivenDualParamControl(ParamControl *control1, ParamControl *control2,
                              QWidget *parent = nullptr, bool split=false);
};

class ConfigDrivenParamToggleControl : public ParamControl {
  Q_OBJECT
public:
  ConfigDrivenParamToggleControl(const QString &param, const QString &title, const QString &desc,
                     const QString &icon, const std::vector<QString> &button_params,
                     const std::vector<QString> &button_texts, QWidget *parent = nullptr,
                     const int minimum_button_width = 225);
  void refreshButtons(bool state);

signals:
  void buttonClicked(const bool checked);

private:
  Params params;
  QButtonGroup *button_group;
};

class ConfigDrivenParamValueToggleControl : public ParamControl {
  Q_OBJECT

public:
  ConfigDrivenParamValueToggleControl(const QString &param, const QString &title, const QString &desc, const QString &icon,
                          const int &minValue, const int &maxValue, const std::map<int, QString> &valueLabels,
                          QWidget *parent = nullptr, const bool &loop = true, const QString &label = "", const int &division = 1,
                          const std::vector<QString> &button_params = std::vector<QString>(),
                          const std::vector<QString> &button_texts = std::vector<QString>(),
                          const int minimum_button_width = 225);

  void updateValue(int increment);
  void refresh();
  void updateControl(int newMinValue, int newMaxValue, const QString &newLabel, int newDivision);

protected:
  void showEvent(QShowEvent *event) override;

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
  QPushButton *createButton(const QString &text, QWidget *parent);
};

class ConfigDrivenSegmentedControl : public ParamControl {
  Q_OBJECT

public:
  ConfigDrivenSegmentedControl(
    const QString &param, const QString &title,
    const QString &desc, const QString &icon,
    const QVector<QPair<QString, QString>> &options,
    QWidget *parent = nullptr);

  void refresh() ;

private:
  void updateSelection();

  // Declare a member to store the options:
  QVector<QPair<QString, QString>> optionsList;
  QButtonGroup *buttonGroup;
  QString paramName;
  QHBoxLayout *segmentLayout;
};
