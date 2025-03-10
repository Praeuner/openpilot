// selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_dialogs.h

#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QScreen>
#include <QGuiApplication>
#include <QScrollArea>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QTextEdit>
#include <QProcess>
#include <QCheckBox>
#include <QTimer>
#include <iostream>

#include "bp_panel_controls.h"
#include "bp_panel_base.h"
#include "common/params.h"

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#include <QPlatformSurfaceEvent>
#endif

// class BPPanel;
class BPParamListDialog;
class BPButton;
class BPParamViewerDialog;

class BPDialogBase : public QDialog {
public:
  explicit BPDialogBase(QWidget *parent = nullptr) : QDialog(parent) { setWindowFlags(Qt::Window | Qt::FramelessWindowHint); }
  bool m_fullscreenApplied = false;

  void setupFullscreen() {
    // If we have already done it, do nothing
    if (m_fullscreenApplied)
      return;
    m_fullscreenApplied = true;

    setFixedSize(2160, 1080);
    show();
#ifdef QCOM2
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    if (native && windowHandle()) {
      wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", windowHandle()));
      if (s) {
        wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
        wl_surface_commit(s);
      }
      setWindowState(Qt::WindowFullScreen);
      layout()->activate();
    }
#endif
  }

  // bool event(QEvent *e) override {
  //   if (e->type() == QEvent::PlatformSurface) {
  //     std::cout << "PlatformSurface event" << std::endl;
  //     auto *pe = static_cast<QPlatformSurfaceEvent *>(e);
  //     if (pe->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
  //       std::cout << "QPlatformSurfaceEvent::SurfaceCreated event" << std::endl;
  //       setupFullscreen();
  //     }
  //   }
  //   return QDialog::event(e);
  // }

protected:
  void setScaledTitleText(QLabel *label, const QString &text) {
    label->setText(text);

    // Get available width (total header width minus back button, spacer, and margins)
    int availableWidth = 2160 - (2 * 220) - (2 * 30) - 100; // 2160px screen width - 2*button width - 2*margins - extra padding

    // Start with desired size and scale down if needed
    int fontSize = 50;
    QFont font = label->font();
    font.setWeight(QFont::DemiBold);

    do {
      font.setPixelSize(fontSize);
      QFontMetrics fm(font);
      int textWidth = fm.horizontalAdvance(text);

      if (textWidth <= availableWidth || fontSize <= 30) {
        break;
      }
      fontSize -= 2;
    } while (true);

    label->setFont(font);
  }
};

class BPConfirmationDialog : public BPDialogBase {
  Q_OBJECT

public:
  struct ConfirmConfig {
    QString title;
    QString prompt;
    QString confirmText = "Yes";
    QString cancelText = "No";
    bool richText = false;
    // Colors for buttons:
    QString confirmColor = "#2196F3";
    QString cancelColor = "#404040";
  };

  explicit BPConfirmationDialog(const ConfirmConfig &config, QWidget *parent = nullptr);
  static BPConfirmationDialog *showConfirmation(const ConfirmConfig &config, QWidget *parent = nullptr);
  static BPConfirmationDialog *showMessage(const ConfirmConfig &config, QWidget *parent = nullptr);

signals:
  void confirmed(bool accepted);

private:
  QLabel *title_label = nullptr;
  QLabel *prompt_label = nullptr;
  QPushButton *yesButton = nullptr;
  QPushButton *noButton = nullptr;
  bool rich;
  QString title;
  QString prompt;
  QString yesText;
  QString noText;
};

class BPSelectionDialog : public BPDialogBase {
  Q_OBJECT

public:
  struct Option {
    QString display; // Display name
    QString value;   // Stored value
  };

  static QString getValue(const QString &title, const QVector<Option> &options, const QString &currentValue, QWidget *parent = nullptr);
  static QString getSelection(const QString &title, const QStringList &items, const QString &current, QWidget *parent = nullptr);

private:
  explicit BPSelectionDialog(const QString &title, const QVector<Option> &options, const QString &currentValue, QWidget *parent = nullptr);
  QString selected;
  QString currentValue;
  QPushButton *selectButton = nullptr;
  QButtonGroup *buttonGroup = nullptr;
};

// =====================================
// BPCommandDialog
// =====================================
class BPCommandDialog : public BPDialogBase {
  Q_OBJECT

public:
  explicit BPCommandDialog(QWidget *parent = nullptr);

  void executeCommand(const QString &command, const QString &title, const QString &workingDir = QString(), const QJsonArray &actionButtons = QJsonArray());

signals:
  void dialogVisibilityChanged(bool visible);

protected:
  void showEvent(QShowEvent *event) override {
    QDialog::showEvent(event);
    emit dialogVisibilityChanged(true);
  }

  void hideEvent(QHideEvent *event) override {
    QDialog::hideEvent(event);
    emit dialogVisibilityChanged(false);
  }

private:
  // ==============
  // Internal UI
  // ==============
  QVBoxLayout *main_layout;
  QLabel *title_label;
  QTextEdit *outputText;
  QHBoxLayout *buttonLayout;
  BPButton *killButton;
  BPButton *closeButton;
  QProcess *process;

  // ==============
  // Internal logic
  // ==============
  void setupDialogStyle();
  void setupCommandUI(const QString &title);
  void setupActionButtons(const QJsonArray &actionButtons);
  QPushButton *createActionButton(const QJsonObject &buttonObj);

private slots:
  void handleProcessOutput();
  void handleProcessError();
  void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void killProcess();
};

// BPFileViewerControl: A "VIEW FILE" style control with BP UI
class BPFileViewerDialog : public BPDialogBase {
  Q_OBJECT

public:
  explicit BPFileViewerDialog(QWidget *parent = nullptr);
  void loadFileAndShow(const QString &path, const QString &header, const QString &fallbackTitle);

private:
  QVBoxLayout *main_layout;
  QLabel *title_label;
  QTextEdit *contentText;
  BPButton *closeButton;

  void setupDialogStyle();
  void setupUI(const QString &title);
};

class BPParamViewerDialog : public BPDialogBase {
  Q_OBJECT

public:
  explicit BPParamViewerDialog(QWidget *parent = nullptr);
  ~BPParamViewerDialog();
  void setupParamViewer(const QString &title, const QString &param);

public slots:
  void refreshParamValue();
  void toggleAutoRefresh(bool enabled);

private:
  void setupDialogStyle();
  void setupUI(const QString &title);

  QTimer *getRefreshTimer() { return TimerManager::getInstance().getTimer("paramViewer_refresh"); }

  QVBoxLayout *main_layout;
  QLabel *title_label;
  QTextEdit *paramContent;
  BPButton *closeButton;
  QCheckBox *autoRefreshCheckbox;
  QString paramName;
  Params params;
};

class BPParamListDialog : public BPDialogBase {
  Q_OBJECT

public:
  explicit BPParamListDialog(QWidget *parent = nullptr);
  void setupParamList();

signals:
  void paramViewRequested(const QString &param);

private:
  void setupDialogStyle();
  void setupUI();

  QVBoxLayout *main_layout;
  QLabel *title_label;
  BPButton *closeButton;
};
