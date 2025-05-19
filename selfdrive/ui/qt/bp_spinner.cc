// selfdrive/ui/bluepilot/qt/bp_spinner.cc
#include "selfdrive/ui/qt/bp_spinner.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <QGuiApplication>
#include <QApplication>
#include <QGridLayout>
#include <QPainter>
#include <QString>
#include <QTransform>
#include <QProcess>
#include <QScreen>

#include "system/hardware/hw.h"
#include "selfdrive/ui/qt/qt_window.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_updater_panel.h"

// Constants
constexpr QSize spinner_size = QSize(500, 500);

// BPTrackWidget implementation for animated spinner
BPTrackWidget::BPTrackWidget(QWidget *parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAttribute(Qt::WA_PaintOnScreen, false);
  setFixedSize(spinner_size);

  // Load the spinner track image
  QPixmap track_original = loadPixmap("../assets/img_spinner_track.png");

  // Load the comma image
  QPixmap comma_original = loadPixmap("../assets/img_spinner_comma.png");

  // Calculate size for comma (larger - 100% of spinner size)
  int comma_size = qMin(width(), height());

  // Scale the comma image (stationary)
  QPixmap comma_img = comma_original.scaled(comma_size, comma_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

  // Calculate position to center the comma
  int comma_x = (width() - comma_img.width()) / 2;
  int comma_y = (height() - comma_img.height()) / 2;

  // Calculate size for track (smaller - 90% of spinner size)
  int track_size = qMin(width(), height()) * 0.9;

  // Scale the track image
  QPixmap track_img = track_original.scaled(track_size, track_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

  // Calculate position to center the track
  int track_x = (width() - track_img.width()) / 2;
  int track_y = (height() - track_img.height()) / 2;

  // Create animation frames for rotating track
  for (int i = 0; i < track_imgs.size(); ++i) {
    // Create fresh pixmap for this frame
    QPixmap pm(spinner_size);
    pm.fill(Qt::black);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // Setup transformation to rotate the track around center
    p.translate(width() / 2, height() / 2);
    p.rotate(i * (360.0 / spinner_fps));
    p.translate(-width() / 2, -height() / 2);

    // Draw the rotating track image (centered)
    p.drawPixmap(track_x, track_y, track_img);

    // Reset transformation to draw comma in fixed position
    p.resetTransform();

    // Draw the stationary comma in the center
    p.drawPixmap(comma_x, comma_y, comma_img);

    // Store the completed frame
    track_imgs[i] = pm.copy();
  }

  // Use optimized animation setup to reduce flickering
  m_anim.setDuration(1000);
  m_anim.setStartValue(0);
  m_anim.setEndValue(int(track_imgs.size() - 1));
  m_anim.setLoopCount(-1);
  m_anim.start();

  // Use the improved connection that reduces unnecessary repaints
  connect(&m_anim, &QVariantAnimation::valueChanged, [this](const QVariant &value) {
    // Only update if the frame actually changed (avoids duplicate repaints)
    static int lastFrame = -1;
    int currentFrame = value.toInt();
    if (currentFrame != lastFrame) {
      lastFrame = currentFrame;
      update();
    }
  });
}

void BPTrackWidget::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  painter.setClipRect(event->rect());
  painter.drawPixmap(0, 0, track_imgs[m_anim.currentValue().toInt()]);
}

// OutputModal implementation
OutputModal::OutputModal(BPSpinner *parent) : QDialog(parent), spinnerParent(parent) {
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setStyleSheet("background-color: #121212; font-size: 30px;");

  // Enable double buffering to reduce flicker
  setAttribute(Qt::WA_PaintOnScreen, false);
  setAttribute(Qt::WA_OpaquePaintEvent, true);

  // Create main layout
  layout = new QVBoxLayout(this);
  layout->setContentsMargins(40, 40, 40, 40);
  layout->setSpacing(25);

  // Header layout with X button and title
  QHBoxLayout *headerLayout = new QHBoxLayout();

  // Create circular X button
  closeButton = new QPushButton("×", this);
  closeButton->setFixedSize(90, 90);
  closeButton->setStyleSheet(
      "QPushButton { background-color: #333333; color: white; border-radius: 40px; font-size: 80px; font-weight: bold; } QPushButton:pressed { background-color: #444444; }");
  headerLayout->addWidget(closeButton);

  // Title
  titleLabel = new QLabel("Build Output", this);
  titleLabel->setStyleSheet("font-size: 50px; font-weight: bold; color: white;");
  headerLayout->addWidget(titleLabel, 1, Qt::AlignCenter);

  // Add empty widget for symmetry
  QWidget *spacer = new QWidget();
  spacer->setFixedSize(80, 80);
  headerLayout->addWidget(spacer);

  layout->addLayout(headerLayout);

  // Text area for compile output
  textArea = new QTextEdit(this);
  textArea->setReadOnly(true);
  textArea->setStyleSheet("QTextEdit { background-color: #1e1e1e; color: white; border-radius: 10px; font-family: monospace; font-size: 40px; padding: 10px; } "
                          "QTextEdit::selection { background-color: transparent; }");
  layout->addWidget(textArea, 1);

  // Configure text area to prevent text selection during touch scrolling
  textArea->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);

  setupTouchScrolling();

  // Button layout
  buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(20);

  // Reboot button (hidden by default)
  rebootButton = new QPushButton("Reboot", this);
  rebootButton->setStyleSheet("QPushButton { background-color:rgb(228, 19, 19); color: white; border-radius: 20px; padding: 20px; font-size: 60px; } QPushButton:pressed { "
                              "background-color:rgb(112, 10, 10); }");
  rebootButton->setFixedHeight(100);
  rebootButton->setVisible(false);
  buttonLayout->addWidget(rebootButton);

  // Update Tool button (hidden by default)
  updateToolButton = new QPushButton("Updater", this);
  updateToolButton->setStyleSheet(
      "QPushButton { background-color: #33aa33; color: white; border-radius: 20px; padding: 20px; font-size: 60px; } QPushButton:pressed { background-color: #228822; }");
  updateToolButton->setFixedHeight(100);
  updateToolButton->setVisible(false);
  buttonLayout->addWidget(updateToolButton);

  layout->addLayout(buttonLayout);

  // Add scroll-to-bottom button
  scrollToBottomButton = new QPushButton("↓", this);
  scrollToBottomButton->setFixedSize(120, 120);
  scrollToBottomButton->setStyleSheet("QPushButton { background-color: #465BEA; color: white; border-radius: 40px; font-size: 80px; font-weight: bold; } QPushButton:pressed { "
                                      "background-color: rgba(100, 100, 100, 0.8); }");
  scrollToBottomButton->setCursor(Qt::PointingHandCursor);
  scrollToBottomButton->setToolTip("Scroll to bottom");
  scrollToBottomButton->setVisible(false); // Initially hidden
  connect(scrollToBottomButton, &QPushButton::clicked, this, &OutputModal::scrollToBottom);

  // Connect signals
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
  connect(rebootButton, &QPushButton::clicked, []() { QProcess::execute("reboot"); });
  connect(updateToolButton, &QPushButton::clicked, [this]() {
    hide();
    if (spinnerParent) {
      spinnerParent->launchUpdaterPanel();
    }
  });

  // Make dialog fullscreen
  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen) {
    setFixedSize(screen->size());
  }
}

void OutputModal::setupTouchScrolling() {
  // Enable touch scrolling with optimized settings
  QScroller::grabGesture(textArea->viewport(), QScroller::LeftMouseButtonGesture);
  QScrollerProperties properties = QScroller::scroller(textArea->viewport())->scrollerProperties();

  // Configure optimal scrolling properties
  properties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
  properties.setScrollMetric(QScrollerProperties::DragStartDistance, QVariant::fromValue(0.001));
  properties.setScrollMetric(QScrollerProperties::MaximumVelocity, QVariant::fromValue(0.5));
  properties.setScrollMetric(QScrollerProperties::MousePressEventDelay, QVariant::fromValue(100));
  properties.setScrollMetric(QScrollerProperties::DragVelocitySmoothingFactor, QVariant::fromValue(0.1));

  QScroller::scroller(textArea->viewport())->setScrollerProperties(properties);

  // Apply custom scrollbar styling
  textArea->setStyleSheet(textArea->styleSheet() + R"(
    QScrollBar:vertical {
      width: 12px;
      background: transparent;
      margin: 0px;
    }
    QScrollBar::handle:vertical {
      background: white;
      min-height: 30px;
      border-radius: 6px;
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
    QScrollBar:horizontal {
      height: 12px;
      background: transparent;
      margin: 0px;
    }
    QScrollBar::handle:horizontal {
      background: white;
      min-width: 30px;
      border-radius: 6px;
    }
    QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
      width: 0px;
    }
    QScrollBar::left-arrow:horizontal, QScrollBar::right-arrow:horizontal {
      width: 0px;
    }
    QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
      background: none;
    }
  )");

  connect(textArea->verticalScrollBar(), &QScrollBar::valueChanged, this, &OutputModal::updateScrollButtonVisibility);

  // Prevent text selection during scrolling
  textArea->viewport()->installEventFilter(this);
}

bool OutputModal::eventFilter(QObject *obj, QEvent *event) {
  // Prevent text selection during touch scrolling
  if (obj == textArea->viewport()) {
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease) {
      if (QScroller::hasScroller(textArea->viewport())) {
        // If we're in a scrolling session, don't select text
        return true;
      }
    }
  }
  return QDialog::eventFilter(obj, event);
}

void OutputModal::setText(const QString &text) {
  // Skip update if text hasn't changed
  if (textArea->toPlainText() == text) {
    return;
  }

  // Block signals to prevent unnecessary processing during text update
  textArea->blockSignals(true);

  // Store the current scroll position and check if we're at the bottom
  QScrollBar *scrollBar = textArea->verticalScrollBar();
  bool wasAtBottom = false;
  if (scrollBar) {
    // Consider "at bottom" if within 30 pixels of maximum
    wasAtBottom = (scrollBar->value() >= scrollBar->maximum() - 30);
  }

  // Use document-based update instead of setText for better performance
  QTextDocument *doc = textArea->document();
  doc->setPlainText(text);

  // Only auto-scroll if we were already at the bottom
  if (wasAtBottom && scrollBar) {
    // Use a single-shot timer to defer scrolling until after layout
    QTimer::singleShot(0, this, [this]() {
      QScrollBar *scrollBar = textArea->verticalScrollBar();
      if (scrollBar) {
        scrollBar->setValue(scrollBar->maximum());
      }
      // Update scroll button visibility after scroll completes
      updateScrollButtonVisibility();
    });
  } else {
    updateScrollButtonVisibility();
  }

  // Re-enable signals
  textArea->blockSignals(false);
}

void OutputModal::setTitle(const QString &title) { titleLabel->setText(title); }

void OutputModal::setErrorMode(bool isError) {
  if (isError) {
    std::cout << "Setting error mode" << std::endl;
    setWindowTitle("Build Errors");
    titleLabel->setText("Build Errors");
    closeButton->setVisible(false);
    rebootButton->setVisible(true);
    updateToolButton->setVisible(true);
  } else {
    std::cout << "Setting info mode" << std::endl;
    setWindowTitle("Build Output");
    titleLabel->setText("Build Output");
    closeButton->setVisible(true);
    rebootButton->setVisible(false);
    updateToolButton->setVisible(false);
  }

  // Ensure the layout and display are updated
  layout->activate();
  update();

  // Scroll to bottom when switching to error mode
  if (isError) {
    scrollToBottom();
  }
}

void OutputModal::scrollToBottom() {
  QScrollBar *scrollBar = textArea->verticalScrollBar();
  if (scrollBar) {
    QTextCursor cursor = textArea->textCursor();
    cursor.movePosition(QTextCursor::End);
    textArea->setTextCursor(cursor);
    scrollBar->setValue(scrollBar->maximum());
  }
  updateScrollButtonVisibility();
}

void OutputModal::updateScrollButtonVisibility() {
  QScrollBar *scrollBar = textArea->verticalScrollBar();
  if (scrollBar) {
    // Show button only when not at bottom (with small tolerance)
    bool atBottom = (scrollBar->value() >= scrollBar->maximum() - 30);
    scrollToBottomButton->setVisible(!atBottom);
  }
}

void OutputModal::resizeEvent(QResizeEvent *event) {
  QDialog::resizeEvent(event);
  positionScrollButton();
}

void OutputModal::positionScrollButton() {
  // Position at bottom right with 30px margin
  scrollToBottomButton->move(width() - scrollToBottomButton->width() - 100, height() - scrollToBottomButton->height() - 150);
}

void OutputModal::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);
  positionScrollButton();

  // Use timer to apply rotation after window is shown
  QTimer::singleShot(0, this, &OutputModal::applyRotation);
}

void OutputModal::applyRotation() {
#ifdef QCOM2
  // Set fixed size first
  setFixedSize(2160, 1080);

  // Prevent UI drawing during rotation
  setUpdatesEnabled(false);

  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  if (native && windowHandle()) {
    wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", windowHandle()));

    if (s) {
      wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
      wl_surface_commit(s);

      setWindowState(Qt::WindowFullScreen);
      layout->activate();
    }
    positionScrollButton();
  }

  // Re-enable updates and do a single repaint
  QTimer::singleShot(100, this, [this]() {
    setUpdatesEnabled(true);
    update();
  });
#endif
}

// BPSpinner implementation
BPSpinner::BPSpinner(QWidget *parent) : QWidget(parent), hasError(false) {
  // Enable double buffering to reduce flicker
  setAttribute(Qt::WA_PaintOnScreen, false);
  setAttribute(Qt::WA_OpaquePaintEvent, true);

  QVBoxLayout *mainLayout = new QVBoxLayout();
  mainLayout->setContentsMargins(150, 100, 150, 200); // Reduced top margin to push spinner higher
  mainLayout->setSpacing(15);

  // Less stretch at top - use integer values for addStretch
  mainLayout->addStretch(1);

  BPTrackWidget *trackWidget = new BPTrackWidget(this);
  mainLayout->addWidget(trackWidget, 0, Qt::AlignHCenter);

  // More spacing to ensure separation between spinner and progress bar
  mainLayout->addSpacing(250);

  // Fixed container for progress bar and status text
  QWidget *statusContainer = new QWidget();
  statusContainer->setFixedHeight(300);

  // Use grid layout with fixed row heights for stability
  QGridLayout *statusGridLayout = new QGridLayout(statusContainer);
  statusGridLayout->setRowMinimumHeight(0, 50);  // Fixed height for progress bar row
  statusGridLayout->setRowMinimumHeight(1, 250); // For status text row
  statusGridLayout->setVerticalSpacing(40);
  statusGridLayout->setContentsMargins(0, 0, 0, 0); // Reduce internal margins

  // Progress bar in row 0
  progressBar = new QProgressBar();
  progressBar->setRange(0, 100);
  progressBar->setTextVisible(false);
  progressBar->setFixedHeight(30);
  progressBar->setFixedWidth(800);
  statusGridLayout->addWidget(progressBar, 0, 0, 1, 1, Qt::AlignHCenter);

  // Status text in row 1
  QVBoxLayout *textContainer = new QVBoxLayout();
  textContainer->setContentsMargins(0, 0, 0, 0);
  textContainer->setSpacing(0);

  statusTextLabel = new QLabel();
  statusTextLabel->setWordWrap(true);
  statusTextLabel->setAlignment(Qt::AlignCenter);
  statusTextLabel->setFixedWidth(1600);

  // Set a larger font with more line height for better readability
  QFont textFont = statusTextLabel->font();
  textFont.setPointSize(30);
  statusTextLabel->setFont(textFont);
  statusTextLabel->setMinimumHeight(200);

  textContainer->addWidget(statusTextLabel, 0, Qt::AlignHCenter | Qt::AlignTop);
  statusGridLayout->addLayout(textContainer, 1, 0, 1, 1);

  mainLayout->addWidget(statusContainer, 0, Qt::AlignHCenter);

  // More stretch at bottom - use integer values for addStretch
  mainLayout->addStretch(3);

  setLayout(mainLayout);

  // Hollow info button
  infoButton = new QPushButton(this);
  infoButton->setFixedSize(80, 80);
  infoButton->setStyleSheet("QPushButton { background-color: transparent; color: white; border: 2px solid white; border-radius: 40px; } QPushButton:pressed { background-color: "
                            "rgba(255, 255, 255, 0.2); }");
  infoButton->setText("i");
  QFont font = infoButton->font();
  font.setPointSize(60);
  font.setBold(true);
  infoButton->setFont(font);

  // Connect signals
  connect(infoButton, &QPushButton::clicked, [this]() { showOutputModal(false); });

  // Create output modal
  outputModal = new OutputModal(this);

  // Setup stdin notifier for progress and text updates
  notifier = new QSocketNotifier(fileno(stdin), QSocketNotifier::Read);
  QObject::connect(notifier, &QSocketNotifier::activated, this, &BPSpinner::update);

  // Setup output update timer for debouncing
  outputUpdateTimer.setSingleShot(true);
  connect(&outputUpdateTimer, &QTimer::timeout, this, &BPSpinner::updateOutputModalText);

  // Set widget style
  setStyleSheet(R"(
    BPSpinner { background-color: black; }
    QLabel { color: white; font-size: 60px; background-color: transparent; }
    QProgressBar { background-color: #373737; border: none; border-radius: 15px; }
    QProgressBar::chunk { border-radius: 15px; background-color: white; }
  )");
}

void BPSpinner::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  // Add 20px padding from left and bottom edges
  infoButton->move(20, height() - infoButton->height() - 20);
}

void BPSpinner::update(int n) {
  std::string line;
  std::getline(std::cin, line);
  if (!line.empty()) {
    parseInput(line);
  }
}

void BPSpinner::parseInput(const std::string &line) {
  // Trim whitespace
  std::string trimmedLine = line;
  trimmedLine.erase(trimmedLine.begin(), std::find_if(trimmedLine.begin(), trimmedLine.end(), [](unsigned char ch) { return !std::isspace(ch); }));
  trimmedLine.erase(std::find_if(trimmedLine.rbegin(), trimmedLine.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), trimmedLine.end());

  // Handle build retry
  if (trimmedLine == "BUILD_RETRY") {
    std::cout << "Build_retry detected" << std::endl;
    outputModal->setText("Build retry detected, clearing output modal text and buffer");
    outputBuffer.clear();
    // Reset error state since we're trying again
    hasError = false;

    // Update modal back to info mode if it's visible
    if (outputModal && outputModal->isVisible()) {
      outputModal->setErrorMode(false);
    }
    return;
  }

  // Handle build failure
  if (trimmedLine == "BUILD_FAILED") {
    std::cout << "BUILD_FAILED detected" << std::endl;
    hasError = true;
    std::cout << "Showing error modal" << std::endl;
    showOutputModal(true);
    std::cout << "Error modal queued" << std::endl;
    return;
  }

  // Check for build error patterns
  if (trimmedLine.find("error:") != std::string::npos || trimmedLine.find("ERROR:") != std::string::npos || trimmedLine.find("fatal:") != std::string::npos ||
      trimmedLine.find("FAILED") != std::string::npos || trimmedLine.find("Build failed") != std::string::npos) {

    std::cout << "Build error detected: " << trimmedLine << std::endl;
    hasError = true;

    // Update modal if it's already visible
    if (outputModal && outputModal->isVisible()) {
      outputModal->setErrorMode(true);
    }
  }

  // Process percentage and text updates
  size_t pipePos = line.find('|');
  if (pipePos != std::string::npos) {
    // Format: PERCENTAGE|TEXT
    std::string percentageStr = line.substr(0, pipePos);
    std::string text = line.substr(pipePos + 1);

    try {
      int percentage = std::stoi(percentageStr);
      progressBar->setValue(percentage);
      // Don't call updateStatusText, call storeOutput directly to avoid duplication
      statusTextLabel->setText(QString::fromStdString(text));
      storeOutput(QString::fromStdString(text));
    } catch (const std::exception &e) {
      storeOutput(QString::fromStdString(line));
    }
  } else {
    // Check if line is just a number (percentage)
    bool isNumber = !line.empty() && std::all_of(line.begin(), line.end(), ::isdigit);

    if (isNumber) {
      try {
        int percentage = std::stoi(line);
        progressBar->setValue(percentage);
      } catch (const std::exception &e) {
        storeOutput(QString::fromStdString(line));
      }
    } else {
      // Otherwise, update status text and store output
      statusTextLabel->setText(QString::fromStdString(line));
      storeOutput(QString::fromStdString(line));
    }
  }
}

void BPSpinner::updateProgress(float cur, float total) {
  int percentage = static_cast<int>(100.0 * cur / total);
  progressBar->setValue(percentage);
}

void BPSpinner::updateStatusText(const QString &text) {
  // Update the label text
  statusTextLabel->setText(text);

  // Separately store output
  storeOutput(text);
}

void BPSpinner::storeOutput(const QString &text) {
  // Skip empty text
  if (text.trimmed().isEmpty()) {
    return;
  }

  bool needsUpdate = false;

  // Skip duplicates
  if (outputBuffer.empty() || outputBuffer.back() != text.toStdString()) {
    // Store output line (limit to 1000 lines)
    outputBuffer.push_back(text.toStdString());
    if (outputBuffer.size() > 1000) {
      outputBuffer.pop_front();
    }
    needsUpdate = true;
  }

  // Check for build completion
  if (text.contains("Build completed", Qt::CaseInsensitive) || text.contains("Build finished", Qt::CaseInsensitive)) {
    if (hasError) {
      std::cout << "Build completed with errors, showing error modal" << std::endl;
      showOutputModal(true);
      // Modal will be updated when shown, no need for extra update
      return;
    }
  }

  // Update modal if visible and changes were made
  if (needsUpdate && outputModal && outputModal->isVisible()) {
    queueOutputUpdate();
  }
}

void BPSpinner::queueOutputUpdate() {
  // Debounce multiple rapid updates (30ms is usually a good debounce time)
  outputUpdateTimer.start(30);
}

void BPSpinner::updateOutputModalText() {
  QString outputText;
  for (const auto &line : outputBuffer) {
    outputText += QString::fromStdString(line) + "\n";
  }
  outputModal->setText(outputText);
}

void BPSpinner::showOutputModal(bool isError) {
  // Force error mode if hasError is true
  isError = isError || hasError;

  // Update text content
  updateOutputModalText();

  // Set modal mode based on isError
  outputModal->setErrorMode(isError);

  // Show modal if not visible
  if (!outputModal->isVisible()) {
    outputModal->show();
  }

  // Bring to front and scroll
  outputModal->raise();
  outputModal->activateWindow();
  outputModal->scrollToBottom();
}

void BPSpinner::launchUpdaterPanel() {
  // Store visibility state for error modal
  errorModalWasVisible = outputModal->isVisible() && hasError;

  // Create container widget
  QWidget *container = new QWidget(nullptr);
  container->setObjectName("updaterContainer");

  // Create horizontal layout
  QHBoxLayout *layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // Create sidebar
  QWidget *sidebar = new QWidget(container);
  sidebar->setFixedWidth(100);
  sidebar->setStyleSheet("background-color: #121212;");

  QVBoxLayout *sidebarLayout = new QVBoxLayout(sidebar);

  // Create close button
  QPushButton *closeButton = new QPushButton("×", sidebar);
  closeButton->setFixedSize(100, 100);
  closeButton->setStyleSheet(R"(
    QPushButton {
      background-color: #333333;
      color: white;
      border-radius: 40px;
      font-size: 50px;
      font-weight: bold;
    }
    QPushButton:pressed {
      background-color: #444444;
    }
  )");

  sidebarLayout->addWidget(closeButton, 0, Qt::AlignTop | Qt::AlignHCenter);
  sidebarLayout->addStretch();

  // Create scroll area
  QScrollArea *scrollArea = new QScrollArea(container);
  scrollArea->setWidgetResizable(true);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  // Create updater panel
  BPUpdaterPanel *panel = new BPUpdaterPanel(scrollArea);
  scrollArea->setWidget(panel);

  // Add sidebar and scroll area to layout
  layout->addWidget(sidebar);
  layout->addWidget(scrollArea);

  // Connect close button
  connect(closeButton, &QPushButton::clicked, container, &QWidget::close);

  // Clean up
  connect(container, &QWidget::destroyed, [panel]() { panel->deleteLater(); });

  // Apply styling
  container->setStyleSheet(R"(
    QWidget#updaterContainer {
      background-color: black;
    }
    QGroupBox {
      background-color: #121212;
      border-color: #404040;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      left: 20px;
      padding: 0 10px;
      color: white;
    }
    QScrollArea {
      background-color: black;
      border: none;
    }
    QScrollArea > QWidget > QWidget {
      background-color: black;
    }
    QScrollBar:vertical {
      width: 8px;
      background: rgba(30, 30, 30, 0.2);
      margin: 0px;
      border-radius: 4px;
    }
    QScrollBar::handle:vertical {
      background: rgba(70, 91, 234, 0.7);
      min-height: 30px;
      border-radius: 4px;
    }
    QScrollBar::handle:vertical:hover {
      background: rgba(70, 91, 234, 0.9);
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
    QLabel {
      color: white;
    }
    QFrame, QWidget {
      background-color: transparent;
    }
    #mainRepoGroup, #submoduleGroup {
      background-color: #121212;
    }
    QPushButton {
      background-color: #363636;
    }
  )");

  // Restore error modal when panel closes
  connect(container, &QWidget::destroyed, [this]() {
    if (errorModalWasVisible) {
      QTimer::singleShot(100, [this]() { showOutputModal(true); });
    }
  });

  // Handle QCOM2 rotation
#ifdef QCOM2
  container->setFixedSize(2160, 1080);

  // Apply rotation after widget is shown
  QTimer::singleShot(0, [container]() {
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    if (native && container->windowHandle()) {
      wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", container->windowHandle()));

      if (s) {
        wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
        wl_surface_commit(s);
        container->setWindowState(Qt::WindowFullScreen);
      }
    }
  });
#endif

  // Show container
  container->show();
}

int main(int argc, char *argv[]) {
  initApp(argc, argv);
  QApplication a(argc, argv);
  BPSpinner spinner;
  setMainWindow(&spinner);
  return a.exec();
}