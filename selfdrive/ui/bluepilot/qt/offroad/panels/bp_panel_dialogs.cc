// selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_dialogs.cc

#include "bp_panel_dialogs.h"
#include "selfdrive/ui/bluepilot/bp_logging.h"

BPSelectionDialog::BPSelectionDialog(const QString &title, const QVector<Option> &options, const QString &currentValue, QWidget *parent)
    : BPDialogBase(parent), currentValue(currentValue) {
  // Constructor implementation remains the same but uses options instead of items
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setModal(true);
  // setFixedSize(2160, 1080);

  buttonGroup = new QButtonGroup(this);
  buttonGroup->setExclusive(true);

  // Semi-transparent background
  setStyleSheet("background-color: rgba(0, 0, 0, 0.75);");

  // Create main layout
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(45, 35, 45, 45);
  mainLayout->setSpacing(40);

  // Create container widget
  QWidget *container = new QWidget(this);
  container->setFixedWidth(1800);
  container->setStyleSheet("QWidget { background-color: #242424; border-radius: 20px; padding: 5px; }");

  QVBoxLayout *containerLayout = new QVBoxLayout(container);
  containerLayout->setSpacing(30);
  containerLayout->setContentsMargins(40, 40, 40, 40);

  // Add title
  QLabel *titleLabel = new QLabel(title, this);
  titleLabel->setStyleSheet("font-size: 60px; font-weight: 600; color: white;");
  titleLabel->setAlignment(Qt::AlignCenter);
  containerLayout->addWidget(titleLabel);

  // Create scrollable area
  QScrollArea *scrollArea = new QScrollArea(this);
  scrollArea->setStyleSheet(R"(
    QScrollArea {
      border: none;
      background-color: transparent;
    }
    QScrollBar:vertical {
      width: 24px;
      margin: 0px;
      padding: 2px;
      background: transparent;
    }
    QScrollBar::handle:vertical {
      background: #666666;
      min-height: 100px;
      border-radius: 12px;
      margin: 0 4px;
    }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
      height: 0px;
    }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
      background: none;
    }
  )");
  scrollArea->setWidgetResizable(true);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  // Enable touch scrolling
  QScroller::grabGesture(scrollArea->viewport(), QScroller::TouchGesture);

  // Create container for buttons
  QWidget *buttonContainer = new QWidget(scrollArea);
  QVBoxLayout *buttonLayout = new QVBoxLayout(buttonContainer);
  buttonLayout->setSpacing(20);
  buttonLayout->setContentsMargins(30, 30, 30, 30);

  // Add option buttons
  for (const auto &opt : options) {
    QPushButton *button = new QPushButton(opt.display, buttonContainer);
    button->setCheckable(true);
    button->setChecked(opt.value == currentValue);
    button->setStyleSheet(QString(R"(
      QPushButton {
        background-color: #363636;
        color: white;
        border: none;
        border-radius: 15px;
        padding: 30px;
        font-size: 50px;
        text-align: left;
        margin: 8px;
      }
      QPushButton:pressed {
        background-color: #1976D2;
      }
      QPushButton:checked {
        background-color: #2196F3;
      }
    )"));

    buttonGroup->addButton(button);
    connect(button, &QPushButton::clicked, [this, opt]() {
      selected = opt.value;
      if (selectButton) {
        selectButton->setEnabled(true); // Always enable select button when an option is chosen
      }
    });
    buttonLayout->addWidget(button);
  }

  scrollArea->setWidget(buttonContainer);
  containerLayout->addWidget(scrollArea);

  // Add action buttons
  QHBoxLayout *actionButtonLayout = new QHBoxLayout();
  actionButtonLayout->setSpacing(20);

  QString actionButtonStyle = R"(
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
    QPushButton:disabled {
      background-color: #404040;
      color: #888888;
    }
  )";

  // Cancel button
  QPushButton *cancelBtn = new QPushButton(tr("Cancel"), container);
  cancelBtn->setStyleSheet(actionButtonStyle.arg("#404040", "#505050"));
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  // Select button
  selectButton = new QPushButton(tr("Select"), container);
  selectButton->setStyleSheet(actionButtonStyle.arg("#2196F3", "#1976D2"));
  // Initialize selected value to current value and enable select button if there's a current selection
  selected = currentValue;
  selectButton->setEnabled(!currentValue.isEmpty());
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
}

QString BPSelectionDialog::getValue(const QString &title, const QVector<Option> &options, const QString &currentValue, QWidget *parent) {
  BPSelectionDialog *dialog = new BPSelectionDialog(title, options, currentValue, parent);
  dialog->setupFullscreen();
  int ret = dialog->exec();
  QString result = (ret == QDialog::Accepted) ? dialog->selected : QString();
  dialog->deleteLater();
  return result;
}

QString BPSelectionDialog::getSelection(const QString &title, const QStringList &items, const QString &current, QWidget *parent) {
  QVector<Option> options;
  for (const QString &item : items) {
    options.append({item, item}); // For simple selection, display and value are the same
  }
  return getValue(title, options, current, parent);
}

// =====================================
// BPConfirmationDialog
// =====================================

BPConfirmationDialog::BPConfirmationDialog(const ConfirmConfig &config, QWidget *parent) : BPDialogBase(parent) {
  // Set up a semi-transparent background overlay
  setStyleSheet("BPConfirmationDialog { background-color: rgba(0, 0, 0, 0.75); }");

  // Create main layout
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Create centered container for content - SCALED UP 25%
  QWidget *container = new QWidget(this);
  container->setFixedWidth(1750);                                                          // Was 1400
  container->setStyleSheet("QWidget { background-color: #242424; border-radius: 25px; }"); // Was 20px

  QVBoxLayout *containerLayout = new QVBoxLayout(container);
  containerLayout->setSpacing(50);                     // Was 40
  containerLayout->setContentsMargins(75, 75, 75, 75); // Was 60, 60, 60, 60

  // Title - SCALED UP 25%
  QLabel *titleLabel = new QLabel(config.title, container);
  titleLabel->setAlignment(Qt::AlignCenter);
  titleLabel->setStyleSheet("QLabel { font-size: 75px; font-weight: 600; color: white; }"); // Was 60px
  containerLayout->addWidget(titleLabel);

  // Prompt - SCALED UP 25%
  QLabel *promptLabel = new QLabel(config.prompt, container);
  promptLabel->setAlignment(Qt::AlignCenter);
  promptLabel->setWordWrap(true);
  promptLabel->setStyleSheet("QLabel { font-size: 63px; color: #DDDDDD; padding: 25px; }"); // Was 50px, 20px
  containerLayout->addWidget(promptLabel);

  // Buttons - SCALED UP 25%
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(25); // Was 20

  QString actionButtonStyle = QString(R"(
        QPushButton {
            background-color: %1;
            color: white;
            border: none;
            border-radius: 19px; /* Was 15px */
            padding: 38px 75px; /* Was 30px 60px */
            font-size: 63px; /* Was 50px */
            font-weight: 500;
            min-width: 375px; /* Was 300px */
        }
        QPushButton:pressed {
            background-color: %2;
        }
  )");

  // Button creation logic unchanged - styles defined above are already scaled
  if (config.cancelText.isEmpty()) {
    // Single button mode
    yesButton = new QPushButton(config.confirmText, container);
    yesButton->setStyleSheet(actionButtonStyle.arg(config.confirmColor, "#1976D2"));
    connect(yesButton, &QPushButton::clicked, [this]() {
      emit confirmed(true);
      accept();
    });

    buttonLayout->addStretch();
    buttonLayout->addWidget(yesButton);
    buttonLayout->addStretch();
  } else {
    // Two button mode
    noButton = new QPushButton(config.cancelText, container);
    noButton->setStyleSheet(actionButtonStyle.arg(config.cancelColor, "#505050"));
    connect(noButton, &QPushButton::clicked, [this]() {
      emit confirmed(false);
      reject();
    });

    yesButton = new QPushButton(config.confirmText, container);
    yesButton->setStyleSheet(actionButtonStyle.arg(config.confirmColor, "#1976D2"));
    connect(yesButton, &QPushButton::clicked, [this]() {
      emit confirmed(true);
      accept();
    });

    buttonLayout->addWidget(noButton);
    buttonLayout->addWidget(yesButton);
  }

  containerLayout->addLayout(buttonLayout);

  // Center the container in the main layout
  mainLayout->addStretch();
  QHBoxLayout *centerLayout = new QHBoxLayout();
  centerLayout->addStretch();
  centerLayout->addWidget(container);
  centerLayout->addStretch();
  mainLayout->addLayout(centerLayout);
  mainLayout->addStretch();

  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
}

BPConfirmationDialog *BPConfirmationDialog::showConfirmation(const ConfirmConfig &config, QWidget *parent) {
  auto *dialog = new BPConfirmationDialog(config, parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setupFullscreen();
  return dialog;
}

BPConfirmationDialog *BPConfirmationDialog::showMessage(const ConfirmConfig &config, QWidget *parent) {
  ConfirmConfig messageConfig = config;                                                         // Create a mutable copy
  messageConfig.cancelText = QString();                                                         // Set to empty string for single-button mode
  messageConfig.confirmColor = config.confirmColor.isEmpty() ? "#2196F3" : config.confirmColor; // Set to Material Blue 500
  return showConfirmation(messageConfig, parent);
}

// ===========================
// BPCommandDialog
// ===========================
BPCommandDialog::BPCommandDialog(QWidget *parent) : BPDialogBase(parent), process(nullptr) {
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

  // Layout for the entire dialog
  main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(30, 20, 30, 20);
  main_layout->setSpacing(0);

  setupDialogStyle();
}

void BPCommandDialog::setupDialogStyle() {
  // Apply a full-screen black style, or whatever you prefer
  setStyleSheet(R"(
    BPCommandDialog, QDialog {
      background-color: #000000;
    }
    QWidget {
      background-color: #000000;
      color: white;
    }
    QLabel {
      background-color: #000000;
      color: #FFFFFF;
    }
    QScrollArea {
      background-color: #000000;
    }
    QScrollArea > QWidget > QWidget {
      background-color: #000000;
    }
  )");
}

// 1) Sets up the main BP UI (title label, text area, kill/close buttons)
void BPCommandDialog::setupCommandUI(const QString &title) {
  // Title
  title_label = new QLabel(title + " - " + tr("Output"), this);
  title_label->setStyleSheet("font-size: 60px; font-weight: 600; background-color: #000;");
  main_layout->addWidget(title_label);
  main_layout->addSpacing(30);

  // Output Text
  outputText = new QTextEdit(this);
  outputText->setReadOnly(true);
  outputText->setTextInteractionFlags(Qt::NoTextInteraction);
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
    }
    QTextEdit QScrollBar:vertical:hover {
      background: #2B2B2B;
    }
    QTextEdit QScrollBar:vertical:pressed {
      background: #3B3B3B;
    }
  )");

  // Add touch scrolling capabilities
  QScroller::grabGesture(outputText->viewport(), QScroller::LeftMouseButtonGesture);

  main_layout->addWidget(outputText);

  // Button row
  buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(20);

  // "Stop Command" button (kill)
  killButton = new BPButton(tr("Stop Command"), this);
  killButton->setMinimumHeight(100);
  killButton->setStyleSheet(R"(
    BPButton {
      background-color: #EA4646;
      border-radius: 20px;
      font-size: 45px;
      font-weight: 500;
      color: white;
    }
    BPButton:hover {
      background-color: #F15959;
    }
    BPButton:pressed {
      background-color: #F43030;
    }
  )");
  buttonLayout->addWidget(killButton);

  // "Retry" button (initially hidden, shown on failure/timeout)
  retryButton = new BPButton(tr("Retry"), this);
  retryButton->setMinimumHeight(100);
  retryButton->setVisible(false);
  retryButton->setStyleSheet(R"(
    BPButton {
      background-color: #7B1FA2;
      border-radius: 20px;
      font-size: 45px;
      font-weight: 500;
      color: white;
    }
    BPButton:hover {
      background-color: #8E24AA;
    }
    BPButton:pressed {
      background-color: #6A1B9A;
    }
    BPButton:disabled {
      background-color: #4F4F4F;
      color: #888888;
    }
  )");
  buttonLayout->addWidget(retryButton);

  // "Close" button (initially disabled until the process finishes)
  closeButton = new BPButton(tr("Command is Running..."), this);
  closeButton->setEnabled(false);
  closeButton->setMinimumHeight(100);
  closeButton->setStyleSheet(R"(
    BPButton {
      background-color: #4F4F4F;
      border-radius: 20px;
      font-size: 45px;
      font-weight: 500;
      color: #888888;
    }
    BPButton:hover {
      background-color: #5F5F5F;
    }
    BPButton:pressed {
      background-color: #6F6F6F;
    }
    BPButton:disabled {
      background-color: #4F4F4F;
      color: #888888;
    }
  )");
  buttonLayout->addWidget(closeButton);

  main_layout->addSpacing(50);
  main_layout->addLayout(buttonLayout);

  // Connect signals
  connect(killButton, &QPushButton::clicked, this, &BPCommandDialog::killProcess);
  connect(retryButton, &QPushButton::clicked, this, &BPCommandDialog::retryCommand);
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

// 2) Store parameters and start the command
void BPCommandDialog::executeCommand(const QString &command, const QString &title, const QString &workingDir, const QJsonArray &actionButtons, int timeoutMs, bool showRetry) {
  // Store parameters for potential retry
  storedCommand = command;
  storedTitle = title;
  storedWorkingDir = workingDir;
  storedActionButtons = actionButtons;
  storedTimeoutMs = timeoutMs;
  storedShowRetry = showRetry;

  // Build out the UI
  setupCommandUI(title);
  setupActionButtons(actionButtons);

  // Setup the QProcess
  process = new QProcess(this);
  if (!workingDir.isEmpty()) {
    QDir dir(workingDir);
    if (dir.exists()) {
      process->setWorkingDirectory(workingDir);
    } else {
      BPLog::bpWarn() << "[bp.command.dialog] Warning: Working directory does not exist: " << workingDir.toStdString() << std::endl;
    }
  }

  // Connect signals
  connect(process, &QProcess::readyReadStandardOutput, this, &BPCommandDialog::handleProcessOutput);
  connect(process, &QProcess::readyReadStandardError, this, &BPCommandDialog::handleProcessError);
  connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &BPCommandDialog::handleProcessFinished);
  connect(this, &QDialog::finished, process, &QProcess::deleteLater);

  // Setup timeout if specified
  timeoutTimer = nullptr;
  if (storedTimeoutMs > 0) {
    timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, [this]() {
      if (process && process->state() == QProcess::Running) {
        // Add brief message to output
        outputText->append(QString("\n<span style='color: #ff7c30;'>⏱ Command timeout reached (%1s)</span>").arg(storedTimeoutMs / 1000));

        // Kill the process
        process->kill();

        // Update close button to show timeout error
        closeButton->setEnabled(true);
        closeButton->setText(tr("⏱ TIMEOUT: Command exceeded %1 seconds - Click to Close").arg(storedTimeoutMs / 1000));
        closeButton->setStyleSheet(R"(
          BPButton {
            background-color: #EA4646;
            border-radius: 20px;
            font-size: 40px;
            font-weight: 500;
            color: white;
          }
          BPButton:hover {
            background-color: #F15959;
          }
          BPButton:pressed {
            background-color: #F43030;
          }
        )");

        // Show retry button
        retryButton->setVisible(true);
        retryButton->setEnabled(true);

        // Hide kill button since process is dead
        killButton->setVisible(false);

        BPLog::bpWarn() << "[bp.command.dialog] Command timed out after " << storedTimeoutMs / 1000 << " seconds" << std::endl;
      }
    });
    BPLog::bpDebugGeneral() << "[bp.command.dialog] Timeout set to " << storedTimeoutMs / 1000 << " seconds" << std::endl;
  }

  setupFullscreen();

  // Start the process
  startCommand();
}

// 2b) Start or restart the command
void BPCommandDialog::startCommand() {
  // Reset UI state
  retryButton->setVisible(false);
  killButton->setVisible(true);
  closeButton->setEnabled(false);
  closeButton->setText(tr("Command is Running..."));
  closeButton->setStyleSheet(R"(
    BPButton {
      background-color: #4F4F4F;
      border-radius: 20px;
      font-size: 45px;
      font-weight: 500;
      color: #888888;
    }
    BPButton:hover {
      background-color: #5F5F5F;
    }
    BPButton:pressed {
      background-color: #6F6F6F;
    }
    BPButton:disabled {
      background-color: #4F4F4F;
      color: #888888;
    }
  )");

  // Restart timeout timer if it exists
  if (timeoutTimer && storedTimeoutMs > 0) {
    // Explicitly stop timer first to ensure clean restart
    timeoutTimer->stop();
    timeoutTimer->start(storedTimeoutMs);
    BPLog::bpDebugGeneral() << "[bp.command.dialog] Restarted timeout timer: " << storedTimeoutMs / 1000 << " seconds" << std::endl;
  }

  // Ensure process is not already running before starting
  if (process && process->state() != QProcess::NotRunning) {
    BPLog::bpWarn() << "[bp.command.dialog] Process still running, terminating before restart" << std::endl;
    process->kill();
    process->waitForFinished(1000);
  }

  // Start the process
  BPLog::bpDebugGeneral() << "[bp.command.dialog] executing command: " << storedCommand.toStdString() << std::endl;
  process->start("/bin/bash", QStringList() << "-c" << storedCommand);
}

// 3) Creates action buttons but doesn't add them to layout yet
// They will be shown based on their showWhen condition (success/failure/always)
void BPCommandDialog::setupActionButtons(const QJsonArray &actionButtons) {
  // Clear any previous action buttons
  actionButtonWidgets.clear();

  int maxActionButtons = 3;
  int addedButtons = 0;

  for (const auto &btnVal : actionButtons) {
    if (addedButtons >= maxActionButtons) {
      BPLog::bpInfo() << "[bp.command.dialog] Exceeded maximum of 3 action buttons. Skipping extras." << std::endl;
      break;
    }
    QJsonObject buttonObj = btnVal.toObject();
    QString showWhen = buttonObj["showWhen"].toString("always"); // Default to "always" if not specified
    QPushButton *actionBtn = createActionButton(buttonObj);
    if (actionBtn) {
      // Store the button with its showWhen condition
      // It will be shown in handleProcessFinished() based on the condition
      ActionButtonInfo info;
      info.button = actionBtn;
      info.showWhen = showWhen.toLower(); // Normalize to lowercase
      actionButtonWidgets.append(info);
      addedButtons++;
    }
  }
}
QPushButton *BPCommandDialog::createActionButton(const QJsonObject &buttonObj) {
  QString buttonText = buttonObj["text"].toString();
  QString buttonCommand = buttonObj["command"].toString();
  QString buttonAction = buttonObj["action"].toString();
  bool buttonConfirm = buttonObj["confirm"].toBool();
  QString buttonConfirmText = buttonObj["confirm_text"].toString();
  QString buttonConfirmYesText = buttonObj["confirm_yes_text"].toString();
  QString buttonConfirmNoText = buttonObj["confirm_no_text"].toString();

  // Use BPButton for consistent UI feel
  BPButton *actionBtn = new BPButton(buttonText, this);

  // Make the buttons smaller, with some "Material-like" colors and rounding:
  actionBtn->setMinimumHeight(100); // a bit smaller than the previous 120
  actionBtn->setMaximumWidth(300);  // optional width constraint, tweak as desired

  // Example "Material" color scheme. Adjust to your liking:
  actionBtn->setStyleSheet(R"(
    BPButton {
      background-color: #2196F3;       /* Material Blue 500 */
      border-radius: 10px;
      font-size: 45px;
      font-weight: 500;
      color: #FFFFFF;
    }
    BPButton:hover {
      background-color: #1E88E5;       /* Material Blue 600 */
    }
    BPButton:pressed {
      background-color: #1976D2;       /* Material Blue 700 */
    }
  )");

  // On click, optionally confirm, then run the command or action
  connect(actionBtn, &QPushButton::clicked, [=]() {
    auto executeAction = [=]() {
      if (!buttonAction.isEmpty()) {
        // Handle built-in actions
        if (buttonAction == "reboot") {
          Params params;
          params.putBool("DoReboot", true);
        } else {
          BPLog::bpWarn() << "[bp.command.dialog] Unknown action: " << buttonAction.toStdString() << std::endl;
        }
      } else if (!buttonCommand.isEmpty()) {
        // Execute custom command
        QProcess::execute("/bin/bash", QStringList() << "-c" << buttonCommand);
      }
    };

    if (buttonConfirm) {
      BPConfirmationDialog::ConfirmConfig config;
      config.title = tr("Confirmation Required");
      config.prompt = buttonConfirmText.isEmpty() ? tr("Are you sure?") : buttonConfirmText;
      config.confirmText = buttonConfirmYesText.isEmpty() ? tr("Yes") : buttonConfirmYesText;
      config.cancelText = buttonConfirmNoText.isEmpty() ? tr("No") : buttonConfirmNoText;
      config.richText = true; // Enable HTML for better formatting
      auto *dialog = BPConfirmationDialog::showConfirmation(config, this);

      connect(dialog, &BPConfirmationDialog::confirmed, this, [=](bool accepted) {
        if (accepted) {
          executeAction();
        }
      });
    } else {
      executeAction();
    }
  });

  return actionBtn;
}

// 5) Standard process handlers
void BPCommandDialog::handleProcessOutput() {
  QString output = QString::fromUtf8(process->readAllStandardOutput());
  outputText->append(output);
}
void BPCommandDialog::handleProcessError() {
  QString error = QString::fromUtf8(process->readAllStandardError());
  outputText->append("<span style='color: #ff7c30;'>" + error.toHtmlEscaped() + "</span>");
}
void BPCommandDialog::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
  closeButton->setEnabled(true);
  killButton->hide();

  if (exitStatus == QProcess::CrashExit) {
    // user killed it or it crashed (possibly from timeout handler)
    // Timeout handler already set retry button visibility, so just return
    return;
  }
  if (exitCode != 0) {
    outputText->append(QString("\n<span style='color: #ff7c30;'>Command failed with exit code: %1</span>").arg(exitCode));
    closeButton->setText(tr("Close (Command Failed | Exit code: %1)").arg(exitCode));
    closeButton->setStyleSheet(R"(
      BPButton {
        background-color: #EA4646;
        font-size: 45px;
        font-weight: 500;
        border-radius: 20px;
        color: white;
      }
      BPButton:pressed {
        background-color: #F15959;
      }
    )");

    // Show retry button on failure (if enabled)
    retryButton->setVisible(storedShowRetry);
    retryButton->setEnabled(storedShowRetry);

    // Show action buttons that should appear on failure
    if (!actionButtonWidgets.isEmpty()) {
      BPLog::bpDebugGeneral() << "[bp.command.dialog] Command failed, adding action buttons for failure/always" << std::endl;
      for (const auto &info : actionButtonWidgets) {
        if (info.showWhen == "failure" || info.showWhen == "always") {
          buttonLayout->addWidget(info.button);
          info.button->show();
          BPLog::bpDebugGeneral() << "[bp.command.dialog] Added action button with showWhen=" << info.showWhen.toStdString() << std::endl;
        }
      }
    }
  } else {
    outputText->append("\n<span style='color: #50d332;'>Command completed successfully</span>");
    closeButton->setText(tr("Close (Completed Successfully)"));
    closeButton->setStyleSheet(R"(
      BPButton {
        background-color: #33Ab4C;
        font-size: 45px;
        font-weight: 500;
        border-radius: 20px;
        color: white;
      }
      BPButton:pressed {
        background-color: #2A9040;
      }
    )");

    // Hide retry button on success
    retryButton->setVisible(false);

    // Show action buttons that should appear on success
    if (!actionButtonWidgets.isEmpty()) {
      BPLog::bpDebugGeneral() << "[bp.command.dialog] Command succeeded, adding action buttons for success/always" << std::endl;
      for (const auto &info : actionButtonWidgets) {
        if (info.showWhen == "success" || info.showWhen == "always") {
          buttonLayout->addWidget(info.button);
          info.button->show();
          BPLog::bpDebugGeneral() << "[bp.command.dialog] Added action button with showWhen=" << info.showWhen.toStdString() << std::endl;
        }
      }
    }
  }
}
void BPCommandDialog::killProcess() {
  if (process && process->state() != QProcess::NotRunning) {
    outputText->append("\n<span style='color: #ff7c30;'>Process terminated by user</span>");
    process->kill();
    killButton->hide();
    closeButton->setEnabled(true);
    closeButton->setText(tr("Close (Terminated)"));
    closeButton->setStyleSheet(R"(
      BPButton {
        background-color: #EA4646;
        font-size: 45px;
        font-weight: 500;
        border-radius: 20px;
        color: white;
      }
      BPButton:pressed {
        background-color: #F15959;
      }
    )");
  }
}

void BPCommandDialog::retryCommand() {
  BPLog::bpInfo() << "[bp.command.dialog] Retrying command" << std::endl;

  // Ensure previous process is fully stopped before retrying
  if (process && process->state() != QProcess::NotRunning) {
    BPLog::bpDebugGeneral() << "[bp.command.dialog] Waiting for previous process to stop before retry..." << std::endl;
    process->kill();
    process->waitForFinished(2000);
  }

  // Clear output and add retry message
  outputText->clear();
  outputText->append(QString("<b>Retrying command:</b>\n\n%1\n\n").arg(storedCommand));

  // Disable retry button while running
  retryButton->setEnabled(false);
  retryButton->setVisible(false);

  // Start the command again
  startCommand();
}

BPFileViewerDialog::BPFileViewerDialog(QWidget *parent) : BPDialogBase(parent) {
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setupDialogStyle();
  main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);
  // Initialize pointers to nullptr
  title_label = nullptr;
  contentText = nullptr;
}

void BPFileViewerDialog::setupDialogStyle() {
  setStyleSheet(R"(
    BPFileViewerDialog, QDialog {
      background-color: #000000;
    }
    QWidget {
      background-color: #000000;
      color: white;
    }
    QLabel {
      color: #FFFFFF;
    }
    QTextEdit {
      background-color: #1B1B1B;
      color: #C9C9C9;
      font-family: monospace;
      font-size: 35px;
      border: none;
      padding: 50px;
    }
  )");
}

void BPFileViewerDialog::setupUI(const QString &title) {
  // Create a header widget
  QWidget *header_widget = new QWidget(this);
  header_widget->setFixedHeight(130);
  header_widget->setStyleSheet("background-color: #202020;");

  QHBoxLayout *header_layout = new QHBoxLayout(header_widget);
  header_layout->setContentsMargins(30, 20, 30, 20);

  // "Back" button (renamed to back_btn; same style as BPNestedDialog)
  BPBackButton *back_btn = new BPBackButton(this);
  connect(back_btn, &BPBackButton::clicked, this, &QDialog::accept);

  // Title label in the center
  title_label = new QLabel(this);
  title_label->setStyleSheet("font-weight: 600; color: white;");
  title_label->setAlignment(Qt::AlignCenter);
  setScaledTitleText(title_label, title); // dynamically scales the label text

  // Assemble the header
  header_layout->addWidget(back_btn);
  header_layout->addStretch(1);
  header_layout->addWidget(title_label);
  header_layout->addStretch(1);

  // Symmetric spacer on the right to balance the Back button
  QWidget *spacer = new QWidget();
  spacer->setFixedSize(back_btn->size());
  header_layout->addWidget(spacer);

  main_layout->addWidget(header_widget);

  // Container for file content with slight margin padding
  QWidget *contentContainer = new QWidget(this);
  QVBoxLayout *contentLayout = new QVBoxLayout(contentContainer);
  // Set a margin/padding (adjust values as needed)
  contentLayout->setContentsMargins(30, 30, 30, 30);

  // Create contentText if not already created
  if (!contentText) {
    contentText = new QTextEdit(this);
    contentText->setReadOnly(true);
    // Disable text selection
    contentText->setTextInteractionFlags(Qt::NoTextInteraction);
    // Add touch scrolling gesture
    QScroller::grabGesture(contentText->viewport(), QScroller::LeftMouseButtonGesture);

    // Use your stylesheet for contentText if needed
    contentText->setStyleSheet(R"(
      QTextEdit {
        background-color: #1B1B1B;
        color: #C9C9C9;
        font-family: monospace;
        font-size: 35px;
        border: none;
        padding: 50px;
      }
    )");
  }
  contentLayout->addWidget(contentText);
  main_layout->addWidget(contentContainer);
}

void BPFileViewerDialog::loadFileAndShow(const QString &path, const QString &header, const QString &fallbackTitle) {
  QString finalTitle = header.isEmpty() ? fallbackTitle : header;
  setupUI(finalTitle);

  // Attempt to read the file
  QFile file(path);
  QString content;
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    content = tr("<b>Error:</b><br>Could not open file: %1").arg(path);
  } else {
    QString fileData = QString::fromUtf8(file.readAll());
    if (fileData.isEmpty()) {
      content = tr("File is empty");
    } else {
      content = "<pre style='white-space: pre-wrap; margin:0; padding:0; background-color:transparent;'>" + fileData.toHtmlEscaped() + "</pre>";
    }
  }

  contentText->setHtml(content);

  setupFullscreen();
}

BPParamViewerDialog::~BPParamViewerDialog() { TimerManager::getInstance().removeTimer("paramViewer_refresh"); }

BPParamViewerDialog::BPParamViewerDialog(QWidget *parent) : BPDialogBase(parent) {
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setupDialogStyle();

  main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(30, 20, 30, 20);
  main_layout->setSpacing(0);

  auto refreshTimer = TimerManager::getInstance().createTimer("paramViewer_refresh");
  refreshTimer->setInterval(2000);
  connect(refreshTimer, &QTimer::timeout, this, &BPParamViewerDialog::refreshParamValue);
}

void BPParamViewerDialog::setupDialogStyle() {
  setStyleSheet(R"(
    BPParamViewerDialog {
      background-color: #000000;
    }
    QLabel {
      color: white;
    }
    QTextEdit {
      background-color: #1B1B1B;
      color: #C9C9C9;
      font-family: monospace;
      font-size: 35px;
      border: none;
      padding: 50px;
    }
    QCheckBox {
      color: #C9C9C9;
      font-size: 35px;
    }
    QCheckBox::indicator {
      width: 40px;
      height: 40px;
      border: 2px solid white;
      border-radius: 20px;
    }
    QCheckBox::indicator:checked {
      background-color: #33Ab4C;
    }
    QCheckBox::indicator:unchecked {
      background-color: #EA4646;
    }
  )");
}

void BPParamViewerDialog::setupUI(const QString &title) {
  // Header widget
  QWidget *header_widget = new QWidget(this);
  header_widget->setFixedHeight(130);
  header_widget->setStyleSheet("background-color: #202020;");

  QHBoxLayout *header_layout = new QHBoxLayout(header_widget);
  header_layout->setContentsMargins(30, 20, 30, 20);

  // "Back" button (renamed to back_btn; same style as BPNestedDialog)
  BPBackButton *back_btn = new BPBackButton(this);
  connect(back_btn, &BPBackButton::clicked, this, &QDialog::accept);

  // Title label
  title_label = new QLabel(this);
  setScaledTitleText(title_label, title);
  title_label->setStyleSheet("font-weight: 600; color: white;");
  title_label->setAlignment(Qt::AlignCenter);

  // Auto refresh label & checkbox on the right
  QHBoxLayout *refreshLayout = new QHBoxLayout();
  refreshLayout->setSpacing(10);

  QLabel *toggleLabel = new QLabel(tr("Auto Refresh"));
  toggleLabel->setStyleSheet("font-size: 35px; font-weight: 200; color: #C9C9C9;");
  toggleLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  refreshLayout->addWidget(toggleLabel);

  autoRefreshCheckbox = new QCheckBox(this);
  autoRefreshCheckbox->setFixedSize(60, 60);
  autoRefreshCheckbox->setChecked(true);
  refreshLayout->addWidget(autoRefreshCheckbox);

  // Build the header layout
  header_layout->addWidget(back_btn);
  header_layout->addStretch(1);
  header_layout->addWidget(title_label);
  header_layout->addStretch(1);
  header_layout->addLayout(refreshLayout);

  main_layout->addWidget(header_widget);
  main_layout->addSpacing(30);

  // Parameter content
  paramContent = new QTextEdit(this);
  paramContent->setReadOnly(true);
  paramContent->setStyleSheet(R"(
    QTextEdit {
      font-family: monospace;
      font-size: 35px;
      font-weight: 200;
      color: #C9C9C9;
      background-color: #1B1B1B;
      padding: 50px;
      border: none;
    }
  )");
  main_layout->addWidget(paramContent);

  main_layout->addSpacing(30);

  setupFullscreen();
}

void BPParamViewerDialog::setupParamViewer(const QString &title, const QString &param) {
  paramName = param;
  setupUI(title + " | " + param);
  refreshParamValue();
  auto refreshTimer = getRefreshTimer();
  if (refreshTimer) {
    refreshTimer->start();
  }
}

void BPParamViewerDialog::refreshParamValue() {
  QString rawValue = QString::fromStdString(params.get(paramName.toStdString()));
  QString content = rawValue.isEmpty() ? tr("No data available for this parameter.") : rawValue;
  paramContent->setText(content);
}

void BPParamViewerDialog::toggleAutoRefresh(bool enabled) {
  auto refreshTimer = getRefreshTimer();
  if (enabled) {
    refreshTimer->start();
  } else {
    refreshTimer->stop();
  }
}

BPParamListDialog::BPParamListDialog(QWidget *parent) : BPDialogBase(parent) {
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setupDialogStyle();

  main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(30, 20, 30, 20);
  main_layout->setSpacing(0);
}

void BPParamListDialog::setupDialogStyle() {
  setStyleSheet(R"(
    BPParamListDialog {
      background-color: #000000;
    }
    QLabel {
      color: white;
    }
    QFrame {
      background-color: #242424;
      border-radius: 10px;
    }
    QWidget {
      background-color: #000000;  /* Add this */
    }
    QScrollArea {
      background-color: #000000;  /* Add this */
      border: none;
    }
    QScrollArea > QWidget {
      background-color: #000000;  /* Add this */
      border: none;
    }
    QScrollArea > QWidget > QWidget {
      background-color: #000000;  /* Add this */
      border: none;
    }
  )");
}

void BPParamListDialog::setupUI() {
  // Header widget
  QWidget *header_widget = new QWidget(this);
  header_widget->setFixedHeight(130);
  header_widget->setStyleSheet("background-color: #202020;");

  QHBoxLayout *header_layout = new QHBoxLayout(header_widget);
  header_layout->setContentsMargins(30, 20, 30, 20);

  // "Back" button (renamed to back_btn; same style as BPNestedDialog)
  BPBackButton *back_btn = new BPBackButton(this);
  connect(back_btn, &BPBackButton::clicked, this, &QDialog::accept);

  // Title label in the center
  title_label = new QLabel(this);
  setScaledTitleText(title_label, tr("Available Parameters"));
  title_label->setStyleSheet("font-weight: 600; color: white;");
  title_label->setAlignment(Qt::AlignCenter);

  // Build the header layout
  header_layout->addWidget(back_btn);
  header_layout->addStretch(1);
  header_layout->addWidget(title_label);
  header_layout->addStretch(1);

  // Spacer on the right to match the button size
  QWidget *spacer = new QWidget();
  spacer->setFixedSize(back_btn->size());
  header_layout->addWidget(spacer);

  main_layout->addWidget(header_widget);
}

void BPParamListDialog::setupParamList() {
  // First, set up the header and footer.
  setupUI();

  // Create a widget to hold the parameter list.
  QWidget *paramWidget = new QWidget(this);
  QVBoxLayout *paramLayout = new QVBoxLayout(paramWidget);
  paramLayout->setSpacing(20);
  paramLayout->setContentsMargins(25, 25, 25, 25);

  // Retrieve all parameter keys from Params().
  std::vector<std::string> allParams = Params().allKeys();
  for (const auto &param : allParams) {
    QString paramStr = QString::fromStdString(param);

    // Create a frame for each parameter.
    QFrame *paramFrame = new QFrame(this);
    paramFrame->setStyleSheet(R"(
      QFrame {
        background-color: #242424;
        border-radius: 10px;
        min-height: 130px;
      }
    )");

    QHBoxLayout *frameLayout = new QHBoxLayout(paramFrame);
    frameLayout->setContentsMargins(25, 25, 25, 25);
    frameLayout->setSpacing(40);

    // Create a "VIEW" button.
    BPButton *viewBtn = new BPButton(tr("View"), paramFrame);
    viewBtn->setMinimumHeight(100);
    viewBtn->setMinimumWidth(200);
    viewBtn->setStyleSheet(R"(
      BPButton {
        background-color: #363636;
        border-radius: 30px;
        font-size: 36px;
        font-weight: 500;
        color: white;
        padding: 0px 30px;
      }
      BPButton:hover {
        background-color: #404040;
      }
      BPButton:pressed {
        background-color: #505050;
      }
      BPButton:disabled {
        background-color: #202020;
        color: #777777;
      }
    )");

    // Create a label to show the parameter name.
    QLabel *paramLabel = new QLabel(paramStr, paramFrame);
    paramLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
    paramLabel->setWordWrap(true);

    frameLayout->addWidget(viewBtn, 0, Qt::AlignLeft | Qt::AlignVCenter);
    frameLayout->addWidget(paramLabel, 1);

    // When the VIEW button is clicked, emit a signal.
    connect(viewBtn, &BPButton::clicked, [this, paramStr]() { emit paramViewRequested(paramStr); });

    paramLayout->addWidget(paramFrame);
  }

  // Place the parameter widget inside a scroll view.
  BPScrollView *scroll = new BPScrollView(paramWidget, this);
  scroll->setStyleSheet(R"(
    QScrollArea {
      background-color: #000000;
      border: none;
    }
    QScrollArea > QWidget {
      background-color: #000000;
      border: none;
    }
    QScrollArea > QWidget > QWidget {
      background-color: #000000;
    }
    QScrollBar:vertical {
      width: 12px;
      margin: 0px;
      padding: 2px;
      background: transparent;
    }
    QScrollBar::handle:vertical {
      background: #666666;
      min-height: 100px;
      border-radius: 6px;
      margin: 0 2px;
    }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
      height: 0px;
    }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
      background: none;
    }
  )");
  // scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // Insert the scroll area above the close button
  main_layout->addWidget(scroll, 1);

  main_layout->addSpacing(20);

  setupFullscreen();
}
