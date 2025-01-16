// selfdrive/ui/bluepilot/qt/offroad/panels/git/git_directory_panel.h
#pragma once

// Forward declaration first
class GitDirectoryPanel;

#include <QWidget>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QProcess>
#include <QTimer>
#include <QtConcurrent>
#include <QScroller>
#include <QScrollerProperties>
#include <QScrollArea>
#include <QScrollBar>
#include <QThread>
#include <QTextEdit>
#include <iostream>

#ifdef QCOM2
#include <QGuiApplication>
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#include <QPlatformSurfaceEvent>
#endif

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/qt/widgets/controls.h"
#define AbstractControl AbstractControlSP
#define ParamControl ParamControlSP
#define ButtonControl ButtonControlSP
#define InputDialog InputDialogSP
#else
#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/widgets/input.h"
#endif

#include "selfdrive/ui/qt/offroad/settings.h"
#include "selfdrive/ui/qt/widgets/input.h"

class BranchSelector : public AbstractControl {
    Q_OBJECT
public:
    BranchSelector(const QString &title, const QString &text, const QString &desc, QWidget *parent = nullptr);
    QString getValue() const { return branchNameLabel->text().split(" ").first(); }
    void setValue(const QString &val, const QString &status = "", const QString &statusColor = "");
    QStringList getBranches(bool includeRemote) const;

signals:
    void clicked();

private:
    QPushButton btn;
    QLabel* branchNameLabel;
};

class GitStatusWidget : public QWidget {
    Q_OBJECT
public:
    explicit GitStatusWidget(QWidget* parent = nullptr);
    void setLastCheckTime(const QDateTime& time) { lastCheckTime = time; }
    QString getStatusText() const { return statusText; }
    QString getStatusColor() const { return statusColor; }
    void setStatusText(const QString& text) { statusText = text; }
    void setStatusColor(const QString& color) { statusColor = color; }
    void refresh();

signals:
    void statusUpdated();

private:
    QLabel* lastCommitLabel;
    QLabel* lastCommitInfo;
    QString statusText;
    QString statusColor;
    void updateStatus();
    QDateTime lastCheckTime;
};

class SubmoduleWidget : public QWidget {
    Q_OBJECT
public:
    QPushButton* updateModuleButton;
    QPushButton* resetModuleButton;
    QPushButton* repairModuleButton;
    explicit SubmoduleWidget(const QString& name, QWidget* parent = nullptr);
    void refresh();

signals:
    void statusChanged();

private slots:
    void handleModuleUpdate();
    void handleModuleReset();
    void handleModuleRepair();
    void handleShowCommits();

private:
    QString submoduleName;
    QLabel* statusLabel;

    QPushButton* showCommitsButton;
    QDateTime lastCheckTime;
    void updateStatus();
    // bool hasUpdatesAvailable() const;
    // bool hasLocalChanges() const;

    GitDirectoryPanel* findGitManagerPanel() const;
};

class GitDirectoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit GitDirectoryPanel(QWidget* parent = nullptr);
    ~GitDirectoryPanel();
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    static void showCommitHistory(QWidget* parent, const QString& title, const QString& workingDir);
    static QString getTimeAgoString(const QDateTime& time);
    static QString getTimeDateString(const QDateTime& time);
    void showCommandOutputDialog(const QString& title, const QString& command,
                                            const QString& workingDir, int timeoutMs,
                                            bool showKillBtn, bool showRetryBtn,
                                            bool showRebootBtn);

    static void setupFullscreenDialog(QDialog* dialog) {
        #ifdef QCOM2
        QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
        wl_surface *s = reinterpret_cast<wl_surface*>(native->nativeResourceForWindow("surface", dialog->windowHandle()));
        if (s) {
            wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
            wl_surface_commit(s);
        }
        dialog->setWindowState(Qt::WindowFullScreen);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->layout()->activate();
        void *egl = native->nativeResourceForWindow("egldisplay", dialog->windowHandle());
        assert(egl != nullptr);
        #endif
    }

private slots:
    void refreshAll();
    void handleRepoUpdate();
    void handleRepoUpdateAll();
    void handleRepoRepair();
    void handleRepoReset();
    void showLastCommits();
    void updateBranchList();
    void switchBranch(const QString& branch);
    void handleBranchSelection();

protected:
    bool event(QEvent *event) override {
        switch (event->type()) {
          case QEvent::MouseMove:
          case QEvent::MouseButtonPress:
          case QEvent::MouseButtonRelease:
          case QEvent::KeyPress:
          case QEvent::KeyRelease:
          case QEvent::Wheel:
            resetMaxDurationTimer();
            break;
          default:
            break;
        }
        return QWidget::event(event);
    }

private:
    Params params;
    bool commandInProgress = false;
    bool errorDialogShowing = false;
    QTimer* autoUpdateCheckTimer = nullptr;
    QTimer* activityTimer = nullptr;
    QLabel* updateChkBtnLabelTxt = nullptr;
    QLabel* updateChkBtnTimeTxt = nullptr;
    void startAutoUpdateChecks();
    void stopAutoUpdateChecks();
    void simulateActivity();
    void stopActivitySimulation();
    void resetMaxDurationTimer();
    bool isValidGitRepo() const;
    void showErrorState(const QString& message);
    QDialog* currentDialog = nullptr;
    QDateTime lastRefreshTime;
    QTimer* initTimer = nullptr;
    int initStage = 0;
    int currentSubmoduleIndex = 0;

    bool shouldRefresh() {
        return lastRefreshTime.isNull() || lastRefreshTime.secsTo(QDateTime::currentDateTime()) > 30;
    }

    bool isOnroad() {
        return params.getBool("IsOnroad");
    }

    void staggeredInit();
    // void refreshSubmodule(int index);
    bool hasUncommittedChanges() const;
    bool hasUpdatesAvailable() const;
    void setupParamMonitoring();
    QGroupBox* mainRepoGroup;
    GitStatusWidget* mainRepoStatus;
    BranchSelector* branchSelector;
    QPushButton* checkUpdatesButton;
    QPushButton* updateRepoButton;
    QPushButton* updateAllButton;
    QPushButton* repairRepoButton;
    QPushButton* resetRepoButton;
    QPushButton* showCommitsButton;

    QList<SubmoduleWidget*> submoduleWidgets;
    QTimer* refreshTimer;

    QDateTime lastUpdateCheck;
    void updateCheckUpdatesButtonText();

    void setupMainRepoSection();
    void setupSubmoduleSection();
    void setupLayout();
    void checkForUpdates();

    struct CommandResult {
        bool success;
        QString output;
        QString error;
        int exitCode;
        bool timedOut;
    };

    static CommandResult executeGitCommand(const QString& command,
                                     const QString& workingDir,
                                     int timeoutMs = 30000) {
        CommandResult result;
        result.success = false;
        result.timedOut = false;
        result.exitCode = -1;

        QProcess process;
        process.setWorkingDirectory(workingDir);

        process.start("/bin/bash", QStringList() << "-c" << command);

        if (!process.waitForStarted(5000)) {
            result.error = "Failed to start command: " + command;
            return result;
        }

        // Track overall execution time
        QElapsedTimer totalTimer;
        totalTimer.start();

        // Read output asynchronously
        QByteArray stdoutData;
        QByteArray stderrData;

        while (process.state() != QProcess::NotRunning) {
            // Check if total timeout has been exceeded
            if (totalTimer.elapsed() > timeoutMs) {
                process.kill();
                result.timedOut = true;
                result.error = "Command timed out after " +
                              QString::number(timeoutMs/1000) + " seconds";
                return result;
            }

            // Use a shorter timeout for individual reads
            if (process.waitForReadyRead(1000)) {
                stdoutData += process.readAllStandardOutput();
                stderrData += process.readAllStandardError();
            }
        }

        // Get remaining output
        stdoutData += process.readAllStandardOutput();
        stderrData += process.readAllStandardError();

        result.exitCode = process.exitCode();
        result.output = QString::fromUtf8(stdoutData);
        result.error = QString::fromUtf8(stderrData);
        result.success = (result.exitCode == 0);

        return result;
    }

    // Time-stamped cached internet connectivity result
    mutable QDateTime lastInternetCheckTime;
    mutable bool lastInternetCheckResult = false;
    static constexpr int INTERNET_CHECK_INTERVAL_SECS = 30;
    QLabel* updaterPanelStatusLabel;

    enum class UpdaterStatus {
        OK,
        NO_INTERNET,
        SSH_MISSING,
        SSH_AUTH_FAILED,
        NO_REMOTE_BRANCH,
        ONROAD
    };

    // Define message mapping using tuples
    const std::vector<std::tuple<UpdaterStatus, QString>> STATUS_MESSAGES = {
        {UpdaterStatus::OK, ""},
        {UpdaterStatus::NO_INTERNET, tr("No Internet Available")},
        {UpdaterStatus::SSH_MISSING, tr("SSH Config Missing")},
        {UpdaterStatus::SSH_AUTH_FAILED, tr("SSH Authentication Failed")},
        {UpdaterStatus::NO_REMOTE_BRANCH, tr("No Remote Branch")},
        {UpdaterStatus::ONROAD, tr("Vehicle in Motion")}
    };

    // Method declaration
    void updateStatusLabel(UpdaterStatus status) const;
    void updateButtonStates();
    bool isInternetAvailable() const;

    mutable QDateTime lastSSHCheckTime;
    mutable bool lastSSHCheckResult = false;
    static constexpr int SSH_CHECK_INTERVAL_SECS = 30;
    bool isSSHValid() const;
    bool checkAndRestoreSSH();
    bool checkRootDiskSpace();
    bool repairRootDiskSpace();
    bool restoreSSHFromUtility();
};
