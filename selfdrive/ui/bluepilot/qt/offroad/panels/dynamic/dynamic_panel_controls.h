// selfdrive/ui/bluepilot/qt/offroad/panels/dynamic/dynamic_panel_controls.h

#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QStyle>
#include <cmath>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/qt/widgets/controls.h"
#define AbstractControl AbstractControlSP
#define ParamControl ParamControlSP
#define ButtonControl ButtonControlSP
#define ElidedLabel ElidedLabelSP
#define ToggleControl ToggleControlSP
#else
#include "selfdrive/ui/qt/widgets/controls.h"
#endif

#include "common/params.h"
#include "dynamic_panel_utils.h"
#include "dynamic_panel_statistics.h"

class DynamicPanelButtonIconControl : public AbstractControl {
  Q_OBJECT

public:
  DynamicPanelButtonIconControl(const QString &title, const QString &text, const QString &desc = "", const QString &icon = "", QWidget *parent = nullptr);
  inline void setText(const QString &text) { btn.setText(text); }
  inline QString text() const { return btn.text(); }

signals:
  void clicked();

public slots:
  void setEnabled(bool enabled) { btn.setEnabled(enabled); }

private:
  QPushButton btn;
};

class DynamicPanelParamManageControl : public ParamControl {
  Q_OBJECT

public:
  DynamicPanelParamManageControl(const QString &param, const QString &title, const QString &desc, const QString &icon, QWidget *parent = nullptr);
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

class DynamicPanelParamValueControl : public ParamControl {
  Q_OBJECT

public:
  DynamicPanelParamValueControl(const QString &param, const QString &title, const QString &desc, const QString &icon,
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

class DynamicPanelParamValueControlFloat : public ParamControl {
  Q_OBJECT

public:
  DynamicPanelParamValueControlFloat(const QString &param, const QString &title, const QString &desc, const QString &icon,
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

class DynamicPanelControlFactory {
public:
  static DynamicPanelParamValueControl* createIntegerControl(
    const QString &param, const QString &title, const QString &desc,
    int minValue, int maxValue, int increment = 1, bool loop = false,
    const QString &label = "", const std::map<int, QString> &valueLabels = {});

  static DynamicPanelParamValueControlFloat* createFloatControl(
    const QString &param, const QString &title, const QString &desc,
    float minValue, float maxValue, float increment = 0.1f, bool loop = false,
    const QString &label = "", const std::map<int, QString> &valueLabels = {}, float division = 1.0f);

  static ParamControl* createToggleControl(const QString &param, const QString &title,
                                         const QString &desc, const QString &icon);

  static StatCardBase* createStatsCard(const QString &type, QWidget *parent = nullptr);
};

class DynamicPanelDualParamControl : public QFrame {
  Q_OBJECT

public:
  DynamicPanelDualParamControl(ParamControl *control1, ParamControl *control2,
                              QWidget *parent = nullptr, bool split=false);
};

class DynamicPanelParamToggleControl : public ParamControl {
  Q_OBJECT
public:
  DynamicPanelParamToggleControl(const QString &param, const QString &title, const QString &desc,
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

class DynamicPanelParamValueToggleControl : public ParamControl {
  Q_OBJECT

public:
  DynamicPanelParamValueToggleControl(const QString &param, const QString &title, const QString &desc, const QString &icon,
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

class DynamicPanelSegmentedControl : public ParamControl {
  Q_OBJECT

public:
  DynamicPanelSegmentedControl(
    const QString &param, const QString &title,
    const QString &desc, const QString &icon,
    const QVector<QPair<QString, QString>> &options,
    const QString &defaultValue = QString(),
    QWidget *parent = nullptr);
  void setEnabled(bool enabled);

  void refresh() ;

private:
  void updateSelection();

  // Declare a member to store the options:
  QVector<QPair<QString, QString>> optionsList;
  QButtonGroup *buttonGroup;
  QString paramName;
  QHBoxLayout *segmentLayout;
  QString defaultValue;
};
