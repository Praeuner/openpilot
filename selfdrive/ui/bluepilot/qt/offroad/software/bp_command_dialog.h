// selfdrive/ui/bluepilot/qt/offroad/software/bp_command_dialog.h
// Command execution dialog with real-time output, non-blocking UI

#pragma once

#include <QDialog>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "common/params.h"

/**
 * BPCommandDialog - Execute shell commands with real-time output display
 *
 * Features:
 * - Non-blocking UI thread execution
 * - Real-time stdout/stderr display
 * - Elapsed time and timeout tracking
 * - CPU core count display
 * - Kill/Retry/Reboot/Restart UI buttons
 * - Power save management (QCOM2)
 * - BP-styled modal dialog
 *
 * Usage:
 *   BPCommandDialog::execute(parent, "Update Repository", "git pull", "/data/openpilot");
 */
class BPCommandDialog : public QDialog {
  Q_OBJECT

public:
  struct CommandConfig {
    QString title;
    QString command;
    QString workingDir;
    int timeoutMs = 120000;  // 2 minutes default
    bool showKillBtn = true;
    bool showRetryBtn = true;
    bool showRebootBtn = false;
    bool showRestartUIBtn = false;
  };

  explicit BPCommandDialog(const CommandConfig &config, QWidget *parent = nullptr);
  ~BPCommandDialog();

  // Static convenience method to show dialog
  static void execute(QWidget *parent, const QString &title, const QString &command,
                      const QString &workingDir = "", int timeoutMs = 120000,
                      bool showKillBtn = true, bool showRetryBtn = true,
                      bool showRebootBtn = false, bool showRestartUIBtn = false);

  // Check if changes are UI-only (for intelligent reboot vs restart UI)
  static bool checkIfUIOnlyChanges();

  // Power management methods
  bool isPowerSaveActive() const;
  void disablePowerSave();
  void restorePowerSave();

  // Setup fullscreen dialog for QCOM2
  static void setupFullscreenDialog(QDialog *dialog);

signals:
  void commandFinished(int exitCode, bool success);

private:
  void setupUI();
  void startCommand();
  void setupProcess();
  void setupTimers();
  void updateElapsedTime();
  QString formatTime(int totalSeconds);

  // Config
  CommandConfig config;
  Params params;

  // UI Components
  QVBoxLayout *mainLayout;
  QLabel *titleLabel;
  QLabel *coresLabel;
  QTextEdit *outputText;
  QPushButton *closeButton;
  QPushButton *killButton;
  QPushButton *retryButton;
  QPushButton *rebootButton;
  QPushButton *restartUIButton;

  // Process
  QProcess *process;

  // Timers
  QTimer *timeoutTimer;
  QTimer *runtimeTimer;
  QTimer *coresUpdateTimer;
  QElapsedTimer *elapsedTimer;

  // State
  bool commandInProgress = false;
  bool powerSaveWasActive = false;
};
