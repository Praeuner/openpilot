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
#include <QTextStream>

#include "common/version.h"
#include <QScrollBar>
#include <QFrame>
#include <QPropertyAnimation>
#include <QStyle>
#include <QTimer>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#endif

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include "common/params.h"
#include "bp_nested_view.h"
#include "bp_panel_dialogs.h"
#include "cereal/gen/cpp/car.capnp.h"
#include <capnp/serialize.h>
#include <kj/array.h>

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

    // Extra-large touch-friendly scrollbar styling (32px wide, 120px min-height, 16px radius)
    QString style = R"(
      QScrollBar:vertical {
        width: 32px;
        background: transparent;
        margin: 0px;
        padding: 4px;
      }
      QScrollBar::handle:vertical {
        background: #666666;
        border-radius: 16px;
        min-height: 120px;
        margin: 0 6px;
      }
      QScrollBar::handle:vertical:hover {
        background: #888888;
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

    // Optimize for smooth kinetic scrolling on Comma 3X Wayland
    sp.setScrollMetric(QScrollerProperties::DragStartDistance, 0.005);
    sp.setScrollMetric(QScrollerProperties::DragVelocitySmoothingFactor, 0.6);
    sp.setScrollMetric(QScrollerProperties::MinimumVelocity, 0.15);
    sp.setScrollMetric(QScrollerProperties::MaximumVelocity, 2.5);
    sp.setScrollMetric(QScrollerProperties::AcceleratingFlickSpeedupFactor, 1.8);
    sp.setScrollMetric(QScrollerProperties::DecelerationFactor, 0.3);
    sp.setScrollMetric(QScrollerProperties::FrameRate, QScrollerProperties::Fps60);
    sp.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.5);
    sp.setScrollMetric(QScrollerProperties::OvershootScrollDistanceFactor, 0.2);
    sp.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, QVariant::fromValue<QScrollerProperties::OvershootPolicy>(QScrollerProperties::OvershootAlwaysOff));
    sp.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, QVariant::fromValue<QScrollerProperties::OvershootPolicy>(QScrollerProperties::OvershootAlwaysOff));
    sp.setScrollMetric(QScrollerProperties::MousePressEventDelay, 0.01);

    scroller->setScrollerProperties(sp);
    scroller->grabGesture(this->viewport(), QScroller::LeftMouseButtonGesture);
  }

protected:
  void hideEvent(QHideEvent *e) override { verticalScrollBar()->setValue(0); }
};

// Helper class for making labels clickable
class ClickableLabel : public QObject {
  Q_OBJECT
public:
  ClickableLabel(QObject *parent, std::function<void()> callback) : QObject(parent), onClick(callback) {}

protected:
  bool eventFilter(QObject *obj, QEvent *event) override {
    if (event->type() == QEvent::TouchBegin) {
      BPLog::bpInfo() << "[ClickableLabel] TouchBegin event received" << std::endl;
      touchActive = true;
      event->accept();
      return true;
    } else if (event->type() == QEvent::TouchEnd && touchActive) {
      BPLog::bpInfo() << "[ClickableLabel] TouchEnd event received - triggering onClick" << event << std::endl;
      touchActive = false;
      onClick();
      event->accept();
      return true;
    } else if (event->type() == QEvent::TouchCancel) {
      BPLog::bpInfo() << "[ClickableLabel] TouchCancel event received" << event << touchActive << std::endl;
      touchActive = false;
      event->accept();
      return true;
    } else if (event->type() == QEvent::MouseButtonPress) {
      BPLog::bpInfo() << "[ClickableLabel] MouseButtonPress event received" << event << touchActive << std::endl;
      event->accept();
      return true;
    } else if (event->type() == QEvent::MouseButtonRelease) {
      BPLog::bpInfo() << "[ClickableLabel] MouseButtonRelease event received - triggering onClick" << event << touchActive << std::endl;
      onClick();
      event->accept();
      return true;
    }
    return QObject::eventFilter(obj, event);
  }

private:
  std::function<void()> onClick;
  bool touchActive = false;
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

  // Set custom colors for the toggle based on state
  void setStateColors(const QString &checkedColor, const QString &uncheckedColor) {
    colorWhenChecked = checkedColor;
    colorWhenUnchecked = uncheckedColor;
    useCustomColors = true;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw background
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, width(), height()), height() / 2, height() / 2);

    // Use custom colors if set, otherwise use defaults
    QColor bgColor;
    if (useCustomColors) {
      bgColor = isChecked() ? QColor(colorWhenChecked) : QColor(colorWhenUnchecked);
    } else {
      bgColor = isChecked() ? QColor("#2196F3") : QColor("#808080");
    }

    painter.fillPath(path, bgColor);

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
  bool useCustomColors = false;
  QString colorWhenChecked;
  QString colorWhenUnchecked;
};

// Parameter toggle button - button that toggles a boolean parameter with dynamic styling
class BPParamToggleButton : public QFrame {
  Q_OBJECT

public:
  BPParamToggleButton(const QString &param, const QString &title, const QString &desc,
                      const QString &buttonText, QWidget *parent = nullptr)
      : QFrame(parent), paramName(param.toStdString()), defaultButtonText(buttonText) {

    QVBoxLayout *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(25, 25, 25, 25);
    main_layout->setSpacing(15);

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

    // Title
    titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(QString(R"(
      QLabel {
        font-size: %1px;
        color: white;
        font-weight: 500;
      }
    )").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);
    main_layout->addWidget(titleLabel);

    // Description
    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc);
      descLabel->setStyleSheet(QString(R"(
        QLabel {
          font-size: %1px;
          color: #AAAAAA;
        }
      )").arg(sizes.descSize));
      descLabel->setWordWrap(true);
      main_layout->addWidget(descLabel);
    }

    // Button
    button = new QPushButton(buttonText);
    button->setMinimumHeight(150);
    button->setStyleSheet(QString(R"(
      QPushButton {
        border-radius: 20px;
        font-size: %1px;
        font-weight: 450;
        padding: 0 25px;
        color: #FFFFFF;
        background-color: #393939;
      }
      QPushButton:pressed {
        background-color: #4A4A4A;
      }
    )").arg(sizes.buttonTextSize));
    main_layout->addWidget(button);

    setStyleSheet(R"(
      BPParamToggleButton {
        background-color: #242424;
        border-radius: 10px;
      }
    )");

    // Initialize param
    ParamUtils::initializeParam(paramName);

    QObject::connect(button, &QPushButton::clicked, [this]() {
      bool currentValue = params.getBool(paramName);
      bool newValue = !currentValue;

      params.putBool(paramName, newValue);
      ParamUtils::logParamChange(paramName, currentValue ? "On" : "Off", newValue ? "On" : "Off");

      updateDynamicState();
      emit valueChanged(newValue);
    });

    updateDynamicState();
  }

  void refresh() {
    updateDynamicState();
  }

  void setDescription(const QString &desc) {
    if (descLabel) {
      descLabel->setText(desc);
    }
  }

  // Enable dynamic button text based on parameter state
  void enableDynamicButtonText(const QString &enabledText, const QString &disabledText) {
    dynamicTextEnabled = true;
    buttonTextWhenEnabled = enabledText;
    buttonTextWhenDisabled = disabledText;
    updateDynamicState();
  }

  // Enable dynamic styling based on parameter state
  void enableDynamicStyling(const QString &bgColorEnabled, const QString &bgColorDisabled,
                            const QString &bgColorEnabledPressed = QString(),
                            const QString &bgColorDisabledPressed = QString(),
                            const QString &textColor = "#FFFFFF") {
    dynamicStylingEnabled = true;
    bgColorWhenEnabled = bgColorEnabled;
    bgColorWhenDisabled = bgColorDisabled;
    bgColorWhenEnabledPressed = bgColorEnabledPressed.isEmpty() ? bgColorEnabled : bgColorEnabledPressed;
    bgColorWhenDisabledPressed = bgColorDisabledPressed.isEmpty() ? bgColorDisabled : bgColorDisabledPressed;
    textColorDynamic = textColor;
    updateDynamicState();
  }

  // Set different confirmation texts for on/off
  void setConfirmationTexts(const QString &confirmOn, const QString &confirmOff,
                            const QString &confirmYes = "Confirm", const QString &confirmNo = "Cancel") {
    confirmTextOn = confirmOn;
    confirmTextOff = confirmOff;
    confirmYesText = confirmYes;
    confirmNoText = confirmNo;
    requireConfirmation = true;
  }

  void setDisabledReasons(const QStringList &reasons) {
    disabledReasons = reasons;
    if (disabledReasons.isEmpty()) {
      if (disabledReasonLabel) {
        disabledReasonLabel->setVisible(false);
      }
    } else {
      if (!disabledReasonLabel) {
        createDisabledReasonUI();
      }
      disabledReasonLabel->setVisible(true);
      QString reasonText = "• " + disabledReasons.join("\n• ");
      disabledReasonLabel->setText(reasonText);
    }
  }

signals:
  void valueChanged(bool state);

protected:
  void showEvent(QShowEvent *event) override {
    updateDynamicState();
    QFrame::showEvent(event);
  }

private:
  void createDisabledReasonUI() {
    // Create disabled reason label showing reasons inline (hidden by default)
    BPTextSizes sizes = BPTextSizes::getSizes();
    disabledReasonLabel = new QLabel();
    disabledReasonLabel->setStyleSheet(QString(R"(
        QLabel {
            font-size: %1px;
            color: #FF9800;
            font-weight: 450;
            padding: 10px 15px;
            background-color: transparent;
            border-radius: 8px;
            border: 1px solid rgba(255, 152, 0, 0.3);
        }
    )").arg(sizes.reasonLabelSize));
    disabledReasonLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    disabledReasonLabel->setVisible(false);
    disabledReasonLabel->setWordWrap(false);
    disabledReasonLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // Add to main layout (QVBoxLayout is set in constructor) - align right
    QLayout *layoutPtr = layout();
    if (layoutPtr) {
      layoutPtr->addWidget(disabledReasonLabel);
      layoutPtr->setAlignment(disabledReasonLabel, Qt::AlignRight);
    }
  }

  void updateDynamicState() {
    bool paramValue = params.getBool(paramName);
    BPTextSizes sizes = BPTextSizes::getSizes();

    // Update button text
    if (dynamicTextEnabled) {
      button->setText(paramValue ? buttonTextWhenEnabled : buttonTextWhenDisabled);
    }

    // Update styling
    if (dynamicStylingEnabled) {
      QString bgColor = paramValue ? bgColorWhenEnabled : bgColorWhenDisabled;
      QString bgColorPressed = paramValue ? bgColorWhenEnabledPressed : bgColorWhenDisabledPressed;

      button->setStyleSheet(QString(R"(
        QPushButton {
          border-radius: 20px;
          font-size: %4px;
          font-weight: 450;
          padding: 0 25px;
          color: %1;
          background-color: %2;
        }
        QPushButton:pressed {
          background-color: %3;
        }
      )").arg(textColorDynamic, bgColor, bgColorPressed).arg(sizes.buttonTextSize));
    }
  }

  QPushButton *button;
  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
  QLabel *disabledReasonLabel = nullptr;
  QStringList disabledReasons;
  std::string paramName;
  Params params;
  QString defaultButtonText;

  // Dynamic text support
  bool dynamicTextEnabled = false;
  QString buttonTextWhenEnabled;
  QString buttonTextWhenDisabled;

  // Dynamic styling support
  bool dynamicStylingEnabled = false;
  QString bgColorWhenEnabled;
  QString bgColorWhenDisabled;
  QString bgColorWhenEnabledPressed;
  QString bgColorWhenDisabledPressed;
  QString textColorDynamic = "#FFFFFF";

  // Confirmation support
  bool requireConfirmation = false;
  QString confirmTextOn;
  QString confirmTextOff;
  QString confirmYesText = "Confirm";
  QString confirmNoText = "Cancel";
};

class BPToggleControl : public QFrame {
  Q_OBJECT

public:
  BPToggleControl(const QString &param, const QString &title, const QString &desc, QWidget *parent = nullptr) : QFrame(parent), paramName(param.toStdString()) {

    QHBoxLayout *main_layout = new QHBoxLayout();
    main_layout->setContentsMargins(25, 25, 25, 25); // Increased margins
    main_layout->setSpacing(50);                     // Increased spacing
    setLayout(main_layout);

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

    // Left side - toggle
    toggle = new BPToggle();
    main_layout->addWidget(toggle, 0, Qt::AlignLeft | Qt::AlignVCenter);

    // Right side content
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setSpacing(10); // Increased spacing

    titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(QString(R"(
        QLabel {
            font-size: %1px;
            color: white;
            font-weight: 500;
        }
        QLabel:disabled {
            color: #666666;
        }
    )").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc);
      descLabel->setStyleSheet(QString(R"(
          QLabel {
              font-size: %1px;
              color: #AAAAAA;
          }
          QLabel:disabled {
              color: #444444;
          }
      )").arg(sizes.descSize));
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

        // Update dynamic title and styling after parameter change
        updateDynamicTitle();
        updateDynamicStyling();

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

  void setDescription(const QString &desc) {
    if (descLabel) {
      descLabel->setText(desc);
    }
  }

  QString getDescription() const {
    if (descLabel) {
      return descLabel->text();
    }
    return QString();
  }

  void setTitle(const QString &title) {
    if (titleLabel) {
      titleLabel->setText(title);
    }
  }

  void setDisabledReasons(const QStringList &reasons) {
    disabledReasons = reasons;
    if (disabledReasons.isEmpty()) {
      if (disabledReasonLabel) {
        disabledReasonLabel->setVisible(false);
      }
    } else {
      if (!disabledReasonLabel) {
        createDisabledReasonUI();
      }
      disabledReasonLabel->setVisible(true);
      QString reasonText = "• " + disabledReasons.join("\n• ");
      disabledReasonLabel->setText(reasonText);
    }
  }

  // Enable dynamic title updates based on parameter state
  void enableDynamicTitle(const QString &enabledTitle, const QString &disabledTitle) {
    dynamicTitleEnabled = true;
    titleWhenEnabled = enabledTitle;
    titleWhenDisabled = disabledTitle;
    updateDynamicTitle();
  }

  // Enable dynamic styling based on parameter state
  void enableDynamicStyling(const QString &bgColorEnabled, const QString &bgColorDisabled,
                            const QString &bgColorEnabledPressed = QString(),
                            const QString &bgColorDisabledPressed = QString(),
                            const QString &textColor = "#FFFFFF") {
    dynamicStylingEnabled = true;
    bgColorWhenEnabled = bgColorEnabled;
    bgColorWhenDisabled = bgColorDisabled;
    bgColorWhenEnabledPressed = bgColorEnabledPressed.isEmpty() ? bgColorEnabled : bgColorEnabledPressed;
    bgColorWhenDisabledPressed = bgColorDisabledPressed.isEmpty() ? bgColorDisabled : bgColorDisabledPressed;
    textColorDynamic = textColor;
    updateDynamicStyling();
  }

  void updateDynamicTitle() {
    if (!dynamicTitleEnabled) return;
    bool paramValue = params.getBool(paramName);
    setTitle(paramValue ? titleWhenEnabled : titleWhenDisabled);
  }

  void updateDynamicStyling() {
    if (!dynamicStylingEnabled) return;

    // Apply colors to the toggle switch itself
    // When checked (enabled), use "enabled" color
    // When unchecked (disabled), use "disabled" color
    toggle->setStateColors(bgColorWhenEnabled, bgColorWhenDisabled);
  }

signals:
  void toggleFlipped(bool state);

protected:
  void showEvent(QShowEvent *event) override {
    refresh();
    updateDynamicTitle();
    updateDynamicStyling();
    QFrame::showEvent(event);
  }

private:
  void createDisabledReasonUI() {
    // Create disabled reason label showing reasons inline (hidden by default)
    disabledReasonLabel = new QLabel();
    disabledReasonLabel->setStyleSheet(R"(
        QLabel {
            font-size: 28px;
            color: #FF9800;
            font-weight: 450;
            padding: 10px 15px;
            background-color: transparent;
            border-radius: 8px;
            border: 1px solid rgba(255, 152, 0, 0.3);
        }
    )");
    disabledReasonLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    disabledReasonLabel->setVisible(false);
    disabledReasonLabel->setWordWrap(false);
    disabledReasonLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // Add to layout - get the text layout which is the second item in the main HBoxLayout
    QHBoxLayout *mainLayout = qobject_cast<QHBoxLayout*>(layout());
    if (mainLayout && mainLayout->count() > 1) {
      QLayoutItem *item = mainLayout->itemAt(1);
      if (item && item->layout()) {
        QVBoxLayout *textLayout = qobject_cast<QVBoxLayout*>(item->layout());
        if (textLayout) {
          textLayout->addWidget(disabledReasonLabel, 0, Qt::AlignRight);
        }
      }
    }
  }

  BPToggle *toggle;
  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
  QLabel *disabledReasonLabel = nullptr;
  QStringList disabledReasons;
  std::string paramName;
  Params params;

  // Dynamic title support
  bool dynamicTitleEnabled = false;
  QString titleWhenEnabled;
  QString titleWhenDisabled;

  // Dynamic styling support
  bool dynamicStylingEnabled = false;
  QString bgColorWhenEnabled;
  QString bgColorWhenDisabled;
  QString bgColorWhenEnabledPressed;
  QString bgColorWhenDisabledPressed;
  QString textColorDynamic = "#FFFFFF";
};

class BPSelectionControl : public QFrame {
  Q_OBJECT

public:
  BPSelectionControl(const QString &param, const QString &title, const QString &desc, QWidget *parent = nullptr, bool hideDesc = false) : QFrame(parent), paramName(param.toStdString()), defaultDesc(desc), hideDescription(hideDesc) {

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

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
    titleLabel->setStyleSheet(QString(R"(
        QLabel {
            font-size: %1px;
            color: white;
            font-weight: 500;
        }
        QLabel:disabled {
            color: #666666;
        }
    )").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);
    rightLayout->addWidget(titleLabel);

    // Selected value label: shows selected value (blue) if present; otherwise, default description (gray)
    selectedValueLabel = new QLabel(this);
    selectedValueLabel->setWordWrap(true);
    selectedValueLabel->setText(defaultDesc);
    selectedValueLabel->setStyleSheet(QString(R"(
        QLabel {
            font-size: %1px;
            color: #AAAAAA;
        }
        QLabel:disabled {
            color: #666666;
        }
    )").arg(sizes.descSize));
    rightLayout->addWidget(selectedValueLabel);

    // Description label: shows white description text below the selected value (unless hidden)
    if (!hideDescription) {
      descLabel = new QLabel(defaultDesc, this);
      descLabel->setWordWrap(true);
      descLabel->setStyleSheet(QString(R"(
          QLabel {
              font-size: %1px;
              color: #AAAAAA;
          }
          QLabel:disabled {
              color: #444444;
          }
      )").arg(sizes.descSize));
      rightLayout->addWidget(descLabel);
    } else {
      descLabel = nullptr;
    }
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

  // When a value is selected, call this to update the selected value label with the display name.
  // The description label shows the description text below only when a value is selected.
  void setSelectedValue(const QString &value) {
    if (!value.isEmpty() && options.contains(value)) {
      // Show selected value in blue
      selectedValueLabel->setText(options[value]);
      selectedValueLabel->setStyleSheet(R"(
        QLabel {
          font-size: 32px;
          font-weight: 500;
          color: #2196F3;
        }
        QLabel:disabled {
          color: #555555;
        }
      )");
      // Show description below when value is selected
      if (descLabel) {
        descLabel->setText(defaultDesc);
        descLabel->setVisible(true);
      }
    } else if (!value.isEmpty()) {
      // Fallback: show the raw value if no mapping found
      selectedValueLabel->setText(value);
      selectedValueLabel->setStyleSheet(R"(
        QLabel {
          font-size: 32px;
          font-weight: 500;
          color: #2196F3;
        }
        QLabel:disabled {
          color: #555555;
        }
      )");
      // Show description below when value is selected
      if (descLabel) {
        descLabel->setText(defaultDesc);
        descLabel->setVisible(true);
      }
    } else {
      // No value selected - show placeholder text and hide description to avoid duplication
      selectedValueLabel->setText(defaultDesc.isEmpty() ? "Select a value" : defaultDesc);
      selectedValueLabel->setStyleSheet(R"(
        QLabel {
          font-size: 32px;
          color: #AAAAAA;
        }
        QLabel:disabled {
          color: #444444;
        }
      )");
      // Hide description when no value is selected to avoid duplication
      if (descLabel) {
        descLabel->setVisible(false);
      }
    }
  }

  void setDescription(const QString &desc) {
    defaultDesc = desc;
    if (descLabel) {
      descLabel->setText(desc);
    }
    // If no value selected, update selectedValueLabel too
    std::string value = Params().get(paramName);
    if (value.empty()) {
      selectedValueLabel->setText(desc);
    }
  }

  void setDisabledReasons(const QStringList &reasons) {
    disabledReasons = reasons;
    if (disabledReasons.isEmpty()) {
      if (disabledReasonLabel) {
        disabledReasonLabel->setVisible(false);
      }
    } else {
      if (!disabledReasonLabel) {
        createDisabledReasonUI();
      }
      disabledReasonLabel->setVisible(true);
      QString reasonText = "• " + disabledReasons.join("\n• ");
      disabledReasonLabel->setText(reasonText);
    }
  }

signals:
  void clicked();

private:
  void createDisabledReasonUI() {
    // Create disabled reason label showing reasons inline (hidden by default)
    disabledReasonLabel = new QLabel();
    disabledReasonLabel->setStyleSheet(R"(
        QLabel {
            font-size: 28px;
            color: #FF9800;
            font-weight: 450;
            padding: 10px 15px;
            background-color: transparent;
            border-radius: 8px;
            border: 1px solid rgba(255, 152, 0, 0.3);
        }
    )");
    disabledReasonLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    disabledReasonLabel->setVisible(false);
    disabledReasonLabel->setWordWrap(false);
    disabledReasonLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // Add to text layout (right side) - get the text layout which is the second item in the main HBoxLayout
    QHBoxLayout *mainLayout = qobject_cast<QHBoxLayout*>(layout());
    if (mainLayout && mainLayout->count() > 1) {
      QLayoutItem *item = mainLayout->itemAt(1);
      if (item && item->layout()) {
        QVBoxLayout *textLayout = qobject_cast<QVBoxLayout*>(item->layout());
        if (textLayout) {
          textLayout->insertWidget(textLayout->count() - 1, disabledReasonLabel, 0, Qt::AlignRight);
        }
      }
    }
  }

  BPButton *selectButton;
  QLabel *titleLabel;
  QLabel *selectedValueLabel;
  QLabel *descLabel;
  QLabel *disabledReasonLabel = nullptr;
  QStringList disabledReasons;
  std::string paramName;
  QString defaultDesc;
  bool hideDescription;
  QMap<QString, QString> options; // Map from value to display name
};

class BPSegmentedControl : public QFrame {
  Q_OBJECT

public:
  BPSegmentedControl(const QString &param, const QString &title, const QString &desc, const QVector<QPair<QString, QString>> &options, const QString &defaultValue = QString(),
                     QWidget *parent = nullptr, const QVector<QString> &descList = QVector<QString>(), bool showDescBottom = false)
      : QFrame(parent), paramName(param.toStdString()), optionDescriptions(descList), useDescriptionList(!descList.isEmpty()), useBottomDescLayout(showDescBottom) {

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

    // Use vertical layout when showDescBottom is true, horizontal otherwise
    QVBoxLayout *mainLayout = nullptr;
    QHBoxLayout *layout = nullptr;

    if (useBottomDescLayout) {
      mainLayout = new QVBoxLayout(this);
      mainLayout->setContentsMargins(25, 25, 25, 25);
      mainLayout->setSpacing(15);
    } else {
      layout = new QHBoxLayout(this);
      layout->setContentsMargins(25, 25, 25, 25);
      layout->setSpacing(50);
    }

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

      QString btnStyle = QString(R"(
        QPushButton {
          background-color: #363636;
          border: 1px solid #404040;
          border-right: 1px solid #505050;  /* Lighter right border */
          color: white;
          font-size: %1px;
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
      )").arg(sizes.segmentedButtonSize);

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

    // Title and description setup
    titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(QString(R"(
        QLabel {
            font-size: %1px;
            color: white;
            font-weight: 500;
        }
        QLabel:disabled {
            color: #666666;
        }
    )").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);

    // Create descLabel if we have a description OR if we'll be using description lists
    if (!desc.isEmpty() || !descList.isEmpty()) {
      descLabel = new QLabel(desc);
      descLabel->setStyleSheet(QString(R"(
          QLabel {
              font-size: %1px;
              color: #AAAAAA;
          }
          QLabel:disabled {
              color: #555555;
          }
      )").arg(sizes.descSize));
      descLabel->setWordWrap(true);
      descLabel->setTextFormat(Qt::RichText);
    }

    // Create disabled reason label showing reasons inline (hidden by default)
    disabledReasonLabel = new QLabel();
    disabledReasonLabel->setStyleSheet(QString(R"(
        QLabel {
            font-size: %1px;
            color: #FF9800;
            font-weight: 450;
            padding: 10px 15px;
            background-color: transparent;
            border-radius: 8px;
            border: 1px solid rgba(255, 152, 0, 0.3);
        }
    )").arg(sizes.reasonLabelSize));
    disabledReasonLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    disabledReasonLabel->setVisible(false);
    disabledReasonLabel->setWordWrap(false);
    disabledReasonLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // Layout assembly based on showDescBottom
    if (useBottomDescLayout) {
      // Vertical layout: control at top, title below buttons, desc, then disabled info at bottom
      // Wrap control in horizontal layout with stretch to prevent unnecessary expansion
      QHBoxLayout *controlWrapper = new QHBoxLayout();
      controlWrapper->addLayout(controlLayout);
      controlWrapper->addStretch();
      mainLayout->addLayout(controlWrapper);

      // Add spacing between segmented control and title
      mainLayout->addSpacing(20);

      // Add title below the buttons
      mainLayout->addWidget(titleLabel);

      // Add description if we have one
      if (descLabel) {
        mainLayout->addWidget(descLabel);
      }

      // Add disabled reason on its own row at the bottom - align right
      mainLayout->addWidget(disabledReasonLabel, 0, Qt::AlignRight);
    } else {
      // Horizontal layout: control left, title/desc right (original behavior)
      layout->addLayout(controlLayout);

      QVBoxLayout *textLayout = new QVBoxLayout();
      textLayout->setSpacing(10);
      textLayout->addWidget(titleLabel);

      // Add description if we have one
      if (descLabel) {
        textLayout->addWidget(descLabel);
      }

      textLayout->addStretch();

      // Add disabled reason on its own row at the bottom - align right
      textLayout->addWidget(disabledReasonLabel, 0, Qt::AlignRight);

      layout->addLayout(textLayout, 1);
    }

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
      BPLog::bpInfo() << "[bp.segmented.control] Using existing parameter - " << paramName << ": " << existingValue << std::endl;
    } else if (!defaultValue.isEmpty()) {
      // Only set default if param doesn't exist yet
      params.put(paramName, defaultValue.toStdString());
      BPLog::bpInfo() << "[bp.segmented.control] Parameter initialized - " << paramName << ": " << defaultValue.toStdString() << " (from constructor default)" << std::endl;
    }

    // Handle button clicks
    connect(buttonGroup, QOverload<int>::of(&QButtonGroup::buttonClicked), [this](int id) {
      std::string oldValue = params.get(paramName);
      std::string newValue = optionMap[id].toStdString();
      params.put(paramName, newValue);
      ParamUtils::logParamChange(paramName, oldValue, newValue);
      updateDescriptionList(id);
      emit valueChanged();
    });

    refresh();

    // Initialize description list if provided
    if (useDescriptionList) {
      int currentIndex = 0;
      QString currentValue = QString::fromStdString(params.get(paramName));
      for (auto it = optionMap.begin(); it != optionMap.end(); ++it) {
        if (it.value() == currentValue) {
          currentIndex = it.key();
          break;
        }
      }
      updateDescriptionList(currentIndex);
      descLabel->setTextFormat(Qt::RichText);
    }
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

  void setDescription(const QString &desc) {
    if (descLabel) {
      descLabel->setText(desc);
    }
  }

  void updateDescriptionList(int selectedIndex, bool forceDisabled = false) {
    if (!useDescriptionList || !descLabel || optionDescriptions.isEmpty()) {
      return;
    }

    // Check if control is enabled to determine text colors
    bool widgetEnabled = isEnabled();
    bool actuallyEnabled = !forceDisabled && widgetEnabled;
    QString selectedColor = actuallyEnabled ? "white" : "#555555";
    QString unselectedColor = actuallyEnabled ? "#AAAAAA" : "#444444";

    BPLog::bpDebugGeneral() << "[BPSegmentedControl]" << QString::fromStdString(paramName).toStdString()
             << "updateDescriptionList - widgetEnabled:" << widgetEnabled
             << "forceDisabled:" << forceDisabled
             << "actuallyEnabled:" << actuallyEnabled
             << "selectedColor:" << selectedColor.toStdString()
             << std::endl;

    QStringList formattedDescriptions;
    for (int i = 0; i < optionDescriptions.size(); i++) {
      QString desc = optionDescriptions[i];
      if (i == selectedIndex) {
        // Highlight selected option with appropriate color based on enabled state
        formattedDescriptions.append(QString("<font color='%1'><b>⦿ %2</b></font>").arg(selectedColor, desc));
      } else {
        // Gray out non-selected options with appropriate color based on enabled state
        formattedDescriptions.append(QString("<font color='%1'>⦿ %2</font>").arg(unselectedColor, desc));
      }
    }

    QString formattedText = formattedDescriptions.join("<br>");
    descLabel->setText(formattedText);
  }

  void setButtonEnabled(int buttonIndex, bool enabled) {
    QPushButton *btn = qobject_cast<QPushButton*>(buttonGroup->button(buttonIndex));
    if (btn) {
      btn->setEnabled(enabled);
    }
  }

  void updateButtonStates(const QVector<bool> &enabledStates) {
    for (int i = 0; i < enabledStates.size() && i < buttonGroup->buttons().size(); i++) {
      setButtonEnabled(i, enabledStates[i]);
    }
  }

  void setDisabledReasons(const QStringList &reasons) {
    disabledReasons = reasons;
    if (disabledReasons.isEmpty()) {
      if (disabledReasonLabel) {
        disabledReasonLabel->setVisible(false);
      }
    } else {
      disabledReasonLabel->setVisible(true);
      QString reasonText = "• " + disabledReasons.join("\n• ");
      disabledReasonLabel->setText(reasonText);
    }
  }

signals:
  void valueChanged();

private:
  QButtonGroup *buttonGroup;
  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
  QLabel *disabledReasonLabel = nullptr;
  QStringList disabledReasons;
  std::string paramName;
  QMap<int, QString> optionMap;
  Params params;
  QVector<QString> optionDescriptions;
  bool useDescriptionList = false;
  bool useBottomDescLayout = false;
};

class BPNumericControl : public QFrame {
  Q_OBJECT

public:
  BPNumericControl(const QString &param, const QString &title, const QString &desc, double minValue, double maxValue, double increment, bool isFloat = false, double division = 1.0,
                   QWidget *parent = nullptr)
      : QFrame(parent), paramName(param.toStdString()), min(minValue), max(maxValue), inc(increment), isFloatType(isFloat), div(division) {

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

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
    valueLabel->setStyleSheet(QString(R"(
        QLabel {
            font-size: %1px;
            color: #2196F3;
            min-width: 150px; /* Increased from 120px */
        }
        QLabel:disabled {
            color: #666666;
        }
    )").arg(sizes.valueDisplaySize));
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
    titleLabel->setStyleSheet(QString(R"(
        QLabel {
            font-size: %1px;
            color: white;
            font-weight: 500;
        }
        QLabel:disabled {
            color: #666666;
        }
    )").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc);
      descLabel->setStyleSheet(QString(R"(
            QLabel {
                font-size: %1px;
                color: #AAAAAA;
            }
            QLabel:disabled {
                color: #444444;
            }
        )").arg(sizes.descSize));
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

  void setDescription(const QString &desc) {
    if (descLabel) {
      descLabel->setText(desc);
    }
  }

  void setDisabledReasons(const QStringList &reasons) {
    disabledReasons = reasons;
    if (disabledReasons.isEmpty()) {
      if (disabledReasonLabel) {
        disabledReasonLabel->setVisible(false);
      }
    } else {
      if (!disabledReasonLabel) {
        createDisabledReasonUI();
      }
      disabledReasonLabel->setVisible(true);
      QString reasonText = "• " + disabledReasons.join("\n• ");
      disabledReasonLabel->setText(reasonText);
    }
  }

signals:
  void valueChanged();

private:
  void createDisabledReasonUI() {
    // Create disabled reason label showing reasons inline (hidden by default)
    disabledReasonLabel = new QLabel();
    disabledReasonLabel->setStyleSheet(R"(
        QLabel {
            font-size: 28px;
            color: #FF9800;
            font-weight: 450;
            padding: 10px 15px;
            background-color: transparent;
            border-radius: 8px;
            border: 1px solid rgba(255, 152, 0, 0.3);
        }
    )");
    disabledReasonLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    disabledReasonLabel->setVisible(false);
    disabledReasonLabel->setWordWrap(false);
    disabledReasonLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // Add to text layout (right side) - get the text layout which is the second item in the main HBoxLayout
    QHBoxLayout *mainLayout = qobject_cast<QHBoxLayout*>(layout());
    if (mainLayout && mainLayout->count() > 1) {
      QLayoutItem *item = mainLayout->itemAt(1);
      if (item && item->layout()) {
        QVBoxLayout *textLayout = qobject_cast<QVBoxLayout*>(item->layout());
        if (textLayout) {
          textLayout->insertWidget(textLayout->count() - 1, disabledReasonLabel, 0, Qt::AlignRight);
        }
      }
    }
  }

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
  QLabel *disabledReasonLabel = nullptr;
  QStringList disabledReasons;
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
  explicit BPCommandControl(const QString &title, const QString &desc, const QString &buttonText, const QString &command, const QString &action = QString(),
                            const QJsonObject &actionData = QJsonObject(), const QString &workingDir = QString(),
                            bool requireConfirm = false, const QString &confirmText = QString(), const QString &confirmYesText = QString(),
                            const QString &confirmNoText = QString(), const QJsonArray &actionButtons = QJsonArray(), QWidget *parent = nullptr)
      : QFrame(parent), cmd(command), actionName(action), actionConfig(actionData), cmdTitle(title), cmdWorkingDir(workingDir), cmdRequireConfirm(requireConfirm), cmdConfirmText(confirmText), cmdConfirmYesText(confirmYesText),
        cmdConfirmNoText(confirmNoText), cmdActionButtons(actionButtons) {

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

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
    titleLabel->setStyleSheet(QString(R"(
        QLabel {
            font-size: %1px;
            color: white;
            font-weight: 500;
        }
        QLabel:disabled {
            color: #666666;
        }
    )").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);
    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet(QString(R"(
          QLabel {
              font-size: %1px;
              color: #AAAAAA;
          }
          QLabel:disabled {
              color: #444444;
          }
      )").arg(sizes.descSize));
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }
    // Do not add an extra stretch here.
    layout->addLayout(textLayout, 1);

    // Connect the button
    connect(executeButton, &BPButton::clicked, this, [=]() {
      // If action is defined, use action handler; otherwise use command handler
      if (!actionName.isEmpty()) {
        emit actionRequested(actionName, actionConfig);
      } else {
        emit commandRequested(cmd,      // the actual shell command
                              cmdTitle, // for the command dialog title
                              cmdWorkingDir, cmdActionButtons, cmdRequireConfirm, cmdConfirmText, cmdConfirmYesText, cmdConfirmNoText);
      }
    });
  }

  // Set custom button color (preserves existing BPButton styling)
  void setButtonStyle(const QString &bgColor, const QString &bgColorPressed, const QString &textColor) {
    BPTextSizes sizes = BPTextSizes::getSizes();
    QString styleSheet = QString(R"(
      QPushButton {
        background-color: %1;
        border: none;
        border-radius: 40px;
        color: %2;
        font-size: %4px;
        font-weight: 500;
        padding: 15px 30px;
      }
      QPushButton:hover {
        background-color: %1;
      }
      QPushButton:pressed {
        background-color: %3;
      }
      QPushButton:disabled {
        background-color: #202020;
        color: #666666;
      }
    )").arg(bgColor, textColor, bgColorPressed).arg(sizes.descSize);

    executeButton->setStyleSheet(styleSheet);
  }

  // Override setEnabled to propagate disabled state to labels for proper styling
  void setEnabled(bool enabled) {
    QFrame::setEnabled(enabled);
    executeButton->setEnabled(enabled);
    titleLabel->setEnabled(enabled);
    if (descLabel) {
      descLabel->setEnabled(enabled);
    }
  }

signals:
  // Emitted when the user clicks "execute". Parent handles confirm logic + launching the dialog.
  void commandRequested(const QString &command, const QString &title, const QString &workingDir, const QJsonArray &actionButtons, bool requireConfirm, const QString &confirmText,
                        const QString &confirmYesText, const QString &confirmNoText);

  // Emitted when the button is configured to use action handler
  void actionRequested(const QString &action, const QJsonObject &actionData);

private:
  // UI
  BPButton *executeButton;
  QLabel *titleLabel{nullptr};
  QLabel *descLabel{nullptr};

  // Internals
  QString cmd;
  QString actionName;
  QJsonObject actionConfig;
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

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

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
    titleLabel->setStyleSheet(QString("font-size: %1px; color: white; font-weight: 500;").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);
    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet(QString("font-size: %1px; color: #AAAAAA;").arg(sizes.descSize));
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

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

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
    titleLabel->setStyleSheet(QString("font-size: %1px; color: white; font-weight: 500;").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet(QString("font-size: %1px; color: #AAAAAA;").arg(sizes.descSize));
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
    BPTextSizes sizes = BPTextSizes::getSizes();
    viewButton->setEnabled(enabled);
    titleLabel->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 500;").arg(sizes.titleSize).arg(enabled ? "white" : "#666666"));
    if (descLabel) {
      descLabel->setStyleSheet(QString("font-size: %1px; color: %2;").arg(sizes.descSize).arg(enabled ? "#AAAAAA" : "#444444"));
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

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

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
    titleLabel->setStyleSheet(QString("font-size: %1px; color: white; font-weight: 500;").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet(QString("font-size: %1px; color: #AAAAAA;").arg(sizes.descSize));
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
    BPTextSizes sizes = BPTextSizes::getSizes();
    viewButton->setEnabled(enabled);
    titleLabel->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 500;").arg(sizes.titleSize).arg(enabled ? "white" : "#666666"));
    if (descLabel) {
      descLabel->setStyleSheet(QString("font-size: %1px; color: %2;").arg(sizes.descSize).arg(enabled ? "#AAAAAA" : "#444444"));
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

class BPStaticParamDisplay : public QFrame {
  Q_OBJECT

public:
  BPStaticParamDisplay(const QString &param, const QString &title, const QString &desc,
                       const QString &processor = "", QWidget *parent = nullptr)
    : QFrame(parent), paramName(param), valueProcessor(processor) {

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

    // Main layout with consistent margins/spacing
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side: Title & Description
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(10);

    titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet(QString("font-size: %1px; color: white; font-weight: 500;").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet(QString("font-size: %1px; color: #AAAAAA;").arg(sizes.descSize));
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }

    layout->addLayout(textLayout, 1);

    // Right side: Value display - vertically centered
    QVBoxLayout *valueLayout = new QVBoxLayout();
    valueLayout->setContentsMargins(0, 0, 0, 0);
    valueLayout->setSpacing(0);

    valueLabel = new QLabel(this);
    valueLabel->setStyleSheet(QString("font-size: %1px; color: #2196F3; font-weight: 600;").arg(sizes.valueDisplaySize));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setWordWrap(true);
    valueLayout->addWidget(valueLabel);

    // Center the value vertically
    layout->addLayout(valueLayout, 0);
    layout->setAlignment(valueLayout, Qt::AlignVCenter);

    // Frame styling
    setStyleSheet(R"(
      BPStaticParamDisplay {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    refresh();
  }

  void refresh() {
    BPTextSizes sizes = BPTextSizes::getSizes();
    std::string value = params.get(paramName.toStdString());
    if (value.empty()) {
      valueLabel->setText(tr("Not Set"));
      valueLabel->setStyleSheet(QString("font-size: %1px; color: #666666; font-weight: 600;").arg(sizes.valueDisplaySize));
    } else {
      QString displayValue = QString::fromStdString(value);

      // Apply value processor if specified
      if (valueProcessor == "first_part") {
        // Extract first part before " / " separator
        QStringList parts = displayValue.split(" / ");
        if (!parts.isEmpty()) {
          displayValue = parts[0].trimmed();
        }
      }

      valueLabel->setText(displayValue);
      valueLabel->setStyleSheet(QString("font-size: %1px; color: #2196F3; font-weight: 600;").arg(sizes.valueDisplaySize));
    }
  }

  void setEnabled(bool enabled) {
    BPTextSizes sizes = BPTextSizes::getSizes();
    titleLabel->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 500;").arg(sizes.titleSize).arg(enabled ? "white" : "#666666"));
    if (descLabel) {
      descLabel->setStyleSheet(QString("font-size: %1px; color: %2;").arg(sizes.descSize).arg(enabled ? "#AAAAAA" : "#444444"));
    }
    valueLabel->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 600;").arg(sizes.valueDisplaySize).arg(enabled ? "#2196F3" : "#444444"));
  }

private:
  QString paramName;
  QString valueProcessor;
  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
  QLabel *valueLabel;
  Params params;
};

// BPFileParamDisplay - Similar to BPStaticParamDisplay but reads from a file
class BPFileParamDisplay : public QFrame {
  Q_OBJECT

public:
  BPFileParamDisplay(const QString &filePath, const QString &title, const QString &desc,
                     const QString &prefix = "", const QString &suffix = "", QWidget *parent = nullptr)
    : QFrame(parent), fileName(filePath), valuePrefix(prefix), valueSuffix(suffix) {

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

    // Main layout with consistent margins/spacing
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side: Title & Description
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(10);

    titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet(QString("font-size: %1px; color: white; font-weight: 500;").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet(QString("font-size: %1px; color: #AAAAAA;").arg(sizes.descSize));
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }

    layout->addLayout(textLayout, 1);

    // Right side: Value display - vertically centered
    QVBoxLayout *valueLayout = new QVBoxLayout();
    valueLayout->setContentsMargins(0, 0, 0, 0);
    valueLayout->setSpacing(0);

    valueLabel = new QLabel(this);
    valueLabel->setStyleSheet(QString("font-size: %1px; color: #2196F3; font-weight: 600;").arg(sizes.valueDisplaySize));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setWordWrap(true);
    valueLayout->addWidget(valueLabel);

    // Center the value vertically
    layout->addLayout(valueLayout, 0);
    layout->setAlignment(valueLayout, Qt::AlignVCenter);

    // Frame styling
    setStyleSheet(R"(
      BPFileParamDisplay {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    refresh();
  }

  void refresh() {
    BPTextSizes sizes = BPTextSizes::getSizes();
    QString value;

    // Special case: if fileName is "COMMA_VERSION", use the define from version.h
    if (fileName == "COMMA_VERSION") {
      value = QString(COMMA_VERSION);
    } else {
      QFile file(fileName);

      if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        // For regular files, just read the first line
        value = in.readLine().trimmed();
        file.close();
      }
    }

    if (value.isEmpty()) {
      valueLabel->setText(tr("Not Found"));
      valueLabel->setStyleSheet(QString("font-size: %1px; color: #666666; font-weight: 600;").arg(sizes.valueDisplaySize));
    } else {
      QString displayValue = valuePrefix + value + valueSuffix;
      valueLabel->setText(displayValue);
      valueLabel->setStyleSheet(QString("font-size: %1px; color: #2196F3; font-weight: 600;").arg(sizes.valueDisplaySize));
    }
  }

  void setEnabled(bool enabled) {
    BPTextSizes sizes = BPTextSizes::getSizes();
    titleLabel->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 500;").arg(sizes.titleSize).arg(enabled ? "white" : "#666666"));
    if (descLabel) {
      descLabel->setStyleSheet(QString("font-size: %1px; color: %2;").arg(sizes.descSize).arg(enabled ? "#AAAAAA" : "#444444"));
    }
    valueLabel->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 600;").arg(sizes.valueDisplaySize).arg(enabled ? "#2196F3" : "#444444"));
  }

private:
  QString fileName;
  QString valuePrefix;
  QString valueSuffix;
  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
  QLabel *valueLabel;
};

class BPNestedControlsButton : public QFrame {
  Q_OBJECT

public:
  BPNestedControlsButton(const QString &title, const QString &desc, const QString &buttonText, const QString &icon = QString(), QWidget *parent = nullptr) : QFrame(parent) {

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

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
        openButton->setStyleSheet(QString(R"(
          BPButton {
            background-color: #363636;
            border-radius: 30px;
            font-size: %1px;
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
        )").arg(sizes.valueDisplaySize));
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
      titleLabel->setStyleSheet(QString("font-size: %1px; color: white; font-weight: 500;").arg(sizes.titleSize));
      titleLabel->setWordWrap(true);
      textLayout->addWidget(titleLabel);
    }

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc);
      descLabel->setStyleSheet(QString("font-size: %1px; color: #AAAAAA;").arg(sizes.descSize));
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
    BPTextSizes sizes = BPTextSizes::getSizes();
    openButton->setEnabled(enabled);
    if (titleLabel) {
      titleLabel->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 500;").arg(sizes.titleSize).arg(enabled ? "white" : "#666666"));
    }
    if (descLabel) {
      descLabel->setStyleSheet(QString("font-size: %1px; color: %2;").arg(sizes.descSize).arg(enabled ? "#AAAAAA" : "#444444"));
    }
  }

signals:
  void clicked();

private:
  BPButton *openButton;
  QLabel *titleLabel = nullptr;
  QLabel *descLabel = nullptr;
};

class BPTextInputControl : public QFrame {
  Q_OBJECT

public:
  BPTextInputControl(const QString &param, const QString &title, const QString &desc,
                     const QString &buttonText = "ADD", const QString &placeholder = "",
                     QWidget *parent = nullptr)
      : QFrame(parent), paramName(param.toStdString()), placeholderText(placeholder) {

    setStyleSheet(R"(
      BPTextInputControl {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side: button
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    inputButton = new BPButton(buttonText, this);
    inputButton->setMinimumWidth(250);
    inputButton->setMinimumHeight(100);
    buttonLayout->addWidget(inputButton);
    layout->addLayout(buttonLayout, 0);
    layout->setAlignment(buttonLayout, Qt::AlignVCenter);

    // Right side: title, description, and current value
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

    // Current value display
    valueLabel = new QLabel(this);
    valueLabel->setStyleSheet("font-size: 36px; color: #2196F3; font-weight: 600;");
    valueLabel->setWordWrap(true);
    textLayout->addWidget(valueLabel);

    layout->addLayout(textLayout, 1);

    connect(inputButton, &BPButton::clicked, this, &BPTextInputControl::onButtonClicked);
    refresh();
  }

  void refresh() {
    std::string value = params.get(paramName);
    if (value.empty()) {
      valueLabel->setText("");
      valueLabel->setVisible(false);
      inputButton->setText(initialButtonText);
    } else {
      valueLabel->setText(QString::fromStdString(value));
      valueLabel->setVisible(true);
      inputButton->setText("REMOVE");
    }
  }

  void setButtonText(const QString &text) {
    initialButtonText = text;
    refresh();
  }

signals:
  void textEntered(const QString &text);
  void textRemoved();

private slots:
  void onButtonClicked() {
    if (inputButton->text() == "REMOVE") {
      params.remove(paramName);
      emit textRemoved();
      refresh();
    } else {
      emit showTextInputDialog(QString::fromStdString(paramName), titleLabel->text(), placeholderText);
    }
  }

signals:
  void showTextInputDialog(const QString &param, const QString &title, const QString &placeholder);

private:
  BPButton *inputButton;
  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
  QLabel *valueLabel;
  std::string paramName;
  QString placeholderText;
  QString initialButtonText = "ADD";
  Params params;
};

class BPHtmlViewerControl : public QFrame {
  Q_OBJECT

public:
  BPHtmlViewerControl(const QString &title, const QString &desc, const QString &htmlPath,
                      const QString &dialogTitle = "", QWidget *parent = nullptr)
      : QFrame(parent), htmlFilePath(htmlPath), dialogHeader(dialogTitle.isEmpty() ? title : dialogTitle) {

    setStyleSheet(R"(
      BPHtmlViewerControl {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side: button
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    openButton = new BPButton("VIEW", this);
    openButton->setMinimumWidth(250);
    openButton->setMinimumHeight(100);
    buttonLayout->addWidget(openButton);
    layout->addLayout(buttonLayout, 0);
    layout->setAlignment(buttonLayout, Qt::AlignVCenter);

    // Right side: title and description
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

    connect(openButton, &BPButton::clicked, this, [=]() {
      emit htmlViewRequested(htmlFilePath, dialogHeader);
    });
  }

  void setEnabled(bool enabled) {
    QWidget::setEnabled(enabled);
    openButton->setEnabled(enabled);

    if (titleLabel) {
      titleLabel->setStyleSheet(QString("font-size: 40px; color: %1; font-weight: 500;").arg(enabled ? "white" : "#555555"));
    }
    if (descLabel) {
      descLabel->setStyleSheet(QString("font-size: 32px; color: %1;").arg(enabled ? "#AAAAAA" : "#444444"));
    }
  }

signals:
  void htmlViewRequested(const QString &htmlPath, const QString &header);

private:
  BPButton *openButton;
  QLabel *titleLabel = nullptr;
  QLabel *descLabel = nullptr;
  QString htmlFilePath;
  QString dialogHeader;
};

// WiFi List Control - displays available WiFi networks
#include "selfdrive/ui/qt/network/wifi_manager.h"
#include <QMouseEvent>

class BPWifiItem : public QFrame {
  Q_OBJECT

public:
  explicit BPWifiItem(QWidget *parent = nullptr);
  void setNetwork(const Network &n, const QPixmap &statusIcon, const QPixmap &strengthIcon, bool showForget);

signals:
  void connectToNetwork(const Network n);
  void forgetNetwork(const Network n);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;

private:
  Network network;
  QLabel *ssidLabel;
  QLabel *statusIconLabel;
  QLabel *strengthLabel;
  QPushButton *connectingLabel;
  QPushButton *forgetBtn;

  // Track drag for scroll detection
  QPoint pressPos;
  bool isDragging = false;
};

class BPWifiListControl : public QFrame {
  Q_OBJECT

public:
  // Constructor with title/desc only (creates own WifiManager) - for legacy JSON panel use
  explicit BPWifiListControl(const QString &title, const QString &desc, QWidget *parent = nullptr);

  // Constructor with shared WifiManager (for native panels) - PREFERRED
  explicit BPWifiListControl(const QString &title, const QString &desc, WifiManager *sharedWifi, QWidget *parent = nullptr);

  void refreshNetworks();
  void connectHiddenNetwork();
  void scanNetworks();
  void changeTetheringPassword();
  void editApn();
  void configureWifiMetered();

  WifiManager* getWifiManager() { return wifi; }

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void loadNetworkIcons();
  BPWifiItem *getWifiItem(int index);
  void onConnectToNetwork(const Network n);
  void onForgetNetwork(const Network n);
  void init(const QString &title, const QString &desc);

  WifiManager *wifi;
  Params params;
  bool ownsWifiManager = true;  // True if we created the WifiManager, false if shared

  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
  QLabel *scanningLabel;
  QWidget *wifiListContainer;
  QVBoxLayout *wifiListLayout;
  QTimer *tetheringTimer;

  std::vector<BPWifiItem*> wifiItems;
  QPixmap lockIcon, checkmarkIcon, slashIcon;
  std::vector<QPixmap> strengthIcons;
};

/**
 * BPWifiMeteredControl - Segmented control for WiFi metered settings
 *
 * This control manages the WiFi network metered state (default/metered/unmetered)
 * directly through NetworkManager, not through params.
 */
class BPWifiMeteredControl : public QFrame {
  Q_OBJECT

public:
  explicit BPWifiMeteredControl(const QString &title, const QString &desc, QWidget *parent = nullptr);
  void refresh();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void updateSelectedButton();
  BPWifiListControl* findWifiListControl();

  QString titleText;
  QString descText;
  QLabel *titleLabel = nullptr;
  QLabel *descLabel = nullptr;
  QVector<QPushButton*> buttons;
  QTimer *refreshTimer;
  MeteredType currentMetered = MeteredType::UNKNOWN;
};

/**
 * BPPlatformDisplayControl - Display vehicle platform/fingerprint info
 *
 * Shows the current vehicle fingerprint from CarParamsPersistent or manual
 * selection from CarPlatformBundle, with color coding:
 * - Green: Auto-fingerprinted
 * - Blue: Manually selected
 * - Yellow: Unrecognized/not detected
 */
class BPPlatformDisplayControl : public QFrame {
  Q_OBJECT

public:
  BPPlatformDisplayControl(const QString &title, const QString &desc,
                          const QString &valueColor = "#0086E9", QWidget *parent = nullptr)
    : QFrame(parent), defaultValueColor(valueColor) {

    // Get text sizes
    BPTextSizes sizes = BPTextSizes::getSizes();

    // Main layout with consistent margins/spacing
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(50);

    // Left side: Title & Description
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(10);

    titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet(QString("font-size: %1px; color: white; font-weight: 500;").arg(sizes.titleSize));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    if (!desc.isEmpty()) {
      descLabel = new QLabel(desc, this);
      descLabel->setStyleSheet(QString("font-size: %1px; color: #AAAAAA;").arg(sizes.descSize));
      descLabel->setWordWrap(true);
      textLayout->addWidget(descLabel);
    }

    layout->addLayout(textLayout, 1);

    // Right side: Value display - vertically centered
    QVBoxLayout *valueLayout = new QVBoxLayout();
    valueLayout->setContentsMargins(0, 0, 0, 0);
    valueLayout->setSpacing(0);

    valueLabel = new QLabel(this);
    valueLabel->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 600;")
                              .arg(sizes.valueDisplaySize).arg(defaultValueColor));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setWordWrap(true);
    valueLayout->addWidget(valueLabel);

    // Center the value vertically
    layout->addLayout(valueLayout, 0);
    layout->setAlignment(valueLayout, Qt::AlignVCenter);

    // Frame styling
    setStyleSheet(R"(
      BPPlatformDisplayControl {
        background-color: #242424;
        border-radius: 10px;
        min-height: 150px;
      }
    )");

    refresh();
  }

  void refresh() {
    BPTextSizes sizes = BPTextSizes::getSizes();
    QString platform = tr("Unrecognized Vehicle");
    QString platform_color = "#FFD500";  // Yellow for unrecognized

    // Check for manual selection first (CarPlatformBundle)
    std::string platform_bundle = params.get("CarPlatformBundle");
    if (!platform_bundle.empty()) {
      QString platformBundleStr = QString::fromStdString(platform_bundle);
      QJsonDocument json = QJsonDocument::fromJson(platformBundleStr.toUtf8());
      if (!json.isNull() && json.isObject()) {
        QString name = json.object().value("name").toString();
        if (!name.isEmpty()) {
          platform = name;
          platform_color = "#0086E9";  // Blue for manual selection
        }
      }
    } else {
      // Check for auto-fingerprint (CarParamsPersistent)
      auto cp_bytes = params.get("CarParamsPersistent");
      if (!cp_bytes.empty()) {
        try {
          // Parse the capnp message
          kj::ArrayPtr<const capnp::word> words_ptr;
          size_t words_size = cp_bytes.size() / sizeof(capnp::word) + 1;
          kj::Array<capnp::word> aligned_buf = kj::heapArray<capnp::word>(words_size < 512 ? 512 : words_size);
          memcpy(aligned_buf.begin(), cp_bytes.data(), cp_bytes.size());
          words_ptr = aligned_buf.slice(0, words_size);

          capnp::FlatArrayMessageReader cmsg(words_ptr);
          cereal::CarParams::Reader CP = cmsg.getRoot<cereal::CarParams>();

          QString fingerprint = QString::fromStdString(CP.getCarFingerprint().cStr());

          if (fingerprint != "MOCK" && !fingerprint.isEmpty()) {
            platform = fingerprint;
            platform_color = "#00F100";  // Green for auto-fingerprint
          }
        } catch (const std::exception &e) {
          BPLog::bpWarn() << "[bp.platform_display] Error parsing CarParams: " << e.what() << std::endl;
        }
      }
    }

    // Update the display
    valueLabel->setText(platform);
    valueLabel->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 600;")
                              .arg(sizes.valueDisplaySize).arg(platform_color));
  }

  void setEnabled(bool enabled) {
    BPTextSizes sizes = BPTextSizes::getSizes();
    titleLabel->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 500;")
                              .arg(sizes.titleSize).arg(enabled ? "white" : "#666666"));
    if (descLabel) {
      descLabel->setStyleSheet(QString("font-size: %1px; color: %2;")
                               .arg(sizes.descSize).arg(enabled ? "#AAAAAA" : "#444444"));
    }
    QString currentColor = enabled ? defaultValueColor : "#444444";
    valueLabel->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 600;")
                              .arg(sizes.valueDisplaySize).arg(currentColor));
  }

private:
  QString defaultValueColor;
  QLabel *titleLabel;
  QLabel *descLabel = nullptr;
  QLabel *valueLabel;
  Params params;
};

