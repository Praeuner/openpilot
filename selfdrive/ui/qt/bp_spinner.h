// selfdrive/ui/qt/bp_spinner.h
#pragma once

#include <array>
#include <deque>
#include <string>

#include <QDialog>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSocketNotifier>
#include <QTextEdit>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWidget>
#include <QScroller>
#include <QScrollerProperties>
#include <QApplication>
#include <QScreen>
#include <QWidget>
#include <QScrollBar>

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#include <QPlatformSurfaceEvent>
#endif

#include "system/hardware/hw.h"

// Forward declaration
class BPSpinner;

constexpr int spinner_fps = 30;

// Modal for displaying compile output and errors
class OutputModal : public QDialog {
  Q_OBJECT

public:
  explicit OutputModal(BPSpinner *parent = nullptr);
  void setText(const QString &text);
  void setTitle(const QString &title);
  void setErrorMode(bool isError);
  void showEvent(QShowEvent *event) override;
  void applyRotation();
  bool eventFilter(QObject *obj, QEvent *event) override;
  void scrollToBottom();

private:
  QVBoxLayout *layout;
  QLabel *titleLabel;
  QTextEdit *textArea;
  QPushButton *closeButton;
  QPushButton *rebootButton;
  QPushButton *updateToolButton;
  QHBoxLayout *buttonLayout;
  BPSpinner *spinnerParent;
  QPushButton *scrollToBottomButton;
  void updateScrollButtonVisibility();
  void positionScrollButton();
  void setupTouchScrolling();
  void resizeEvent(QResizeEvent *event) override;
};

// Enhanced spinner with additional features
class BPSpinner : public QWidget {
  Q_OBJECT

public:
  explicit BPSpinner(QWidget *parent = 0);
  void showOutputModal(bool isError = false);
  void launchUpdaterPanel();

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  bool errorModalWasVisible = false;
  QLabel *statusTextLabel;
  QProgressBar *progressBar;
  QPushButton *infoButton;
  QSocketNotifier *notifier;
  OutputModal *outputModal;
  std::deque<std::string> outputBuffer;
  bool hasError;

  void update(int n);
  void parseInput(const std::string &line);
  void updateProgress(float cur, float total);
  void updateStatusText(const QString &text);
  void storeOutput(const QString &text);
  void updateOutputModalText();
};

// QT widget to display animated spinner
class BPTrackWidget : public QWidget {
  Q_OBJECT
public:
  BPTrackWidget(QWidget *parent = nullptr);

private:
  void paintEvent(QPaintEvent *event) override;
  QVariantAnimation m_anim;
  std::array<QPixmap, spinner_fps> track_imgs;
};