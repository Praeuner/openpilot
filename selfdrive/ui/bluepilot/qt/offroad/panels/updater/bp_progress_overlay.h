// selfdrive/ui/bluepilot/qt/offroad/panels/updater/bp_progress_overlay.h

#pragma once

#ifndef BP_PROGRESS_OVERLAY_H
#define BP_PROGRESS_OVERLAY_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QPropertyAnimation>

// Modern progress overlay that appears over the panel
class BPProgressOverlay : public QWidget {
  Q_OBJECT

public:
  explicit BPProgressOverlay(QWidget *parent = nullptr);

  void showOperation(const QString &operation);
  void updateProgress(const QString &message);
  void showComplete(bool success, const QString &message);
  void showError(const QString &error);

signals:
  void cancelRequested();
  void retryRequested();
  void closeRequested();

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QWidget *contentWidget;
  QLabel *titleLabel;
  QLabel *statusLabel;
  QTextEdit *outputText;
  QPushButton *cancelButton;
  QPushButton *retryButton;
  QPushButton *closeButton;

  bool isComplete;
  bool wasSuccessful;

  void setupUI();
  void fadeIn();
  void fadeOut();
  void appendOutput(const QString &text);
};

#endif // BP_PROGRESS_OVERLAY_H
