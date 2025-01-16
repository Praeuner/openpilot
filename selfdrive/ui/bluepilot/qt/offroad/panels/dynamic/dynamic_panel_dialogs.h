// selfdrive/ui/bluepilot/qt/offroad/panels/dynamic/dynamic_panel_dialogs.h

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

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/qt/widgets/controls.h"
#define AbstractControl AbstractControlSP
#define ParamControl ParamControlSP
#define ButtonControl ButtonControlSP
#else
#include "selfdrive/ui/qt/widgets/controls.h"
#endif

#include "selfdrive/ui/qt/widgets/scrollview.h"

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#include <QPlatformSurfaceEvent>
#endif

class DynamicPanelConfirmationDialog : public ConfirmationDialog {
    Q_OBJECT

public:
    explicit DynamicPanelConfirmationDialog(const QString &prompt_text, const QString &confirm_text,
                                          const QString &cancel_text, const bool rich, QWidget* parent);
    static bool toggle(const QString &prompt_text, const QString &confirm_text, QWidget *parent);
    static bool toggleAlert(const QString &prompt_text, const QString &button_text, QWidget *parent);
    static bool yesorno(const QString &prompt_text, QWidget *parent);
};

class DynamicPanelFullScreenDialog : public QDialog {
    Q_OBJECT

public:
    explicit DynamicPanelFullScreenDialog(QWidget *parent = nullptr);
    void setupContent(const QString& title, const QString& content);
    void setupFullscreen();
    QString getDialogStyle() const;
    QVBoxLayout* main_layout;
    #ifdef QCOM2
    void setupWaylandSurface();
    #endif

protected:
    QLabel* title_label;
    ScrollView* scroll;
    QPushButton* close_btn;
};

class DynamicPanelCommandDialog : public DynamicPanelFullScreenDialog {
    Q_OBJECT

public:
    explicit DynamicPanelCommandDialog(QWidget *parent = nullptr);
    void executeCommand(const QString& command, const QString& title,
                       const QString& workingDir = QString(),
                       const QJsonArray& actionButtons = QJsonArray());

Q_SIGNALS:
    void dialogVisibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override {
        DynamicPanelFullScreenDialog::showEvent(event);
        emit dialogVisibilityChanged(true);
    }

    void hideEvent(QHideEvent* event) override {
        DynamicPanelFullScreenDialog::hideEvent(event);
        emit dialogVisibilityChanged(false);
    }

private:
    QTextEdit* outputText;
    QPushButton* killButton;
    QPushButton* closeButton;
    QProcess* process;
    QHBoxLayout* buttonLayout;

    void setupCommandUI(const QString& title);
    void setupActionButtons(const QJsonArray& actionButtons);
    void handleProcessOutput();
    void handleProcessError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void killProcess();
    QPushButton* createActionButton(const QJsonObject& buttonObj);
    QString getButtonStyle(const QJsonObject& style) const;
};


class DynamicPanelParamViewerDialog : public DynamicPanelFullScreenDialog {
    Q_OBJECT

public:
    explicit DynamicPanelParamViewerDialog(QWidget *parent = nullptr);
    void setupParamViewer(const QString& title, const QString& param);

public slots:  // Change private slots to public slots
    void refreshParamValue();
    void toggleAutoRefresh(bool enabled);

private:
    QTextEdit* paramContent;
    // QPushButton* refreshButton;
    QTimer* refreshTimer;
    QString paramName;
    QCheckBox* autoRefreshCheckbox;
};
