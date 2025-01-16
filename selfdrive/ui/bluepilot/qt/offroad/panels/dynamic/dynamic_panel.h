// selfdrive/ui/bluepilot/qt/offroad/panels/dynamic/dynamic_panel.h

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

#include "selfdrive/ui/qt/widgets/scrollview.h"
#include "selfdrive/ui/qt/offroad/settings.h"
#include "dynamic_panel_utils.h"
#include "dynamic_panel_controls.h"
#include "dynamic_panel_dialogs.h"

class DynamicPanelListWidget : public QWidget {
  Q_OBJECT
public:
  explicit DynamicPanelListWidget(QWidget *parent = 0) : QWidget(parent), outer_layout(this) {
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

class DynamicPanel : public DynamicPanelListWidget {
    Q_OBJECT

public:
    QString panelName;
    explicit DynamicPanel(SettingsWindow *parent, const QString &configPath = QString());
    ~DynamicPanel();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onControlValueChanged();

private:
    QTimer refreshTimer;
    bool isRefreshing = false;
    bool keepScreenAwake = false;
    bool hasVisibleDialog = false;

    LogTreeFormatter logFormatter;
    void refreshPanel();
    void createGroup(const QJsonObject& group);
    QString getBaseGroupBoxStyle();
    void createTabPanel(const QJsonObject& group);
    QWidget* createTabContent(const QJsonArray& tabGroups);
    QWidget* createControl(const QJsonObject& control);
    void handleGroupReset(const QString& groupName);
  QGroupBox *createStyledGroupBox(const QString &title);
  QPushButton* createResetButton();
  void updateConditionsForAllControls();
  void updateActivitySimulation();

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

  void logConditionCheck(const QString& controlName, const std::function<void()>& logFunc) {
      const int width = 80;
      std::string separator(width, '=');
      std::string controlNameStr = controlName.toStdString();
      int padding = (width - controlNameStr.length() - 2) / 2;
      std::string centeredName = std::string(padding, ' ') + controlNameStr + std::string(padding, ' ');

      std::cout << "\n" << separator << std::endl;
      std::cout << centeredName << std::endl;
      std::cout << separator << std::endl;

      logFunc();

      std::cout << separator << "\n" << std::endl;
  }

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

  void fixSpStyle(QWidget* ctrl) {
      #ifdef SUNNYPILOT
      if (ctrl && ctrl->inherits("ParamControlSP")) {
          QVBoxLayout* main_layout = qobject_cast<QVBoxLayout*>(ctrl->layout());
          if (main_layout) {
              main_layout->setContentsMargins(0, 0, 0, 0);
              main_layout->setSpacing(0);

              if (main_layout->count() > 0) {
                  QHBoxLayout* hlayout = qobject_cast<QHBoxLayout*>(main_layout->itemAt(0)->layout());
                  if (hlayout) {
                      hlayout->setContentsMargins(0, 0, 0, 0);
                      hlayout->setSpacing(5);

                      // Fix ElidedLabelSP
                      QList<QWidget*> elidedLabels = ctrl->findChildren<QWidget*>("ElidedLabelSP");
                      for (QWidget* label : elidedLabels) {
                          label->setStyleSheet(R"(
                              color: #aaaaaa;
                              background: transparent;
                          )");
                      }

                      if (hlayout->count() > 1) {
                          QWidget* icon_label = hlayout->itemAt(1)->widget();
                          if (icon_label) {
                              icon_label->setVisible(false);
                          }
                      }
                  }
              }
          }
      }
      #endif
  }
};
