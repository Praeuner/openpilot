// bp_panel_base.h

#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QJsonObject>
#include <QJsonArray>
#include <QTabWidget>
#include <QTabBar>
#include <QPainter>
#include <map>
#include <vector>

class BPToggleControl;

#include "bp_utils.h"
#include "bp_panel_controls.h"
#include "bp_panel_conditions.h"
#include "bp_panel_dialogs.h"

// Simplified ListWidget that doesn't embed scroll area (will be wrapped by ScrollView externally)
class BPPanelListWidget : public QWidget {
  Q_OBJECT

  friend class BPNestedView;
  friend class BPNavBarView;

public:
  explicit BPPanelListWidget(QWidget *parent = nullptr) : QWidget(parent), outer_layout(this) {
    // Simple layout structure like the smooth-scrolling ListWidget
    outer_layout.setMargin(0);
    outer_layout.setSpacing(0);
    outer_layout.addLayout(&inner_layout);
    inner_layout.setMargin(0);
    inner_layout.setSpacing(50);  // Default spacing for BP panels
    inner_layout.setSizeConstraint(QLayout::SetMinimumSize);
  }

  inline void addItem(QWidget *w) { inner_layout.addWidget(w); }
  inline void addItem(QLayout *layout) { inner_layout.addLayout(layout); }
  inline void setSpacing(int spacing) { inner_layout.setSpacing(spacing); }

private:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.fillRect(rect(), Qt::transparent);
  }
  QVBoxLayout outer_layout;
  QVBoxLayout inner_layout;
};

class BPPanelBase : public BPPanelListWidget {
  Q_OBJECT

public:
  explicit BPPanelBase(QWidget *parent = nullptr);
  virtual ~BPPanelBase();

  // Group and tab panel creation
  virtual void createGroup(const QJsonObject &group);
  virtual void createTabPanel(const QJsonObject &group);
  virtual QWidget *createTabContent(const QJsonArray &tabGroups);

  virtual bool loadConfig(const QString &configPath);
  virtual void refresh();

  // Activity simulation
  virtual void updateActivitySimulation();

protected:
  struct GroupData {
    QGroupBox *groupBox;
    std::vector<QWidget *> controls;
    std::vector<QWidget *> dividers;  // Track dividers separately for visibility management
  };

  // Core state
  bool isRefreshing = false;
  bool keepScreenAwake = false;
  Params params;
  LogTreeFormatter logFormatter;
  std::map<QString, GroupData> groups;
  std::map<std::string, BPToggleControl *> toggles;
  QJsonObject configJson;
  class BPActionHandler *actionHandler = nullptr;

  // Dynamic signal connection registry
  using SignalConnector = std::function<void(QWidget*, QObject*)>;
  std::map<QString, SignalConnector> signalConnectors;

  // Dynamic list generator registry
  using ListGenerator = std::function<QMap<QString, QString>()>;
  std::map<QString, ListGenerator> listGenerators;

  // Registry setup
  virtual void registerSignalConnectors();
  virtual void registerListGenerators();
  virtual void connectSignal(const QString &signalName, QWidget *widget, QObject *target);

  // Control creation
  virtual QWidget *processControlCreation(const QJsonObject &control);
  virtual QWidget *createToggleControl(const QJsonObject &control);
  virtual QWidget *createParamToggleButton(const QJsonObject &control);
  virtual QWidget *createSegmentedControl(const QJsonObject &control);
  virtual QWidget *createNumericControl(const QJsonObject &control, bool isFloat);
  virtual QWidget *createSelectionControl(const QJsonObject &control);
  virtual QWidget *createParamViewerControl(const QJsonObject &control);
  virtual QWidget *createParamListViewerControl(const QJsonObject &control);
  virtual QWidget *createStaticParamDisplayControl(const QJsonObject &control);
  virtual QWidget *createFileParamDisplayControl(const QJsonObject &control);
  virtual QWidget *createTextInputControl(const QJsonObject &control);
  virtual QWidget *createHtmlViewerControl(const QJsonObject &control);
  virtual QWidget *createFileViewerControl(const QJsonObject &control);
  virtual QWidget *createRecentChangesControl(const QJsonObject &control);
  virtual QWidget *createCommandButtonControl(const QJsonObject &control);
  virtual QWidget *createRestartUIControl(const QJsonObject &control);
  virtual QWidget *createStaticTextControl(const QJsonObject &control);
  virtual QWidget *createPlatformDisplayControl(const QJsonObject &control);
  virtual QWidget *createNestedControlsButton(const QJsonObject &control);

  // Group management
  virtual QGroupBox *createStyledGroupBox(const QString &title);
  virtual QPushButton *createResetButton();
  virtual void handleGroupReset(const QString &groupName);
  virtual void updateResetButtonVisibility(QGroupBox *group);
  virtual void resetGroupControls(const std::vector<QWidget *> &controls);

  // Condition and visibility management
  virtual void updateConditionsForAllControls();
  virtual void updateGroupVisibility();
  virtual void updateToggles();

  // Event handlers
  virtual void showEvent(QShowEvent *event) override;
  virtual void hideEvent(QHideEvent *event) override;

  // Validation
  virtual bool validateControlBasics(const QJsonObject &control);

protected slots:
  virtual void onControlValueChanged();

signals:
  void controlValueChanged();
  void groupResetRequested(const QString &groupName);
  void dialogVisibilityChanged(bool visible);
  void showDriverView();
  void reviewTrainingGuide();
  void showLanguageSelector();
  void showRegulatory();
};
