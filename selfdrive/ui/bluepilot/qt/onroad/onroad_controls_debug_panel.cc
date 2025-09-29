// selfdrive/ui/bluepilot/qt/onroad/onroad_controls_debug_panel.cc

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include "selfdrive/ui/bluepilot/qt/onroad/onroad_controls_debug_panel.h"
#include "selfdrive/ui/bluepilot/qt/onroad/lateral_debug_panel.h"
#include "selfdrive/ui/bluepilot/qt/onroad/long_debug_panel.h"
#include "selfdrive/ui/bluepilot/qt/onroad/other_debug_panel.h"
#include "selfdrive/ui/bluepilot/qt/sidebar.h"
#include "selfdrive/ui/qt/sidebar.h"

#include <QVBoxLayout>
#include <QLinearGradient>
#include <QPainterPath>
#include <QStyleOption>
#include <QGuiApplication>
#include <QScreen>
#include <QDateTime>

ControlNavButton::ControlNavButton(const QIcon &icon, QWidget *parent) : QPushButton(parent) {
  setCheckable(true);
  setFlat(true);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setFixedSize(140, 140); // Fixed size for the button

  // Ensure no default margins interfere
  setContentsMargins(0, 0, 0, 0);

  // Center-aligned layout
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->setAlignment(Qt::AlignCenter);

  // Icon label centered in the button
  iconLabel = new QLabel(this);
  iconLabel->setFixedSize(80, 80);
  QPixmap pixmap = icon.pixmap(75, 75);
  iconLabel->setPixmap(pixmap);
  iconLabel->setAlignment(Qt::AlignCenter);
  iconLabel->setStyleSheet("background: transparent;");
  layout->addWidget(iconLabel, 0, Qt::AlignCenter);

  // Button styling
  setStyleSheet(R"(
    ControlNavButton {
      background: rgba(54, 54, 54, 230);
      border-radius: 10px;
      padding: 15px;
      margin: 5px;
      border: 1px solid rgba(60, 60, 60, 150);
    }
    ControlNavButton:checked {
      background: rgba(33, 150, 243, 230);
      border-bottom: 3px solid rgba(100, 181, 246, 230);
    }
    ControlNavButton:hover:!checked {
      background: rgba(66, 66, 66, 230);
      border-bottom: 3px solid rgba(85, 85, 85, 230);
    }
  )");
}

void ControlNavButton::setSelected(bool selected) { setChecked(selected); }

OnroadControlsDebugPanel::OnroadControlsDebugPanel(QWidget *parent) : QWidget(parent) {
  // std::cout << "DebugPanel: Constructor called with parent:" << (parent ? "valid" : "null") << std::endl;

  qRegisterMetaType<const UIState *>("const UIState*");

  // Initialize panel dimensions dynamically
  m_panelWidth = calculatePanelWidth();
  setFixedWidth(m_panelWidth);
  setFixedHeight(parent->height());

  // std::cout << "DebugPanel: Initial dimensions set to:" << m_panelWidth << "x" << parent->height() << std::endl;

  // Set window flags for proper docking
  setWindowFlags(Qt::FramelessWindowHint);
  // setAttribute(Qt::WA_TranslucentBackground);
  // setAttribute(Qt::WA_NoSystemBackground);

  // Set size policy to fixed
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  // Position panel at the right edge
  move(parent->width() - m_panelWidth, 0);

  // std::cout << "DebugPanel: Initial position set to:" << (parent->width() - m_panelWidth) << ",0" << std::endl;

  // Note: Parent resize events are handled via resizeEvent override
  // std::cout << "DebugPanel: Parent resize handling via resizeEvent override" << std::endl;

  // Create main horizontal layout with no margins
  QHBoxLayout *mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, NAV_BUTTONS_WIDTH, 0); // Reserve space for nav buttons
  mainLayout->setSpacing(0);

  // Create content area for the panels
  QWidget *contentWidget = new QWidget(this);
  QVBoxLayout *contentLayout = new QVBoxLayout(contentWidget);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);

  // Initialize tab panels
  m_lateralPanel = new LateralDebugPanel(contentWidget);
  m_longPanel = new LongDebugPanel(contentWidget);
  m_otherPanel = new OtherDebugPanel(contentWidget);

  // Add panels to content layout
  contentLayout->addWidget(m_lateralPanel);
  contentLayout->addWidget(m_longPanel);
  contentLayout->addWidget(m_otherPanel);

  // Add content widget to main layout
  mainLayout->addWidget(contentWidget);

  // Setup tab bar
  setupTabs();
  setupCloseButton();

  // Hide all panels initially
  m_lateralPanel->hide();
  m_longPanel->hide();
  m_otherPanel->hide();

  // Show the first panel by default
  m_lateralPanel->show();
  m_currentTabIndex = 0;

  // Animation setup
  m_animation = new QPropertyAnimation(this, "pos");
  m_animation->setDuration(300);
  m_animation->setEasingCurve(QEasingCurve::OutCubic);

  // Add animation finished handler
  connect(m_animation, &QPropertyAnimation::finished, [this]() {
    if (m_visible) {
      // Double-check visibility and position at end of animation
      BPLog::bpDebugGeneral() << "Animation finished - panel should be visible at:" << x() << "," << y() << std::endl;
      raise(); // Make extra sure we're on top
    } else {
      // If animation finished but we're not visible, actually hide the widget
      hide();
    }
  });

  // Initialize update timer
  m_updateTimer = new QTimer(this);
  m_updateTimer->setInterval(UPDATE_INTERVAL_MS);
  m_updateTimer->setSingleShot(false);
  connect(m_updateTimer, &QTimer::timeout, this, &OnroadControlsDebugPanel::updatePanels);
  m_updateTimer->start();

  // Initialize size check timer
  QTimer *sizeCheckTimer = new QTimer(this);
  sizeCheckTimer->setInterval(500); // Check every 500ms for better responsiveness
  sizeCheckTimer->setSingleShot(false);
  connect(sizeCheckTimer, &QTimer::timeout, [this]() {
    if (m_visible && needsSizeUpdate()) {
      // std::cout << "DebugPanel: Size check timer detected size mismatch, forcing refresh" << std::endl;
      forceRefresh();
    }
    // Ensure panel stays on top when visible
    if (m_visible) {
      raise();
    }
  });
  sizeCheckTimer->start();

  // Gesture recognition
  grabGesture(Qt::SwipeGesture);
  setAttribute(Qt::WA_TransparentForMouseEvents, false);

  // Set window flags to stay on top
  setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

  // Ensure we're on top
  raise();

  // Initialize gradients
  m_mainGradient = QLinearGradient(0, 0, 0, height());
  m_mainGradient.setColorAt(0, QColor(30, 30, 30, 230));
  m_mainGradient.setColorAt(1, QColor(20, 20, 20, 230));
  m_mainGradient.setCoordinateMode(QGradient::ObjectBoundingMode);

  m_navGradient = QLinearGradient(0, 0, 0, height());
  m_navGradient.setColorAt(0, QColor(30, 30, 30, 230));
  m_navGradient.setColorAt(1, QColor(20, 20, 20, 230));
  m_navGradient.setCoordinateMode(QGradient::ObjectBoundingMode);

  m_gradientsInitialized = true;

  // Initialize as hidden but don't actually toggle animation
  hide();
  m_visible = false;

  // std::cout << "DebugPanel: Constructor completed, panel hidden" << std::endl;
}

OnroadControlsDebugPanel::~OnroadControlsDebugPanel() { delete m_updateTimer; }

void OnroadControlsDebugPanel::setupCloseButton() {
  m_closeButton = new QPushButton("X", this);
  m_closeButton->setStyleSheet(R"(
    QPushButton {
      background-color: rgba(60, 60, 60, 230);
      color: white;
      font-size: 40px;
      font-weight: bold;
      border-radius: 15px;
      padding: 10px;
      border: 2px solid #aaaaaa;
    }
    QPushButton:hover {
      background-color: rgba(120, 120, 120, 240);
      border: 2px solid #ffffff;
    }
    QPushButton:pressed {
      background-color: rgba(180, 180, 180, 255);
      color: #333333;
    }
  )");
  m_closeButton->setFixedSize(60, 60);
  m_closeButton->move(30, 30);
  connect(m_closeButton, &QPushButton::clicked, this, &OnroadControlsDebugPanel::toggleVisibility);
}

void OnroadControlsDebugPanel::setupTabs() {
  // Create the nav buttons
  QIcon lateralIcon("../assets/offroad/icon_steering.png");
  QIcon longIcon("../assets/offroad/icon_brake_gas_pedals.png");
  QIcon otherIcon("../assets/offroad/icon_debug.png");

  // Create a container widget for the nav buttons
  QWidget *navContainer = new QWidget(this);
  navContainer->setFixedWidth(NAV_BUTTONS_WIDTH);
  navContainer->setStyleSheet("background: transparent;");

  // Layout for buttons
  m_navLayout = new QVBoxLayout(navContainer);
  m_navLayout->setContentsMargins(10, 20, 10, 20);
  m_navLayout->setSpacing(15);
  m_navLayout->setAlignment(Qt::AlignCenter);

  // Create buttons
  auto createNavButton = [&](const QIcon &icon, int index) {
    ControlNavButton *button = new ControlNavButton(icon, navContainer);
    connect(button, &QPushButton::clicked, [this, index]() { tabSelected(index); });
    m_navLayout->addWidget(button);
    m_navButtons.push_back(button);
    return button;
  };

  createNavButton(lateralIcon, 0);
  createNavButton(longIcon, 1);
  createNavButton(otherIcon, 2);

  // Add stretch to center the buttons vertically
  m_navLayout->addStretch();

  // Position the nav container at the right edge of the panel
  navContainer->move(width() - navContainer->width(), 0);
  navContainer->setFixedHeight(height());

  // Select the first button by default
  tabSelected(0);
}

void OnroadControlsDebugPanel::tabSelected(int index) {
  // Hide all panels
  m_lateralPanel->hide();
  m_longPanel->hide();
  m_otherPanel->hide();

  // Show selected panel only if we're visible
  if (m_visible) {
    switch (index) {
    case 0:
      m_lateralPanel->show();
      break;
    case 1:
      m_longPanel->show();
      break;
    case 2:
      m_otherPanel->show();
      break;
    }

    // Make sure close button is on top
    if (m_closeButton) {
      m_closeButton->raise();
    }

    // Ensure entire panel stays on top
    raise();
    activateWindow();
  }

  // Update button states
  for (int i = 0; i < m_navButtons.size(); i++) {
    m_navButtons[i]->setSelected(i == index);
  }

  m_currentTabIndex = index;
  update();
}

void OnroadControlsDebugPanel::updateState(const UIState &s) { scheduleUpdate(s); }

void OnroadControlsDebugPanel::scheduleUpdate(const UIState &s) {
  // Store a pointer to the state for processing in the timer callback
  QMutexLocker locker(&m_updateMutex);
  m_lastState = &s;
  m_updatePending.store(true);
}

void OnroadControlsDebugPanel::updatePanels() {
  // Process the pending update if there is one
  if (m_updatePending.load() && m_lastState != nullptr) {
    QMutexLocker locker(&m_updateMutex);

    // Only update the currently visible panel to save resources
    if (m_visible) {
      switch (m_currentTabIndex) {
      case 0:
        m_lateralPanel->updateState(*m_lastState);
        break;
      case 1:
        m_longPanel->updateState(*m_lastState);
        break;
      case 2:
        m_otherPanel->updateState(*m_lastState);
        break;
      }

      // Keep panel on top during updates
      raise();
    }

    m_updatePending.store(false);
  }
}

bool OnroadControlsDebugPanel::event(QEvent *event) {
  if (event->type() == QEvent::Gesture) {
    return gestureEvent(static_cast<QGestureEvent *>(event));
  }
  return QWidget::event(event);
}

bool OnroadControlsDebugPanel::gestureEvent(QGestureEvent *event) {
  if (QGesture *swipe = event->gesture(Qt::SwipeGesture)) {
    QSwipeGesture *swipeGesture = static_cast<QSwipeGesture *>(swipe);

    if (swipeGesture->horizontalDirection() == QSwipeGesture::Left) {
      if (!m_visible) {
        toggleVisibility();
      }
    } else if (swipeGesture->horizontalDirection() == QSwipeGesture::Right) {
      if (m_visible) {
        toggleVisibility();
      }
    }
    return true;
  }
  return false;
}

int OnroadControlsDebugPanel::calculatePanelWidth() const {
  if (!parentWidget()) {
    // std::cout << "DebugPanel: No parent widget, using default width 800" << std::endl;
    return 800; // Default fallback width if no parent
  }

  int parentWidth = parentWidget()->width();
  if (parentWidth <= 0) {
    // std::cout << "DebugPanel: Invalid parent width:" << parentWidth << "using default 800" << std::endl;
    return 800; // Invalid width, use default
  }

  // std::cout << "DebugPanel: Parent width:" << parentWidth << std::endl;

  // Check if sidebar is visible by looking for it in the parent's children
  bool sidebarVisible = false;
  int actualSidebarWidth = SIDEBAR_WIDTH; // Default to our constant

  QList<QWidget*> children = parentWidget()->findChildren<QWidget*>();
  for (QWidget* child : children) {
    if (qobject_cast<SidebarBP*>(child) || qobject_cast<Sidebar*>(child)) {
      sidebarVisible = child->isVisible();
      // Get the actual sidebar width dynamically
      if (sidebarVisible) {
        actualSidebarWidth = child->width();
      }
      // std::cout << "DebugPanel: Found sidebar, visible:" << sidebarVisible
                // << " width:" << actualSidebarWidth << std::endl;
      break;
    }
  }

  // Calculate available width - fill entire space minus sidebar when visible
  int availableWidth;
  if (sidebarVisible) {
    // Fill the entire space from sidebar to right edge
    availableWidth = parentWidth - actualSidebarWidth;
    // std::cout << "DebugPanel: Sidebar visible, filling available width:" << availableWidth
              // << " (parent:" << parentWidth << " - sidebar:" << actualSidebarWidth << ")" << std::endl;
  } else {
    // Fill the entire width when sidebar is hidden
    availableWidth = parentWidth;
    // std::cout << "DebugPanel: Sidebar hidden, filling full width:" << availableWidth << std::endl;
  }

  // Ensure we have a reasonable minimum width for usability
  int minWidth = NAV_BUTTONS_WIDTH + 300; // 160px for nav + 300px minimum for content

  // No maximum width constraint - fill all available space
  // Only apply minimum width constraint
  if (availableWidth < minWidth) {
    // std::cout << "DebugPanel: Available width " << availableWidth
              // << " is less than minimum " << minWidth << ", using minimum" << std::endl;
    availableWidth = minWidth;
  }

  // std::cout << "DebugPanel: Final calculated width:" << availableWidth
            // << " (min:" << minWidth << ")" << std::endl;

  return availableWidth;
}

void OnroadControlsDebugPanel::updatePosition() {
  if (parentWidget()) {
    int parentWidth = parentWidget()->width();
    int parentHeight = parentWidget()->height();

    // std::cout << "DebugPanel: updatePosition called - parent size:" << parentWidth << "x" << parentHeight << std::endl;

    // Calculate panel width dynamically
    m_panelWidth = calculatePanelWidth();
    setFixedWidth(m_panelWidth);
    setFixedHeight(parentHeight);

    // std::cout << "DebugPanel: Panel size set to:" << m_panelWidth << "x" << parentHeight << std::endl;

    // Position panel based on visibility
    if (m_visible) {
      int xPos = parentWidth - m_panelWidth;
      move(xPos, 0);
      // std::cout << "DebugPanel: Panel visible, positioned at x:" << xPos << std::endl;
    } else {
      move(parentWidth, 0);
              // std::cout << "DebugPanel: Panel hidden, positioned off-screen at x:" << parentWidth << std::endl;
    }

    // Reposition nav container if it exists
    if (!m_navButtons.empty()) {
      QWidget *navContainer = m_navButtons[0]->parentWidget();
      if (navContainer) {
        navContainer->move(width() - navContainer->width(), 0);
        navContainer->setFixedHeight(height());
        // std::cout << "DebugPanel: Nav container repositioned" << std::endl;
      }
    }

    // Reposition close button
    if (m_closeButton) {
      m_closeButton->move(20, 20);
      // std::cout << "DebugPanel: Close button repositioned" << std::endl;
    }
  } else {
    // std::cout << "DebugPanel: updatePosition called but no parent widget" << std::endl;
  }
}

void OnroadControlsDebugPanel::handleParentResize() {
  // std::cout << "DebugPanel: handleParentResize called" << std::endl;

  // Force recalculation of dimensions and position
  if (parentWidget()) {
    updatePosition();
    // std::cout << "DebugPanel: handleParentResize completed successfully" << std::endl;
  } else {
    // std::cout << "DebugPanel: handleParentResize called but no parent widget" << std::endl;
  }
}

void OnroadControlsDebugPanel::forceRefresh() {
  // std::cout << "DebugPanel: forceRefresh called" << std::endl;

  // Force complete recalculation and update
  if (parentWidget()) {
    // Recalculate width based on current sidebar state
    m_panelWidth = calculatePanelWidth();
    setFixedWidth(m_panelWidth);
    setFixedHeight(parentWidget()->height());

    // std::cout << "DebugPanel: forceRefresh - new size:" << m_panelWidth << "x" << parentWidget()->height() << std::endl;

    // Update position
    updatePosition();

    // Force a repaint
    update();

    // std::cout << "DebugPanel: forceRefresh completed" << std::endl;
  } else {
    // std::cout << "DebugPanel: forceRefresh called but no parent widget" << std::endl;
  }
}

bool OnroadControlsDebugPanel::needsSizeUpdate() const {
  if (!parentWidget()) {
    return false;
  }

  int calculatedWidth = calculatePanelWidth();
  bool needsUpdate = (calculatedWidth != m_panelWidth) || (height() != parentWidget()->height());

  // std::cout << "DebugPanel: needsSizeUpdate check - current:" << m_panelWidth << "x" << height()
           // << "calculated:" << calculatedWidth << "x" << parentWidget()->height()
           // << "needs update:" << needsUpdate << std::endl;

  return needsUpdate;
}

void OnroadControlsDebugPanel::resizeEvent(QResizeEvent *event) {
  // std::cout << "DebugPanel: resizeEvent called with new size:" << event->size().width() << "x" << event->size().height() << std::endl;

  updatePosition();
  QWidget::resizeEvent(event);

  // std::cout << "DebugPanel: resizeEvent completed" << std::endl;
}

void OnroadControlsDebugPanel::toggleVisibility() {
  // std::cout << "DebugPanel: toggleVisibility called, current visibility:" << m_visible << std::endl;

  m_visible = !m_visible;
  m_animation->stop();

  // std::cout << "DebugPanel: New visibility:" << m_visible << std::endl;

  // Make sure we're properly sized before animating
  if (parentWidget()) {
    // Force recalculation of dimensions using dynamic sizing
    m_panelWidth = calculatePanelWidth();
    setFixedWidth(m_panelWidth);
    setFixedHeight(parentWidget()->height());

    // std::cout << "DebugPanel: toggleVisibility - panel sized to:" << m_panelWidth << "x" << parentWidget()->height() << std::endl;
  }

  // Get the current parent width (after ensuring dimensions are correct)
  int parentWidth = parentWidget()->width();

  QPoint startPos, endPos;
  if (m_visible) {
    // Start from completely off-screen right
    startPos = QPoint(parentWidth, 0);
    // End at visible position on right edge
    endPos = QPoint(parentWidth - m_panelWidth, 0);

    // Show the widget before animation starts
    show();
    raise();
    activateWindow();

    // Show the relevant panel
    tabSelected(m_currentTabIndex);
  } else {
    // Start from current position
    startPos = QPoint(x(), 0);
    // End at off-screen right
    endPos = QPoint(parentWidth, 0);

    // Hide all panels
    m_lateralPanel->hide();
    m_longPanel->hide();
    m_otherPanel->hide();
  }

  // Make sure we're at the start position before animating
  move(startPos);

  // std::cout << "DebugPanel: Animation positions - start:" << startPos.x() << "," << startPos.y() << " end:" << endPos.x() << "," << endPos.y() << std::endl;

  // Clear any existing animation properties and reset
  m_animation->setTargetObject(this);
  m_animation->setPropertyName("pos");

  m_animation->setStartValue(startPos);
  m_animation->setEndValue(endPos);
  m_animation->start();

  // std::cout << "DebugPanel: Animation started" << std::endl;
}

void OnroadControlsDebugPanel::drawBackground(QPainter &p) {
  // Draw main panel background
  QPainterPath mainPath;
  mainPath.addRect(rect());

  // Use cached gradients
  p.fillPath(mainPath, m_mainGradient);

  // Draw nav panel background with same style
  QRect navRect(width() - NAV_BUTTONS_WIDTH, 0, NAV_BUTTONS_WIDTH, height());
  QPainterPath navPath;
  navPath.addRect(navRect);

  p.fillPath(navPath, m_navGradient);

  // Draw subtle borders
  p.setPen(QPen(QColor(60, 60, 60, 150), 1));
  p.drawPath(mainPath);
  p.drawPath(navPath);
}

void OnroadControlsDebugPanel::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Draw background
  drawBackground(p);
}
