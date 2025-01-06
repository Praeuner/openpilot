// selfdrive/ui/bluepilot/qt/offroad/config_driven_panel_dialogs.h
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

class ConfigDrivenConfirmationDialog : public ConfirmationDialog {
    Q_OBJECT

public:
    explicit ConfigDrivenConfirmationDialog(const QString &prompt_text, const QString &confirm_text,
                                          const QString &cancel_text, const bool rich, QWidget* parent);
    static bool toggle(const QString &prompt_text, const QString &confirm_text, QWidget *parent);
    static bool toggleAlert(const QString &prompt_text, const QString &button_text, QWidget *parent);
    static bool yesorno(const QString &prompt_text, QWidget *parent);
};

class ConfigDrivenFullScreenDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConfigDrivenFullScreenDialog(QWidget *parent = nullptr);
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

class ConfigDrivenCommandDialog : public ConfigDrivenFullScreenDialog {
    Q_OBJECT

public:
    explicit ConfigDrivenCommandDialog(QWidget *parent = nullptr);
    void executeCommand(const QString& command, const QString& title,
                       const QString& workingDir = QString(),
                       const QJsonArray& actionButtons = QJsonArray());

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


class ConfigDrivenParamViewerDialog : public ConfigDrivenFullScreenDialog {
    Q_OBJECT

public:
    explicit ConfigDrivenParamViewerDialog(QWidget *parent = nullptr);
    void setupParamViewer(const QString& title, const QString& param);

public slots:  // Change private slots to public slots
    void refreshParamValue();
    void toggleAutoRefresh(bool enabled);

private:
    QTextEdit* paramContent;
    QPushButton* refreshButton;
    QTimer* refreshTimer;
    QString paramName;
    QCheckBox* autoRefreshCheckbox;
};
