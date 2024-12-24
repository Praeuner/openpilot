// selfdrive/ui/qt/offroad/git_manager_panel.cc

#include "selfdrive/ui/ui.h"
#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/widgets/scrollview.h"
#include "selfdrive/ui/qt/offroad/git_manager_panel.h"
#include "common/params.h"

#include <QScrollArea>
#include <QDialog>
#include <QTextEdit>
#include <QScreen>
#include <QFile>
#include <QTableWidget>
#include <QCoreApplication>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QWidget>
#include <QApplication>
#include <QProcess>
#include <QTimer>
#include <iostream>
#include <QtConcurrent>

GitStatusWidget::GitStatusWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(3);  // Reduced spacing

    // Create a QFrame as a container
    QFrame* commitFrame = new QFrame(this);
    commitFrame->setStyleSheet(R"(
        QFrame {
            background-color: #1B1B1B;
            border-radius: 20px;
            padding: 5px;
        }
    )");

    QVBoxLayout* commitLayout = new QVBoxLayout(commitFrame);
    commitLayout->setContentsMargins(15, 10, 15, 10);  // Reduced margins
    commitLayout->setSpacing(5);  // Reduced spacing

    // Create header container with horizontal layout
    QWidget* headerContainer = new QWidget(this);
    QHBoxLayout* headerLayout = new QHBoxLayout(headerContainer);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);

    // Add header label
    QLabel* headerLabel = new QLabel(tr("Last Commit"), this);
    headerLabel->setStyleSheet("font-size: 30px; color: #888888; font-weight: 500;");
    headerLayout->addWidget(headerLabel);

    // Add spacer to push commit info to right
    headerLayout->addStretch();

    // Add commit id/timestamp container
    lastCommitInfo = new QLabel(this);
    lastCommitInfo->setStyleSheet("font-size: 30px; color: #888888;");
    headerLayout->addWidget(lastCommitInfo);

    commitLayout->addWidget(headerContainer);

    // Create commit message label
    lastCommitLabel = new QLabel(this);
    lastCommitLabel->setStyleSheet("font-size: 30px; color: white; padding-top: 5px;");
    lastCommitLabel->setWordWrap(true);
    commitLayout->addWidget(lastCommitLabel);

    mainLayout->addWidget(commitFrame);
}

void GitStatusWidget::refresh() {
    if (!isVisible()) return;

    lastCheckTime = QDateTime::currentDateTime();
    QtConcurrent::run([=]() {
        QProcess process;
        process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");

        try {
            process.start("git", QStringList() << "status" << "--porcelain");
            if (!process.waitForFinished(10000)) {
                std::cerr << "Git status command timed out" << std::endl;
                return;
            }

            QString status;
            QString color;
            if (process.exitCode() == 0) {
                status = QString::fromUtf8(process.readAllStandardOutput());
                if (status.isEmpty()) {
                    status = tr(" - Clean");
                    color = "#50d332";
                } else {
                    status = tr(" - Modified");
                    color = "#ff7c30";
                }
            }

            process.start("git", QStringList() << "log" << "-1" << "--pretty=format:%h - %s (%cr)");
            if (!process.waitForFinished(10000)) {
                std::cerr << "Git log command timed out" << std::endl;
                return;
            }

            QString lastCommit;
            if (process.exitCode() == 0) {
                lastCommit = QString::fromUtf8(process.readAllStandardOutput());
            }

            QMetaObject::invokeMethod(this, [=]() {
                QString statusWithTime = status;
                if (lastCheckTime.isValid()) {
                    statusWithTime += QString(" (%1)").arg(GitManagerPanel::getTimeDateString(lastCheckTime));
                }
                statusText = statusWithTime;
                statusColor = color;
                lastCommitLabel->setText(lastCommit.isEmpty() ? "No commits" : tr("Last Commit: %1").arg(lastCommit));
                emit statusUpdated();
            }, Qt::QueuedConnection);

            QMetaObject::invokeMethod(this, [=]() {
                if (!lastCommit.isEmpty()) {
                    // Split hash and message at the first " - "
                    QString hash = lastCommit.section(" - ", 0, 0);
                    QString message = lastCommit.section(" - ", 1);

                    // Get time part with parentheses
                    QString timeAgo = message.section("(", -1);
                    if (!timeAgo.isEmpty()) {
                        timeAgo = "(" + timeAgo;
                    }

                    // Get commit message without the time part
                    QString commitMsg = message.section("(", 0, 0).trimmed();

                    // Set the commit info (hash and timestamp)
                    QString commitInfo = QString("<span style='color: #465BEA; font-family: monospace;'>%1</span> "
                                              "<span style='color: #888888;'>%2</span>")
                                              .arg(hash, timeAgo);
                    lastCommitInfo->setText(commitInfo);

                    // Set the commit message
                    lastCommitLabel->setText(commitMsg);
                } else {
                    lastCommitInfo->clear();
                    lastCommitLabel->setText(tr("No commits"));
                }
                emit statusUpdated();
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            std::cerr << "Exception in GitStatusWidget::refresh:" << e.what() << std::endl;
        }
    });
}

void GitStatusWidget::updateStatus() {
    refresh();
}

SubmoduleWidget::SubmoduleWidget(const QString& name, QWidget* parent) : QWidget(parent), submoduleName(name) {
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 10, 0, 10);

    // Create a widget to contain the name and status labels
    QWidget* labelContainer = new QWidget(this);
    QHBoxLayout* labelLayout = new QHBoxLayout(labelContainer);
    labelLayout->setContentsMargins(0, 0, 0, 0);
    labelLayout->setSpacing(10);

    QLabel* nameLabel = new QLabel(name, this);
    nameLabel->setStyleSheet("font-size: 35px; font-weight: bold;");
    nameLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    statusLabel = new QLabel(this);
    statusLabel->setStyleSheet("font-size: 35px;");
    statusLabel->setText(tr("• Checking..."));
    statusLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    // Add both labels to the container layout with vertical center alignment
    labelLayout->addWidget(nameLabel, 0, Qt::AlignVCenter);
    labelLayout->addWidget(statusLabel, 0, Qt::AlignVCenter);
    labelLayout->addStretch();

    // Add the label container to the main layout
    layout->addWidget(labelContainer);

    // Button section remains the same
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    updateModuleButton = new QPushButton(tr("Update"), this);
    resetModuleButton = new QPushButton(tr("Reset"), this);
    repairModuleButton = new QPushButton(tr("Repair"), this);
    showCommitsButton = new QPushButton(tr("History"), this);

    QString buttonStyle = R"(
        QPushButton {
            border-radius: 20px;
            font-size: 35px;
            padding: 10px;
            background-color: #393939;
            color: white;
            min-width: 150px;
        }
        QPushButton:pressed { background-color: #494949; }
        QPushButton:disabled { background-color: #393939; color: #888888; }
    )";

    updateModuleButton->setStyleSheet(buttonStyle);
    resetModuleButton->setStyleSheet(buttonStyle);
    repairModuleButton->setStyleSheet(buttonStyle);
    showCommitsButton->setStyleSheet(buttonStyle);

    updateModuleButton->setVisible(false);
    resetModuleButton->setVisible(false);

    buttonLayout->addWidget(updateModuleButton);
    buttonLayout->addWidget(resetModuleButton);
    buttonLayout->addWidget(repairModuleButton);
    buttonLayout->addWidget(showCommitsButton);
    buttonLayout->setContentsMargins(0, 0, 20, 0);

    // layout->addLayout(infoLayout);
    layout->addLayout(buttonLayout);

    connect(updateModuleButton, &QPushButton::clicked, this, &SubmoduleWidget::handleModuleUpdate);
    connect(resetModuleButton, &QPushButton::clicked, this, &SubmoduleWidget::handleModuleReset);
    connect(repairModuleButton, &QPushButton::clicked, this, &SubmoduleWidget::handleModuleRepair);
    connect(showCommitsButton, &QPushButton::clicked, this, &SubmoduleWidget::handleShowCommits);
}

void SubmoduleWidget::handleModuleUpdate() {
    QString command = QString("git submodule update --init --recursive %1").arg(submoduleName);
    if (auto* panel = findGitManagerPanel()) {
        panel->showCommandOutputDialog(tr("Update Submodule"), command, "", 30000, true, true, true);
    }
}

void SubmoduleWidget::handleModuleReset() {
    if (!ConfirmationDialog::confirm(
        tr("Are you sure you want to reset the submodule '%1'?\n"
           "This will lose all local changes.").arg(submoduleName),
        tr("Reset"),
        this)) {
        return;
    }

    QString command = QString("cd %1 && git reset --hard HEAD").arg(submoduleName);
    if (auto* panel = findGitManagerPanel()) {
        panel->showCommandOutputDialog(tr("Reset Submodule"), command, "", 30000, true, true, true);
    }
}

void SubmoduleWidget::handleModuleRepair() {
    if (!ConfirmationDialog::confirm(
        tr("Are you sure you want to repair the submodule '%1'?\n"
           "This will remove and re-download it.").arg(submoduleName),
        tr("Repair"),
        this)) {
        return;
    }

    QString command = QString(
        "git submodule deinit -f %1 && "
        "rm -rf .git/modules/%1 && "
        "git submodule update --init --recursive %1"
    ).arg(submoduleName);

    if (auto* panel = findGitManagerPanel()) {
        panel->showCommandOutputDialog(tr("Repair Submodule"), command, "", 30000, true, true, true);
    }
}

void SubmoduleWidget::handleShowCommits() {
    if (auto* panel = findGitManagerPanel()) {
        QString title = submoduleName + " - Last 30 Commits";
        panel->showCommitHistory(
            panel,
            title,
            qApp->applicationDirPath() + "/../.." + "/" + submoduleName
        );
    }
}

GitManagerPanel* SubmoduleWidget::findGitManagerPanel() const {
    QWidget* parent = parentWidget();
    while (parent) {
        if (auto* panel = qobject_cast<GitManagerPanel*>(parent)) {
            return panel;
        }
        parent = parent->parentWidget();
    }
    return nullptr;
}

void SubmoduleWidget::refresh() {
    if (!isVisible()) return;

    lastCheckTime = QDateTime::currentDateTime();

    QtConcurrent::run([=]() {
        QProcess process;
        QString workingDir = qApp->applicationDirPath() + "/../.." + "/" + submoduleName;
        process.setWorkingDirectory(workingDir);

        try {
            // First check if the directory exists and is a git repository
            process.start("git", QStringList() << "rev-parse" << "--git-dir");
            if (!process.waitForFinished(5000)) {
                QMetaObject::invokeMethod(this, [=]() {
                    statusLabel->setText(tr("Not Initialized"));
                    statusLabel->setStyleSheet("color: #ff7c30; font-size: 25px;");
                }, Qt::QueuedConnection);
                return;
            }

            // Check submodule status
            process.start("git", QStringList() << "status" << "--porcelain");
            if (!process.waitForFinished(5000)) {
                std::cerr << "Git status command timed out for " << submoduleName.toStdString() << std::endl;
                return;
            }

            bool localChanges = !QString::fromUtf8(process.readAllStandardOutput()).isEmpty();

            // Check for updates
            process.start("git", QStringList() << "fetch");
            process.waitForFinished(30000);

            process.start("git", QStringList() << "rev-list" << "HEAD..@{u}" << "--count");
            if (!process.waitForFinished(5000)) {
                return;
            }

            bool hasUpdates = QString::fromUtf8(process.readAllStandardOutput()).trimmed().toInt() > 0;

            // Update status label based on conditions
            QString status;
            QString color;

            if (localChanges) {
                status = tr("(Modified)");
                color = "#ff7c30";
            } else if (hasUpdates) {
                status = tr("(Updates Available)");
                color = "#465BEA";
            } else {
                status = tr("(Up to date)");
                color = "#50d332";
            }

            // Ensure status updates are applied on the UI thread
            QMetaObject::invokeMethod(this, [=]() {
                statusLabel->setText(status);
                statusLabel->setStyleSheet(QString("color: %1; font-size: 25px; border: none;").arg(color));
                updateModuleButton->setVisible(hasUpdates);
                resetModuleButton->setVisible(localChanges);
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            std::cerr << "Exception in SubmoduleWidget::refresh for " << submoduleName.toStdString()
                    << ": " << e.what() << std::endl;
        }
    });
}

void SubmoduleWidget::updateStatus() {
    refresh();
}

GitManagerPanel::~GitManagerPanel() {
    if (autoUpdateCheckTimer) {
        autoUpdateCheckTimer->stop();
        delete autoUpdateCheckTimer;
    }
    std::cout << "GitManagerPanel destructor complete" << std::endl;
}

GitManagerPanel::GitManagerPanel(QWidget* parent) : QWidget(parent), branchSelector(nullptr) {
    std::cout << "GitManagerPanel constructor start" << std::endl;

    setFixedWidth(1640);

    if (!isValidGitRepo()) {
        showErrorState(tr("Error: Not a valid git repository.\nPlease ensure /data/openpilot is a valid git repository."));
        return;
    }

    // Initialize the activity timer
    activityTimer = new QTimer(this);
    activityTimer->setInterval(9000); // 9 seconds
    connect(activityTimer, &QTimer::timeout, this, &GitManagerPanel::simulateActivity);

    // Add automatic update check timer
    autoUpdateCheckTimer = new QTimer(this);
    autoUpdateCheckTimer->setInterval(900000);  // 15 minutes
    connect(autoUpdateCheckTimer, &QTimer::timeout, this, &GitManagerPanel::checkForUpdates);
    autoUpdateCheckTimer->start();

    // Add the timer before setting up the layout
    QTimer* timeUpdateTimer = new QTimer(this);
    timeUpdateTimer->setInterval(60000);  // Update every minute
    connect(timeUpdateTimer, &QTimer::timeout, this, [this]() {
        if (mainRepoStatus) mainRepoStatus->refresh();
        for (auto* widget : submoduleWidgets) {
            if (widget) widget->refresh();
        }
    });
    timeUpdateTimer->start();

    setupLayout();
    updateBranchList();
    updateButtonStates();
    std::cout << "GitManagerPanel constructor end" << std::endl;
}

void GitManagerPanel::simulateActivity() {
    // Create a mouse move event at the current cursor position
    QPoint globalPos = QCursor::pos();
    QPoint localPos = this->mapFromGlobal(globalPos);

    // Add small random movement to simulate real activity
    localPos += QPoint(rand() % 5 - 2, rand() % 5 - 2);
    globalPos += QPoint(rand() % 5 - 2, rand() % 5 - 2);

    QMouseEvent mouseEvent(QEvent::MouseMove, localPos, globalPos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);

    std::cout << "Simulating activity in GitManagerPanel" << std::endl;
    // Send the event to this widget
    QCoreApplication::sendEvent(this, &mouseEvent);
}

void GitManagerPanel::stopActivitySimulation() {
    std::cout << "Stopping GitManagerPanel activity simulation | max duration timer stopped" << std::endl;
    activityTimer->stop();
}

void GitManagerPanel::resetMaxDurationTimer() {
    // Reset the max duration timer
    QTimer::singleShot(270000, this, &GitManagerPanel::stopActivitySimulation); // 4 minutes and 30 seconds
}

void GitManagerPanel::setupMainRepoSection() {
    mainRepoGroup = new QGroupBox(tr("Openpilot Directory"), this);
    mainRepoGroup->setStyleSheet(R"(
        QGroupBox {
            border: 1px solid #cccccc;
            border-radius: 10px;
            margin-top: 30px;
            font-weight: bold;
            padding: 20px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 20px;
            padding: 0 15px;
            color: white;
            font-size: 40px;
        }
    )");

    QVBoxLayout* mainRepoLayout = new QVBoxLayout(mainRepoGroup);
    mainRepoLayout->setContentsMargins(20, 20, 20, 20);
    mainRepoLayout->setSpacing(15);

    // Create info section
    QWidget* infoWidget = new QWidget(mainRepoGroup);
    QVBoxLayout* infoLayout = new QVBoxLayout(infoWidget);
    infoLayout->setSpacing(10);

    updaterPanelStatusLabel = new QLabel(this);
    updaterPanelStatusLabel->setStyleSheet("font-size: 35px; color: red;");
    updaterPanelStatusLabel->setVisible(false);
    updaterPanelStatusLabel->setAlignment(Qt::AlignRight);
    infoLayout->addWidget(updaterPanelStatusLabel);
    updateStatusLabel(UpdaterStatus::OK);

    mainRepoStatus = new GitStatusWidget(infoWidget);
    infoLayout->addWidget(mainRepoStatus);

    // Add branch selector with improved styling
    branchSelector = new BranchSelector("", "", "");
    branchSelector->setStyleSheet(R"(
        QWidget {
            background-color: #2D2D2D;
            border-radius: 10px;
            padding: 5px;
        }
    )");
    connect(branchSelector, &BranchSelector::clicked, this, &GitManagerPanel::handleBranchSelection);
    infoLayout->addWidget(branchSelector);

    mainRepoLayout->addWidget(infoWidget);

    // Create button container with improved layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(15);

    // Update button styles
    QString baseButtonStyle = R"(
        QPushButton {
            border-radius: 10px;
            font-size: 35px;
            padding: 15px 25px;
            min-height: 70px;
            font-weight: 500;
            color: white;
        }
        QPushButton:pressed { opacity: 0.8; }
    )";

    // Create and style buttons with specific colors
    checkUpdatesButton = new QPushButton(this);
    checkUpdatesButton->setAutoFillBackground(true);

    // Create the permanent layout and labels
    QVBoxLayout* updateBtnLayout = new QVBoxLayout(checkUpdatesButton);
    updateBtnLayout->setSpacing(0);
    updateBtnLayout->setContentsMargins(0, 0, 0, 0);

    updateChkBtnLabelTxt = new QLabel(tr("Check Updates"), checkUpdatesButton);
    updateChkBtnLabelTxt->setAlignment(Qt::AlignCenter);
    updateChkBtnLabelTxt->setStyleSheet("color: white; font-size: 35px; background: transparent;");

    updateChkBtnTimeTxt = new QLabel("", checkUpdatesButton);
    updateChkBtnTimeTxt->setAlignment(Qt::AlignCenter);
    updateChkBtnTimeTxt->setStyleSheet("color: white; opacity: 0.8; font-size: 25px; background: transparent;");
    updateChkBtnTimeTxt->setFixedHeight(updateChkBtnTimeTxt->sizeHint().height());
    updateChkBtnTimeTxt->setMinimumHeight(0);

    // Add stretches for vertical centering
    updateBtnLayout->addStretch();
    updateBtnLayout->addWidget(updateChkBtnLabelTxt);
    updateBtnLayout->addWidget(updateChkBtnTimeTxt);
    updateBtnLayout->addStretch();

    checkUpdatesButton->setStyleSheet(QString(baseButtonStyle) +
        "QPushButton { background-color: #465BEA; }"
        "QPushButton:pressed { background-color: #3049F4; }"
        "QPushButton:disabled { background-color: #4F4F4F; color: #888888; }");

    updateRepoButton = new QPushButton(tr("Update"));
    updateRepoButton->setStyleSheet(QString(baseButtonStyle) +
        "QPushButton { background-color: #33AB4C; }"
        "QPushButton:pressed { background-color: #2A9040; }"
        "QPushButton:disabled { background-color: #4F4F4F; color: #888888; }");
    updateRepoButton->setVisible(false);

    updateAllButton = new QPushButton(tr("Update All"));
    updateAllButton->setStyleSheet(QString(baseButtonStyle) +
        "QPushButton { background-color: #33AB4C; }"
        "QPushButton:pressed { background-color: #2A9040; }"
        "QPushButton:disabled { background-color: #4F4F4F; color: #888888; }");
    updateAllButton->setVisible(false);

    repairRepoButton = new QPushButton(tr("Repair"));
    repairRepoButton->setStyleSheet(QString(baseButtonStyle) +
        "QPushButton { background-color: #7B1FA2; }"
        "QPushButton:pressed { background-color: #6A1B9A; }"
        "QPushButton:disabled { background-color: #4F4F4F; color: #888888; }");

    resetRepoButton = new QPushButton(tr("Reset"));
    resetRepoButton->setStyleSheet(QString(baseButtonStyle) +
        "QPushButton { background-color: #EA4646; }"
        "QPushButton:pressed { background-color: #F43030; }"
        "QPushButton:disabled { background-color: #4F4F4F; color: #888888; }");
    resetRepoButton->setVisible(false);

    showCommitsButton = new QPushButton(tr("History"));
    showCommitsButton->setStyleSheet(QString(baseButtonStyle) +
        "QPushButton { background-color: #33AB4C; }"
        "QPushButton:pressed { background-color: #2A9040; }"
        "QPushButton:disabled { background-color: #4F4F4F; color: #888888; }");

    buttonLayout->addWidget(checkUpdatesButton);
    buttonLayout->addWidget(updateRepoButton);
    buttonLayout->addWidget(updateAllButton);
    buttonLayout->addWidget(repairRepoButton);
    buttonLayout->addWidget(resetRepoButton);
    buttonLayout->addWidget(showCommitsButton);

    mainRepoLayout->addLayout(buttonLayout);

    // Connect signals
    connect(checkUpdatesButton, &QPushButton::clicked, this, &GitManagerPanel::checkForUpdates);
    connect(updateRepoButton, &QPushButton::clicked, this, &GitManagerPanel::handleRepoUpdate);
    connect(updateAllButton, &QPushButton::clicked, this, &GitManagerPanel::handleRepoUpdateAll);
    connect(repairRepoButton, &QPushButton::clicked, this, &GitManagerPanel::handleRepoRepair);
    connect(resetRepoButton, &QPushButton::clicked, this, &GitManagerPanel::handleRepoReset);
    connect(showCommitsButton, &QPushButton::clicked, this, &GitManagerPanel::showLastCommits);
}

void GitManagerPanel::setupSubmoduleSection() {
    QGroupBox* submoduleGroup = new QGroupBox(tr("Submodules"), this);
    submoduleGroup->setStyleSheet(R"(
      QGroupBox {
        border: 1px solid #cccccc;
        border-radius: 10px;
        margin-top: 30px;
        font-weight: bold;
        padding: 15px;
      }
      QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top left;
        left: 20px;
        padding: 0 10px;
        color: white;
      }
    )");

    QVBoxLayout* submoduleLayout = new QVBoxLayout(submoduleGroup);
    submoduleLayout->setContentsMargins(20, 20, 20, 20);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet(R"(
        QScrollArea {
            border: none;
            background: transparent;
        }
        QScrollBar:vertical {
            width: 10px;
            background: #1e1e1e;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            min-height: 30px;
            border-radius: 5px;
            background: #465BEA;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {
            height: 0px;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }
    )");

    QWidget* scrollContent = new QWidget(scrollArea);
    QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 15, 0);

    QProcess process;
    process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");
    process.start("git", QStringList() << "submodule" << "status");
    process.waitForFinished();
    QString output = QString::fromUtf8(process.readAllStandardOutput());

    if (output.isEmpty()) {
        QLabel* errorLabel = new QLabel(tr("No submodules found"), this);
        errorLabel->setStyleSheet(R"(
            font-size: 45px;
            color: #ff7c30;
            padding: 40px;
            background-color: #1B1B1B;
            border-radius: 10px;
        )");
        errorLabel->setAlignment(Qt::AlignCenter);
        scrollLayout->addWidget(errorLabel);
    } else {
        for (const QString& line : output.split("\n", QString::SkipEmptyParts)) {
            QString submoduleName = line.mid(1).split(" ").at(1);
            SubmoduleWidget* submodule = new SubmoduleWidget(submoduleName, this);
            submoduleWidgets.append(submodule);
            scrollLayout->addWidget(submodule);
        }
    }

    scrollContent->setLayout(scrollLayout);
    scrollArea->setWidget(scrollContent);
    submoduleLayout->addWidget(scrollArea);
    submoduleGroup->setLayout(submoduleLayout);
}

bool GitManagerPanel::isValidGitRepo() const {
    QProcess process;
    process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");
    process.start("git", QStringList() << "rev-parse" << "--git-dir");
    return process.waitForFinished(5000) && process.exitCode() == 0;
}

void GitManagerPanel::setupLayout() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    if (!isValidGitRepo()) {
        QLabel* errorLabel = new QLabel(tr("Error: Not a valid git repository.\nPlease ensure /data/openpilot is a valid git repository."), this);
        errorLabel->setStyleSheet(R"(
            font-size: 45px;
            color: #ff7c30;
            padding: 40px;
            background-color: #1B1B1B;
            border-radius: 10px;
        )");
        errorLabel->setAlignment(Qt::AlignCenter);
        mainLayout->addWidget(errorLabel);
        setLayout(mainLayout);
        return;
    }

    setupMainRepoSection();
    mainLayout->addWidget(mainRepoGroup);

    QTimer::singleShot(0, this, [this, mainLayout]() {
        setupSubmoduleSection();
        QList<QGroupBox*> groups = findChildren<QGroupBox*>();
        if (groups.size() > 1) {
            mainLayout->addWidget(groups[1]);
        }
    });

    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(60000);
    connect(refreshTimer, &QTimer::timeout, this, &GitManagerPanel::refreshAll);

    setLayout(mainLayout);
}

void GitManagerPanel::showEvent(QShowEvent *event) {
    std::cout << "Showing GitManagerPanel" << std::endl;
    QWidget::showEvent(event);

    // Reset initialization state
    initStage = 0;
    currentSubmoduleIndex = 0;

    // Clean up existing timer if any
    if (initTimer) {
        initTimer->stop();
        disconnect(initTimer, nullptr, this, nullptr);
        delete initTimer;
        initTimer = nullptr;
    }

    // Create new timer
    initTimer = new QTimer(this);
    connect(initTimer, &QTimer::timeout, this, &GitManagerPanel::staggeredInit);

    // Start staggered initialization
    initTimer->start(100);

    // Start activity simulation
    activityTimer->start();
    resetMaxDurationTimer();

    // Start auto update checks
    startAutoUpdateChecks();

    std::cout << "Started initialization and activity timers" << std::endl;
}

void GitManagerPanel::hideEvent(QHideEvent *event) {
    std::cout << "Hiding GitManagerPanel" << std::endl;

    if (refreshTimer) {
        refreshTimer->stop();
        std::cout << "Refresh timer stopped" << std::endl;
    }

    if (initTimer) {
        initTimer->stop();
        disconnect(initTimer, nullptr, this, nullptr);
        delete initTimer;
        initTimer = nullptr;
        std::cout << "Initialization timer cleaned up" << std::endl;
    }

    // Stop activity timer
    if (activityTimer) {
        activityTimer->stop();
        std::cout << "Activity timer stopped" << std::endl;
    }

    // Stop auto update checks
    stopAutoUpdateChecks();

    QWidget::hideEvent(event);
}

void GitManagerPanel::startAutoUpdateChecks() {
    if (!autoUpdateCheckTimer) {
        autoUpdateCheckTimer = new QTimer(this);
        autoUpdateCheckTimer->setInterval(900000);  // 15 minutes
        connect(autoUpdateCheckTimer, &QTimer::timeout, this, &GitManagerPanel::checkForUpdates);
    }

    // Initial check after a short delay when UI is shown
    QTimer::singleShot(2000, this, &GitManagerPanel::checkForUpdates);

    // Start the timer for subsequent checks
    autoUpdateCheckTimer->start();
}

void GitManagerPanel::stopAutoUpdateChecks() {
    if (autoUpdateCheckTimer) {
        autoUpdateCheckTimer->stop();
    }
}

void GitManagerPanel::refreshAll() {
    if (!shouldRefresh()) {
        return;
    }
    lastRefreshTime = QDateTime::currentDateTime();

    // Refresh main repo status
    if (mainRepoStatus) {
        mainRepoStatus->refresh();
    }

    // Update branch list
    updateBranchList();

    // Check for updates and local changes
    QtConcurrent::run([=]() {
        bool hasLocal = hasUncommittedChanges();
        bool hasUpdates = hasUpdatesAvailable();

        QMetaObject::invokeMethod(this, [=]() {

            resetRepoButton->setVisible(hasLocal);
            updateRepoButton->setVisible(hasUpdates);

            updateButtonStates();
            QString status;
            QString color;
            if (hasLocal && hasUpdates) {
                status = tr("(Updates Available - Local Modified)");
                color = "#FF3C0F";  // Or any color that stands out
            } else if (hasUpdates) {
                status = tr("(Updates Available)");
                color = "#465BEA";
            } else if (hasLocal) {
                status = tr("(Modified)");
                color = "#ff7c30";
            } else {
                status = tr("(Clean)");
                color = "#50d332";
            }

            if (branchSelector) {
                QString currentBranch = branchSelector->getValue();
                branchSelector->setValue(currentBranch, status, color);
            }
        }, Qt::QueuedConnection);
    });

    // Refresh all submodules with a slight delay between each
    for (int i = 0; i < submoduleWidgets.size(); ++i) {
        QTimer::singleShot(i * 500, this, [this, i]() {
            if (i < submoduleWidgets.size() && submoduleWidgets[i]) {
                submoduleWidgets[i]->refresh();
            }
        });
    }
}

QStringList BranchSelector::getBranches(bool includeRemote) const {
    QStringList branches;
    QProcess process;
    process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");

    if (includeRemote) {
        // Fetch remote branches first
        process.start("git", QStringList() << "fetch" << "--prune");
        process.waitForFinished(30000);

        // Get only origin remote branches
        process.start("git", QStringList() << "branch" << "-r");
        if (process.waitForFinished(5000)) {
            QString output = QString::fromUtf8(process.readAllStandardOutput());
            for (QString branch : output.split("\n", QString::SkipEmptyParts)) {
                branch = branch.trimmed();
                // Only include branches that start with "origin/" and aren't HEAD
                if (branch.startsWith("origin/") && !branch.contains("HEAD")) {
                    // Remove the "origin/" prefix
                    branch.remove(0, 7); // "origin/".length() == 7
                    if (!branch.isEmpty() && !branches.contains(branch)) {
                        branches.append(branch);
                    }
                }
            }
        }
    }

    // Get local branches
    process.start("git", QStringList() << "branch");
    if (process.waitForFinished(5000)) {
        QString output = QString::fromUtf8(process.readAllStandardOutput());
        for (QString branch : output.split("\n", QString::SkipEmptyParts)) {
            branch = branch.trimmed();
            if (branch.startsWith("*")) {
                branch = branch.mid(2);
            }
            if (!branches.contains(branch)) {
                branches.append(branch);
            }
        }
    }

    return branches;
}

void GitManagerPanel::updateBranchList() {
    if (!branchSelector) return;

    QProcess process;
    process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");

    // Just get current branch for initial display
    process.start("git", QStringList() << "branch");
    if (process.waitForFinished(5000)) {
        QString output = QString::fromUtf8(process.readAllStandardOutput());
        for (const QString& branch : output.split("\n", QString::SkipEmptyParts)) {
            QString cleanBranch = branch.trimmed();
            if (cleanBranch.startsWith("*")) {
                branchSelector->setValue(cleanBranch.mid(2));
                break;
            }
        }
    }
}

void GitManagerPanel::handleBranchSelection() {
    QTimer::singleShot(0, this, [this]() {
        bool internetAvailable = isInternetAvailable();
        QStringList branches = branchSelector->getBranches(internetAvailable);
        QString currentBranch = branchSelector->getValue();

        if (branches.isEmpty()) {
            ConfirmationDialog::alert(
                tr("Unable to get branch list. Please check your internet connection or repository status."),
                this);
            return;
        }

        QString selection = MultiOptionDialog::getSelection(
            internetAvailable ? tr("Select Branch (Online)") : tr("Select Branch (Offline)"),
            branches,
            currentBranch,
            this
        );

        if (!selection.isEmpty()) {
            switchBranch(selection);
            branchSelector->setValue(selection);
        }
    });
}

void GitManagerPanel::switchBranch(const QString& branch) {
    if (hasUncommittedChanges()) {
        if (!ConfirmationDialog::confirm(
            tr("You have uncommitted changes that will be lost if you switch branches.\nContinue?"),
            tr("Yes"),
            this)) {
            return;
        }
    }

    QString command = QString(
        "git checkout %1 -f && "
        "git reset --hard && "
        "git clean -fd && "
        "git submodule update --init --recursive && scons -j$(nproc)"
    ).arg(branch);

    showCommandOutputDialog(tr("Switching Branch"), command, "", 300000, true, true, true);
}

bool GitManagerPanel::hasUncommittedChanges() const {
    QProcess process;
    process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");
    process.start("git", QStringList() << "status" << "--porcelain");

    if (!process.waitForStarted(5000)) {
        std::cerr << "Git status command failed to start" << std::endl;
        return false;
    }

    if (!process.waitForFinished(5000)) {
        process.kill();
        std::cerr << "Git status command timed out" << std::endl;
        return false;
    }

    return !QString::fromUtf8(process.readAllStandardOutput()).isEmpty();
}

bool GitManagerPanel::hasUpdatesAvailable() const {
    QProcess process;
    process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");

    // Fetch updates from remote with timeout
    process.start("git", QStringList() << "fetch");
    if (!process.waitForFinished(30000)) {
        process.kill();
        std::cerr << "Git fetch timed out" << std::endl;
        return false;
    }

    // Check if local branch is behind remote
    process.start("git", QStringList() << "rev-list" << "HEAD..@{u}" << "--count");
    if (!process.waitForFinished(5000)) {
        process.kill();
        std::cerr << "Git rev-list command timed out" << std::endl;
        return false;
    }

    return QString::fromUtf8(process.readAllStandardOutput()).trimmed().toInt() > 0;
}

void GitManagerPanel::checkForUpdates() {
    // Kill system.updated.updated
    std::system("killall system.updated.updated");

    checkUpdatesButton->setEnabled(false);
    updateChkBtnLabelTxt->setText(tr("Checking..."));
    updateChkBtnTimeTxt->setText("");

    QtConcurrent::run([=]() {
        QString workingDir = qApp->applicationDirPath() + "/../..";

        QMetaObject::invokeMethod(this, [=]() {
            updateChkBtnLabelTxt->setText(tr("Checking..."));
            updateChkBtnTimeTxt->setText("");
        }, Qt::QueuedConnection);


        // Fetch updates
        auto fetchResult = executeGitCommand("git fetch --all", workingDir, 45000);

        if (!fetchResult.success) {
            QMetaObject::invokeMethod(this, [=]() {
                checkUpdatesButton->setText(tr("Check Updates"));
                checkUpdatesButton->setEnabled(true);

                // Create custom error dialog
                QDialog* errorDialog = new QDialog(this);
                errorDialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
                errorDialog->setStyleSheet("background-color: black;");
                errorDialog->setFixedSize(2160, 1080);

                QVBoxLayout* layout = new QVBoxLayout(errorDialog);
                layout->setContentsMargins(45, 35, 45, 45);
                layout->setSpacing(30);

                // Add title
                QLabel* titleLabel = new QLabel(tr("Update Check Failed"), errorDialog);
                titleLabel->setStyleSheet("font-size: 90px; font-weight: 600; color: white; background-color: black;");
                layout->addWidget(titleLabel);

                // Add error message
                QString errorMsg = fetchResult.timedOut ?
                    tr("Fetch timed out") : tr("Fetch failed: %1").arg(fetchResult.error);
                QLabel* messageLabel = new QLabel(errorMsg, errorDialog);
                messageLabel->setStyleSheet(R"(
                    font-size: 35px;
                    color: #C9C9C9;
                    background-color: #1B1B1B;
                    padding: 50px;
                    border-radius: 10px;
                )");
                messageLabel->setWordWrap(true);
                layout->addWidget(messageLabel);

                layout->addStretch();

                // Add OK button
                QPushButton* okButton = new QPushButton(tr("OK"), errorDialog);
                okButton->setFixedHeight(160);
                okButton->setStyleSheet(R"(
                    QPushButton {
                        background-color: #465BEA;
                        font-size: 55px;
                        font-weight: 400;
                        border-radius: 10px;
                        color: white;
                    }
                    QPushButton:pressed {
                        background-color: #3049F4;
                    }
                )");
                layout->addWidget(okButton);

                connect(okButton, &QPushButton::clicked, errorDialog, &QDialog::accept);

                errorDialog->show();
                setupFullscreenDialog(errorDialog);
            }, Qt::QueuedConnection);
            return;
        }


        // Check for updates
        auto updateResult = executeGitCommand(
            "git rev-list HEAD..@{u} --count", workingDir, 5000);

        if (!updateResult.success) {
            QMetaObject::invokeMethod(this, [=]() {
                updateChkBtnLabelTxt->setText(tr("Check Updates"));
                updateChkBtnTimeTxt->setText("");
                checkUpdatesButton->setEnabled(true);

                // Create custom error dialog
                QDialog* errorDialog = new QDialog(this);
                errorDialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
                errorDialog->setStyleSheet("background-color: black;");
                errorDialog->setFixedSize(2160, 1080);

                QVBoxLayout* layout = new QVBoxLayout(errorDialog);
                layout->setContentsMargins(45, 35, 45, 45);
                layout->setSpacing(30);

                // Add title
                QLabel* titleLabel = new QLabel(tr("Update Check Failed"), errorDialog);
                titleLabel->setStyleSheet("font-size: 90px; font-weight: 600; color: white; background-color: black;");
                layout->addWidget(titleLabel);

                // Add error message
                QLabel* messageLabel = new QLabel(tr("Failed to check for updates: %1").arg(updateResult.error), errorDialog);
                messageLabel->setStyleSheet(R"(
                    font-size: 35px;
                    color: #C9C9C9;
                    background-color: #1B1B1B;
                    padding: 50px;
                    border-radius: 10px;
                )");
                messageLabel->setWordWrap(true);
                layout->addWidget(messageLabel);

                layout->addStretch();

                // Add OK button
                QPushButton* okButton = new QPushButton(tr("OK"), errorDialog);
                okButton->setFixedHeight(160);
                okButton->setStyleSheet(R"(
                    QPushButton {
                        background-color: #465BEA;
                        font-size: 55px;
                        font-weight: 400;
                        border-radius: 10px;
                        color: white;
                    }
                    QPushButton:pressed {
                        background-color: #3049F4;
                    }
                )");
                layout->addWidget(okButton);

                connect(okButton, &QPushButton::clicked, errorDialog, &QDialog::accept);

                errorDialog->show();
                setupFullscreenDialog(errorDialog);
            }, Qt::QueuedConnection);
            return;
        }

        bool hasUpdates = updateResult.output.trimmed().toInt() > 0;
        lastUpdateCheck = QDateTime::currentDateTime();

        QMetaObject::invokeMethod(this, [=]() {
            updateChkBtnLabelTxt->setText(tr("Check Updates"));
            updateRepoButton->setVisible(hasUpdates);
            if (hasUpdates) {
                if (branchSelector) {
                    QString currentBranch = branchSelector->getValue();
                    branchSelector->setValue(currentBranch, tr("(Updates Available)"), "#465BEA");
                }
            }
            updateCheckUpdatesButtonText();
            checkUpdatesButton->setEnabled(true);
            updateButtonStates();

            // Force refresh of all submodules
            for (auto* widget : submoduleWidgets) {
                if (widget) {
                    widget->refresh();
                }
            }
        }, Qt::QueuedConnection);
    });
}

void GitManagerPanel::staggeredInit() {
    if (initStage >= 5) {  // Changed from 4 to 5 to add new stage
        // Handle all submodules in stage 5
        if (currentSubmoduleIndex < submoduleWidgets.size()) {
            std::cout << "GitManagerPanel: Stage 5 - Refreshing submodule " << currentSubmoduleIndex << std::endl;
            QTimer::singleShot(100, this, [this]() {
                auto* widget = submoduleWidgets[currentSubmoduleIndex];
                if (widget) {
                    widget->refresh();
                    std::cout << "GitManagerPanel: Refreshed submodule " << currentSubmoduleIndex << std::endl;
                } else {
                    std::cerr << "GitManagerPanel: Widget is null for submodule " << currentSubmoduleIndex << std::endl;
                }
                currentSubmoduleIndex++;
                if(initTimer) {
                    initTimer->start(500);
                }
            });
        } else {
            std::cout << "GitManagerPanel: Stage 5 - All submodules processed, stopping init timer" << std::endl;
            if(initTimer) {
                initTimer->stop();
                delete initTimer;
                initTimer = nullptr;
            }
            if(refreshTimer) {
                refreshTimer->start(60000);
            }
        }
        return;
    }

    switch(initStage++) {
    case 0:
        std::cout << "GitManagerPanel: Stage 0 - Just showing UI" << std::endl;
        break;
    case 1:
        std::cout << "GitManagerPanel: Stage 1 - Updating branch list" << std::endl;
        QTimer::singleShot(100, this, [this]() {
            updateBranchList();
        });
        break;
    case 2:
        std::cout << "GitManagerPanel: Stage 2 - Refreshing main repo status" << std::endl;
        QTimer::singleShot(200, this, [this]() {
            if (mainRepoStatus) {
                mainRepoStatus->refresh();
            }
        });
        break;
    case 3:
        std::cout << "GitManagerPanel: Stage 3 - Checking local changes and updates" << std::endl;
        QTimer::singleShot(300, this, [this]() {
            QtConcurrent::run([=]() {
                bool hasLocal = hasUncommittedChanges();
                bool hasUpdates = hasUpdatesAvailable();

                QMetaObject::invokeMethod(this, [=]() {
                    resetRepoButton->setVisible(hasLocal);
                    updateRepoButton->setVisible(hasUpdates);

                    QString status;
                    QString color;
                    if (hasUpdates) {
                        status = tr("(Updates Available)");
                        color = "#465BEA";
                    } else if (hasLocal) {
                        status = tr("(Modified)");
                        color = "#ff7c30";
                    } else {
                        status = tr("(Clean)");
                        color = "#50d332";
                    }

                    if (branchSelector) {
                        QString currentBranch = branchSelector->getValue();
                        branchSelector->setValue(currentBranch, status, color);
                    }

                }, Qt::QueuedConnection);
            });
        });
        break;
    case 4:
        std::cout << "GitManagerPanel: Stage 4 - Checking for updates" << std::endl;
        QTimer::singleShot(2000, this, [this]() {  // 2 second delay after status checks
            checkForUpdates();
        });
        break;
    }
}

QString GitManagerPanel::getTimeAgoString(const QDateTime& time) {
    if (!time.isValid()) return "";

    qint64 secs = time.secsTo(QDateTime::currentDateTime());
    if (secs < 60) return tr("%1 seconds ago").arg(secs);
    if (secs < 3600) return tr("%1 minutes ago").arg(secs/60);
    if (secs < 86400) return tr("%1 hours ago").arg(secs/3600);
    return tr("%1 days ago").arg(secs/86400);
}

QString GitManagerPanel::getTimeDateString(const QDateTime& time) {
    if (!time.isValid()) return "";
    return time.toString("MM/dd h:mm AP");
}

void GitManagerPanel::updateCheckUpdatesButtonText() {
    if (!lastUpdateCheck.isValid()) {
        updateChkBtnTimeTxt->setText("");
    } else {
        updateChkBtnTimeTxt->setText(lastUpdateCheck.toString("MM/dd hh:mm AP"));
    }
}

BranchSelector::BranchSelector(const QString &title, const QString &text, const QString &desc, QWidget *parent)
    : AbstractControl("", desc, "", parent) {
    while (QLayoutItem* item = hlayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    QProcess process;
    process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");

    // Get the remote URL
    process.start("git", QStringList() << "remote" << "get-url" << "origin");
    process.waitForFinished();
    QString remoteUrl = QString::fromUtf8(process.readAllStandardOutput()).trimmed();

    // Extract repository name from the URL
    QString repoInfo;
    if (!remoteUrl.isEmpty()) {
        if (remoteUrl.contains("git@")) {
            repoInfo = remoteUrl.split(":").last().replace(".git", "");
        } else if (remoteUrl.contains("https://")) {
            repoInfo = remoteUrl.split("/").last().replace(".git", "");
        }
    }

    QWidget* labelBox = new QWidget(this);
    QVBoxLayout* labelLayout = new QVBoxLayout(labelBox);
    labelLayout->setSpacing(0);
    labelLayout->setContentsMargins(0, 0, 0, 0);

    // Create repo label with two different colors
    QHBoxLayout* repoLayout = new QHBoxLayout();
    QLabel* repoTitle = new QLabel("Repo: ", labelBox);
    repoTitle->setStyleSheet("font-size: 35px; font-weight: 500; color: #E4E4E4;");
    QLabel* repoValue = new QLabel(repoInfo, labelBox);
    repoValue->setStyleSheet("font-size: 35px; font-weight: 500; color: #A0A0A0;");
    repoLayout->addWidget(repoTitle);
    repoLayout->addWidget(repoValue);
    repoLayout->addStretch();

    // Create branch layout with two different colors
    QHBoxLayout* branchLayout = new QHBoxLayout();
    QLabel* branchTitle = new QLabel("Branch: ", labelBox);
    branchTitle->setStyleSheet("font-size: 35px; color: #E4E4E4;");
    branchNameLabel = new QLabel(labelBox);
    branchNameLabel->setStyleSheet("font-size: 35px; color: #A0A0A0;");
    branchLayout->addWidget(branchTitle);
    branchLayout->addWidget(branchNameLabel);
    branchLayout->addStretch();

    labelLayout->addLayout(repoLayout);
    labelLayout->addLayout(branchLayout);

    btn.setText("CHANGE");
    btn.setFixedSize(220, 100);
    btn.setStyleSheet(R"(
        QPushButton {
            border-radius: 50px;
            font-size: 35px;
            font-weight: 500;
            color: #E4E4E4;
            background-color: #393939;
        }
        QPushButton:pressed {
            background-color: #4a4a4a;
        }
    )");

    hlayout->addWidget(labelBox);
    hlayout->addStretch();
    hlayout->setContentsMargins(0, 10, 0, 10);
    hlayout->addWidget(&btn);

    connect(&btn, &QPushButton::clicked, this, &BranchSelector::clicked);
}

void BranchSelector::setValue(const QString &branchName, const QString &status, const QString &statusColor) {
    if (status.isEmpty()) {
        branchNameLabel->setText(branchName);
    } else {
        QString styledText = QString("%1 <span style='color: %2;'>%3</span>")
            .arg(branchName, statusColor, status);
        branchNameLabel->setTextFormat(Qt::RichText);
        branchNameLabel->setText(styledText);
    }
}

void GitManagerPanel::showCommandOutputDialog(const QString& title, const QString& command,
                                            const QString& workingDir, int timeoutMs,
                                            bool showKillBtn, bool showRetryBtn,
                                            bool showRebootBtn) {
    // Clean up any existing dialog
    if (currentDialog) {
        currentDialog->close();
        currentDialog->deleteLater();
        currentDialog = nullptr;
    }

    // Create and set up process
    QProcess* process = new QProcess(this);
    if (!workingDir.isEmpty()) {
        process->setWorkingDirectory(workingDir);
    } else {
        process->setWorkingDirectory(qApp->applicationDirPath() + "/../..");
    }

    // Create command output dialog with proper flags
    currentDialog = new QDialog(this);
    currentDialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    currentDialog->setStyleSheet("background-color: black;");

    // Create main layout
    QVBoxLayout* layout = new QVBoxLayout(currentDialog);
    layout->setContentsMargins(45, 35, 45, 45);
    layout->setSpacing(0);

    // Add title
    QLabel* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet("font-size: 90px; font-weight: 600; background-color: black;");
    layout->addWidget(titleLabel);
    layout->addSpacing(30);

    // Create output text area
    QTextEdit* outputText = new QTextEdit(currentDialog);
    outputText->setReadOnly(true);
    outputText->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::LinksAccessibleByMouse);
    outputText->setStyleSheet(R"(
        QTextEdit {
            font-family: monospace;
            font-size: 35px;
            font-weight: 200;
            color: #C9C9C9;
            background-color: #1B1B1B;
            padding: 50px;
            border: none;
        }
        QTextEdit QScrollBar:vertical {
            width: 20px;
            background: #1B1B1B;
            margin: 0px;
        }
        QTextEdit QScrollBar::handle:vertical {
            background-color: white;
            min-height: 30px;
            border-radius: 5px;
            margin: 2px;
            width: 16px;
        }
        QTextEdit QScrollBar::add-line:vertical,
        QTextEdit QScrollBar::sub-line:vertical {
            height: 0px;
            background: none;
        }
        QTextEdit QScrollBar::add-page:vertical,
        QTextEdit QScrollBar::sub-page:vertical {
            background: none;
        }
    )");
    outputText->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outputText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    outputText->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
    layout->addWidget(outputText);

    // Create button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(20);

    // Add kill button (if enabled)
    QPushButton* killButton = nullptr;
    if (showKillBtn) {
        killButton = new QPushButton(tr("Stop Command"), currentDialog);
        killButton->setFixedHeight(160);
        killButton->setStyleSheet(R"(
            QPushButton {
                background-color: #EA4646;
                font-size: 55px;
                font-weight: 400;
                border-radius: 10px;
                color: white;
            }
            QPushButton:pressed {
                background-color: #F43030;
            }
        )");
        buttonLayout->addWidget(killButton);
    }

    // Add retry button (if enabled)
    QPushButton* retryButton = nullptr;
    if (showRetryBtn) {
        retryButton = new QPushButton(tr("Retry"), currentDialog);
        retryButton->setFixedHeight(160);
        retryButton->setVisible(false);  // Hide initially
        retryButton->setStyleSheet(R"(
            QPushButton {
                background-color: #7B1FA2;
                font-size: 55px;
                font-weight: 400;
                border-radius: 10px;
                color: white;
            }
            QPushButton:pressed {
                background-color: #6A1B9A;
            }
            QPushButton:disabled {
                background-color: #4F4F4F;
                color: #888888;
            }
        )");
        buttonLayout->addWidget(retryButton);
    }

    // Add reboot button (if enabled)
    QPushButton* rebootButton = nullptr;
    if (showRebootBtn) {
        rebootButton = new QPushButton(tr("Reboot"), currentDialog);
        rebootButton->setFixedHeight(160);
        rebootButton->setVisible(false);  // Hide initially
        rebootButton->setStyleSheet(R"(
            QPushButton {
                background-color: #33Ab4C;
                font-size: 55px;
                font-weight: 400;
                border-radius: 10px;
                color: white;
            }
            QPushButton:pressed {
                background-color: #2A9040;
            }
        )");
        buttonLayout->addWidget(rebootButton);
    }

    // Close button (initially disabled)
    QPushButton* closeButton = new QPushButton(tr("Command is Running..."), currentDialog);
    closeButton->setEnabled(false);
    closeButton->setFixedHeight(160);
    closeButton->setStyleSheet(R"(
        QPushButton {
            background-color: #465BEA;
            font-size: 55px;
            font-weight: 400;
            border-radius: 10px;
            color: white;
        }
        QPushButton:pressed {
            background-color: #3049F4;
        }
        QPushButton:disabled {
            background-color: #4F4F4F;
            color: #888888;
        }
    )");
    buttonLayout->addWidget(closeButton);

    layout->addSpacing(50);
    layout->addLayout(buttonLayout);

    // Add timeout timer
    QTimer* timeoutTimer = new QTimer(currentDialog);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(timeoutMs);

    // Connect process signals for output
    connect(process, &QProcess::readyReadStandardOutput, [=]() {
        QString output = QString::fromUtf8(process->readAllStandardOutput());
        outputText->append(output);
    });

    connect(process, &QProcess::readyReadStandardError, [=]() {
        QString error = QString::fromUtf8(process->readAllStandardError());
        outputText->append("<span style='color: #ff7c30;'>" + error.toHtmlEscaped() + "</span>");
    });

    // Connect timeout handler
    connect(timeoutTimer, &QTimer::timeout, [=]() {
        if (process->state() != QProcess::NotRunning) {
            outputText->append("\n<span style='color: #ff7c30;'>Process timed out after " +
                            QString::number(timeoutMs/1000) + " seconds</span>");
            process->kill();
            if (killButton) killButton->hide();
            if (retryButton) {
                retryButton->setVisible(true);
                retryButton->setEnabled(true);
            }
            closeButton->setEnabled(true);
            closeButton->setText(tr("Close (Timed Out)"));
            closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #EA4646;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 10px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #F43030;
                }
            )");
        }
    });

    // Connect kill button
    if (killButton) {
        connect(killButton, &QPushButton::clicked, [=]() {
            if (process->state() != QProcess::NotRunning) {
                outputText->append("\n<span style='color: #ff7c30;'>Process terminated by user</span>");
                process->kill();
                killButton->hide();
                if (retryButton) retryButton->setEnabled(true);
                closeButton->setEnabled(true);
                closeButton->setText(tr("Close (Terminated)"));
                closeButton->setStyleSheet(R"(
                    QPushButton {
                        background-color: #EA4646;
                        font-size: 55px;
                        font-weight: 400;
                        border-radius: 10px;
                        color: white;
                    }
                    QPushButton:pressed {
                        background-color: #F43030;
                    }
                )");
            }
        });
    }

    // Handle process completion
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [=](int exitCode, QProcess::ExitStatus exitStatus) {
        timeoutTimer->stop();
        closeButton->setEnabled(true);
        if (killButton) killButton->hide();

        if (exitStatus == QProcess::CrashExit) {
            // Show retry button for crash
            if (retryButton) {
                retryButton->setVisible(true);
                retryButton->setEnabled(true);
            }
        } else if (exitCode != 0) {
            outputText->append(QString("\n<span style='color: #ff7c30;'>Command failed with exit code: %1</span>").arg(exitCode));
            closeButton->setText(tr("Close (Command Failed | Exit code: %1)").arg(exitCode));
            closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #EA4646;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 10px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #F43030;
                }
            )");
            // Show retry button for failure
            if (retryButton) {
                retryButton->setVisible(true);
                retryButton->setEnabled(true);
            }
        } else {
            outputText->append("\n<span style='color: #50d332;'>Command completed successfully</span>");
            closeButton->setText(tr("Close (Completed Successfully)"));
            closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #33Ab4C;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 10px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #2A9040;
                }
            )");

            // Hide retry button on success if retry is enabled
            if (retryButton) {
                retryButton->setVisible(false);
            }

            // Show reboot button on success if reboot is enabled
            if (rebootButton) {
                rebootButton->setVisible(true);
            }

            // Refresh everything after successful completion
            if (mainRepoStatus) {
                mainRepoStatus->refresh();
            }
            for (auto* widget : submoduleWidgets) {
                if (widget) {
                    widget->refresh();
                }
            }
        }
    });

    // Add retry button functionality
    if (retryButton) {
        connect(retryButton, &QPushButton::clicked, [=]() {
            outputText->clear();
            outputText->append(tr("Retrying command:\n\n%1\n\n").arg(command));
            retryButton->setEnabled(false);
            retryButton->setVisible(false);  // Hide when retrying
            closeButton->setEnabled(false);
            closeButton->setText(tr("Command is Running..."));
            closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #465BEA;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 10px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #3049F4;
                }
                QPushButton:disabled {
                    background-color: #4F4F4F;
                    color: #888888;
                }
            )");
            if (killButton) {
                killButton->show();
            }

            // Reset and start timeout timer
            timeoutTimer->start();

            // Start process again
            process->start("/bin/bash", QStringList() << "-c" << command);
        });
    }

    // Connect reboot button
    if (rebootButton) {
        connect(rebootButton, &QPushButton::clicked, [=]() {
            if (ConfirmationDialog::confirm(
                tr("Are you sure you want to reboot?"),
                tr("Reboot"),
                currentDialog)) {
                params.putBool("DoReboot", true);
                QProcess::execute("reboot");
            }
        });
    }

    // Connect close button and cleanup
    connect(closeButton, &QPushButton::clicked, currentDialog, &QDialog::accept);
    connect(currentDialog, &QDialog::finished, [=]() {
        timeoutTimer->stop();
        process->deleteLater();
        if (currentDialog) {
            currentDialog->deleteLater();
            currentDialog = nullptr;
        }
    });

    // Set dialog size and show
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        currentDialog->setFixedSize(2160, 1080);
    }
    currentDialog->show();
    setupFullscreenDialog(currentDialog);

    // Start timeout timer
    timeoutTimer->start();

    // Start process
    outputText->append(tr("Executing command:\n\n%1\n\n").arg(command));
    process->start("/bin/bash", QStringList() << "-c" << command);
}

void GitManagerPanel::showCommitHistory(QWidget* parent, const QString& title, const QString& workingDir) {
    QDialog* dialog = new QDialog(parent);
    dialog->setWindowTitle(title);
    dialog->setModal(true);

    QVBoxLayout* layout = new QVBoxLayout(dialog);

    // Add title
    QLabel* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(R"(
        QLabel {
            font-size: 50px;
            font-weight: 600;
            margin: 0px;
            padding: 0px;
            background-color: transparent;
        }
    )");
    layout->addWidget(titleLabel);
    layout->addSpacing(30);

    // Create a scroll area for touch scrolling
    QScrollArea* scrollArea = new QScrollArea(dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet(R"(
        QScrollArea {
            border: none;
            background-color: #1B1B1B;
        }
        QScrollBar:vertical {
            width: 10px;
            background: #1e1e1e;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            min-height: 30px;
            border-radius: 5px;
            background: #465BEA;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {
            height: 0px;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }
    )");

    QWidget* scrollContent = new QWidget(scrollArea);
    QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);

    QTableWidget* table = new QTableWidget(scrollContent);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({tr("Commit ID"), tr("Description"), tr("Time")});
    table->setShowGrid(false);
    table->setSelectionMode(QAbstractItemView::NoSelection);  // Disable selection
    table->setFocusPolicy(Qt::NoFocus);  // Prevent focus rectangle
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(100);
    table->setAlternatingRowColors(true);
    table->setWordWrap(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Enable touch scrolling
    QScroller::grabGesture(table->viewport(), QScroller::LeftMouseButtonGesture);
    QScroller::grabGesture(scrollArea->viewport(), QScroller::LeftMouseButtonGesture);

    // Configure scroller properties for smoother scrolling
    QScrollerProperties properties = QScroller::scroller(table->viewport())->scrollerProperties();
    properties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
    properties.setScrollMetric(QScrollerProperties::DragStartDistance, QVariant::fromValue(0.001));
    properties.setScrollMetric(QScrollerProperties::MinimumVelocity, QVariant::fromValue(0.0));
    properties.setScrollMetric(QScrollerProperties::MaximumVelocity, QVariant::fromValue(0.5));
    properties.setScrollMetric(QScrollerProperties::AcceleratingFlickMaximumTime, QVariant::fromValue(0.4));
    QScroller::scroller(table->viewport())->setScrollerProperties(properties);

    table->setStyleSheet(R"(
        QTableWidget {
            font-family: monospace;
            font-size: 35px;
            padding: 20px;
            background-color: #1B1B1B;
            color: #C9C9C9;
            border: none;
            alternate-background-color: #232323;
            min-height: 130px;
        }
        QHeaderView::section {
            background-color: #2D2D2D;
            color: #C9C9C9;
            padding: 10px;
            border: none;
            font-weight: bold;
            font-size: 35px;
        }
        QTableWidget::item {
            padding: 20px;
            border-right: 1px solid #404040;  /* Add vertical lines between columns */
        }
        QTableWidget::item:last-child {
            border-right: none;  /* Remove border for last column */
        }
        QTableWidget::item:nth-child(2) {
            white-space: normal;  /* Enable word wrapping for the description column */
            word-wrap: break-word;
        }
    )");

    table->setShowGrid(false);
    table->setStyleSheet(table->styleSheet() + "QTableWidget { gridline-color: #404040; }");

    QProcess process;
    process.setWorkingDirectory(workingDir);

    // get the last 30 commits
    process.start("git", QStringList() << "log" << "-n" << "30" << "--pretty=format:%h|||%s|||%cr");

    if (!process.waitForFinished(10000)) {
        std::cerr << "Git log command timed out" << std::endl;
        process.kill();
        return;
    }

    QString output = QString::fromUtf8(process.readAllStandardOutput());
    QStringList commits = output.split("\n", QString::SkipEmptyParts);

    table->setRowCount(commits.size());

    for (int i = 0; i < commits.size(); ++i) {
        QStringList parts = commits[i].split("|||");
        if (parts.size() == 3) {
            auto createItem = [](const QString& text, Qt::Alignment alignment) {
                QTableWidgetItem* item = new QTableWidgetItem(text);
                item->setTextAlignment(alignment);
                return item;
            };

            table->setItem(i, 0, createItem(parts[0], Qt::AlignLeft | Qt::AlignVCenter));
            table->setItem(i, 1, createItem(parts[1], Qt::AlignLeft | Qt::AlignVCenter));
            table->setItem(i, 2, createItem(parts[2], Qt::AlignLeft | Qt::AlignVCenter));
        }
    }

    table->setColumnWidth(0, 260);
    table->setColumnWidth(2, 350);

    // for (int row = 0; row < table->rowCount(); ++row) {
    //     table->resizeRowToContents(row);
    // }

    scrollLayout->addWidget(table);
    scrollContent->setLayout(scrollLayout);
    scrollArea->setWidget(scrollContent);
    layout->addWidget(scrollArea);

    QPushButton* closeButton = new QPushButton(tr("Close"), dialog);
    closeButton->setStyleSheet(R"(
        QPushButton {
            border-radius: 10px;
            font-size: 35px;
            padding: 15px;
            background-color: #465BEA;
            color: white;
            min-height: 60px;
        }
        QPushButton:pressed { background-color: #3049F4; }
    )");

    layout->addWidget(closeButton);
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);

    // Apply fullscreen settings
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        dialog->setFixedSize(2160, 1080);
    }

    dialog->show();
    setupFullscreenDialog(dialog);

    dialog->exec();
}

void GitManagerPanel::handleRepoUpdate() {
    // If there are uncommitted local changes, confirm before proceeding
    if (hasUncommittedChanges()) {
      if (!ConfirmationDialog::confirm(
            tr("You have local changes that will be overwritten by this update. Continue?"),
            tr("Yes"),
            this)) {
        return;
      }
    }

    // Kill system.updated.updated
    std::system("killall system.updated.updated");

    // Reset local changes
    executeGitCommand("git reset --hard HEAD && git clean -fd", qApp->applicationDirPath(), 30000);

    // Fetch, pull, and update
    showCommandOutputDialog(tr("Update Openpilot"),
        "git fetch && git pull && git submodule update --init --recursive && scons -j$(nproc)",
        "", 600000, true, true, true);
}

void GitManagerPanel::handleRepoUpdateAll() {
    // Kill system.updated.updated
    std::system("killall system.updated.updated");

    // Reset local changes
    executeGitCommand("git reset --hard HEAD && git clean -fd", qApp->applicationDirPath(), 30000);

    // Fetch, pull, and update all submodules
    showCommandOutputDialog(tr("Update All Submodules"),
        "git fetch && git pull --ff-only && git submodule update --init --recursive && scons -j$(nproc)",
        "", 180000, true, true, true);
}

void GitManagerPanel::handleRepoRepair() {
    if (!ConfirmationDialog::confirm(
        tr("Are you sure you want to repair the repository?\n"
          "This will completely remove and re-download the repository."),
        tr("Repair"), this)) {
        return;
    }

    // Set correct paths
    QString tempScript = "/data/repo_repair.sh";
    QString sourceScript = "/data/openpilot/scripts/git_ui/repo_repair.sh";

    // Verify source script exists
    if (!QFile::exists(sourceScript)) {
        ConfirmationDialog::alert(tr("Source script not found at: ") + sourceScript, this);
        return;
    }

    // Clean up any existing copy
    if (QFile::exists(tempScript)) {
        if (!QFile::remove(tempScript)) {
            ConfirmationDialog::alert(tr("Failed to remove existing temp script"), this);
            return;
        }
        std::cout << "tempScript removed at " << tempScript.toStdString() << std::endl;
    }

    // Copy script to /data
    if (!QFile::copy(sourceScript, tempScript)) {
        ConfirmationDialog::alert(tr("Failed to copy script. Error: ") + QString::number(errno), this);
        return;
    }

    // Make executable
    if (QProcess::execute("chmod", QStringList() << "+x" << tempScript) != 0) {
        ConfirmationDialog::alert(tr("Failed to make script executable"), this);
        QFile::remove(tempScript);
        return;
    }

    // Run repair script
    showCommandOutputDialog(tr("Repairing Openpilot"), tempScript, "", 600000, true, true, true);
}

void GitManagerPanel::handleRepoReset() {
    if (ConfirmationDialog::confirm(
        tr("Are you sure you want to reset all changes? This cannot be undone."),
        tr("Reset"),
        this)) {
        showCommandOutputDialog(tr("Reset Changes"), "git reset --hard HEAD && git clean -fd", "", 60000, true, true, true);
    }
}

void GitManagerPanel::showLastCommits() {
    QString branch = branchSelector->getValue();
    QString title = branch + " - Last 30 Commits";
    showCommitHistory(this, title, qApp->applicationDirPath() + "/../..");
}

void GitManagerPanel::showErrorState(const QString& message) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    QLabel* errorLabel = new QLabel(message, this);
    errorLabel->setStyleSheet(R"(
        font-size: 45px;
        color: #ff7c30;
        padding: 40px;
        background-color: #1B1B1B;
        border-radius: 10px;
    )");
    errorLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(errorLabel);
    setLayout(mainLayout);
}

bool GitManagerPanel::isInternetAvailable() const {
    // return false;
    // If we have a recent check, reuse that result
    if (lastInternetCheckTime.isValid()) {
        int secsSinceLastCheck = lastInternetCheckTime.secsTo(QDateTime::currentDateTime());
        if (secsSinceLastCheck < INTERNET_CHECK_INTERVAL_SECS) {
            return lastInternetCheckResult;
        }
    }

    // Perform a new check
    QProcess process;
    QString command = "curl -sSfI --max-time 5 https://github.com";
    process.start("/bin/bash", QStringList() << "-c" << command);

    if (!process.waitForStarted(5000)) {
        lastInternetCheckResult = false;
        lastInternetCheckTime = QDateTime::currentDateTime();
        return false;
    }
    if (!process.waitForFinished(5000)) {
        process.kill();
        lastInternetCheckResult = false;
        lastInternetCheckTime = QDateTime::currentDateTime();
        return false;
    }

    int exitCode = process.exitCode();
    lastInternetCheckResult = (exitCode == 0);
    lastInternetCheckTime = QDateTime::currentDateTime();
    return lastInternetCheckResult;
}

bool GitManagerPanel::isSSHValid() const {
    // If we have a recent check, reuse that result
    if (lastSSHCheckTime.isValid()) {
        int secsSinceLastCheck = lastSSHCheckTime.secsTo(QDateTime::currentDateTime());
        if (secsSinceLastCheck < SSH_CHECK_INTERVAL_SECS) {
            return lastSSHCheckResult;
        }
    }

    // First check if SSH key exists
    if (!QFile::exists("/home/comma/.ssh/github")) {
        lastSSHCheckResult = false;
        lastSSHCheckTime = QDateTime::currentDateTime();
        updateStatusLabel(UpdaterStatus::SSH_MISSING);
        return false;
    }

    // Test SSH connection to GitHub
    QProcess process;
    process.start("ssh", QStringList() << "-T" << "-o" << "BatchMode=yes" << "-o" << "ConnectTimeout=5" << "git@github.com");

    if (!process.waitForStarted(5000)) {
        lastSSHCheckResult = false;
        lastSSHCheckTime = QDateTime::currentDateTime();
        updateStatusLabel(UpdaterStatus::SSH_AUTH_FAILED);
        return false;
    }

    process.waitForFinished(5000);
    QString output = QString::fromUtf8(process.readAllStandardError());

    // Check if authentication was successful
    lastSSHCheckResult = output.contains("successfully authenticated");
    lastSSHCheckTime = QDateTime::currentDateTime();

    if (!lastSSHCheckResult) {
        updateStatusLabel(UpdaterStatus::SSH_AUTH_FAILED);
    }

    return lastSSHCheckResult;
}

bool GitManagerPanel::checkAndRestoreSSH() {
    // Check if SSH exists and is valid
    if (!QFile::exists("/home/comma/.ssh/github") || !isSSHValid()) {
        // Check for backup
        if (QFile::exists("/data/ssh_backup/github")) {
            if (ConfirmationDialog::confirm(
                tr("SSH configuration is missing or invalid. Would you like to restore from backup?"),
                tr("Restore"),
                this)) {
                return restoreSSHFromUtility();
            }
        }
        return false;
    }
    return true;
}

bool GitManagerPanel::restoreSSHFromUtility() {
    // Download CommaUtility script if not present
    QString utilityPath = "/data/CommaUtility.sh";
    if (!QFile::exists(utilityPath)) {
        QProcess wget;
        wget.start("wget", QStringList()
            << "-O" << utilityPath
            << "https://raw.githubusercontent.com/tonesto7/op-utilities/main/CommaUtility.sh");
        if (!wget.waitForFinished(30000)) {
            return false;
        }
        QProcess::execute("chmod", QStringList() << "+x" << utilityPath);
    }

    // Execute restore command
    QProcess restore;
    restore.start(utilityPath, QStringList() << "--restore-ssh");
    restore.waitForFinished();

    // Verify restoration was successful
    return isSSHValid();
}

void GitManagerPanel::updateStatusLabel(UpdaterStatus status) const {
    auto it = std::find_if(STATUS_MESSAGES.begin(), STATUS_MESSAGES.end(),
        [status](const auto& tuple) { return std::get<0>(tuple) == status; });

    if (it != STATUS_MESSAGES.end()) {
        QString statusText = std::get<1>(*it);
        updaterPanelStatusLabel->setText(statusText);
        updaterPanelStatusLabel->setVisible(status != UpdaterStatus::OK);
    }
}

void GitManagerPanel::updateButtonStates() {
    bool internetAvailable = isInternetAvailable();
    bool sshValid = isSSHValid();

    std::cout << "Internet available: " << internetAvailable << std::endl;
    std::cout << "SSH valid: " << sshValid << std::endl;

    // Update status label based on conditions
    if (!internetAvailable) {
        updateStatusLabel(UpdaterStatus::NO_INTERNET);
    } else if (sshValid) {
        updateStatusLabel(UpdaterStatus::OK);
    }
    // Note: SSH-related statuses are handled in isSSHValid()

    // If SSH is invalid, try to restore it
    if (!sshValid && internetAvailable) {
        #ifdef QCOM2
        checkAndRestoreSSH();
        #endif
        sshValid = isSSHValid();
    }

    checkUpdatesButton->setEnabled(internetAvailable && sshValid);
    updateRepoButton->setEnabled(internetAvailable && sshValid);
    updateAllButton->setEnabled(internetAvailable && sshValid);
    repairRepoButton->setEnabled(internetAvailable && sshValid);

    for (auto* widget : submoduleWidgets) {
        widget->updateModuleButton->setEnabled(internetAvailable && sshValid);
        widget->repairModuleButton->setEnabled(internetAvailable && sshValid);
    }
}
