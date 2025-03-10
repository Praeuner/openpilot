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

class BPPanelListWidget : public QWidget {
  Q_OBJECT

  friend class BPNestedView;
  friend class BPNavBarView;

public:
  explicit BPPanelListWidget(QWidget *parent = nullptr) : QWidget(parent) {
    // Create scroll area
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Create container for scroll content
    QWidget *scrollContent = new QWidget(scrollArea);
    outer_layout.setContentsMargins(0, 0, 0, 0);
    outer_layout.setSpacing(0);
    outer_layout.addLayout(&inner_layout);
    inner_layout.setContentsMargins(0, 0, 0, 0);
    inner_layout.setSpacing(50);
    outer_layout.addStretch();

    scrollContent->setLayout(&outer_layout);
    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea);

    // Set scroll area styling
    scrollArea->setStyleSheet(R"(
      QScrollArea {
        background: transparent;
        border: none;
      }
      QScrollBar:vertical {
        width: 24px;
        margin: 0px;
        padding: 2px;
        background: transparent;
      }
      QScrollBar::handle {
        border-top-left-radius: 12px;
        border-top-right-radius: 12px;
        border-bottom-left-radius: 12px;
        border-bottom-right-radius: 12px;
      }
      QScrollBar::handle:vertical {
        background: #666666;
        min-height: 100px;
        border-top-left-radius: 12px;
        border-top-right-radius: 12px;
        border-bottom-left-radius: 12px;
        border-bottom-right-radius: 12px;
        margin: 0 4px;
      }
      QScrollBar::add-line:vertical,
      QScrollBar::sub-line:vertical {
        height: 0px;
      }
      QScrollBar::add-page:vertical,
      QScrollBar::sub-page:vertical {
        background: none;
      }
    )");

    // Enable touch events on the scroll area and its viewport.
    scrollArea->setAttribute(Qt::WA_AcceptTouchEvents, true);
    scrollArea->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
    scrollArea->viewport()->setMouseTracking(true);
    scrollArea->viewport()->setFocusPolicy(Qt::StrongFocus);

    // Enable touch scrolling via QScroller on the viewport.
    QScroller::grabGesture(scrollArea->viewport(), QScroller::TouchGesture);
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
  };

  // Core state
  bool isRefreshing = false;
  bool keepScreenAwake = false;
  Params params;
  LogTreeFormatter logFormatter;
  std::map<QString, GroupData> groups;
  std::map<std::string, BPToggleControl *> toggles;
  QJsonObject configJson;

  // Control creation
  virtual QWidget *processControlCreation(const QJsonObject &control);
  virtual QWidget *createToggleControl(const QJsonObject &control);
  virtual QWidget *createSegmentedControl(const QJsonObject &control);
  virtual QWidget *createNumericControl(const QJsonObject &control, bool isFloat);
  virtual QWidget *createSelectionControl(const QJsonObject &control);
  virtual QWidget *createParamViewerControl(const QJsonObject &control);
  virtual QWidget *createParamListViewerControl(const QJsonObject &control);
  virtual QWidget *createFileViewerControl(const QJsonObject &control);
  virtual QWidget *createCommandButtonControl(const QJsonObject &control);
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
};
