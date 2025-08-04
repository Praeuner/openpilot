// selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_controls.h

#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QGraphicsEffect>
#include <QClipboard>
#include <QProcess>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QScroller>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QPropertyAnimation>
#include <QStyle>
#include <iostream>
#include <cmath>

#include "common/params.h"
#include "bp_nested_view.h"
#include "bp_panel_dialogs.h"

class BPParamViewerDialog;
class BPParamListDialog;
class BPRecentChangesDialog;

class BPScrollView : public QScrollArea {
  Q_OBJECT

public:
  explicit BPScrollView(QWidget *w = nullptr, QWidget *parent = nullptr) : QScrollArea(parent) {
    setWidget(w);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); // Changed from AlwaysOff to AsNeeded
    setStyleSheet("background-color: transparent; border:none");

    // Update scrollbar styling to match nav bar
    QString style = R"(
      QScrollBar:vertical {
        width: 8px;
        background: transparent;
        margin: 0px;
      }
      QScrollBar::handle:vertical {
        background: #666666;
        border-radius: 4px;
        min-height: 20px;
      }
      QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
        height: 0px;
      }
      QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
        background: none;
      }
    )";
    verticalScrollBar()->setStyleSheet(style);
    horizontalScrollBar()->setStyleSheet(style);

    QScroller *scroller = QScroller::scroller(this->viewport());
    QScrollerProperties sp = scroller->scrollerProperties();

    sp.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, QVariant::fromValue<QScrollerProperties::OvershootPolicy>(QScrollerProperties::OvershootAlwaysOff));
    sp.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, QVariant::fromValue<QScrollerProperties::OvershootPolicy>(QScrollerProperties::OvershootAlwaysOff));
    sp.setScrollMetric(QScrollerProperties::MousePressEventDelay, 0.01);
    scroller->grabGesture(this->viewport(), QScroller::LeftMouseButtonGesture);
    scroller->setScrollerProperties(sp);
  }

protected:
  void hideEvent(QHideEvent *e) override { verticalScrollBar()->setValue(0); }
};

class BPButton : public QPushButton {
  Q_OBJECT
public:
  explicit BPButton(const QString &text, QWidget *parent = nullptr) : QPushButton(text, parent) {
    setMinimumHeight(80);
    setMinimumWidth(200);
    setStyleSheet(R"(
      QPushButton {
        background-color: #363636;
        border: none;
        border-radius: 40px;
        color: #FFFFFF;
        font-size: 32px;
        font-weight: 500;
        padding: 15px 30px;
      }
      QPushButton:hover {
        background-color: #404040;
      }
      QPushButton:pressed {
        background-color: #505050;
      }
      QPushButton:disabled {
        background-color: #202020;
        color: #666666;
      }
    )");
  }

protected:
  void mouseReleaseEvent(QMouseEvent *event) override {
    if (isEnabled() && event->button() == Qt::LeftButton) {
      // Ensure the click is properly handled for touch devices
      QPushButton::mouseReleaseEvent(event);
    }
  }
};

/*
 * BPBackButton is a reusable button preconfigured for dialog headers.
 * It inherits from BPButton, sets the fixed size and the common style.
 */
class BPBackButton : public BPButton {
  Q_OBJECT

public:
  explicit BPBackButton(QWidget *parent = nullptr, const QString &text = "< Back") : BPButton(text, parent) {
    setFixedSize(220, 90);
    setupStyle();
  }

private:
  void setupStyle() {
    // Apply the style you used for the back button in dialogs.
    setStyleSheet(R"(
      BPButton {
        background-color: #2196F3;
        border-radius: 20px;
        font-size: 40px;
        font-weight: 600;
        color: white;
        padding: 5px 20px;
      }
      BPButton:hover {
        background-color: #1E88E5;
      }
      BPButton:pressed {
        background-color: #1976D2;
      }
    )");
  }
};

class BPToggle : public QAbstractButton {
  Q_OBJECT
  Q_PROPERTY(float togglePosition READ togglePosition WRITE setTogglePosition)

public:
  explicit BPToggle(QWidget *parent = nullptr) : QAbstractButton(parent) {
    setCheckable(true);
    setFixedSize(140, 70);
  }

  float togglePosition() { return position; }
  void setTogglePosition(float pos) {
    position = pos;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw background
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, width(), height()), height() / 2, height() / 2);

    painter.fillPath(path, isChecked() ? QColor("#2196F3") : QColor("#808080"));

    if (!isEnabled()) {
      painter.fillPath(path, QColor(0, 0, 0, 128)); // Darken when disabled
    }

    // Draw handle
    qreal pos = isChecked() ? width() - height() + 4 : 4;
    qreal handleSize = height() - 8;
    painter.setBrush(isEnabled() ? Qt::white : QColor("#AAAAAA"));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRectF(pos, 4, handleSize, handleSize));
  }

  void mouseReleaseEvent(QMouseEvent *) override {
    if (isEnabled()) {
      toggle();
    }
  }

  void toggle() {
    setChecked(!isChecked());
    emit toggled(isChecked());
  }

private:
  float position = 0.0;
};

class BPToggleControl : public QFrame {
  Q_OBJECT

public:
  BPToggleControl(const QString &param, const QString &title, const QString &desc, QWidget *parent = nullptr) : QFrame(parent), paramName(param.toStdString()) {

    QHBoxLayout *main_layout = new QHBoxLayout();
    main_layout->setContentsMargins(25, 25, 25, 25); // Increased margins
    main_layout->setSpacing(50);                     // Increased spacing
    setLayout(main_layout);

    // Left side - toggle
    toggle = new BPToggle();
    main_layout->addWidget(toggle, 0, Qt::AlignLeft | Qt::AlignVCenter);

    // Right side content
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setSpacing(10); // Increased spacing

    titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(R"(
        QLabel {
            font-size: 40px;
            color: white;
            font-weight: 500;
        }
        QLabel:disabled {
            color: #666666;
        }
    )");
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc);
      descLabel->setStyleSheet(R"(
          QLabel {
              font-size: 32px;
              color: #AAAAAA;
          }
          QLabel:disabled {
              color: #444444;
          }
      )");
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }

    textLayout->addStretch();
    main_layout->addLayout(textLayout, 1);

    setStyleSheet(R"(
      BPToggleControl {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;  /* Increased min-height */
      }
    )");

    // Initialize param with default if not found
    ParamUtils::initializeParam(paramName);

    QObject::connect(toggle, &QAbstractButton::toggled, [this](bool checked) {
      bool oldValue = params.getBool(paramName);

      // Only update and log if there's an actual change
      if (oldValue != checked) {
        params.putBool(paramName, checked);
        ParamUtils::logParamChange(paramName, oldValue ? "On" : "Off", checked ? "On" : "Off");
        emit toggleFlipped(checked);
      }
    });

    refresh();
  }

  void refresh() {
    bool paramValue = params.getBool(paramName);
    if (toggle->isChecked() != paramValue) {
      // Block signals during this update to prevent callback
      toggle->blockSignals(true);
      toggle->setChecked(paramValue);
      toggle->blockSignals(false);
    }
  }

signals:
  void toggleFlipped(bool state);

protected:
  void showEvent(QShowEvent *event) override {
    refresh();
    QFrame::showEvent(event);
  }

private:
  BPToggle *toggle;
  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
  std::string paramName;
  Params params;
};

class BPSelectionControl : public QFrame {
  Q_OBJECT

public:
  BPSelectionControl(const QString &param, const QString &title, const QString &desc, QWidget *parent = nullptr) : QFrame(parent), paramName(param.toStdString()), defaultDesc(desc) {

    // Overall horizontal layout
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(25, 25, 25, 25);
    mainLayout->setSpacing(50);

    // Left side: SELECT button, vertically centered.
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addStretch();
    selectButton = new BPButton("SELECT");
    selectButton->setMinimumWidth(250);
    selectButton->setMinimumHeight(100);
    leftLayout->addWidget(selectButton);
    leftLayout->addStretch();
    mainLayout->addLayout(leftLayout);

    // Right side: Title and info (selected value or description)
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    // Title
    titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet(R"(
        QLabel {
            font-size: 40px;
            color: white;
            font-weight: 500;
        }
        QLabel:disabled {
            color: #666666;
        }
    )");
    titleLabel->setWordWrap(true);
    rightLayout->addWidget(titleLabel);

    // Info label: shows selected value description (blue) if present; otherwise, default description (gray)
    infoLabel = new QLabel(this);
    infoLabel->setWordWrap(true);
    infoLabel->setText(defaultDesc);
    infoLabel->setStyleSheet(R"(
        QLabel {
            font-size: 32px;
            color: #AAAAAA;
        }
        QLabel:disabled {
            color: #666666;
        }
    )");
    rightLayout->addWidget(infoLabel);
    rightLayout->addStretch();

    mainLayout->addLayout(rightLayout, 1);

    setStyleSheet(R"(
      BPSelectionControl {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    QObject::connect(selectButton, &BPButton::clicked, this, &BPSelectionControl::clicked);
  }

  // Set the available options for value-to-display mapping
  void setOptions(const QVector<QPair<QString, QString>> &optionPairs) {
    options.clear();
    for (const auto &pair : optionPairs) {
      options[pair.second] = pair.first; // Map value -> display name
    }
  }

  // When a value is selected, call this to update the info label with the display name.
  // If value is non-empty and found in options, display the corresponding name in blue;
  // otherwise revert to the default description.
  void setSelectedValue(const QString &value) {
    if (!value.isEmpty() && options.contains(value)) {
      infoLabel->setText(options[value]);
      infoLabel->setStyleSheet("font-size: 32px; font-weight: 500; color: #2196F3;");
    } else if (!value.isEmpty()) {
      // Fallback: show the raw value if no mapping found
      infoLabel->setText(value);
      infoLabel->setStyleSheet("font-size: 32px; font-weight: 500; color: #2196F3;");
    } else {
      infoLabel->setText(defaultDesc.isEmpty() ? "Select a value" : defaultDesc);
      infoLabel->setStyleSheet("font-size: 32px; color: #AAAAAA;");
    }
  }

signals:
  void clicked();

private:
  BPButton *selectButton;
  QLabel *titleLabel;
  QLabel *infoLabel;
  std::string paramName;
  QString defaultDesc;
  QMap<QString, QString> options; // Map from value to display name
};

class BPSegmentedControl : public QFrame {
  Q_OBJECT

public:
  BPSegmentedControl(const QString &param, const QString &title, const QString &desc, const QVector<QPair<QString, QString>> &options, const QString &defaultValue = QString(),
                     QWidget *parent = nullptr)
      : QFrame(parent), paramName(param.toStdString()) {

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side - segmented buttons
    QVBoxLayout *controlLayout = new QVBoxLayout();
    controlLayout->setSpacing(10);

    QHBoxLayout *segmentLayout = new QHBoxLayout();
    segmentLayout->setSpacing(0);

    buttonGroup = new QButtonGroup(this);
    buttonGroup->setExclusive(true);

    for (int i = 0; i < options.size(); i++) {
      QPushButton *btn = new QPushButton(options[i].first);
      btn->setCheckable(true);
      btn->setMinimumWidth(180);
      btn->setFixedHeight(80);

      QString btnStyle = R"(
        QPushButton {
          background-color: #363636;
          border: 1px solid #404040;
          border-right: 1px solid #505050;  /* Lighter right border */
          color: white;
          font-size: 31px;
          padding: 5px 15px;
          border-radius: 0px;
        }
        QPushButton:checked {
          background-color: #2196F3;
          border: 1px solid #2196F3;
        }
        QPushButton:hover:!checked {
          background-color: #404040;
        }
        QPushButton:disabled {
          background-color: #202020;
          border-color: #303030;
          color: #666666;
        }
      )";

      // Only add border radius to first and last buttons
      if (i == 0) {
        btnStyle += "QPushButton { border-top-left-radius: 35px; border-bottom-left-radius: 35px; }";
      } else if (i == options.size() - 1) {
        btnStyle += R"(
          QPushButton {
            border-top-right-radius: 35px;
            border-bottom-right-radius: 35px;
            border-right: 1px solid #404040;  /* Reset right border for last button */
          }
        )";
      }

      btn->setStyleSheet(btnStyle);
      buttonGroup->addButton(btn, i);
      segmentLayout->addWidget(btn);
      optionMap[i] = options[i].second;
    }

    controlLayout->addLayout(segmentLayout);

    // Min/Max labels
    QHBoxLayout *labelsLayout = new QHBoxLayout();
    labelsLayout->setSpacing(20);
    labelsLayout->addStretch();
    layout->addLayout(controlLayout);

    // Right side - title and description
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setSpacing(10);

    titleLabel = new QLabel(title);
    titleLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
    titleLabel->setStyleSheet(R"(
        QLabel {
            font-size: 40px;
            color: white;
            font-weight: 500;
        }
        QLabel:disabled {
            color: #666666;
        }
    )");
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc);
      descLabel->setStyleSheet(R"(
          QLabel {
              font-size: 32px;
              color: #AAAAAA;
          }
          QLabel:disabled {
              color: #444444;
          }
      )");
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }

    textLayout->addStretch();
    layout->addLayout(textLayout, 1);

    setStyleSheet(R"(
      BPSegmentedControl {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    // Initialize param with default if not found
    // Get existing parameter value first
    std::string existingValue = params.get(paramName);
    if (!existingValue.empty()) {
      // If parameter already exists with a value, don't modify it
      std::cout << "Using existing parameter - " << paramName << ": " << existingValue << std::endl;
    } else if (!defaultValue.isEmpty()) {
      // Only set default if param doesn't exist yet
      params.put(paramName, defaultValue.toStdString());
      std::cout << "Parameter initialized - " << paramName << ": " << defaultValue.toStdString() << " (from constructor default)" << std::endl;
    }

    // Handle button clicks
    connect(buttonGroup, QOverload<int>::of(&QButtonGroup::buttonClicked), [this](int id) {
      std::string oldValue = params.get(paramName);
      std::string newValue = optionMap[id].toStdString();
      params.put(paramName, newValue);
      ParamUtils::logParamChange(paramName, oldValue, newValue);
      emit valueChanged();
    });

    refresh();
  }

  void refresh() {
    QString currentValue = QString::fromStdString(params.get(paramName));
    for (auto it = optionMap.begin(); it != optionMap.end(); ++it) {
      if (it.value() == currentValue) {
        buttonGroup->button(it.key())->setChecked(true);
        break;
      }
    }
  }

signals:
  void valueChanged();

private:
  QButtonGroup *buttonGroup;
  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
  std::string paramName;
  QMap<int, QString> optionMap;
  Params params;
};

class BPNumericControl : public QFrame {
  Q_OBJECT

public:
  BPNumericControl(const QString &param, const QString &title, const QString &desc, double minValue, double maxValue, double increment, bool isFloat = false, double division = 1.0,
                   QWidget *parent = nullptr)
      : QFrame(parent), paramName(param.toStdString()), min(minValue), max(maxValue), inc(increment), isFloatType(isFloat), div(division) {

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    // layout->setSpacing(50);

    // Create 3D container for numeric controls
    QFrame *numericContainer = new QFrame(this);
    numericContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Increase the container's minimum size
    numericContainer->setMinimumWidth(300);  // Increased from default
    numericContainer->setMinimumHeight(200); // Increased from default
    numericContainer->setStyleSheet(R"(
        QFrame {
            background-color: #363636;  /* Lighter than background for better contrast */
            border-radius: 20px;
            padding: 5px; /* Increased padding */
            border: 1px solid #404040;  /* Subtle border for definition */
        }
        QFrame:disabled {
            background-color: #202020;
            border-color: #303030;
            color: #666666;
        }
    )");

    QGraphicsDropShadowEffect *shadowEffect = new QGraphicsDropShadowEffect(numericContainer);
    shadowEffect->setBlurRadius(10);             // Increased blur for more spread
    shadowEffect->setColor(QColor(0, 0, 0, 40)); // More visible dark shadow
    shadowEffect->setOffset(0, 2);               // Keep it centered for popup effect
    shadowEffect->setEnabled(true);
    numericContainer->setGraphicsEffect(shadowEffect);

    // Container for numeric controls
    QVBoxLayout *controlLayout = new QVBoxLayout(numericContainer);
    controlLayout->setSpacing(15);                    // Reduced spacing to decrease padding between min/max and bottom
    controlLayout->setContentsMargins(25, 15, 25, 5); // Reduced bottom margin to decrease padding at bottom

    QHBoxLayout *numericLayout = new QHBoxLayout();
    numericLayout->setSpacing(25); // Increased spacing

    // Decrement button - make it larger with bigger text
    decrementBtn = new QPushButton("-");
    decrementBtn->setFixedSize(80, 80); // Increased from 60x60
    decrementBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #404040;  /* Lighter than container for contrast */
            border-radius: 40px; /* Increased to match larger size */
            color: white;
            font-size: 48px; /* Increased font size for +/- text */
            font-weight: bold;
            border: 1px solid #505050;  /* Subtle border */
        }
        QPushButton:hover {
            background-color: #454545;
            border: 1px solid #606060;
        }
        QPushButton:pressed {
            background-color: #505050;
            border: 1px solid #707070;
        }
        QPushButton:disabled {
            background-color: #252525;
            color: #666666;
            border: 1px solid #353535;
        }
    )");

    // Value display - make it larger
    valueLabel = new QLabel();
    valueLabel->setStyleSheet(R"(
        QLabel {
            font-size: 48px; /* Increased from 40px */
            color: #2196F3;
            min-width: 150px; /* Increased from 120px */
        }
        QLabel:disabled {
            color: #666666;
        }
    )");
    valueLabel->setAlignment(Qt::AlignCenter);

    // Increment button - make it larger
    incrementBtn = new QPushButton("+");
    incrementBtn->setFixedSize(80, 80); // Increased from 60x60
    incrementBtn->setStyleSheet(decrementBtn->styleSheet());

    numericLayout->addWidget(decrementBtn);
    numericLayout->addWidget(valueLabel);
    numericLayout->addWidget(incrementBtn);

    controlLayout->addLayout(numericLayout);

    // Min/Max labels - make them larger
    QHBoxLayout *labelsLayout = new QHBoxLayout();
    labelsLayout->setContentsMargins(0, 0, 0, 0); // Remove margins to reduce spacing
    minLabel = new QLabel(QString("Min: %1").arg(min));
    maxLabel = new QLabel(QString("Max: %1").arg(max));

    minLabel->setStyleSheet(R"(
        QLabel {
            color: #ff7c30;
            font-size: 34px; /* Increased font size */
        }
        QLabel:disabled {
            color: #666666;
        }
    )");

    maxLabel->setStyleSheet(R"(
        QLabel {
            color: #50d332;
            font-size: 34px; /* Increased font size */
        }
        QLabel:disabled {
            color: #666666;
        }
    )");

    labelsLayout->addWidget(minLabel);
    labelsLayout->addStretch();
    labelsLayout->addWidget(maxLabel);

    controlLayout->addLayout(labelsLayout);

    // Left side layout with numeric container
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->addWidget(numericContainer);
    // Increase the padding around the numeric container
    leftLayout->setContentsMargins(10, 10, 10, 10);
    layout->addLayout(leftLayout);

    // Right side - title and description with vertical centering
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->addStretch();

    titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(R"(
        QLabel {
            font-size: 40px;
            color: white;
            font-weight: 500;
        }
        QLabel:disabled {
            color: #666666;
        }
    )");
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc);
      descLabel->setStyleSheet(R"(
            QLabel {
                font-size: 32px;
                color: #AAAAAA;
            }
            QLabel:disabled {
                color: #444444;
            }
        )");
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }

    textLayout->addStretch();

    // Wrap textLayout in a container that takes full height
    QWidget *textContainer = new QWidget;
    textContainer->setLayout(textLayout);
    layout->addWidget(textContainer, 1);

    // Increase the minimum height of the whole control
    setMinimumHeight(200); // Increased from 150px in the base style

    // Setup button behavior
    decrementBtn->setAutoRepeat(true);
    incrementBtn->setAutoRepeat(true);

    // Initialize or validate numeric parameter
    ParamUtils::initializeNumericParam(paramName, min, max, isFloatType, div);

    connect(decrementBtn, &QPushButton::clicked, [this]() { updateValue(-inc); });
    connect(incrementBtn, &QPushButton::clicked, [this]() { updateValue(inc); });

    refresh();
  }

  void refresh() {
    if (isFloatType) {
      // Get the actual stored value (no division applied)
      double value = QString::fromStdString(params.get(paramName)).toDouble();

      // Determine number of decimal places based on division factor (for display formatting only)
      int decimals = 0;
      if (div > 1.0) {
        decimals = static_cast<int>(log10(div));
      }

      // Format the number with proper decimal places
      valueLabel->setText(QString::number(value, 'f', decimals));
    } else {
      // For integer values
      int value = QString::fromStdString(params.get(paramName)).toInt();
      valueLabel->setText(QString::number(value));
    }
  }

signals:
  void valueChanged();

private:
  void updateValue(double change) {
    // Get the current value
    double currentValue;
    if (isFloatType) {
      currentValue = QString::fromStdString(params.get(paramName)).toDouble();
    } else {
      currentValue = QString::fromStdString(params.get(paramName)).toInt();
    }

    // Calculate new value with appropriate bounds
    double newValue = std::clamp(currentValue + change, min, max);

    // Log parameter change
    ParamUtils::logNumericParamChange(paramName, currentValue, newValue, isFloatType, div);

    // Store the actual value
    if (isFloatType) {
      params.putFloat(paramName, newValue);
    } else {
      params.putInt(paramName, static_cast<int>(newValue));
    }

    refresh();
    emit valueChanged();
  }

  QLabel *titleLabel = nullptr;
  QLabel *descLabel = nullptr;
  QLabel *valueLabel = nullptr;
  QLabel *minLabel = nullptr;
  QLabel *maxLabel = nullptr;
  QPushButton *decrementBtn;
  QPushButton *incrementBtn;
  std::string paramName;
  double min, max, inc, div;
  bool isFloatType;
  Params params;
};

class BPCommandControl : public QFrame {
  Q_OBJECT

public:
  explicit BPCommandControl(const QString &title, const QString &desc, const QString &buttonText, const QString &command, const QString &workingDir = QString(),
                            bool requireConfirm = false, const QString &confirmText = QString(), const QString &confirmYesText = QString(),
                            const QString &confirmNoText = QString(), const QJsonArray &actionButtons = QJsonArray(), QWidget *parent = nullptr)
      : QFrame(parent), cmd(command), cmdTitle(title), cmdWorkingDir(workingDir), cmdRequireConfirm(requireConfirm), cmdConfirmText(confirmText), cmdConfirmYesText(confirmYesText),
        cmdConfirmNoText(confirmNoText), cmdActionButtons(actionButtons) {

    setStyleSheet(R"(
      BPCommandControl {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side: "Execute" button – vertically centered.
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    executeButton = new BPButton(buttonText.isEmpty() ? tr("EXECUTE") : buttonText, this);
    executeButton->setMinimumWidth(250);
    executeButton->setMinimumHeight(100);
    buttonLayout->addWidget(executeButton);
    // Center the button vertically.
    layout->addLayout(buttonLayout, 0);
    layout->setAlignment(buttonLayout, Qt::AlignVCenter);

    // Right side: Title & Description – remove extra stretch to prevent a huge gap.
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(10);
    titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);
    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet("font-size: 32px; color: #AAAAAA;");
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }
    // Do not add an extra stretch here.
    layout->addLayout(textLayout, 1);

    // Connect the button
    connect(executeButton, &BPButton::clicked, this, [=]() {
      emit commandRequested(cmd,      // the actual shell command
                            cmdTitle, // for the command dialog title
                            cmdWorkingDir, cmdActionButtons, cmdRequireConfirm, cmdConfirmText, cmdConfirmYesText, cmdConfirmNoText);
    });
  }

signals:
  // Emitted when the user clicks "execute". Parent handles confirm logic + launching the dialog.
  void commandRequested(const QString &command, const QString &title, const QString &workingDir, const QJsonArray &actionButtons, bool requireConfirm, const QString &confirmText,
                        const QString &confirmYesText, const QString &confirmNoText);

private:
  // UI
  BPButton *executeButton;
  QLabel *titleLabel{nullptr};
  QLabel *descLabel{nullptr};

  // Internals
  QString cmd;
  QString cmdTitle;
  QString cmdWorkingDir;
  bool cmdRequireConfirm;
  QString cmdConfirmText;
  QString cmdConfirmYesText;
  QString cmdConfirmNoText;
  QJsonArray cmdActionButtons;
};

class BPFileViewerControl : public QFrame {
  Q_OBJECT

public:
  // The 'path' is the relative file path to read. We also store 'title', 'desc', etc.
  explicit BPFileViewerControl(const QString &title, const QString &desc, const QString &path, const QString &header, QWidget *parent = nullptr)
      : QFrame(parent), filePath(path), fileHeader(header), ctrlTitle(title), ctrlDesc(desc) {

    setStyleSheet(R"(
      BPFileViewerControl {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side: "VIEW" button – ensure it is vertically centered.
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    viewButton = new BPButton(tr("VIEW"), this);
    viewButton->setMinimumWidth(250);
    viewButton->setMinimumHeight(100);
    buttonLayout->addWidget(viewButton);
    // Set the alignment on the layout so the button is centered vertically.
    layout->addLayout(buttonLayout, 0);
    layout->setAlignment(buttonLayout, Qt::AlignVCenter);

    // Right side: Title & Description
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(10);
    titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);
    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet("font-size: 32px; color: #AAAAAA;");
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }
    // Do not add an extra stretch here.
    layout->addLayout(textLayout, 1);

    // Connect button: opens a "BPFileViewerDialog"
    connect(viewButton, &BPButton::clicked, this, [=]() { emit fileViewRequested(filePath, fileHeader, ctrlTitle); });
  }

signals:
  // Emitted when user clicks "VIEW"
  void fileViewRequested(const QString &path, const QString &header, const QString &fallbackTitle);

private:
  QLabel *titleLabel{nullptr};
  QLabel *descLabel{nullptr};
  BPButton *viewButton;

  QString filePath;
  QString fileHeader;
  QString ctrlTitle;
  QString ctrlDesc;
};

class BPRecentChangesControl : public QFrame {
  Q_OBJECT

public:
  explicit BPRecentChangesControl(const QString &title, const QString &desc, QWidget *parent = nullptr)
      : QFrame(parent), ctrlTitle(title), ctrlDesc(desc) {

    setStyleSheet(R"(
      BPRecentChangesControl {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side: "VIEW" button – ensure it is vertically centered.
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    viewButton = new BPButton(tr("VIEW"), this);
    viewButton->setMinimumWidth(250);
    viewButton->setMinimumHeight(100);
    buttonLayout->addWidget(viewButton);
    // Set the alignment on the layout so the button is centered vertically.
    layout->addLayout(buttonLayout, 0);
    layout->setAlignment(buttonLayout, Qt::AlignVCenter);

    // Right side: Title & Description
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(10);
    titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);
    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet("font-size: 32px; color: #AAAAAA;");
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }
    // Do not add an extra stretch here.
    layout->addLayout(textLayout, 1);

    // Connect button: opens the "BPRecentChangesDialog"
    connect(viewButton, &BPButton::clicked, this, [=]() { emit recentChangesRequested(); });
  }

signals:
  // Emitted when user clicks "VIEW"
  void recentChangesRequested();

private:
  QLabel *titleLabel{nullptr};
  QLabel *descLabel{nullptr};
  BPButton *viewButton;

  QString ctrlTitle;
  QString ctrlDesc;
};

class BPParamViewerControl : public QFrame {
  Q_OBJECT

public:
  BPParamViewerControl(const QString &param, const QString &title, const QString &desc, QWidget *parent = nullptr) : QFrame(parent), paramName(param) {

    // Main layout with consistent margins/spacing
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side: VIEW button - vertically centered
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    buttonLayout->setSpacing(0);
    buttonLayout->setContentsMargins(0, 0, 0, 0);

    viewButton = new BPButton(tr("VIEW"), this);
    viewButton->setMinimumWidth(250);
    viewButton->setMinimumHeight(100);
    buttonLayout->addWidget(viewButton);

    // Center the button vertically
    layout->addLayout(buttonLayout, 0);
    layout->setAlignment(buttonLayout, Qt::AlignVCenter);

    // Right side: Title & Description
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(10);

    titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet("font-size: 32px; color: #AAAAAA;");
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }

    // Do not add extra stretch to text layout
    layout->addLayout(textLayout, 1);

    // Frame styling
    setStyleSheet(R"(
      BPParamViewerControl {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    connect(viewButton, &BPButton::clicked, this, &BPParamViewerControl::viewClicked);
  }

  void setEnabled(bool enabled) {
    viewButton->setEnabled(enabled);
    titleLabel->setStyleSheet(QString("font-size: 40px; color: %1; font-weight: 500;").arg(enabled ? "white" : "#666666"));
    if (descLabel) {
      descLabel->setStyleSheet(QString("font-size: 32px; color: %1;").arg(enabled ? "#AAAAAA" : "#444444"));
    }
  }

signals:
  void viewClicked();

private:
  BPButton *viewButton;
  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
  QString paramName;
};

class BPParamListViewerControl : public QFrame {
  Q_OBJECT

public:
  BPParamListViewerControl(const QString &title, const QString &desc, QWidget *parent = nullptr) : QFrame(parent) {

    // Main layout with consistent margins/spacing
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side: "VIEW ALL" button - vertically centered
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    viewButton = new BPButton(tr("VIEW ALL"), this);
    viewButton->setMinimumWidth(250);
    viewButton->setMinimumHeight(100);
    buttonLayout->addWidget(viewButton);

    // Center the button vertically
    layout->addLayout(buttonLayout, 0);
    layout->setAlignment(buttonLayout, Qt::AlignVCenter);

    // Right side: Title & Description
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(10);

    titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet("font-size: 32px; color: #AAAAAA;");
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }

    layout->addLayout(textLayout, 1);

    // Frame styling
    setStyleSheet(R"(
      BPParamListViewerControl {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    connect(viewButton, &BPButton::clicked, this, &BPParamListViewerControl::onViewClicked);
  }

  void setEnabled(bool enabled) {
    viewButton->setEnabled(enabled);
    titleLabel->setStyleSheet(QString("font-size: 40px; color: %1; font-weight: 500;").arg(enabled ? "white" : "#666666"));
    if (descLabel) {
      descLabel->setStyleSheet(QString("font-size: 32px; color: %1;").arg(enabled ? "#AAAAAA" : "#444444"));
    }
  }

signals:
  void viewClicked(); // Add this signal

private slots:
  void onViewClicked() { emit viewClicked(); }

private:
  BPButton *viewButton;
  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
};

class BPNestedControlsButton : public QFrame {
  Q_OBJECT

public:
  BPNestedControlsButton(const QString &title, const QString &desc, const QString &buttonText, const QString &icon = QString(), QWidget *parent = nullptr) : QFrame(parent) {

    setStyleSheet(R"(
      BPNestedControlsButton {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side: button
    openButton = new BPButton(buttonText.isEmpty() ? tr("OPEN") : buttonText, this);
    openButton->setMinimumWidth(250);
    openButton->setMinimumHeight(100);

    if (!icon.isEmpty()) {
      QPixmap pixmap(icon);
      if (!pixmap.isNull()) {
        QPixmap scaled = pixmap.scaledToHeight(40, Qt::SmoothTransformation);
        openButton->setIcon(QIcon(scaled));
        openButton->setIconSize(QSize(40, 40));

        // Combined base styling with icon spacing
        openButton->setStyleSheet(R"(
          BPButton {
            background-color: #363636;
            border-radius: 30px;
            font-size: 36px;
            font-weight: 500;
            color: white;
            padding: 15px 30px;
            padding-left: 15px;
            padding-right: 15px;
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
          BPButton::text {
            margin-left: 10px;
          }
        )");
      }
    }

    layout->addWidget(openButton);

    // Right side: title and description
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setSpacing(10);

    // Add stretch at top for vertical centering
    if (title.isEmpty() || desc.isEmpty()) {
      textLayout->addStretch();
    }

    if (!title.isEmpty()) {
      titleLabel = new QLabel(title);
      titleLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
      titleLabel->setWordWrap(true);
      textLayout->addWidget(titleLabel);
    }

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc);
      descLabel->setStyleSheet("font-size: 32px; color: #AAAAAA;");
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }

    // Add stretch at bottom for vertical centering
    if (title.isEmpty() || desc.isEmpty()) {
      textLayout->addStretch();
    }

    layout->addLayout(textLayout, 1);

    connect(openButton, &BPButton::clicked, this, &BPNestedControlsButton::clicked);
  }

  void setEnabled(bool enabled) {
    openButton->setEnabled(enabled);
    if (titleLabel) {
      titleLabel->setStyleSheet(QString("font-size: 40px; color: %1; font-weight: 500;").arg(enabled ? "white" : "#666666"));
    }
    if (descLabel) {
      descLabel->setStyleSheet(QString("font-size: 32px; color: %1;").arg(enabled ? "#AAAAAA" : "#444444"));
    }
  }

signals:
  void clicked();

private:
  BPButton *openButton;
  QLabel *titleLabel = nullptr;
  QLabel *descLabel = nullptr;
};
