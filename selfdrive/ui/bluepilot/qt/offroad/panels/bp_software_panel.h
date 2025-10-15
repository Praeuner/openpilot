// selfdrive/ui/bluepilot/qt/offroad/panels/bp_software_panel.h
// BluePilot Software Panel - Native implementation with BP styling
// Handles software updates, branch selection, and version management

#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QTimer>

#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_controls.h"
#include "selfdrive/ui/qt/widgets/input.h"
#include "common/params.h"
#include "common/util.h"

/**
 * BPSoftwarePanel - Native software update panel with BP styling
 *
 * This panel provides software update management, branch selection,
 * and version display using BluePilot controls and styling.
 */
class BPSoftwarePanel : public QWidget {
  Q_OBJECT

public:
  explicit BPSoftwarePanel(QWidget *parent = nullptr);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void setupUI();
  void createVersionInfoGroup();
  void createUpdateControlsGroup();
  void createBranchSelectionGroup();
  void createAdvancedGroup();

  // Helper methods
  QGroupBox* createStyledGroupBox(const QString &title);
  void refreshAll();
  void updateLabels();
  void updateVersionInfo();
  void updateDownloadButton();
  void updateInstallButton();
  void updateBranchSelector();
  void checkForUpdates();
  void searchBranches(const QString &query);

  // Core components
  Params params;

  // Layout
  QVBoxLayout *mainLayout;

  // Groups
  QGroupBox *versionInfoGroup;
  QGroupBox *updateControlsGroup;
  QGroupBox *branchSelectionGroup;
  QGroupBox *advancedGroup;

  // Version Info Group
  QLabel *onroadLabel;
  QLabel *currentVersionLabel;
  QLabel *currentVersionDesc;
  QLabel *newVersionLabel;
  QLabel *newVersionDesc;

  // Update Controls Group
  QPushButton *downloadBtn;
  QLabel *downloadStatusLabel;
  QPushButton *installBtn;
  QLabel *installStatusLabel;

  // Branch Selection Group
  QPushButton *branchBtn;
  QLabel *branchStatusLabel;

  // Advanced Group
  BPToggleControl *disableUpdatesToggle;
  BPCommandControl *uninstallBtn;

  // State tracking
  bool is_onroad = false;
  QString updater_state;

  // Timers
  QTimer *refreshTimer;

private slots:
  void onDownloadClicked();
  void onInstallClicked();
  void onBranchClicked();
  void onUninstallClicked();
  void onDisableUpdatesToggled(bool enabled);
  void updateDisableUpdatesToggle(bool offroad);
};
