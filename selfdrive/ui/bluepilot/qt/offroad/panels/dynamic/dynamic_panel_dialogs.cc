// selfdrive/ui/bluepilot/qt/offroad/panels/dynamic/dynamic_panel_dialogs.cc

#include "dynamic_panel_dialogs.h"
#include <iostream>

DynamicPanelConfirmationDialog::DynamicPanelConfirmationDialog(const QString &prompt_text,
    const QString &confirm_text, const QString &cancel_text, const bool rich, QWidget* parent)
    : ConfirmationDialog(prompt_text, confirm_text, cancel_text, rich, parent) {
}

bool DynamicPanelConfirmationDialog::toggle(const QString &prompt_text, const QString &confirm_text, QWidget *parent) {
    ConfirmationDialog d = ConfirmationDialog(prompt_text, confirm_text, tr("Reboot Later"), false, parent);
    return d.exec();
}

bool DynamicPanelConfirmationDialog::toggleAlert(const QString &prompt_text, const QString &button_text, QWidget *parent) {
    ConfirmationDialog d = ConfirmationDialog(prompt_text, button_text, "", false, parent);
    return d.exec();
}

bool DynamicPanelConfirmationDialog::yesorno(const QString &prompt_text, QWidget *parent) {
    ConfirmationDialog d = ConfirmationDialog(prompt_text, tr("Yes"), tr("No"), false, parent);
    return d.exec();
}

DynamicPanelFullScreenDialog::DynamicPanelFullScreenDialog(QWidget *parent) : QDialog(parent) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setStyleSheet(getDialogStyle());

    main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(30, 20, 30, 20);
    main_layout->setSpacing(0);
}

void DynamicPanelFullScreenDialog::setupContent(const QString& title, const QString& content) {
    title_label = new QLabel(title);
    title_label->setStyleSheet("font-size: 60px; font-weight: 600; background-color: black;");
    main_layout->addWidget(title_label);
    main_layout->addSpacing(30);

    QWidget* contentWidget = new QWidget(this);
    contentWidget->setStyleSheet("background-color: black;");
    QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* text = new QLabel(contentWidget);
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(true);
    text->setText(content);
    text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    text->setStyleSheet("font-size: 35px; font-weight: 200; color: #C9C9C9; background-color:#1B1B1B; padding: 50px 50px;");
    text->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    contentLayout->addWidget(text);
    contentLayout->addStretch();

    scroll = new ScrollView(contentWidget, this);
    main_layout->addWidget(scroll);
    main_layout->addSpacing(50);

    close_btn = new QPushButton(tr("Close"));
    close_btn->setFixedHeight(160);
    close_btn->setStyleSheet(R"(
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
    main_layout->addWidget(close_btn);

    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
}

void DynamicPanelFullScreenDialog::setupFullscreen() {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        setFixedSize(2160, 1080);
    }
    show();
    #ifdef QCOM2
    setupWaylandSurface();
    #endif
}

#ifdef QCOM2
void DynamicPanelFullScreenDialog::setupWaylandSurface() {
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    wl_surface *s = reinterpret_cast<wl_surface*>(native->nativeResourceForWindow("surface", windowHandle()));
    if (s) {
        wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
        wl_surface_commit(s);
    }
    setWindowState(Qt::WindowFullScreen);
    setAttribute(Qt::WA_DeleteOnClose);
    layout()->activate();
    void *egl = native->nativeResourceForWindow("egldisplay", windowHandle());
    assert(egl != nullptr);
}
#endif

QString DynamicPanelFullScreenDialog::getDialogStyle() const {
    return R"(
        QDialog {
            background-color: black;
        }
        QWidget {
            background-color: black;
            color: white;
        }
        QLabel {
            background-color: black;
        }
        QPushButton {
            height: 160px;
            font-size: 55px;
            font-weight: 400;
            border-radius: 10px;
            background-color: #4F4F4F;
        }
        QScrollArea {
            background-color: black;
        }
        QScrollArea > QWidget > QWidget {
            background-color: black;
        }
    )";
}

DynamicPanelCommandDialog::DynamicPanelCommandDialog(QWidget *parent)
    : DynamicPanelFullScreenDialog(parent), process(nullptr) {
}

void DynamicPanelCommandDialog::setupCommandUI(const QString& title) {
    title_label = new QLabel(title + " - " + tr("Output"));
    title_label->setStyleSheet("font-size: 60px; font-weight: 600; background-color: black;");
    main_layout->addWidget(title_label);
    main_layout->addSpacing(30);

    outputText = new QTextEdit(this);
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
    main_layout->addWidget(outputText);

    buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(20);

    killButton = new QPushButton(tr("Stop Command"), this);
    killButton->setFixedHeight(120);
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

    closeButton = new QPushButton(tr("Command is Running..."), this);
    closeButton->setEnabled(false);
    closeButton->setFixedHeight(120);
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

    main_layout->addSpacing(50);
    main_layout->addLayout(buttonLayout);

    connect(killButton, &QPushButton::clicked, this, &DynamicPanelCommandDialog::killProcess);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void DynamicPanelCommandDialog::executeCommand(const QString& command, const QString& title,
                                             const QString& workingDir, const QJsonArray& actionButtons) {
    setupCommandUI(title);
    setupActionButtons(actionButtons);

    process = new QProcess(this);

    if (!workingDir.isEmpty()) {
        QDir dir(workingDir);
        if (dir.exists()) {
            process->setWorkingDirectory(workingDir);
        } else {
            std::cout << "Warning: Working directory does not exist: " << workingDir.toStdString() << std::endl;
        }
    }

    connect(process, &QProcess::readyReadStandardOutput, this, &DynamicPanelCommandDialog::handleProcessOutput);
    connect(process, &QProcess::readyReadStandardError, this, &DynamicPanelCommandDialog::handleProcessError);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &DynamicPanelCommandDialog::handleProcessFinished);

    connect(this, &QDialog::finished, process, &QProcess::deleteLater);

    setupFullscreen();

    std::cout << "Executing command: " << command.toStdString() << std::endl;
    process->start("/bin/bash", QStringList() << "-c" << command);
}

void DynamicPanelCommandDialog::setupActionButtons(const QJsonArray& actionButtons) {
    int maxActionButtons = 3;
    int addedButtons = 0;

    for (const auto& buttonValue : actionButtons) {
        if (addedButtons >= maxActionButtons) {
            std::cout << "Warning: Exceeded maximum of " << maxActionButtons
                     << " action buttons. Skipping remaining buttons." << std::endl;
            break;
        }
        QJsonObject buttonObj = buttonValue.toObject();
        QPushButton* actionButton = createActionButton(buttonObj);
        if (actionButton) {
            buttonLayout->addWidget(actionButton);
            addedButtons++;
        }
    }
}

void DynamicPanelCommandDialog::handleProcessOutput() {
    QString output = QString::fromUtf8(process->readAllStandardOutput());
    outputText->append(output);
}

void DynamicPanelCommandDialog::handleProcessError() {
    QString error = QString::fromUtf8(process->readAllStandardError());
    outputText->append("<span style='color: #ff7c30;'>" + error.toHtmlEscaped() + "</span>");
}

void DynamicPanelCommandDialog::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    closeButton->setEnabled(true);
    killButton->hide();

    if (exitStatus == QProcess::CrashExit) {
        // Handled by kill button
        return;
    }

    if (exitCode != 0) {
        outputText->append(QString("\n<span style='color: #ff7c30;'>Command failed with exit code: %1</span>")
                          .arg(exitCode));
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
    }
}

void DynamicPanelCommandDialog::killProcess() {
    if (process && process->state() != QProcess::NotRunning) {
        outputText->append("\n<span style='color: #ff7c30;'>Process terminated by user</span>");
        process->kill();
        killButton->hide();
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
}

QPushButton* DynamicPanelCommandDialog::createActionButton(const QJsonObject& buttonObj) {
    QString buttonText = buttonObj["text"].toString();
    QString buttonCommand = buttonObj["command"].toString();
    bool buttonConfirm = buttonObj["confirm"].toBool();
    QString buttonConfirmText = buttonObj["confirm_text"].toString();
    QString buttonConfirmYesText = buttonObj["confirm_yes_text"].toString();
    QString buttonConfirmNoText = buttonObj["confirm_no_text"].toString();

    QPushButton* actionButton = new QPushButton(buttonText, this);
    actionButton->setFixedHeight(120);

    if (buttonObj.contains("style")) {
        actionButton->setStyleSheet(getButtonStyle(buttonObj["style"].toObject()));
    } else {
        actionButton->setStyleSheet(R"(
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
    }

    connect(actionButton, &QPushButton::clicked, [=]() {
        if (buttonConfirm) {
            auto confirm = new ConfirmationDialog(
                buttonConfirmText.isEmpty() ? tr("Are you sure?") : buttonConfirmText,
                buttonConfirmYesText.isEmpty() ? tr("Yes") : buttonConfirmYesText,
                buttonConfirmNoText.isEmpty() ? tr("No") : buttonConfirmNoText,
                false, this);
            bool confirmed = confirm->exec();
            delete confirm;
            if (!confirmed) {
                return;
            }
        }
        QProcess::execute("/bin/bash", QStringList() << "-c" << buttonCommand);
    });

    return actionButton;
}

QString DynamicPanelCommandDialog::getButtonStyle(const QJsonObject& style) const {
    QString backgroundColor = style["background_color"].toString().isEmpty() ? "#465BEA"
                            : style["background_color"].toString();
    QString pressedColor = style["pressed_color"].toString().isEmpty() ? "#3049F4"
                          : style["pressed_color"].toString();
    QString disabledColor = style["disabled_color"].toString().isEmpty() ? "#4F4F4F"
                           : style["disabled_color"].toString();
    QString textColor = style["text_color"].toString().isEmpty() ? "white"
                       : style["text_color"].toString();

    return QString(R"(
        QPushButton {
            background-color: %1;
            font-size: 55px;
            font-weight: 400;
            border-radius: 10px;
            color: %2;
        }
        QPushButton:pressed {
            background-color: %3;
        }
        QPushButton:disabled {
            background-color: %4;
            color: %5;
        }
    )").arg(backgroundColor, textColor, pressedColor, disabledColor, textColor);
}

DynamicPanelParamViewerDialog::DynamicPanelParamViewerDialog(QWidget *parent)
    : DynamicPanelFullScreenDialog(parent) {
    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(2000); // 2 second refresh interval
    connect(refreshTimer, &QTimer::timeout, this, &DynamicPanelParamViewerDialog::refreshParamValue);
}

void DynamicPanelParamViewerDialog::setupParamViewer(const QString& title, const QString& param) {
    paramName = param;

    // Setup Title
    title_label = new QLabel(title + " | " + param);
    title_label->setStyleSheet("font-size: 60px; font-weight: 600; background-color: black;");
    title_label->setAlignment(Qt::AlignLeft);
    main_layout->addWidget(title_label);
    main_layout->addSpacing(30);

    // Create horizontal layout for Auto Refresh label and checkbox, aligned to the right
    QHBoxLayout* toggleLayout = new QHBoxLayout();
    toggleLayout->setContentsMargins(0, 0, 0, 0);
    toggleLayout->setSpacing(10); // Adjust spacing as needed

    // Add spacer to push the following widgets to the right
    toggleLayout->addStretch();

    // Auto Refresh Label
    QLabel* toggleLabel = new QLabel(tr("Auto Refresh"));
    toggleLabel->setStyleSheet("font-size: 35px; font-weight: 200; color: #C9C9C9; background-color:transparent;");
    toggleLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    toggleLayout->addWidget(toggleLabel);

    // Auto Refresh Checkbox
    autoRefreshCheckbox = new QCheckBox(this);
    autoRefreshCheckbox->setStyleSheet(R"(
        QCheckBox {
            spacing: 10px;
            font-size: 35px;
            font-weight: 200;
            color: #C9C9C9;
            background-color: transparent;
        }
        QCheckBox::indicator {
            width: 40px;
            height: 40px;
            background-color: #C9C9C9;
            border: 2px solid #FFFFFF;
            border-radius: 20px;
        }
        QCheckBox::indicator:checked {
            background-color: #33Ab4C;
        }
        QCheckBox::indicator:unchecked {
            background-color: #EA4646;
        }
    )");
    autoRefreshCheckbox->setFixedSize(60, 60); // Adjust size as needed
    autoRefreshCheckbox->setChecked(true); // Default state
    // autoRefreshCheckbox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Connect the checkbox state to the auto-refresh functionality
    connect(autoRefreshCheckbox, &QCheckBox::stateChanged, this, [this](int state) {
        if (state == Qt::Checked) {
            refreshTimer->start(); // Start the refresh timer
        } else {
            refreshTimer->stop(); // Stop the refresh timer
        }
    });

    toggleLayout->addWidget(autoRefreshCheckbox, 0, Qt::AlignVCenter); // Ensure vertical centering

    // Add the horizontal layout to the main layout
    main_layout->addLayout(toggleLayout);
    main_layout->addSpacing(20); // Adjust spacing to prevent overlapping

    // Content below toggle
    paramContent = new QTextEdit(this);
    paramContent->setReadOnly(true);
    paramContent->setStyleSheet(R"(
        QTextEdit {
            font-family: monospace;
            font-size: 35px;
            color: #C9C9C9;
            background-color: #1B1B1B;
            padding: 50px;
            border: none;
        }
    )");
    main_layout->addWidget(paramContent);

    // Add close button
    close_btn = new QPushButton(tr("Close"));
    close_btn->setFixedHeight(160);
    close_btn->setStyleSheet(R"(
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
    main_layout->addWidget(close_btn);

    // Connect close button
    connect(close_btn, &QPushButton::clicked, this, [this]() {
        refreshTimer->stop();
        accept();
    });

    // Initial refresh
    refreshParamValue();
}

void DynamicPanelParamViewerDialog::refreshParamValue() {
    Params params;
    QString rawValue = QString::fromStdString(params.get(paramName.toStdString()));
    std::cout << "refreshParamValue: " << rawValue.toStdString() << std::endl;

    QString content;
    if (rawValue.isEmpty()) {
        content = tr("No data available for this parameter.");
    } else {
        content = rawValue;
    }

    paramContent->setText(content);
}

void DynamicPanelParamViewerDialog::toggleAutoRefresh(bool enabled) {
    if (enabled) {
        refreshTimer->start();
    } else {
        refreshTimer->stop();
    }
}
