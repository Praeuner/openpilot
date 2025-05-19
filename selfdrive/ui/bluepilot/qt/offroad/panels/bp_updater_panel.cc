// selfdrive/ui/bluepilot/qt/offroad/panels/bp_updater_panel.cc

#include <QScrollArea>
#include <QDialog>
#include <QTextEdit>
#include <QScreen>
#include <QFile>
#include <QTableWidget>
#include <QCoreApplication>
#include <QTableWidgetItem>
#include <QButtonGroup>
#include <QHeaderView>
#include <QMouseEvent>
#include <QWidget>
#include <QApplication>
#include <QGuiApplication>
#include <QProcess>
#include <QTimer>
#include <iostream>
#include <QtConcurrent>

#include "bp_updater_panel.h"
#include "bp_panel_dialogs.h"
#include "common/params.h"

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#include <QPlatformSurfaceEvent>
#endif

GitStatusWidget::GitStatusWidget(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(3); // Reduced spacing

  // Create a QFrame as a container
  QFrame *commitFrame = new QFrame(this);
  commitFrame->setStyleSheet(R"(
        QFrame {
            background-color: #1B1B1B;
            border-radius: 20px;
            padding: 5px 0px;
        }
    )");

  QVBoxLayout *commitLayout = new QVBoxLayout(commitFrame);
  commitLayout->setContentsMargins(15, 10, 15, 10); // Reduced margins
  commitLayout->setSpacing(5);                      // Reduced spacing

  // Create header container with horizontal layout
  QWidget *headerContainer = new QWidget(this);
  QHBoxLayout *headerLayout = new QHBoxLayout(headerContainer);
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(0);

  // Add header label
  QLabel *headerLabel = new QLabel(tr("Last Commit"), this);
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
  if (!isVisible())
    return;

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

      QMetaObject::invokeMethod(
          this,
          [=]() {
            QString statusWithTime = status;
            if (lastCheckTime.isValid()) {
              statusWithTime += QString(" (%1)").arg(BPUpdaterPanel::getTimeDateString(lastCheckTime));
            }
            statusText = statusWithTime;
            statusColor = color;
            lastCommitLabel->setText(lastCommit.isEmpty() ? "No commits" : tr("Last Commit: %1").arg(lastCommit));
            emit statusUpdated();
          },
          Qt::QueuedConnection);

      QMetaObject::invokeMethod(
          this,
          [=]() {
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
          },
          Qt::QueuedConnection);

    } catch (const std::exception &e) {
      std::cerr << "Exception in GitStatusWidget::refresh:" << e.what() << std::endl;
    }
  });
}

void GitStatusWidget::updateStatus() { refresh(); }

SubmoduleWidget::SubmoduleWidget(const QString &name, QWidget *parent) : QWidget(parent), submoduleName(name) {
  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 10, 0, 10);

  // Create a widget to contain the name and status labels
  QWidget *labelContainer = new QWidget(this);
  QHBoxLayout *labelLayout = new QHBoxLayout(labelContainer);
  labelLayout->setContentsMargins(0, 0, 0, 0);
  labelLayout->setSpacing(10);

  QLabel *nameLabel = new QLabel(name, this);
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
  QHBoxLayout *buttonLayout = new QHBoxLayout();
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
  if (auto *panel = findGitManagerPanel()) {
    panel->showCommandOutputDialog(tr("Update Submodule"), command, "", 30000, true, true, true);
  }
}

void SubmoduleWidget::handleModuleReset() {
  if (!BPUpdateConfirmDialog::confirm(tr("Reset Submodule"),
                                      tr("Are you sure you want to reset the submodule '%1'?\n"
                                         "This will lose all local changes.")
                                          .arg(submoduleName),
                                      tr("Reset"), tr("Cancel"), this)) {
    return;
  }

  QString command = QString("cd %1 && git reset --hard HEAD").arg(submoduleName);
  if (auto *panel = findGitManagerPanel()) {
    panel->showCommandOutputDialog(tr("Reset Submodule"), command, "", 30000, true, true, true);
  }
}

void SubmoduleWidget::handleModuleRepair() {
  if (!BPUpdateConfirmDialog::confirm(tr("Repair Submodule"),
                                      tr("Are you sure you want to repair the submodule '%1'?\n"
                                         "This will remove and re-download it.")
                                          .arg(submoduleName),
                                      tr("Repair"), tr("Cancel"), this)) {
    return;
  }

  QString command = QString("git submodule deinit -f %1 && "
                            "rm -rf .git/modules/%1 && "
                            "git submodule update --init --recursive %1")
                        .arg(submoduleName);

  if (auto *panel = findGitManagerPanel()) {
    panel->showCommandOutputDialog(tr("Repair Submodule"), command, "", 30000, true, true, true);
  }
}

void SubmoduleWidget::handleShowCommits() {
  if (auto *panel = findGitManagerPanel()) {
    QString title = submoduleName + " - Last 30 Commits";
    panel->showCommitHistory(panel, title, qApp->applicationDirPath() + "/../.." + "/" + submoduleName);
  }
}

BPUpdaterPanel *SubmoduleWidget::findGitManagerPanel() const {
  QWidget *parent = parentWidget();
  while (parent) {
    if (auto *panel = qobject_cast<BPUpdaterPanel *>(parent)) {
      return panel;
    }
    parent = parent->parentWidget();
  }
  return nullptr;
}

void SubmoduleWidget::refresh() {
  if (!isVisible())
    return;

  lastCheckTime = QDateTime::currentDateTime();

  QtConcurrent::run([=]() {
    QProcess process;
    QString workingDir = qApp->applicationDirPath() + "/../.." + "/" + submoduleName;
    process.setWorkingDirectory(workingDir);

    try {
      // First check if the directory exists and is a git repository
      process.start("git", QStringList() << "rev-parse" << "--git-dir");
      if (!process.waitForFinished(5000)) {
        QMetaObject::invokeMethod(
            this,
            [=]() {
              statusLabel->setText(tr("Not Initialized"));
              statusLabel->setStyleSheet("color: #ff7c30; font-size: 25px;");
            },
            Qt::QueuedConnection);
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
      QMetaObject::invokeMethod(
          this,
          [=]() {
            statusLabel->setText(status);
            statusLabel->setStyleSheet(QString("color: %1; font-size: 25px; border: none;").arg(color));
            updateModuleButton->setVisible(hasUpdates);
            resetModuleButton->setVisible(localChanges);
          },
          Qt::QueuedConnection);

    } catch (const std::exception &e) {
      std::cerr << "Exception in SubmoduleWidget::refresh for " << submoduleName.toStdString() << ": " << e.what() << std::endl;
    }
  });
}

void SubmoduleWidget::updateStatus() { refresh(); }

BPUpdaterPanel::~BPUpdaterPanel() {
  if (autoUpdateCheckTimer) {
    autoUpdateCheckTimer->stop();
    delete autoUpdateCheckTimer;
  }
  std::cout << "BPUpdaterPanel destructor complete" << std::endl;
}

BPUpdaterPanel::BPUpdaterPanel(QWidget *parent) : QWidget(parent), branchSelector(nullptr) {
  std::cout << "BPUpdaterPanel constructor start" << std::endl;

  // setFixedWidth(1640);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  // Add a small margin to the left and right of the panel
  setContentsMargins(20, 0, 20, 0);

  if (!isValidGitRepo()) {
    showErrorState(tr("Error: Not a valid git repository.\nPlease ensure /data/openpilot is a valid git repository."));
    return;
  }

  // Initialize the activity timer
  setupParamMonitoring();
  activityTimer = new QTimer(static_cast<QObject *>(this));
  activityTimer->setInterval(9000); // 9 seconds
  connect(activityTimer, &QTimer::timeout, this, &BPUpdaterPanel::simulateActivity);

  // Add automatic update check timer
  autoUpdateCheckTimer = new QTimer(static_cast<QObject *>(this));
  autoUpdateCheckTimer->setInterval(1800000); // 30 minutes
  connect(autoUpdateCheckTimer, &QTimer::timeout, this, &BPUpdaterPanel::checkForUpdates);
  autoUpdateCheckTimer->start();

  // Add the timer before setting up the layout
  QTimer *timeUpdateTimer = new QTimer(static_cast<QObject *>(this));
  timeUpdateTimer->setInterval(60000); // Update every minute
  connect(timeUpdateTimer, &QTimer::timeout, this, [this]() {
    if (mainRepoStatus)
      mainRepoStatus->refresh();
    for (auto *widget : submoduleWidgets) {
      if (widget)
        widget->refresh();
    }
  });
  timeUpdateTimer->start();

  setupLayout();
  updateBranchList();
  updateButtonStates();
  std::cout << "BPUpdaterPanel constructor end" << std::endl;
}

void BPUpdaterPanel::simulateActivity() {
  // Only run if this widget is visible
  if (!this->isVisible()) {
    return;
  }

  if (commandInProgress) {
    std::cout << "Simulating activity: command in progress" << std::endl;
    resetMaxDurationTimer();
  } else {
    std::cout << "Simulating activity in BPUpdaterPanel" << std::endl;
  }

  // Create a mouse move event at the current cursor position
  QPoint globalPos = QCursor::pos();
  QPoint localPos = this->mapFromGlobal(globalPos);

  // Add small random movement to simulate real activity
  localPos += QPoint(rand() % 5 - 2, rand() % 5 - 2);
  globalPos += QPoint(rand() % 5 - 2, rand() % 5 - 2);

  QMouseEvent mouseEvent(QEvent::MouseMove, localPos, globalPos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);

  // Send the event to this widget
  QCoreApplication::sendEvent(this, &mouseEvent);
}

void BPUpdaterPanel::stopActivitySimulation() {
  std::cout << "Stopping BPUpdaterPanel activity simulation | max duration timer stopped" << std::endl;
  activityTimer->stop();
}

void BPUpdaterPanel::resetMaxDurationTimer() {
  // Reset the max duration timer
  QTimer::singleShot(270000, this, &BPUpdaterPanel::stopActivitySimulation); // 4 minutes and 30 seconds
}

void BPUpdaterPanel::setupMainRepoSection() {
  mainRepoGroup = new QGroupBox(tr("Openpilot Directory"), static_cast<QWidget *>(this));
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

  QVBoxLayout *mainRepoLayout = new QVBoxLayout(mainRepoGroup);
  mainRepoLayout->setContentsMargins(0, 20, 0, 20);
  mainRepoLayout->setSpacing(15);

  // Create info section
  QWidget *infoWidget = new QWidget(mainRepoGroup);
  QVBoxLayout *infoLayout = new QVBoxLayout(infoWidget);
  infoLayout->setSpacing(10);

  updaterPanelStatusLabel = new QLabel(static_cast<QWidget *>(this));
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
            background-color: transparent !important;
            border-radius: 10px;
            padding: 5px;
        }
    )");
  connect(branchSelector, &BranchSelector::clicked, this, &BPUpdaterPanel::handleBranchSelection);
  infoLayout->addWidget(branchSelector);

  mainRepoLayout->addWidget(infoWidget);

  // Create button container with improved layout
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(15);

  // Create and style buttons with specific colors
  checkUpdatesButton = new QPushButton(static_cast<QWidget *>(this));
  checkUpdatesButton->setAutoFillBackground(true);

  // Create the permanent layout and labels
  QVBoxLayout *updateBtnLayout = new QVBoxLayout(checkUpdatesButton);
  updateBtnLayout->setSpacing(0);
  updateBtnLayout->setContentsMargins(0, 0, 0, 0);

  updateChkBtnLabelTxt = new QLabel(tr("Check Updates"), checkUpdatesButton);
  updateChkBtnLabelTxt->setObjectName("updateChkBtnLabelTxt");
  updateChkBtnLabelTxt->setAlignment(Qt::AlignCenter);
  updateChkBtnLabelTxt->setStyleSheet("font-size: 35px; background: transparent;");
  updateChkBtnLabelTxt->setAttribute(Qt::WA_TranslucentBackground);

  updateChkBtnTimeTxt = new QLabel("Checking...", checkUpdatesButton);
  updateChkBtnTimeTxt->setObjectName("updateChkBtnTimeTxt");
  updateChkBtnTimeTxt->setAlignment(Qt::AlignCenter);
  updateChkBtnTimeTxt->setStyleSheet("font-size: 25px; background: transparent; opacity: 0.8;");
  updateChkBtnTimeTxt->setAttribute(Qt::WA_TranslucentBackground);
  updateChkBtnTimeTxt->setFixedHeight(updateChkBtnTimeTxt->sizeHint().height());
  updateChkBtnTimeTxt->setMinimumHeight(0);

  // Add stretches for vertical centering
  updateBtnLayout->addStretch();
  updateBtnLayout->addWidget(updateChkBtnLabelTxt);
  updateBtnLayout->addWidget(updateChkBtnTimeTxt);
  updateBtnLayout->addStretch();

  QString buttonStyles = R"(
    QPushButton { border-radius: 10px; font-size: 35px; padding: 15px 25px; min-height: 70px; font-weight: 500; background-color: %1; color: %2; }
    QPushButton:pressed { background-color: %3; color: %4; }
    QPushButton:disabled { background-color: %5; color: %6; }
  )";

  checkUpdatesButton->setStyleSheet(buttonStyles.arg("#465BEA", "white", "#3049F4", "white", "#4F4F4F", "#888888"));

  updateRepoButton = new QPushButton(tr("Update"));
  updateRepoButton->setStyleSheet(buttonStyles.arg("#33AB4C", "white", "#2A9040", "white", "#4F4F4F", "#888888"));
  updateRepoButton->setVisible(false);

  updateAllButton = new QPushButton(tr("Update All"));
  updateAllButton->setStyleSheet(buttonStyles.arg("#33AB4C", "white", "#2A9040", "white", "#4F4F4F", "#888888"));
  updateAllButton->setVisible(false);

  repairRepoButton = new QPushButton(tr("Repair"));
  repairRepoButton->setStyleSheet(buttonStyles.arg("#7B1FA2", "white", "#6A1B9A", "white", "#4F4F4F", "#888888"));

  resetRepoButton = new QPushButton(tr("Reset"));
  resetRepoButton->setStyleSheet(buttonStyles.arg("#EA4646", "white", "#F43030", "white", "#4F4F4F", "#888888"));
  resetRepoButton->setVisible(false);

  showCommitsButton = new QPushButton(tr("History"));
  showCommitsButton->setStyleSheet(buttonStyles.arg("#33AB4C", "white", "#2A9040", "white", "#4F4F4F", "#888888"));

  buttonLayout->addWidget(checkUpdatesButton);
  buttonLayout->addWidget(updateRepoButton);
  buttonLayout->addWidget(updateAllButton);
  buttonLayout->addWidget(repairRepoButton);
  buttonLayout->addWidget(resetRepoButton);
  buttonLayout->addWidget(showCommitsButton);

  mainRepoLayout->addLayout(buttonLayout);

  // Connect signals
  connect(checkUpdatesButton, &QPushButton::clicked, this, &BPUpdaterPanel::checkForUpdates);
  connect(updateRepoButton, &QPushButton::clicked, this, &BPUpdaterPanel::handleRepoUpdate);
  connect(updateAllButton, &QPushButton::clicked, this, &BPUpdaterPanel::handleRepoUpdateAll);
  connect(repairRepoButton, &QPushButton::clicked, this, &BPUpdaterPanel::handleRepoRepair);
  connect(resetRepoButton, &QPushButton::clicked, this, &BPUpdaterPanel::handleRepoReset);
  connect(showCommitsButton, &QPushButton::clicked, this, &BPUpdaterPanel::showLastCommits);
}

void BPUpdaterPanel::setupSubmoduleSection() {
  QGroupBox *submoduleGroup = new QGroupBox(tr("Submodules"), static_cast<QWidget *>(this));
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

  QVBoxLayout *submoduleLayout = new QVBoxLayout(submoduleGroup);
  submoduleLayout->setContentsMargins(20, 20, 20, 20);

  BPScrollArea *scrollArea = new BPScrollArea(static_cast<QWidget *>(this));
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

  QWidget *scrollContent = new QWidget(scrollArea);
  QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);
  scrollLayout->setContentsMargins(0, 0, 15, 0);

  QProcess process;
  process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");
  process.start("git", QStringList() << "submodule" << "status");
  process.waitForFinished();
  QString output = QString::fromUtf8(process.readAllStandardOutput());

  if (output.isEmpty()) {
    QLabel *errorLabel = new QLabel(tr("No submodules found"), static_cast<QWidget *>(this));
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
    for (const QString &line : output.split("\n", QString::SkipEmptyParts)) {
      QString submoduleName = line.mid(1).split(" ").at(1);
      SubmoduleWidget *submodule = new SubmoduleWidget(submoduleName, static_cast<QWidget *>(this));
      submoduleWidgets.append(submodule);
      scrollLayout->addWidget(submodule);
    }
  }

  scrollContent->setLayout(scrollLayout);
  scrollArea->setWidget(scrollContent);
  submoduleLayout->addWidget(scrollArea);
  submoduleGroup->setLayout(submoduleLayout);
}

bool BPUpdaterPanel::isValidGitRepo() const {
  QProcess process;
  process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");
  process.start("git", QStringList() << "rev-parse" << "--git-dir");
  return process.waitForFinished(5000) && process.exitCode() == 0;
}

void BPUpdaterPanel::setupLayout() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(20);
  mainLayout->setContentsMargins(10, 10, 10, 10);

  if (!isValidGitRepo()) {
    QLabel *errorLabel = new QLabel(tr("Error: Not a valid git repository.\nPlease ensure /data/openpilot is a valid git repository."), this);
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
    QList<QGroupBox *> groups = findChildren<QGroupBox *>();
    if (groups.size() > 1) {
      mainLayout->addWidget(groups[1]);
    }
  });

  refreshTimer = new QTimer(this);
  refreshTimer->setInterval(60000);
  connect(refreshTimer, &QTimer::timeout, this, &BPUpdaterPanel::refreshAll);

  setLayout(mainLayout);
}

void BPUpdaterPanel::showEvent(QShowEvent *event) {
  std::cout << "Showing BPUpdaterPanel" << std::endl;
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
  connect(initTimer, &QTimer::timeout, this, &BPUpdaterPanel::staggeredInit);

  // Start staggered initialization
  initTimer->start(100);

  // Start activity simulation
  activityTimer->start();
  resetMaxDurationTimer();

  // Start auto update checks
  startAutoUpdateChecks();

  std::cout << "Started initialization and activity timers" << std::endl;
}

void BPUpdaterPanel::hideEvent(QHideEvent *event) {
  std::cout << "Hiding BPUpdaterPanel" << std::endl;

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

void BPUpdaterPanel::startAutoUpdateChecks() {
  // Only start auto updates if panel is visible and not onroad
  if (!isVisible() || isOnroad()) {
    return;
  }

  if (!autoUpdateCheckTimer) {
    autoUpdateCheckTimer = new QTimer(this);
    autoUpdateCheckTimer->setInterval(1800000); // 30 minutes
    connect(autoUpdateCheckTimer, &QTimer::timeout, this, &BPUpdaterPanel::checkForUpdates);
  }

  // Initial check after a short delay when UI is shown
  QTimer::singleShot(2000, this, [this]() {
    if (!isOnroad()) {
      checkForUpdates();
    }
  });

  // Start the timer for subsequent checks
  autoUpdateCheckTimer->start();
}

void BPUpdaterPanel::notifyShallowRepository() {
  if (BPUpdateConfirmDialog::
          confirm(tr("Shallow Repository Detected"),
                  tr("This is a shallow git repository with limited history. Some branch operations may not work correctly. Would you like to fetch the full repository history?"),
                  tr("Fetch Full History"), tr("Not Now"), this)) {
    handleUnshallow();
  }
}

void BPUpdaterPanel::handleUnshallow() {
  if (!BPUpdateConfirmDialog::confirm(tr("Fetch Full Repository"),
                                      tr("Are you sure you want to fetch the full repository history?\n"
                                         "This will download the entire commit history which might take some time depending on your internet connection."),
                                      tr("Fetch"), tr("Cancel"), this)) {
    return;
  }

  // Command to convert shallow clone to full clone
  QString command = "rm -f .git/index.lock && git fetch --unshallow";

  // Show command output dialog
  showCommandOutputDialog(tr("Fetching Full Repository History"), command, "", 1800000, true, true, true); // 30 minute timeout

  // After unshallowing, refresh the UI to hide the warning widget
  QTimer::singleShot(2000, this, &BPUpdaterPanel::refreshAll);
}

void BPUpdaterPanel::stopAutoUpdateChecks() {
  if (autoUpdateCheckTimer) {
    autoUpdateCheckTimer->stop();
  }
}

void BPUpdaterPanel::refreshAll() {
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

    QMetaObject::invokeMethod(
        this,
        [=]() {
          resetRepoButton->setVisible(hasLocal);
          updateRepoButton->setVisible(hasUpdates);

          updateButtonStates();
          QString status;
          QString color;
          if (hasLocal && hasUpdates) {
            status = tr("(Updates Available - Local Modified)");
            color = "#FF3C0F"; // Or any color that stands out
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
        },
        Qt::QueuedConnection);
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

void BranchSelector::createLoadingOverlay() {
  loadingOverlay = new QWidget(this);
  loadingOverlay->setVisible(false);
  loadingOverlay->setStyleSheet("background-color: rgba(0, 0, 0, 0.7); border-radius: 20px;");

  QVBoxLayout *overlayLayout = new QVBoxLayout(loadingOverlay);

  loadingLabel = new QLabel(tr("Retrieving Branches..."), loadingOverlay);
  loadingLabel->setStyleSheet("color: white; font-size: 35px; background: transparent;");
  loadingLabel->setAlignment(Qt::AlignCenter);

  overlayLayout->addWidget(loadingLabel);
  loadingOverlay->setLayout(overlayLayout);
}

void BranchSelector::showLoadingOverlay(bool show) {
  if (show) {
    loadingOverlay->setGeometry(rect());
    loadingOverlay->raise();
  }
  loadingOverlay->setVisible(show);
}

void BranchSelector::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (loadingOverlay && loadingOverlay->isVisible()) {
    loadingOverlay->setGeometry(rect());
  }
}

BranchSelector::GitCommandResult BranchSelector::executeGitCommand(const QString &command, const QString &workingDir, int timeoutMs) const {
  GitCommandResult result;
  QProcess process;
  process.setWorkingDirectory(workingDir);

  process.start("/bin/bash", QStringList() << "-c" << command);

  if (!process.waitForStarted(5000)) {
    result.success = false;
    result.error = "Failed to start command: " + command;
    return result;
  }

  // Make sure we wait for the process to finish or kill it
  if (!process.waitForFinished(timeoutMs)) {
    process.kill();
    // Wait a bit more to make sure it's terminated
    process.waitForFinished(1000);
    result.success = false;
    result.error = "Command timed out: " + command;
    return result;
  }

  result.success = (process.exitCode() == 0);
  result.output = QString::fromUtf8(process.readAllStandardOutput());
  result.error = QString::fromUtf8(process.readAllStandardError());
  return result;
}

void BranchSelector::getBranchesAsync(bool includeRemote, std::function<void(QStringList)> callback) {
  emit branchLoadingStarted();
  showLoadingOverlay(true);

  QtConcurrent::run([this, includeRemote, callback]() {
    QStringList branches;
    QString workingDir = qApp->applicationDirPath() + "/../..";
    bool isShallow = false;

    // Check if shallow repository
    auto shallowCheck = executeGitCommand("git rev-parse --is-shallow-repository", workingDir, 5000);
    if (shallowCheck.success && shallowCheck.output.trimmed() == "true") {
      isShallow = true;
    }

    // Always get local branches first
    auto localBranchResult = executeGitCommand("git branch", workingDir, 10000);
    if (localBranchResult.success) {
      for (QString branch : localBranchResult.output.split("\n", QString::SkipEmptyParts)) {
        branch = branch.trimmed();
        if (branch.startsWith("*")) {
          branch = branch.mid(2);
        }
        if (!branch.isEmpty() && !branches.contains(branch)) {
          branches.append(branch);
        }
      }
    }

    // ALWAYS try to get remote branches, regardless of includeRemote parameter
    // First try using git branch -r (works with cached info, no network needed)
    auto remoteBranchResult = executeGitCommand("git branch -r", workingDir, 10000);
    if (remoteBranchResult.success) {
      for (QString branch : remoteBranchResult.output.split("\n", QString::SkipEmptyParts)) {
        branch = branch.trimmed();
        if (branch.contains("->"))
          continue;

        if (branch.startsWith("origin/")) {
          branch = branch.mid(7); // Remove "origin/"
          if (!branch.isEmpty() && !branches.contains(branch)) {
            branches.append(branch);
          }
        }
      }
    }

    // If internet is available (includeRemote = true), then also try fetching updates
    if (includeRemote) {
      // Use a more reliable way to get remote branches
      auto lsRemoteResult = executeGitCommand("git ls-remote --heads origin", workingDir, 15000);
      if (lsRemoteResult.success) {
        QString output = lsRemoteResult.output;
        QRegExp rx("refs/heads/([^\\s]+)");
        int pos = 0;
        while ((pos = rx.indexIn(output, pos)) != -1) {
          QString branch = rx.cap(1);
          if (!branch.isEmpty() && !branches.contains(branch)) {
            branches.append(branch);
          }
          pos += rx.matchedLength();
        }
      }
    }

    branches.sort();

    QMetaObject::invokeMethod(
        this,
        [this, branches, isShallow, callback]() {
          showLoadingOverlay(false);
          emit branchLoadingFinished();
          callback(branches);

          // Notify about shallow repository
          if (isShallow) {
            QWidget *parent = this->parentWidget();
            while (parent) {
              if (auto *panel = qobject_cast<BPUpdaterPanel *>(parent)) {
                QMetaObject::invokeMethod(panel, "notifyShallowRepository", Qt::QueuedConnection);
                break;
              }
              parent = parent->parentWidget();
            }
          }
        },
        Qt::QueuedConnection);
  });
}

void BPUpdaterPanel::updateBranchList() {
  if (!branchSelector)
    return;

  QProcess process;
  process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");

  // Just get current branch for initial display
  process.start("git", QStringList() << "branch");
  if (process.waitForFinished(5000)) {
    QString output = QString::fromUtf8(process.readAllStandardOutput());
    for (const QString &branch : output.split("\n", QString::SkipEmptyParts)) {
      QString cleanBranch = branch.trimmed();
      if (cleanBranch.startsWith("*")) {
        branchSelector->setValue(cleanBranch.mid(2));
        break;
      }
    }
  }
}

void BPUpdaterPanel::handleBranchSelection() {
  QTimer::singleShot(0, this, [this]() {
    // Always try to get remote branches
    branchSelector->getBranchesAsync(true, [this](QStringList branches) {
      if (branches.isEmpty()) {
        BPUpdateConfirmDialog::alert(tr("Unable to get branch list. Please check your repository status."), this);
        return;
      }

      QString currentBranch = branchSelector->getValue();
      QString selection = BPUpdaterSelectionDialog::getSelection(tr("Select Branch"), branches, currentBranch, this);

      if (!selection.isEmpty()) {
        switchBranch(selection);
        branchSelector->setValue(selection);
      }
    });
  });
}

void BPUpdaterPanel::switchBranch(const QString &branch) {
  if (hasUncommittedChanges()) {
    if (!BPUpdateConfirmDialog::confirm(tr("Switch Branch"), tr("You have uncommitted changes that will be lost if you switch branches.\nContinue?"), tr("Yes"), tr("Cancel"),
                                        this)) {
      return;
    }
  }

  // Check if branch exists locally and remotely
  QProcess process;
  process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");

  // Check local branches
  process.start("git", QStringList() << "branch");
  process.waitForFinished(5000);
  QStringList localBranches = QString(process.readAllStandardOutput()).split("\n", QString::SkipEmptyParts);
  bool branchExistsLocally = false;

  for (QString localBranch : localBranches) {
    localBranch = localBranch.trimmed();
    if (localBranch.startsWith("*")) {
      localBranch = localBranch.mid(2);
    }
    if (localBranch == branch) {
      branchExistsLocally = true;
      break;
    }
  }

  // Check remote branches
  process.start("git", QStringList() << "branch" << "-r");
  process.waitForFinished(5000);
  QStringList remoteBranches = QString(process.readAllStandardOutput()).split("\n", QString::SkipEmptyParts);
  bool branchExistsRemotely = false;

  for (QString remoteBranch : remoteBranches) {
    remoteBranch = remoteBranch.trimmed();
    if (remoteBranch == "origin/" + branch) {
      branchExistsRemotely = true;
      break;
    }
  }

  QString command;

  if (branchExistsLocally && branchExistsRemotely) {
    // Branch exists both locally and remotely - handle potential rebase
    command = QString("git checkout -f %1 && "
                      "git fetch origin && "
                      "git reset --hard origin/%1 && " // Reset to remote version
                      "git clean -fd && git pull && "
                      "git submodule update --init --recursive && scons -j$(nproc)")
                  .arg(branch);
  } else if (branchExistsLocally) {
    // Branch exists only locally
    command = QString("git checkout -f %1 && "
                      "git reset --hard && "
                      "git clean -fd && git pull && "
                      "git submodule update --init --recursive && scons -j$(nproc)")
                  .arg(branch);
  } else if (branchExistsRemotely) {
    // Branch exists only remotely - create tracking branch
    command = QString("git checkout -f -b %1 origin/%1 && "
                      "git reset --hard && "
                      "git clean -fd && git pull && "
                      "git submodule update --init --recursive && scons -j$(nproc)")
                  .arg(branch);
  } else {
    // Try to fetch the branch from remote
    command = QString("git fetch origin %1:%1 && "
                      "git checkout -f %1 && "
                      "git reset --hard && "
                      "git clean -fd && "
                      "git pull && "
                      "git submodule update --init --recursive && scons -j$(nproc)")
                  .arg(branch);
  }

  showCommandOutputDialog(tr("Switching Branch"), command, "", 1800000, true, true, true);
}

bool BPUpdaterPanel::hasUncommittedChanges() const {
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

bool BPUpdaterPanel::hasUpdatesAvailable() const {
  QProcess process;
  process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");

  // First check if current branch has an upstream branch
  process.start("git", QStringList() << "rev-parse" << "--abbrev-ref" << "--symbolic-full-name" << "@{u}");
  if (!process.waitForFinished(5000) || process.exitCode() != 0) {
    process.kill();
    std::cerr << "Current branch has no upstream configured" << std::endl;
    QMetaObject::invokeMethod(const_cast<BPUpdaterPanel *>(this), [this]() { updateStatusLabel(UpdaterStatus::NO_REMOTE_BRANCH); }, Qt::QueuedConnection);
    return false;
  }

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

void BPUpdaterPanel::checkForUpdates() {
  // Skip check if another error dialog is showing
  if (errorDialogShowing) {
    return;
  }

  // Check if we're onroad - don't allow updates while driving
  if (isOnroad()) {
    if (checkUpdatesButton) {
      checkUpdatesButton->setEnabled(false);
      updateChkBtnLabelTxt->setText(tr("Updates Disabled"));
      updateChkBtnTimeTxt->setText(tr("(Vehicle in Motion)"));
    }
    return;
  }

  // Check if panel is visible for manual updates
  if (!isVisible() && !autoUpdateCheckTimer) {
    return;
  }

  // Kill system.updated.updated
  std::system("killall system.updated.updated");

  checkUpdatesButton->setEnabled(false);
  updateChkBtnLabelTxt->setStyleSheet("color: #888888; font-size: 35px; background: transparent;");
  updateChkBtnTimeTxt->setStyleSheet("color: #888888; font-size: 25px; background: transparent; opacity: 0.8;");
  updateChkBtnLabelTxt->setText(tr("Checking..."));
  updateChkBtnTimeTxt->setText("");

  QtConcurrent::run([=]() {
    QString workingDir = qApp->applicationDirPath() + "/../..";

    // First check if current branch has an upstream branch
    auto upstreamCheck = executeGitCommand("git rev-parse --abbrev-ref --symbolic-full-name @{u}", workingDir, 5000);

    if (!upstreamCheck.success) {
      QMetaObject::invokeMethod(
          this,
          [=]() {
            updateChkBtnLabelTxt->setText(tr("Check Updates"));
            updateChkBtnTimeTxt->setText("");
            checkUpdatesButton->setEnabled(true);
            updateStatusLabel(UpdaterStatus::NO_REMOTE_BRANCH);
            updateButtonStates();
          },
          Qt::QueuedConnection);
      return;
    }

    QMetaObject::invokeMethod(
        this,
        [=]() {
          updateChkBtnLabelTxt->setText(tr("Checking..."));
          updateChkBtnTimeTxt->setText("");
        },
        Qt::QueuedConnection);

    // Fetch updates
    auto fetchResult = executeGitCommand("rm -f .git/index.lock && git fetch --all", workingDir, 45000);

    if (!fetchResult.success) {
      QMetaObject::invokeMethod(
          this,
          [=]() {
            // Only show error dialog if panel is visible
            if (isVisible() && !errorDialogShowing) {
              errorDialogShowing = true;

              // Create custom error dialog
              QDialog *errorDialog = new QDialog(this);
              errorDialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
              errorDialog->setStyleSheet("background-color: black;");
              errorDialog->setFixedSize(2160, 1080);

              QVBoxLayout *layout = new QVBoxLayout(errorDialog);
              layout->setContentsMargins(45, 35, 45, 45);
              layout->setSpacing(30);

              // Add title
              QLabel *titleLabel = new QLabel(tr("Update Check Failed"), errorDialog);
              titleLabel->setStyleSheet("font-size: 90px; font-weight: 600; color: white; background-color: black;");
              layout->addWidget(titleLabel);

              // Add error message
              QString errorMsg = fetchResult.timedOut ? tr("Fetch timed out") : tr("Fetch failed: %1").arg(fetchResult.error);
              QLabel *messageLabel = new QLabel(errorMsg, errorDialog);
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
              QPushButton *okButton = new QPushButton(tr("OK"), errorDialog);
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
              connect(errorDialog, &QDialog::finished, [this]() { errorDialogShowing = false; });

              errorDialog->show();
              setupFullscreenDialog(errorDialog);
            }

            // checkUpdatesButton->setText(tr("Check Updates"));
            updateChkBtnLabelTxt->setStyleSheet("color: white; font-size: 35px; background: transparent;");
            updateChkBtnTimeTxt->setStyleSheet("color: white; font-size: 25px; background: transparent; opacity: 0.8;");
            checkUpdatesButton->setEnabled(true);
          },
          Qt::QueuedConnection);
      return;
    }

    // Check for updates
    auto updateResult = executeGitCommand("git rev-list HEAD..@{u} --count", workingDir, 5000);

    if (!updateResult.success) {
      QMetaObject::invokeMethod(
          this,
          [=]() {
            // Only show error dialog if panel is visible
            if (isVisible() && !errorDialogShowing) {
              errorDialogShowing = true;

              // Create custom error dialog
              QDialog *errorDialog = new QDialog(this);
              errorDialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
              errorDialog->setStyleSheet("background-color: black;");
              errorDialog->setFixedSize(2160, 1080);

              QVBoxLayout *layout = new QVBoxLayout(errorDialog);
              layout->setContentsMargins(45, 35, 45, 45);
              layout->setSpacing(30);

              // Add title
              QLabel *titleLabel = new QLabel(tr("Update Check Failed"), errorDialog);
              titleLabel->setStyleSheet("font-size: 90px; font-weight: 600; color: white; background-color: black;");
              layout->addWidget(titleLabel);

              // Add error message
              QLabel *messageLabel = new QLabel(tr("Failed to check for updates: %1").arg(updateResult.error), errorDialog);
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
              QPushButton *okButton = new QPushButton(tr("OK"), errorDialog);
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
              connect(errorDialog, &QDialog::finished, [this]() { errorDialogShowing = false; });

              errorDialog->show();
              setupFullscreenDialog(errorDialog);
            }

            updateChkBtnLabelTxt->setText(tr("Check Updates"));
            updateChkBtnTimeTxt->setText("");
            checkUpdatesButton->setEnabled(true);
          },
          Qt::QueuedConnection);
      return;
    }

    bool hasUpdates = updateResult.output.trimmed().toInt() > 0;
    lastUpdateCheck = QDateTime::currentDateTime();

    QMetaObject::invokeMethod(
        this,
        [=]() {
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
          for (auto *widget : submoduleWidgets) {
            if (widget) {
              widget->refresh();
            }
          }
        },
        Qt::QueuedConnection);
  });
}

void BPUpdaterPanel::staggeredInit() {
  if (initStage >= 5) { // Changed from 4 to 5 to add new stage
    // Handle all submodules in stage 5
    if (currentSubmoduleIndex < submoduleWidgets.size()) {
      std::cout << "BPUpdaterPanel: Stage 5 - Refreshing submodule " << currentSubmoduleIndex << std::endl;
      QTimer::singleShot(100, this, [this]() {
        auto *widget = submoduleWidgets[currentSubmoduleIndex];
        if (widget) {
          widget->refresh();
          std::cout << "BPUpdaterPanel: Refreshed submodule " << currentSubmoduleIndex << std::endl;
        } else {
          std::cerr << "BPUpdaterPanel: Widget is null for submodule " << currentSubmoduleIndex << std::endl;
        }
        currentSubmoduleIndex++;
        if (initTimer) {
          initTimer->start(500);
        }
      });
    } else {
      std::cout << "BPUpdaterPanel: Stage 5 - All submodules processed, stopping init timer" << std::endl;
      if (initTimer) {
        initTimer->stop();
        delete initTimer;
        initTimer = nullptr;
      }
      if (refreshTimer) {
        refreshTimer->start(60000);
      }
    }
    return;
  }

  switch (initStage++) {
  case 0:
    std::cout << "BPUpdaterPanel: Stage 0 - Just showing UI" << std::endl;
    break;
  case 1:
    std::cout << "BPUpdaterPanel: Stage 1 - Updating branch list" << std::endl;
    QTimer::singleShot(100, this, [this]() { updateBranchList(); });
    break;
  case 2:
    std::cout << "BPUpdaterPanel: Stage 2 - Refreshing main repo status" << std::endl;
    QTimer::singleShot(200, this, [this]() {
      if (mainRepoStatus) {
        mainRepoStatus->refresh();
      }
    });
    break;
  case 3:
    std::cout << "BPUpdaterPanel: Stage 3 - Checking local changes and updates" << std::endl;
    QTimer::singleShot(300, this, [this]() {
      QtConcurrent::run([=]() {
        bool hasLocal = hasUncommittedChanges();
        bool hasUpdates = hasUpdatesAvailable();

        QMetaObject::invokeMethod(
            this,
            [=]() {
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
            },
            Qt::QueuedConnection);
      });
    });
    break;
  case 4:
    std::cout << "BPUpdaterPanel: Stage 4 - Checking for updates" << std::endl;
    QTimer::singleShot(2000, this, [this]() { // 2 second delay after status checks
      checkForUpdates();
    });
    break;
  }
}

QString BPUpdaterPanel::getTimeAgoString(const QDateTime &time) {
  if (!time.isValid())
    return "";

  qint64 secs = time.secsTo(QDateTime::currentDateTime());
  if (secs < 60)
    return tr("%1 seconds ago").arg(secs);
  if (secs < 3600)
    return tr("%1 minutes ago").arg(secs / 60);
  if (secs < 86400)
    return tr("%1 hours ago").arg(secs / 3600);
  return tr("%1 days ago").arg(secs / 86400);
}

QString BPUpdaterPanel::getTimeDateString(const QDateTime &time) {
  if (!time.isValid())
    return "";
  return time.toString("MM/dd h:mm AP");
}

void BPUpdaterPanel::updateCheckUpdatesButtonText() {
  if (!lastUpdateCheck.isValid()) {
    updateChkBtnTimeTxt->setText("");
  } else {
    updateChkBtnTimeTxt->setText(lastUpdateCheck.toString("MM/dd hh:mm AP"));
  }
}

BranchSelector::BranchSelector(const QString &title, const QString &text, const QString &desc, QWidget *parent) : QWidget(parent) {
  createLoadingOverlay();

  mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  QWidget *labelBox = new QWidget(this);
  QVBoxLayout *labelLayout = new QVBoxLayout(labelBox);
  labelLayout->setSpacing(0);
  labelLayout->setContentsMargins(0, 0, 0, 0);

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

  // Create repo label with two different colors
  QHBoxLayout *repoLayout = new QHBoxLayout();
  repoTitle = new QLabel("Repo: ", labelBox);
  repoTitle->setStyleSheet("font-size: 35px; font-weight: 500; color: #E4E4E4;");
  repoValue = new QLabel(repoInfo, labelBox);
  repoValue->setStyleSheet("font-size: 35px; font-weight: 500; color: #A0A0A0;");
  repoLayout->addWidget(repoTitle);
  repoLayout->addWidget(repoValue);
  repoLayout->addStretch();

  // Create branch layout
  QHBoxLayout *branchLayout = new QHBoxLayout();
  QLabel *branchTitle = new QLabel("Branch: ", labelBox);
  branchTitle->setStyleSheet("font-size: 35px; color: #E4E4E4;");
  branchNameLabel = new QLabel(labelBox);
  branchNameLabel->setStyleSheet("font-size: 35px; color: #A0A0A0;");
  branchLayout->addWidget(branchTitle);
  branchLayout->addWidget(branchNameLabel);
  branchLayout->addStretch();

  labelLayout->addLayout(repoLayout);
  labelLayout->addLayout(branchLayout);

  QPushButton *changeBranchBtn = new QPushButton("CHANGE", this);
  changeBranchBtn->setFixedSize(220, 100);
  changeBranchBtn->setStyleSheet(R"(
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

  connect(changeBranchBtn, &QPushButton::clicked, this, &BranchSelector::clicked);

  mainLayout->addWidget(labelBox);
  mainLayout->addStretch();
  mainLayout->setContentsMargins(0, 10, 0, 10);
  mainLayout->addWidget(changeBranchBtn);
}

void BranchSelector::setValue(const QString &branchName, const QString &status, const QString &statusColor) {
  if (status.isEmpty()) {
    branchNameLabel->setText(branchName);
  } else {
    QString styledText = QString("%1 <span style='color: %2;'>%3</span>").arg(branchName, statusColor, status);
    branchNameLabel->setTextFormat(Qt::RichText);
    branchNameLabel->setText(styledText);
  }
}

void BPUpdaterPanel::showCommandOutputDialog(const QString &title, const QString &command, const QString &workingDir, int timeoutMs, bool showKillBtn, bool showRetryBtn,
                                             bool showRebootBtn) {
  // Clean up any existing dialog
  if (currentDialog) {
    currentDialog->close();
    currentDialog->deleteLater();
    currentDialog = nullptr;
  }

  // Set commandInProgress to true to make sure the UI stays awake while the command is running
  commandInProgress = true;

  // Create and set up process
  QProcess *process = new QProcess(this);
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
  QVBoxLayout *layout = new QVBoxLayout(currentDialog);
  layout->setContentsMargins(45, 35, 45, 45);
  layout->setSpacing(0);

  // Add title
  QLabel *titleLabel = new QLabel(title);
  titleLabel->setStyleSheet("font-size: 90px; font-weight: 600; background-color: black;");
  layout->addWidget(titleLabel);
  layout->addSpacing(30);

  // Create elapsed timer to track runtime
  QElapsedTimer *elapsedTimer = new QElapsedTimer();
  elapsedTimer->start();

  // Format time values as m:ss
  auto formatTime = [](int totalSeconds) {
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    return QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0'));
  };

  // Create output text area
  QTextEdit *outputText = new QTextEdit(currentDialog);
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
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(20);

  // Add kill button (if enabled)
  QPushButton *killButton = nullptr;
  if (showKillBtn) {
    killButton = new QPushButton(tr("Stop Command"), currentDialog);
    killButton->setFixedHeight(100);
    killButton->setStyleSheet(R"(
            QPushButton {
                background-color: #EA4646;
                font-size: 55px;
                font-weight: 400;
                border-radius: 20px;
                color: white;
            }
            QPushButton:pressed {
                background-color: #F43030;
            }
        )");
    buttonLayout->addWidget(killButton);
  }

  // Add retry button (if enabled)
  QPushButton *retryButton = nullptr;
  if (showRetryBtn) {
    retryButton = new QPushButton(tr("Retry"), currentDialog);
    retryButton->setFixedHeight(100);
    retryButton->setVisible(false); // Hide initially
    retryButton->setStyleSheet(R"(
            QPushButton {
                background-color: #7B1FA2;
                font-size: 55px;
                font-weight: 400;
                border-radius: 20px;
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
  QPushButton *rebootButton = nullptr;
  if (showRebootBtn) {
    rebootButton = new QPushButton(tr("Reboot"), currentDialog);
    rebootButton->setFixedHeight(100);
    rebootButton->setVisible(false); // Hide initially
    rebootButton->setStyleSheet(R"(
            QPushButton {
                background-color: #33Ab4C;
                font-size: 55px;
                font-weight: 400;
                border-radius: 20px;
                color: white;
            }
            QPushButton:pressed {
                background-color: #2A9040;
            }
        )");
    buttonLayout->addWidget(rebootButton);
  }

  // Close button (initially disabled)
  QPushButton *closeButton = new QPushButton(tr("Command Running..."), currentDialog);
  closeButton->setEnabled(false);
  closeButton->setFixedHeight(100);
  closeButton->setStyleSheet(R"(
        QPushButton {
            background-color: #465BEA;
            font-size: 55px;
            font-weight: 400;
            border-radius: 20px;
            color: white;
        }
        QPushButton:pressed {
            background-color: #3049F4;
        }
        QPushButton:disabled {
            background-color: #4F4F4F;
            color: white;
        }
    )");
  buttonLayout->addWidget(closeButton);

  layout->addSpacing(50);
  layout->addLayout(buttonLayout);

  // Add timeout timer
  QTimer *timeoutTimer = new QTimer(currentDialog);
  timeoutTimer->setSingleShot(true);
  timeoutTimer->setInterval(timeoutMs);

  // Create runtime display timer that updates every second
  QTimer *runtimeTimer = new QTimer(currentDialog);
  runtimeTimer->setInterval(1000); // Update every second

  // Update the runtime timer on the button
  connect(runtimeTimer, &QTimer::timeout, [=]() {
    if (process->state() == QProcess::NotRunning) {
      return; // Skip updating if process has finished
    }

    int elapsedSecs = elapsedTimer->elapsed() / 1000;
    int timeoutSecs = timeoutMs / 1000;

    // Format as Command Running: (MM:SS/TT:TT)
    QString timerText = tr("Command Running: (%1/%2)").arg(formatTime(elapsedSecs)).arg(formatTime(timeoutSecs));

    // Set the button text with formatting (green timer text)
    closeButton->setText(timerText);
  });

  // Start the runtime timer immediately
  runtimeTimer->start();

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
      outputText->append("\n<span style='color: #ff7c30;'>Process timed out after " + QString::number(timeoutMs / 1000) + " seconds</span>");
      process->kill();
      commandInProgress = false;
      if (killButton)
        killButton->hide();
      if (retryButton) {
        retryButton->setVisible(true);
        retryButton->setEnabled(true);
      }
      closeButton->setEnabled(true);
      closeButton->setText(tr("Close (Timed Out)"));
      closeButton->setFixedHeight(100);
      closeButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #EA4646;
                    font-size: 55px;
                    font-weight: 400;
                    border-radius: 20px;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #F43030;
                }
            )");

      // Stop the runtime timer
      runtimeTimer->stop();
    }
  });

  // Connect kill button
  if (killButton) {
    connect(killButton, &QPushButton::clicked, [=]() {
      if (process->state() != QProcess::NotRunning) {
        outputText->append("\n<span style='color: #ff7c30;'>Process terminated by user</span>");
        process->kill();
        killButton->hide();
        if (retryButton)
          retryButton->setEnabled(true);
        closeButton->setEnabled(true);
        closeButton->setText(tr("Close (Terminated)"));
        closeButton->setStyleSheet(R"(
                    QPushButton {
                        background-color: #EA4646;
                        font-size: 55px;
                        font-weight: 400;
                        border-radius: 20px;
                        color: white;
                    }
                    QPushButton:pressed {
                        background-color: #F43030;
                    }
                )");

        // Stop the runtime timer
        runtimeTimer->stop();

        // Show terminated message
        int elapsedSecs = elapsedTimer->elapsed() / 1000;
        QString finalTime = QString("Terminated at %1").arg(formatTime(elapsedSecs));
      }
    });
  }

  // Handle process completion
  connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=](int exitCode, QProcess::ExitStatus exitStatus) {
    timeoutTimer->stop();
    closeButton->setEnabled(true);
    commandInProgress = false;
    if (killButton)
      killButton->hide();

    // Stop the runtime timer
    runtimeTimer->stop();

    // Show final runtime
    int elapsedSecs = elapsedTimer->elapsed() / 1000;
    QString finalTime = QString("Total Runtime: %1").arg(formatTime(elapsedSecs));

    if (exitStatus == QProcess::CrashExit) {
      // Show retry button for crash
      if (retryButton) {
        retryButton->setVisible(true);
        retryButton->setEnabled(true);
      }
      closeButton->setText(tr("Close (Crashed)"));
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
      for (auto *widget : submoduleWidgets) {
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
      retryButton->setVisible(false); // Hide when retrying
      closeButton->setEnabled(false);
      closeButton->setText(tr("Command Running..."));
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
                    color: white;
                }
            )");
      if (killButton) {
        killButton->show();
      }

      // Reset elapsed timer
      elapsedTimer->restart();

      // Reset and start timer display
      runtimeTimer->start();

      // Reset and start timeout timer
      timeoutTimer->start();

      // Start process again
      process->start("/bin/bash", QStringList() << "-c" << command);
    });
  }

  // Connect reboot button
  if (rebootButton) {
    connect(rebootButton, &QPushButton::clicked, [=]() {
      if (BPUpdateConfirmDialog::confirm(tr("Reboot"), tr("Are you sure you want to reboot?"), tr("Reboot"), tr("Cancel"), currentDialog)) {
        params.putBool("DoReboot", true);
        QProcess::execute("reboot");
      }
    });
  }

  // Connect close button and cleanup
  connect(closeButton, &QPushButton::clicked, currentDialog, &QDialog::accept);
  connect(currentDialog, &QDialog::finished, [=]() {
    timeoutTimer->stop();
    runtimeTimer->stop();
    delete elapsedTimer;
    process->deleteLater();
    if (currentDialog) {
      currentDialog->deleteLater();
      currentDialog = nullptr;
    }
  });

  // Set dialog size and show
  QScreen *screen = QGuiApplication::primaryScreen();
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

void BPUpdaterPanel::showCommitHistory(QWidget *parent, const QString &title, const QString &workingDir) {
  QDialog *dialog = new QDialog(parent);
  dialog->setWindowTitle(title);
  dialog->setModal(true);

  QVBoxLayout *layout = new QVBoxLayout(dialog);

  // Add title
  QLabel *titleLabel = new QLabel(title);
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
  QScrollArea *scrollArea = new QScrollArea(dialog);
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

  QWidget *scrollContent = new QWidget(scrollArea);
  QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);
  scrollLayout->setContentsMargins(0, 0, 0, 0);

  QTableWidget *table = new QTableWidget(scrollContent);
  table->setColumnCount(4);
  table->setHorizontalHeaderLabels({tr("Commit ID"), tr("Description"), tr("Time"), tr("Actions")});
  table->setShowGrid(false);
  table->setSelectionMode(QAbstractItemView::NoSelection); // Disable selection
  table->setFocusPolicy(Qt::NoFocus);                      // Prevent focus rectangle
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

  // First fetch the latest remote history
  process.start("git", QStringList() << "fetch");
  process.waitForFinished(30000);

  // get the last 30 commits
  process.start("git", QStringList() << "log" << "--all" << "-n" << "30" << "--pretty=format:%h|||%s|||%cr");

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
      auto createItem = [](const QString &text, Qt::Alignment alignment) {
        QTableWidgetItem *item = new QTableWidgetItem(text);
        item->setTextAlignment(alignment);
        return item;
      };

      // Add existing columns
      table->setItem(i, 0, createItem(parts[0], Qt::AlignLeft | Qt::AlignVCenter));
      table->setItem(i, 1, createItem(parts[1], Qt::AlignLeft | Qt::AlignVCenter));
      table->setItem(i, 2, createItem(parts[2], Qt::AlignLeft | Qt::AlignVCenter));

      // Add checkout button
      QWidget *buttonContainer = new QWidget();
      QHBoxLayout *buttonLayout = new QHBoxLayout(buttonContainer);
      buttonLayout->setContentsMargins(10, 0, 10, 0);
      buttonLayout->setSpacing(10);

      QPushButton *checkoutButton = new QPushButton(tr("Checkout"));
      checkoutButton->setStyleSheet(R"(
            QPushButton {
                border-radius: 10px;
                font-size: 30px;
                padding: 15px;
                background-color: #465BEA;
                color: white;
                min-width: 175px;
                min-height: 30px;
            }
            QPushButton:pressed { background-color: #3049F4; }
        )");

      // Connect checkout button
      QString commitHash = parts[0];
      connect(checkoutButton, &QPushButton::clicked, [dialog, commitHash, parent]() {
        if (auto *panel = qobject_cast<BPUpdaterPanel *>(parent)) {
          if (BPUpdateConfirmDialog::confirm(tr("Checkout Commit"),
                                             tr("Are you sure you want to checkout commit %1?\n"
                                                "This will lose all local changes.")
                                                 .arg(commitHash),
                                             tr("Checkout"), tr("Cancel"), dialog)) {

            QString command = QString("git checkout %1 -f && "
                                      "git reset --hard && "
                                      "git clean -fd && "
                                      "git submodule update --init --recursive && "
                                      "scons -j$(nproc)")
                                  .arg(commitHash);

            dialog->accept(); // Close history dialog
            panel->showCommandOutputDialog(tr("Checking Out Commit"), command,
                                           "",     // Use default working directory
                                           1800000, // 30 minutes timeout
                                           true,   // Show kill button
                                           true,   // Show retry button
                                           true    // Show reboot button
            );
          }
        }
      });

      buttonLayout->addWidget(checkoutButton);
      buttonLayout->addStretch();
      table->setCellWidget(i, 3, buttonContainer);
    }
  }

  table->setColumnWidth(0, 260);
  table->setColumnWidth(2, 350);
  table->setColumnWidth(3, 300);

  scrollLayout->addWidget(table);
  scrollContent->setLayout(scrollLayout);
  scrollArea->setWidget(scrollContent);
  layout->addWidget(scrollArea);

  QPushButton *closeButton = new QPushButton(tr("Close"), dialog);
  closeButton->setStyleSheet(R"(
        QPushButton {
            border-radius: 10px;
            font-size: 55px;
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
  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen) {
    dialog->setFixedSize(2160, 1080);
  }

  dialog->show();
  setupFullscreenDialog(dialog);

  dialog->exec();
}

void BPUpdaterPanel::handleRepoUpdate() {
  // If there are uncommitted local changes, confirm before proceeding
  if (hasUncommittedChanges()) {
    if (!BPUpdateConfirmDialog::confirm(tr("Update Repository"), tr("You have local changes that will be overwritten by this update. Continue?"), tr("Proceed"), tr("Cancel"),
                                        this)) {
      return;
    }
  } else {
    if (!BPUpdateConfirmDialog::confirm(tr("Update Repository"), tr("Are you sure you want to update this repository?"), tr("Update"), tr("Cancel"), this)) {
      return;
    }
  }

  // Kill system.updated.updated
  std::system("killall system.updated.updated");

  // Reset local changes
  executeGitCommand("git reset --hard HEAD && git clean -fd", qApp->applicationDirPath(), 30000);

  // Fetch, pull, and update
  showCommandOutputDialog(tr("Update Openpilot"), "rm -f .git/index.lock && git fetch && git pull && git submodule update --init --recursive && scons -j$(nproc)", "", 1800000, true, true,
                          true); // 30 minutes timeout
}

void BPUpdaterPanel::handleRepoUpdateAll() {
  // If there are uncommitted local changes, confirm before proceeding
  if (hasUncommittedChanges()) {
    if (!BPUpdateConfirmDialog::confirm(tr("Update All Submodules"), tr("You have local changes that will be overwritten by this update. Continue?"), tr("Proceed"), tr("Cancel"),
                                        this)) {
      return;
    }
  } else {
    if (!BPUpdateConfirmDialog::confirm(tr("Update All Submodules"), tr("Are you sure you want to update all submodules?"), tr("Update"), tr("Cancel"), this)) {
      return;
    }
  }

  // Kill system.updated.updated
  std::system("killall system.updated.updated");

  // Reset local changes
  executeGitCommand("git reset --hard HEAD && git clean -fd", qApp->applicationDirPath(), 30000);

  // Fetch, pull, and update all submodules
  showCommandOutputDialog(tr("Update All Submodules"), "rm -f .git/index.lock && git fetch && git pull --ff-only && git submodule update --init --recursive && scons -j$(nproc)", "", 180000, true, true,
                          true);
}

void BPUpdaterPanel::handleRepoRepair() {
  if (!BPUpdateConfirmDialog::confirm(tr("Repair Repository"),
                                      tr("Are you sure you want to repair the repository?\n"
                                         "This will completely remove and re-download the repository."),
                                      tr("Repair"), tr("Cancel"), this)) {
    return;
  }

  // Set correct paths
  QString tempScript = "/data/repo_repair.sh";
  QString sourceScript = "/data/openpilot/scripts/git_ui/repo_repair.sh";

  // Verify source script exists
  if (!QFile::exists(sourceScript)) {
    BPUpdateConfirmDialog::alert(tr("Source script not found at: ") + sourceScript, this);
    return;
  }

  // Clean up any existing copy
  if (QFile::exists(tempScript)) {
    if (!QFile::remove(tempScript)) {
      BPUpdateConfirmDialog::alert(tr("Failed to remove existing temp script"), this);
      return;
    }
    std::cout << "tempScript removed at " << tempScript.toStdString() << std::endl;
  }

  // Copy script to /data
  if (!QFile::copy(sourceScript, tempScript)) {
    BPUpdateConfirmDialog::alert(tr("Failed to copy script. Error: ") + QString::number(errno), this);
    return;
  }

  // Make executable
  if (QProcess::execute("chmod", QStringList() << "+x" << tempScript) != 0) {
    BPUpdateConfirmDialog::alert(tr("Failed to make script executable"), this);
    QFile::remove(tempScript);
    return;
  }

  // Run repair script
  showCommandOutputDialog(tr("Repairing Openpilot"), tempScript, "", 1800000, true, true, true); // 30 minutes timeout
}

void BPUpdaterPanel::handleRepoReset() {
  if (BPUpdateConfirmDialog::confirm(tr("Reset Changes"), tr("Are you sure you want to reset all changes? This cannot be undone."), tr("Reset"), tr("Cancel"), this)) {
    showCommandOutputDialog(tr("Reset Changes"), "git reset --hard HEAD && git clean -fd", "", 60000, true, true, true);
  }
}

void BPUpdaterPanel::showLastCommits() {
  QString branch = branchSelector->getValue();
  QString title = branch + " - Last 30 Commits";
  showCommitHistory(this, title, qApp->applicationDirPath() + "/../..");
}

void BPUpdaterPanel::showErrorState(const QString &message) {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(20);
  mainLayout->setContentsMargins(10, 10, 10, 10);

  QLabel *errorLabel = new QLabel(message, this);
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

bool BPUpdaterPanel::isInternetAvailable() const {
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

bool BPUpdaterPanel::isSSHValid() const {
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

bool BPUpdaterPanel::checkRootDiskSpace() {
  std::cout << "checkRootDiskSpace: Starting disk space check" << std::endl;

  QProcess process;
  process.start("df", QStringList() << "-h" << "/");
  process.waitForFinished();
  QString output = QString::fromUtf8(process.readAllStandardOutput());
  std::cout << "checkRootDiskSpace: df output: " << output.toStdString() << std::endl;

  QRegExp rx("\\d+(?=%)");
  if (rx.indexIn(output) != -1) {
    int usage = rx.cap(0).toInt();
    std::cout << "checkRootDiskSpace: Parsed usage: " << usage << "%" << std::endl;

    if (usage >= 95) {
      std::cout << "checkRootDiskSpace: High disk usage detected, prompting for repair" << std::endl;
      if (BPUpdateConfirmDialog::confirm(tr("Disk Space Issue"), tr("Root partition is at %1% capacity. Would you like to attempt to repair?").arg(usage), tr("Repair"),
                                         tr("Cancel"), this)) {
        return repairRootDiskSpace();
      }
      std::cout << "checkRootDiskSpace: User declined repair" << std::endl;
      return false;
    }
  } else {
    std::cout << "checkRootDiskSpace: Failed to parse disk usage" << std::endl;
  }

  std::cout << "checkRootDiskSpace: Check completed successfully" << std::endl;
  return true;
}

bool BPUpdaterPanel::repairRootDiskSpace() {
  std::cout << "repairRootDiskSpace: Starting disk repair" << std::endl;
  QProcess process;

  // Remount as writable
  std::cout << "repairRootDiskSpace: Attempting to remount as writable" << std::endl;
  process.start("sudo", QStringList() << "mount" << "-o" << "remount,rw" << "/");
  process.waitForFinished();
  std::cout << "repairRootDiskSpace: Remount exit code: " << process.exitCode() << std::endl;
  if (process.exitCode() != 0) {
    std::cout << "repairRootDiskSpace: Remount error: " << QString::fromUtf8(process.readAllStandardError()).toStdString() << std::endl;
  }

  // Get device path
  std::cout << "repairRootDiskSpace: Getting device path" << std::endl;
  process.start("findmnt", QStringList() << "-n" << "-o" << "SOURCE" << "/");
  process.waitForFinished();
  QString device = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  std::cout << "repairRootDiskSpace: Found device: " << device.toStdString() << std::endl;

  // Resize filesystem
  std::cout << "repairRootDiskSpace: Attempting resize2fs on " << device.toStdString() << std::endl;
  process.start("sudo", QStringList() << "resize2fs" << device);
  process.waitForFinished();
  std::cout << "repairRootDiskSpace: resize2fs exit code: " << process.exitCode() << std::endl;
  if (process.exitCode() != 0) {
    std::cout << "repairRootDiskSpace: resize2fs error: " << QString::fromUtf8(process.readAllStandardError()).toStdString() << std::endl;
  }

  // Verify repair
  std::cout << "repairRootDiskSpace: Verifying repair results" << std::endl;
  process.start("df", QStringList() << "-h" << "/");
  process.waitForFinished();
  QString output = QString::fromUtf8(process.readAllStandardOutput());
  std::cout << "repairRootDiskSpace: Post-repair df output: " << output.toStdString() << std::endl;

  QRegExp rx("\\d+(?=%)");
  if (rx.indexIn(output) != -1) {
    int usage = rx.cap(0).toInt();
    std::cout << "repairRootDiskSpace: Post-repair usage: " << usage << "%" << std::endl;
    if (usage >= 95) {
      std::cout << "repairRootDiskSpace: Repair unsuccessful, usage still high" << std::endl;
      BPUpdateConfirmDialog::alert(tr("Failed to free up space. Please manually clean up the root partition."), this);
      return false;
    }
  } else {
    std::cout << "repairRootDiskSpace: Failed to parse post-repair usage" << std::endl;
  }

  std::cout << "repairRootDiskSpace: Repair completed successfully" << std::endl;
  return true;
}

bool BPUpdaterPanel::checkAndRestoreSSH() {
  std::cout << "checkAndRestoreSSH: Starting SSH check and restore process" << std::endl;

  // Move disk space check to a concurrent operation
  QtConcurrent::run([this]() {
    if (!checkRootDiskSpace()) {
      std::cout << "checkAndRestoreSSH: Disk space check failed" << std::endl;
      return;
    }

    // Check SSH on UI thread
    QMetaObject::invokeMethod(
        this,
        [this]() {
          bool sshExists = QFile::exists("/home/comma/.ssh/github");
          std::cout << "checkAndRestoreSSH: SSH key exists: " << (sshExists ? "yes" : "no") << std::endl;

          if (!sshExists || !isSSHValid()) {
            std::cout << "checkAndRestoreSSH: SSH invalid or missing, checking for backup" << std::endl;
            bool backupExists = QFile::exists("/data/ssh_backup/github");
            std::cout << "checkAndRestoreSSH: SSH backup exists: " << (backupExists ? "yes" : "no") << std::endl;

            if (backupExists) {
              // Show confirmation dialog
              QMetaObject::invokeMethod(
                  this,
                  [this]() {
                    if (BPUpdateConfirmDialog::confirm(tr("SSH Configuration"), tr("SSH configuration is missing or invalid. Would you like to restore from backup?"),
                                                       tr("Restore"), tr("Cancel"), this)) {
                      restoreSSHFromUtility();
                    }
                  },
                  Qt::QueuedConnection);
            }
          }
        },
        Qt::QueuedConnection);
  });

  return true;
}

bool BPUpdaterPanel::restoreSSHFromUtility() {
  std::cout << "restoreSSHFromUtility: Starting SSH restore process" << std::endl;

  // First restore to /home/comma/.ssh
  QString restoreCommand = QString("sudo bash -c '"
                                   "mkdir -p /home/comma/.ssh && "
                                   "cp /data/commautil/backups/ssh/.ssh/github /home/comma/.ssh/github && "
                                   "cp /data/commautil/backups/ssh/.ssh/github.pub /home/comma/.ssh/github.pub && "
                                   "cp /data/commautil/backups/ssh/.ssh/config /home/comma/.ssh/config && "
                                   "chown -R comma:comma /home/comma/.ssh && "
                                   "chmod 600 /home/comma/.ssh/github && "
                                   "chmod 644 /home/comma/.ssh/github.pub && "
                                   "chmod 644 /home/comma/.ssh/config && "

                                   "mount -o remount,rw /persist && "
                                   "cp /data/commautil/backups/ssh/persist_comma/* /persist/comma/ && "

                                   "mkdir -p /usr/default/home/comma/.ssh/ && "
                                   "cp /home/comma/.ssh/config /usr/default/home/comma/.ssh/ && "
                                   "cp /home/comma/.ssh/github* /usr/default/home/comma/.ssh/ && "
                                   "chown -R comma:comma /usr/default/home/comma/.ssh/ && "
                                   "chmod 600 /usr/default/home/comma/.ssh/github && "
                                   "mount -o remount,ro / && mount -o remount,ro /persist"
                                   "'");

  // Use the existing command output dialog pattern
  showCommandOutputDialog(tr("Restoring SSH Configuration"), restoreCommand,
                          "",    // Use default working directory
                          30000, // 30 second timeout
                          true,  // Show kill button
                          true,  // Show retry button
                          false  // Don't show reboot button
  );

  return true;
}

void BPUpdaterPanel::updateStatusLabel(UpdaterStatus status) const {
  auto it = std::find_if(STATUS_MESSAGES.begin(), STATUS_MESSAGES.end(), [status](const auto &tuple) { return std::get<0>(tuple) == status; });

  if (it != STATUS_MESSAGES.end()) {
    QString statusText = std::get<1>(*it);
    updaterPanelStatusLabel->setText(statusText);
    updaterPanelStatusLabel->setVisible(status != UpdaterStatus::OK);
  }
}

void BPUpdaterPanel::updateButtonStates() {
  bool internetAvailable = isInternetAvailable();
  bool sshValid = isSSHValid();
  bool hasRemoteBranch = true;
  bool onroad = isOnroad();

  // Check for remote branch
  QProcess process;
  process.setWorkingDirectory(qApp->applicationDirPath() + "/../..");
  process.start("git", QStringList() << "rev-parse" << "--abbrev-ref" << "--symbolic-full-name" << "@{u}");
  if (!process.waitForFinished(5000) || process.exitCode() != 0) {
    hasRemoteBranch = false;
  }

  std::cout << "Internet available: " << internetAvailable << std::endl;
  std::cout << "SSH valid: " << sshValid << std::endl;
  std::cout << "Has remote branch: " << hasRemoteBranch << std::endl;
  std::cout << "Onroad: " << onroad << std::endl;

  // Update status label based on conditions (in order of priority)
  if (onroad) {
    updateStatusLabel(UpdaterStatus::ONROAD);
  } else if (!internetAvailable) {
    updateStatusLabel(UpdaterStatus::NO_INTERNET);
  } else if (!sshValid) {
    updateStatusLabel(UpdaterStatus::SSH_AUTH_FAILED);
  } else if (!hasRemoteBranch) {
    updateStatusLabel(UpdaterStatus::NO_REMOTE_BRANCH);
  } else {
    updateStatusLabel(UpdaterStatus::OK);
  }

  // Update button states
  bool canCheckUpdates = internetAvailable && sshValid && hasRemoteBranch && !onroad;
  checkUpdatesButton->setEnabled(canCheckUpdates);
  updateRepoButton->setEnabled(canCheckUpdates);
  updateAllButton->setEnabled(canCheckUpdates);
  repairRepoButton->setEnabled(internetAvailable && sshValid && !onroad);

  // Update check button status text
  if (onroad) {
    updateChkBtnLabelTxt->setText(tr("Updates Disabled"));
    updateChkBtnTimeTxt->setText(tr("(Vehicle in Motion)"));
  } else if (!internetAvailable) {
    updateChkBtnLabelTxt->setText(tr("Updates Disabled"));
    updateChkBtnTimeTxt->setText(tr("(No Internet)"));
  } else if (!sshValid) {
    updateChkBtnLabelTxt->setText(tr("Updates Disabled"));
    updateChkBtnTimeTxt->setText(tr("(SSH Error)"));
  } else if (!hasRemoteBranch) {
    updateChkBtnLabelTxt->setText(tr("Updates Disabled"));
    updateChkBtnTimeTxt->setText(tr("(No Remote Branch)"));
  } else {
    updateChkBtnLabelTxt->setText(tr("Check Updates"));
    // Keep the existing time if we have it
    if (!lastUpdateCheck.isValid()) {
      updateChkBtnTimeTxt->setText("");
    } else {
      updateChkBtnTimeTxt->setText(lastUpdateCheck.toString("MM/dd hh:mm AP"));
    }
  }

  // Update button styling
  if (canCheckUpdates) {
    updateChkBtnLabelTxt->setStyleSheet("color: white; font-size: 35px; background: transparent;");
    updateChkBtnTimeTxt->setStyleSheet("color: white; font-size: 25px; background: transparent; opacity: 0.8;");
  } else {
    updateChkBtnLabelTxt->setStyleSheet("color: #888888; font-size: 35px; background: transparent;");
    updateChkBtnTimeTxt->setStyleSheet("color: #888888; font-size: 25px; background: transparent; opacity: 0.8;");
  }

  // Update submodule buttons
  for (auto *widget : submoduleWidgets) {
    widget->updateModuleButton->setEnabled(canCheckUpdates);
    widget->repairModuleButton->setEnabled(internetAvailable && sshValid);
  }

  // Keep status label visible when needed
  // if (updaterPanelStatusLabel) {
  //     updaterPanelStatusLabel->setVisible(onroad || !internetAvailable || !sshValid || !hasRemoteBranch);
  // }
}

void BPUpdaterPanel::setupParamMonitoring() {
  QTimer *paramCheckTimer = new QTimer(static_cast<QObject *>(this));
  paramCheckTimer->setInterval(1000); // Check every second
  connect(paramCheckTimer, &QTimer::timeout, this, [this]() {
    static bool lastOnroadState = false;
    bool currentOnroadState = isOnroad();

    if (lastOnroadState != currentOnroadState) {
      updateButtonStates();
      if (currentOnroadState) {
        stopAutoUpdateChecks();
      } else if (isVisible()) {
        startAutoUpdateChecks();
      }
      lastOnroadState = currentOnroadState;
    }
  });
  paramCheckTimer->start();
}

BPUpdaterSelectionDialog::BPUpdaterSelectionDialog(QWidget *parent) : QDialog(parent) {}

void BPUpdaterSelectionDialog::setupDialog(const QString &title, const QStringList &options, const QString &current) {
  // Set window flags and attributes first
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setFixedSize(2160, 1080);

  // Create main layout with semi-transparent background
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);
  setStyleSheet("background-color: rgba(0, 0, 0, 0.75);");

  // Create container widget (75% width)
  QWidget *container = new QWidget(this);
  container->setFixedWidth(1620);
  container->setStyleSheet(R"(
    QWidget {
      background-color: #242424;
      border-radius: 20px;
      padding: 5px;
    }
  )");

  QVBoxLayout *containerLayout = new QVBoxLayout(container);
  containerLayout->setSpacing(30);
  containerLayout->setContentsMargins(40, 40, 40, 40);

  QLabel *titleLabel = new QLabel(title, container);
  titleLabel->setStyleSheet("font-size: 48px; font-weight: 600; color: white;");
  titleLabel->setAlignment(Qt::AlignCenter);
  containerLayout->addWidget(titleLabel);

  QScrollArea *scrollArea = new BPScrollArea(container);
  QWidget *buttonWidget = new QWidget(scrollArea);
  QVBoxLayout *buttonLayout = new QVBoxLayout(buttonWidget);
  buttonLayout->setSpacing(10);

  currentValue = current;
  QString buttonStyle = R"(
    QPushButton {
      background-color: #363636;
      color: white;
      border: none;
      border-radius: 15px;
      padding: 20px;
      font-size: 40px;
      text-align: left;
      margin: 5px;
    }
    QPushButton:checked {
      background-color: #2196F3;
    }
    QPushButton:pressed {
      background-color: #1976D2;
    }
  )";

  QButtonGroup *buttonGroup = new QButtonGroup(this);
  for (const QString &option : options) {
    QPushButton *btn = new QPushButton(option, buttonWidget);
    btn->setCheckable(true);
    btn->setChecked(option == current);
    btn->setStyleSheet(buttonStyle);
    buttonGroup->addButton(btn);
    connect(btn, &QPushButton::clicked, [this, btn]() {
      selected = btn->text();
      selectButton->setEnabled(selected != currentValue);
    });
    buttonLayout->addWidget(btn);
  }

  scrollArea->setWidget(buttonWidget);
  containerLayout->addWidget(scrollArea, 1);

  // Add button container for Cancel/Select
  QHBoxLayout *actionButtonLayout = new QHBoxLayout();
  actionButtonLayout->setSpacing(20);

  QString actionButtonStyle = R"(
    QPushButton {
      background-color: %1;
      color: white;
      border: none;
      border-radius: 15px;
      padding: 20px 40px;
      font-size: 40px;
      font-weight: 500;
      min-width: 250px;
    }
    QPushButton:pressed {
      background-color: %2;
    }
    QPushButton:disabled {
      background-color: #404040;
      color: #888888;
    }
  )";

  QPushButton *cancelBtn = new QPushButton(tr("Cancel"), container);
  cancelBtn->setStyleSheet(actionButtonStyle.arg("#404040", "#505050"));
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  selectButton = new QPushButton(tr("Select"), container);
  selectButton->setStyleSheet(actionButtonStyle.arg("#2196F3", "#1976D2"));
  selectButton->setEnabled(false);
  connect(selectButton, &QPushButton::clicked, this, &QDialog::accept);

  actionButtonLayout->addWidget(cancelBtn);
  actionButtonLayout->addWidget(selectButton);
  containerLayout->addLayout(actionButtonLayout);

  // Center container in dialog
  mainLayout->addStretch();
  QHBoxLayout *centerLayout = new QHBoxLayout();
  centerLayout->addStretch();
  centerLayout->addWidget(container);
  centerLayout->addStretch();
  mainLayout->addLayout(centerLayout);
  mainLayout->addStretch();

  // Ensure layout is created before showing
  show();

  // Apply Wayland transform
#ifdef QCOM2
  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  if (native && windowHandle()) {
    wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", windowHandle()));
    if (s) {
      wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
      wl_surface_commit(s);
    }
  }
  setWindowState(Qt::WindowFullScreen);
#endif
}

QString BPUpdaterSelectionDialog::getSelection(const QString &title, const QStringList &options, const QString &current, QWidget *parent) {
  BPUpdaterSelectionDialog *dialog = new BPUpdaterSelectionDialog(parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setupDialog(title, options, current);

  if (dialog->exec() == QDialog::Accepted) {
    QString selected = dialog->selected;
    return selected;
  }
  return QString();
}

// In bp_updater_panel.cc
BPUpdateConfirmDialog::BPUpdateConfirmDialog(const QString &title, const QString &prompt, const QString &confirm_text, const QString &cancel_text, QWidget *parent)
    : BPDialogBase(parent) {
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setModal(true);

  // Semi-transparent background
  setStyleSheet("background-color: rgba(0, 0, 0, 0.75);");

  // Create main layout
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(45, 35, 45, 45);
  mainLayout->setSpacing(40);

  // Create container widget
  QWidget *container = new QWidget(this);
  container->setFixedWidth(1400);
  container->setStyleSheet("QWidget { background-color: #242424; border-radius: 20px; }");

  QVBoxLayout *containerLayout = new QVBoxLayout(container);
  containerLayout->setSpacing(40);
  containerLayout->setContentsMargins(60, 60, 60, 60);

  // Title
  QLabel *titleLabel = new QLabel(title, this);
  titleLabel->setStyleSheet("font-size: 60px; font-weight: 600; color: white;");
  titleLabel->setAlignment(Qt::AlignCenter);
  containerLayout->addWidget(titleLabel);

  // Prompt
  QLabel *promptLabel = new QLabel(prompt, this);
  promptLabel->setStyleSheet("font-size: 50px; color: #DDDDDD; padding: 20px;");
  promptLabel->setWordWrap(true);
  promptLabel->setAlignment(Qt::AlignCenter);
  containerLayout->addWidget(promptLabel);

  // Buttons
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(20);

  QString buttonStyle = QString(R"(
    QPushButton {
      background-color: %1;
      color: white;
      border: none;
      border-radius: 15px;
      padding: 30px 60px;
      font-size: 50px;
      font-weight: 500;
      min-width: 300px;
    }
    QPushButton:pressed {
      background-color: %2;
    }
  )");

  if (!cancel_text.isEmpty()) {
    QPushButton *noButton = new QPushButton(cancel_text, this);
    noButton->setStyleSheet(buttonStyle.arg("#404040", "#505050"));
    connect(noButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(noButton);
  }

  QPushButton *yesButton = new QPushButton(confirm_text, this);
  yesButton->setStyleSheet(buttonStyle.arg("#2196F3", "#1976D2"));
  connect(yesButton, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(yesButton);

  containerLayout->addLayout(buttonLayout);

  // Center container in dialog
  mainLayout->addStretch();
  QHBoxLayout *centerLayout = new QHBoxLayout();
  centerLayout->addStretch();
  centerLayout->addWidget(container);
  centerLayout->addStretch();
  mainLayout->addLayout(centerLayout);
  mainLayout->addStretch();

  setupFullscreen();
}

bool BPUpdateConfirmDialog::confirm(const QString &title_text, const QString &prompt_text, const QString &confirm_text, const QString &cancel_text, QWidget *parent) {
  BPUpdateConfirmDialog dlg(title_text.isEmpty() ? tr("Confirmation Required") : title_text, prompt_text, confirm_text, cancel_text.isEmpty() ? tr("Cancel") : cancel_text, parent);
  return dlg.exec() == QDialog::Accepted;
}

void BPUpdateConfirmDialog::alert(const QString &message, QWidget *parent) {
  BPUpdateConfirmDialog dlg(tr("Alert"), message, tr("OK"), "", parent);
  dlg.exec();
}
