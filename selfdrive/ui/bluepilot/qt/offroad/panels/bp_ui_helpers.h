// selfdrive/ui/bluepilot/qt/offroad/panels/bp_ui_helpers.h
// BluePilot UI Helper Functions
//
// Common UI utility functions for BluePilot panels

#pragma once

#include <QWidget>
#include <QFrame>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QEvent>
#include <QMap>
#include <QList>
#include <initializer_list>

/**
 * BPUIHelpers - Static UI utility functions for BluePilot panels
 *
 * Provides reusable UI components and helpers for creating consistent
 * visual elements across different BluePilot panels.
 */
class BPUIHelpers {
public:
  /**
   * Create a standard horizontal divider line
   *
   * Creates a subtle horizontal line for visually separating controls
   * within panel groups. The divider has consistent styling across all panels.
   *
   * @return QWidget* A widget containing the divider line
   */
  static QWidget *createDivider() {
    QWidget *lineContainer = new QWidget();
    QVBoxLayout *containerLayout = new QVBoxLayout(lineContainer);
    containerLayout->setContentsMargins(5, 5, 5, 5);

    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setStyleSheet(R"(
      border: none;
      background-color: rgba(255, 255, 255, 0.5);
      min-height: 1px;
      max-height: 1px;
    )");

    containerLayout->addWidget(line);
    return lineContainer;
  }

  /**
   * Mark a widget to NOT have a divider added after it
   *
   * Use this on widgets that should be visually grouped together
   * without a divider between them.
   *
   * @param widget The widget to mark
   */
  static void setNoDividerAfter(QWidget *widget) {
    if (widget) {
      widget->setProperty("bp_no_divider_after", true);
    }
  }

  /**
   * Check if a widget is marked to not have a divider after it
   */
  static bool hasNoDividerAfter(QWidget *widget) {
    if (!widget) return false;
    return widget->property("bp_no_divider_after").toBool();
  }
};

/**
 * BPDividerManager - Automatic divider management for panel groups
 *
 * UNIVERSAL SYSTEM - Works with ALL BluePilot panel types:
 * - bp_osm_panel (OSM map downloads)
 * - bp_software_panel (Software updates)
 * - bp_network_panel (WiFi/Cellular)
 * - bp_models_panel (Model downloads)
 * - bp_panel_base (JSON-configured panels)
 * - Any custom panel with QGroupBox + QVBoxLayout
 *
 * SUPPORTED CONTROL TYPES:
 * - BPToggleControl, BPCommandControl, BPNumericControl
 * - QPushButton, QLabel, QWidget containers
 * - BPWifiListControl, BPButton, etc.
 * - Any QWidget-derived control
 *
 * ============================================================================
 * USAGE PATTERN 1: Process single group after building
 * ============================================================================
 *   void createMyGroup() {
 *     myGroup = createStyledGroupBox("My Settings");
 *     QVBoxLayout *layout = new QVBoxLayout(myGroup);
 *
 *     layout->addWidget(toggle1);
 *     layout->addWidget(button1);
 *     layout->addWidget(label1);
 *     label1->setVisible(false);  // Initially hidden
 *
 *     // ONE LINE - automatic dividers!
 *     BPDividerManager::processGroup(myGroup);
 *   }
 *
 *   // Later, when visibility changes:
 *   label1->setVisible(true);  // Divider auto-shows!
 *   button1->setVisible(false); // Divider auto-hides!
 *
 * ============================================================================
 * USAGE PATTERN 2: Process multiple groups at once
 * ============================================================================
 *   void setupUI() {
 *     createGroup1();
 *     createGroup2();
 *     createGroup3();
 *
 *     // ONE LINE - process all groups!
 *     BPDividerManager::processGroups({group1, group2, group3});
 *   }
 *
 * ============================================================================
 * USAGE PATTERN 3: Prevent divider between specific controls
 * ============================================================================
 *   layout->addWidget(titleLabel);
 *   layout->addWidget(descLabel);
 *
 *   // Keep title and desc together without divider
 *   BPUIHelpers::setNoDividerAfter(titleLabel);
 *
 *   layout->addWidget(button1);  // Divider WILL appear before button1
 *   BPDividerManager::processGroup(myGroup);
 *
 * ============================================================================
 * REAL-WORLD EXAMPLES FROM ACTUAL PANELS:
 * ============================================================================
 *
 * BP OSM Panel:
 *   createDatabaseUpdateGroup() {
 *     layout->addWidget(updateButton);
 *     layout->addWidget(etaWidget);
 *     layout->addWidget(elapsedWidget);
 *     BPDividerManager::processGroup(databaseUpdateGroup);
 *     // Dividers auto-hide when eta/elapsed widgets hide during download
 *   }
 *
 * BP Software Panel:
 *   createVersionInfoGroup() {
 *     layout->addWidget(currentVersionWidget);
 *     layout->addWidget(newVersionWidget);  // Hidden when no update
 *     layout->addWidget(changesButton);
 *     BPDividerManager::processGroup(versionInfoGroup);
 *     // Divider before newVersionWidget auto-hides when no update available
 *   }
 *
 * BP Network Panel:
 *   createTetheringGroup() {
 *     layout->addWidget(tetheringToggle);
 *     layout->addWidget(passwordButton);  // Only shown when tethering enabled
 *     BPDividerManager::processGroup(tetheringGroup);
 *     // Divider before passwordButton auto-hides when toggle is off
 *   }
 *
 * BP Models Panel:
 *   createLaneTurnGroup() {
 *     layout->addWidget(laneTurnToggle);
 *     layout->addWidget(laneTurnValue);  // Only shown when toggle is on
 *     BPDividerManager::processGroup(laneTurnGroup);
 *     // Divider before value control auto-hides when toggle is off
 *   }
 */
class BPDividerManager : public QObject {
  Q_OBJECT

public:
  explicit BPDividerManager(QVBoxLayout *layout, QObject *parent = nullptr)
    : QObject(parent), targetLayout(layout) {}

  /**
   * Process an existing QGroupBox and add automatic divider management
   *
   * Scans the group's layout, inserts dividers between controls,
   * and sets up automatic visibility tracking.
   *
   * Works with any QGroupBox that has a QVBoxLayout containing widgets.
   * Compatible with all BP panel types and control types.
   *
   * @param group The QGroupBox to process
   * @return BPDividerManager* instance managing the dividers (owned by group)
   */
  static BPDividerManager* processGroup(QGroupBox *group) {
    if (!group) return nullptr;

    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(group->layout());
    if (!layout) return nullptr;

    // Create manager instance (owned by the group)
    BPDividerManager *mgr = new BPDividerManager(layout, group);

    // Collect all existing widgets from the layout (skip already-added dividers)
    QList<QWidget*> existingWidgets;
    for (int i = 0; i < layout->count(); ++i) {
      QLayoutItem *item = layout->itemAt(i);
      if (item && item->widget()) {
        QWidget *w = item->widget();
        // Skip any existing auto-dividers
        if (w->objectName() != "bp_auto_divider") {
          existingWidgets.append(w);
        }
      }
    }

    // Clear the layout (but keep the widgets)
    while (layout->count() > 0) {
      QLayoutItem *item = layout->takeAt(0);
      if (item->widget()) {
        item->widget()->setParent(nullptr);
      }
    }

    // Re-add widgets with dividers
    for (int i = 0; i < existingWidgets.size(); ++i) {
      mgr->addWidget(existingWidgets[i], i > 0);  // Add divider before all but first
    }

    return mgr;
  }

  /**
   * Process multiple QGroupBoxes at once
   *
   * Convenience method to process all groups in a panel.
   *
   * @param groups List of QGroupBox pointers to process
   */
  static void processGroups(std::initializer_list<QGroupBox*> groups) {
    for (QGroupBox *group : groups) {
      processGroup(group);
    }
  }

  /**
   * Process multiple QGroupBoxes from a QList
   */
  static void processGroups(const QList<QGroupBox*> &groups) {
    for (QGroupBox *group : groups) {
      processGroup(group);
    }
  }

  /**
   * Add a widget to the managed layout with automatic divider
   *
   * @param widget The widget to add
   * @param addDividerBefore If true, adds a divider before this widget
   */
  void addWidget(QWidget *widget, bool addDividerBefore = true) {
    if (!widget || !targetLayout) return;

    // Check if we should add a divider before this widget
    if (addDividerBefore && !managedWidgets.isEmpty()) {
      QWidget *prevWidget = managedWidgets.last();
      if (!BPUIHelpers::hasNoDividerAfter(prevWidget)) {
        QWidget *divider = BPUIHelpers::createDivider();
        divider->setObjectName("bp_auto_divider");
        targetLayout->addWidget(divider);
        dividerMap[widget] = divider;

        // Set up visibility tracking
        widget->installEventFilter(this);
        updateDividerVisibility(widget);
      }
    }

    targetLayout->addWidget(widget);
    managedWidgets.append(widget);

    // Always install event filter to track any future visibility changes
    if (!widget->property("bp_divider_tracked").toBool()) {
      widget->installEventFilter(this);
      widget->setProperty("bp_divider_tracked", true);
    }
  }

  /**
   * Update all divider visibility based on current widget state
   */
  void updateAllDividers() {
    for (auto it = dividerMap.begin(); it != dividerMap.end(); ++it) {
      updateDividerVisibility(it.key());
    }
  }

protected:
  bool eventFilter(QObject *obj, QEvent *event) override {
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
      QWidget *widget = qobject_cast<QWidget*>(obj);
      if (widget) {
        // Update the divider before this widget
        if (dividerMap.contains(widget)) {
          updateDividerVisibility(widget);
        }
        // Also update the divider after the previous widget
        updateAllDividers();
      }
    }
    return QObject::eventFilter(obj, event);
  }

private:
  void updateDividerVisibility(QWidget *widget) {
    if (!dividerMap.contains(widget)) return;

    QWidget *divider = dividerMap[widget];
    if (!divider) return;

    // Find the previous visible widget
    int widgetIndex = managedWidgets.indexOf(widget);
    if (widgetIndex <= 0) {
      // This is the first widget, hide its divider
      divider->setVisible(false);
      return;
    }

    // Check if there's any visible widget before this one
    bool hasPreviousVisible = false;
    for (int i = widgetIndex - 1; i >= 0; --i) {
      if (managedWidgets[i]->isVisible()) {
        hasPreviousVisible = true;
        break;
      }
    }

    // Show divider only if this widget is visible AND there's a visible widget before it
    divider->setVisible(widget->isVisible() && hasPreviousVisible);
  }

  QVBoxLayout *targetLayout;
  QList<QWidget*> managedWidgets;
  QMap<QWidget*, QWidget*> dividerMap;  // Maps widget -> divider before it
};
