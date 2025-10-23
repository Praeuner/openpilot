// selfdrive/ui/bluepilot/qt/offroad/panels/bp_software_panel.h
// BluePilot Software Panel - Unified software management panel
//
// Features:
// - Daemon-based updates (safe, automatic)
// - Direct git operations (manual, power user)
// - ParamWatcher for reactive UI updates
// - Non-blocking QtConcurrent operations

#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QTimer>
#include <QProcess>
#include <QScreen>
#include <QtConcurrent>
#include <QDialog>
#include <QGuiApplication>

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#endif

#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_controls.h"
#include "selfdrive/ui/qt/widgets/input.h"
#include "selfdrive/ui/qt/util.h"
#include "common/params.h"
#include "common/util.h"

// Forward declarations
class BPCommandDialog;
class BPGitManager;
class BPRecentChangesDialog;

/**
 * BPSoftwarePanel - Unified software management panel
 *
 * Combines daemon-based updates with advanced git operations
 * - ParamWatcher for reactive UI updates
 * - Works with system/updated/updated.py
 * - Direct git operations for advanced features
 * - Non-blocking QtConcurrent operations
 */
class BPSoftwarePanel : public QWidget {
  Q_OBJECT

public:
  explicit BPSoftwarePanel(QWidget *parent = nullptr);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  // === UI Setup ===
  void setupUI();
  void createVersionInfoGroup();
  void createUpdateControlsGroup();
  void createBranchSelectionGroup();
  void createRepoStatusGroup();
  void createGitOperationsGroup();
  void createSystemGroup();
  void createAdvancedWarning();

  // Helper methods
  QGroupBox* createStyledGroupBox(const QString &title);
  void updateLabels();
  void updateVersionInfo();
  void updateDownloadButton();
  void updateInstallButton();
  void updateBranchSelector();
  void updateRepoStatus();
  void checkForUpdates();
  void searchBranches(const QString &query);

  // Dialog setup for QCOM2 rotation - must be called AFTER dialog->show()
  static void setupFullscreenDialog(QDialog *dialog) {
#ifdef QCOM2
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    if (native && dialog->windowHandle()) {
      wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", dialog->windowHandle()));
      if (s) {
        wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
        wl_surface_commit(s);
      }
    }
    dialog->setWindowState(Qt::WindowFullScreen);
#endif
  }

  // Advanced operations (async, non-blocking)
  void manualUpdate();
  void repairRepository();
  void resetRepository();
  void viewHistory();
  void showRecentChanges();
  void showCommitHistory(const QString &title, const QString &workingDir);

  // Core components
  Params params;
  ParamWatcher *fs_watch;
  BPGitManager *gitManager;

  // === Main Layout ===
  QVBoxLayout *mainLayout;

  // === Version Info ===
  QGroupBox *versionInfoGroup;
  QLabel *onroadLabel;
  QLabel *currentVersionLabel;
  QLabel *currentVersionDesc;
  QLabel *newVersionLabel;
  QLabel *newVersionDesc;
  BPCommandControl *sunnypilotChangesBtn;
  QPushButton *downloadBtn;
  QLabel *downloadStatusLabel;

  // === Update Controls ===
  QGroupBox *updateControlsGroup;
  QPushButton *installBtn;
  QLabel *installStatusLabel;

  // === Branch Selection ===
  QGroupBox *branchSelectionGroup;
  QPushButton *branchBtn;
  QLabel *branchStatusLabel;

  // === Repository Status ===
  QGroupBox *repoStatusGroup;
  QLabel *repoBranchLabel;
  QLabel *repoCommitLabel;
  QLabel *repoTimestampLabel;
  QLabel *repoHashLabel;
  QLabel *repoStatusLabel;
  QTimer *repoStatusTimer;

  // === Git Operations ===
  QGroupBox *gitOperationsGroup;
  BPCommandControl *manualUpdateBtn;
  BPCommandControl *repairBtn;
  BPCommandControl *resetBtn;
  BPCommandControl *historyBtn;

  // === System ===
  QGroupBox *systemGroup;
  BPToggleControl *disableUpdatesToggle;
  BPCommandControl *uninstallBtn;

  // === Warning ===
  QLabel *advancedWarningLabel;

  // State tracking
  bool is_onroad = false;
  QString updater_state;
  bool commandInProgress = false;

private slots:
  void onDownloadClicked();
  void onInstallClicked();
  void onBranchClicked();
  void onUninstallClicked();
  void onDisableUpdatesToggled(bool enabled);
  void updateDisableUpdatesToggle(bool offroad);
  void onManualUpdateClicked();
  void onRepairClicked();
  void onResetClicked();
  void onHistoryClicked();
  void onRecentChangesClicked();
  void onSunnypilotChangesClicked();
};
