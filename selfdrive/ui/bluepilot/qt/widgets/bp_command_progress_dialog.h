// selfdrive/ui/bluepilot/qt/widgets/bp_command_progress_dialog.h
// Simplified progress dialog for updater commands

#pragma once

#include <QDialog>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QJsonObject>

#include "selfdrive/ui/bluepilot/qt/widgets/bp_updater_client.h"

/**
 * BPCommandProgressDialog - Shows progress for updater commands
 *
 * Simplified dialog that shows:
 * - Command title
 * - Progress percentage
 * - Real-time command output
 * - Cancel/Close button
 */
class BPCommandProgressDialog : public QDialog {
  Q_OBJECT

public:
  explicit BPCommandProgressDialog(const QString &title, QWidget *parent = nullptr);

public slots:
  void setProgress(int progress);
  void appendOutput(const QString &output);
  void onSuccess(QJsonObject result);
  void onFailure(const QString &error);

private:
  void setupUI(const QString &title);
  void setupFullscreen();

  QLabel *titleLabel;
  QLabel *progressLabel;
  QTextEdit *outputText;
  QPushButton *cancelButton;
  QPushButton *closeButton;

  bool commandRunning;
};
