// config_driven_panel.h
#pragma once

#include <QFrame>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QWidget>
#include <QStackedLayout>
#include <QTimer>
#include <QTabWidget>
#include <QMouseEvent>
#include <map>
#include <vector>

#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/widgets/scrollview.h"
#include "selfdrive/ui/qt/offroad/settings.h"
#include "config_driven_panel_utils.h"
#include "config_driven_panel_controls.h"
#include "config_driven_panel_dialogs.h"

class ConfigDrivenListWidget : public QWidget {
  Q_OBJECT
public:
  explicit ConfigDrivenListWidget(QWidget *parent = 0) : QWidget(parent), outer_layout(this) {
    outer_layout.setMargin(0);
    outer_layout.setSpacing(0);
    outer_layout.addLayout(&inner_layout);
    inner_layout.setMargin(0);
    inner_layout.setSpacing(25);
    outer_layout.addStretch();
  }
  inline void addItem(QWidget *w) { inner_layout.addWidget(w); }
  inline void addItem(QLayout *layout) { inner_layout.addLayout(layout); }
  inline void setSpacing(int spacing) { inner_layout.setSpacing(spacing); }

private:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setPen(Qt::gray);
    std::vector<QRect> visibleRects;
    for (int i = 0; i < inner_layout.count(); ++i) {
      QWidget *widget = inner_layout.itemAt(i)->widget();
      if (widget && widget->isVisible()) {
        visibleRects.push_back(inner_layout.itemAt(i)->geometry());
      }
    }
  }
  QVBoxLayout outer_layout;
  QVBoxLayout inner_layout;
};

class ConfigDrivenPanel : public ConfigDrivenListWidget {
  Q_OBJECT

public:
  explicit ConfigDrivenPanel(SettingsWindow *parent, const QString &configPath = QString());
  ~ConfigDrivenPanel();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private slots:
  void onControlValueChanged();

private:
  QTimer refreshTimer;
  bool isRefreshing = false;

  void refreshPanel();
  void createGroup(const QJsonObject& group);
  void createTabPanel(const QJsonObject& group);
  QWidget* createTabContent(const QJsonArray& tabGroups);
  QWidget* createControl(const QJsonObject& control);
  void handleGroupReset(const QString& groupName);
  QGroupBox *createStyledGroupBox(const QString &title);
  QPushButton* createResetButton();
  void updateConditionsForAllControls();

  bool validateControlBasics(const QJsonObject& control);
  QJsonObject configJson;

  struct ControlConditions {
      QJsonObject conditions;
      bool hasConditions;
  };
  std::map<QWidget*, ControlConditions> controlConditions;

  bool showResetConfirmation(const QString& tuningType);
  void executeCommand(const QString& command, const QString& title,
                     const QString& workingDir = QString(),
                     const QJsonArray& actionButtons = QJsonArray());
  void updateControlWithDefault(QWidget* ctrl);
  void updateResetButtonVisibility(QGroupBox* group);
  void updateToggles();
  void resetControlTitle(QWidget* control);
  void resetGroupControls(const std::vector<QWidget*>& controls);
  QString getProjectRootPath();

  #ifdef QCOM2
  void setupFullscreenDialog(QDialog* dialog);
  #endif

  void showFullScreenDialog(const QString& title, const QString& content);
  bool isGitRemoteValid(const std::vector<std::string>& searchStrs,
                       const std::vector<std::string>& branchNames);
  bool validateSingleCondition(const QString& key, const QJsonValue& value);
  bool validateConditionObject(const QJsonObject& conditionObj);
  bool validateCompositeConditions(const QJsonObject& conditions);
  void updateGroupVisibility();

  Params params;
  std::map<std::string, ParamControl*> toggles;
  struct GroupData {
      QGroupBox* groupBox;
      std::vector<QWidget*> controls;
  };
  std::map<QString, GroupData> groups;

  QTimer *activityTimer;
  void simulateActivity();
  void stopActivitySimulation();
  void resetMaxDurationTimer();

  bool isCommaDevice() const {
      #ifdef QCOM2
          return true;
      #else
          return false;
      #endif
  }
};

