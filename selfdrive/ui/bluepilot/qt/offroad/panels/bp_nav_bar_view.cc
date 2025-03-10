// selfdrive/ui/bluepilot/qt/offroad/panels/bp_nav_bar_view.cc

#include "bp_nav_bar_view.h"
#include <QIcon>
#include <iostream>

// Custom navigation button implementation - handles individual nav items
NavButton::NavButton(const QString &text, const QIcon &icon, QWidget *parent) : QPushButton(parent) {
  setCheckable(true);
  setFlat(true);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setFixedSize(NAV_PANEL_WIDTH - NAV_BUTTON_MARGIN, NAV_PANEL_WIDTH - (NAV_BUTTON_MARGIN * 2));
  // Ensure no default margins interfere.
  setContentsMargins(0, 0, 0, 0);

  // Adjust the layout margins to account for the 1px border-bottom.
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 8, 0, 8); // 12px top and bottom, 12px left and right
  layout->setSpacing(5);                  // 10px between icon and text
  layout->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);

  // Icon label centered in the button
  QLabel *iconLabel = new QLabel(this);
  iconLabel->setFixedSize(NAV_ICON_SIZE + 4, NAV_ICON_SIZE + 4);
  QPixmap pixmap = icon.pixmap(NAV_ICON_SIZE + 4, NAV_ICON_SIZE + 4);
  iconLabel->setPixmap(pixmap);
  iconLabel->setAlignment(Qt::AlignCenter);
  iconLabel->setStyleSheet("background: transparent;");
  layout->addWidget(iconLabel, 0, Qt::AlignCenter);

  // Text label centered under the icon
  QLabel *textLabel = new QLabel(text);
  textLabel->setAlignment(Qt::AlignCenter);
  textLabel->setWordWrap(true);
  textLabel->setStyleSheet("color: #888888; font-size: 26px; font-weight: 500; background: transparent;");
  layout->addWidget(textLabel, 0, Qt::AlignCenter);

  // Button styling with border-bottom (kept for the nav container)
  setStyleSheet(R"(
    NavButton {
      border: none;
      background: transparent;
      border-bottom: 1px solid #333333;
    }
    NavButton:checked {
      background-color: #363636;
    }
    NavButton:hover:!checked {
      background-color: #2A2A2A;
    }
    NavButton > * {
      background: transparent;
    }
  )");
}

// Update button state when selected
void NavButton::setSelected(bool selected) {
  setChecked(selected);
  QList<QLabel *> labels = findChildren<QLabel *>();
  for (QLabel *label : labels) {
    if (label->pixmap() != nullptr && !label->pixmap()->isNull()) {
      label->setStyleSheet("background: transparent;");
      continue;
    }
    // STYLE: Selected button text color
    label->setStyleSheet(QString("color: %1; font-size: 24px; font-weight: 500; background: transparent;").arg(selected ? "#2196F3" : "#888888"));
  }
}

// Main view constructor
BPNavBarView::BPNavBarView(QWidget *parent) : QWidget(parent) { setupLayout(); }

void BPNavBarView::setupLayout() {
  // Main horizontal layout
  main_layout = new QHBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(30); // Creates a 30px gap between content and nav bar

  // Content stack inside scroll area
  content_stack = new QStackedWidget;
  content_stack->setStyleSheet("background: transparent;");

  // Create BPScrollView for main content
  BPScrollView *scroll_area = new BPScrollView(content_stack, this);

  // Create outer container for nav panel with border
  QFrame *nav_container = new QFrame(this);
  nav_container->setObjectName("nav_container");
  nav_container->setFixedWidth(NAV_PANEL_WIDTH);
  nav_container->setStyleSheet(R"(
     QFrame#nav_container {
       background-color: #242424;  /* Nav bar background color */
       border-radius: 15px;        /* Rounded corners */
       border: 2px solid #666666;  /* Subtle border */
       margin: 0 0 0 10px;         /* Margin around nav bar - changed to left margin */
     }
   )");

  // Inner nav panel without border
  nav_panel = new QWidget();
  nav_panel->setStyleSheet("background: transparent;");

  // Create BPScrollView for navigation panel
  BPScrollView *nav_scroll_area = new BPScrollView(nav_panel, nav_container);

  // Layout for nav container
  QVBoxLayout *container_layout = new QVBoxLayout(nav_container);
  container_layout->setContentsMargins(0, 0, 0, 0);
  container_layout->addWidget(nav_scroll_area);

  // Layout for navigation buttons
  nav_layout = new QVBoxLayout(nav_panel);
  nav_layout->setContentsMargins(0, 0, 0, 0); // Remove margins since buttons have their own
  nav_layout->setSpacing(0);                  // Remove spacing since buttons have dividers
  nav_layout->setAlignment(Qt::AlignTop);

  // Add widgets to main layout in reverse order
  main_layout->addWidget(scroll_area);
  main_layout->addWidget(nav_container);
}

// Initialize view with configuration file
bool BPNavBarView::initialize(const QString &configPath) {
  ConfigManager &config = ConfigManager::getInstance();
  QString actualConfigPath = FileUtils::getProjectRootPath() + configPath;
  if (!config.loadConfig(actualConfigPath)) {
    std::cerr << "Failed to load navigation configuration" << std::endl;
    return false;
  }

  const QJsonObject &configJson = config.getConfig();
  if (!configJson.contains("tabs") || !configJson["tabs"].isArray()) {
    std::cerr << "Invalid navigation configuration - missing tabs array" << std::endl;
    return false;
  }

  createNavItems(configJson["tabs"].toArray());
  return true;
}

// Initialize view with direct JSON configuration
bool BPNavBarView::initialize(const QJsonObject &config) {
  if (!config.contains("tabs") || !config["tabs"].isArray()) {
    std::cerr << "Invalid navigation configuration - missing tabs array" << std::endl;
    return false;
  }

  createNavItems(config["tabs"].toArray());
  return true;
}

// Create navigation items from configuration
void BPNavBarView::createNavItems(const QJsonArray &items) {
  // Clear existing items
  for (auto item : nav_items) {
    if (item.button) {
      nav_layout->removeWidget(item.button);
      delete item.button;
    }
    if (item.panel) {
      content_stack->removeWidget(item.panel);
      delete item.panel;
    }
  }
  nav_items.clear();

  // Create new items
  for (int i = 0; i < items.size(); i++) {
    setupNavItem(items[i].toObject(), i);
  }

  // Select first item if available
  if (!nav_items.empty()) {
    selectNavItem(0);
  }
}

// Set up individual navigation item
void BPNavBarView::setupNavItem(const QJsonObject &item, int index) {
  NavItem nav_item;
  nav_item.title = item["name"].toString();
  nav_item.icon = item["icon"].toString();
  nav_item.config = item;

  // Create panel
  nav_item.panel = new BPPanelBase(content_stack);
  QJsonArray groups = item["groups"].toArray();
  for (const auto &group : groups) {
    nav_item.panel->createGroup(group.toObject());
  }
  content_stack->addWidget(nav_item.panel);

  // Create nav button
  nav_item.button = new NavButton(nav_item.title, QIcon(nav_item.icon), nav_panel);
  nav_layout->addWidget(nav_item.button);

  // Connect button click
  connect(nav_item.button, &NavButton::clicked, [this, index]() { selectNavItem(index); });

  nav_items.push_back(std::move(nav_item));
}

// Handle navigation item selection
void BPNavBarView::selectNavItem(int index) {
  if (index < 0 || index >= nav_items.size())
    return;

  // Update button states
  for (int i = 0; i < nav_items.size(); i++) {
    nav_items[i].button->setSelected(i == index);
  }

  // Show selected panel
  content_stack->setCurrentWidget(nav_items[index].panel);
}

// Refresh the view
void BPNavBarView::refresh() {
  int currentIndex = content_stack->currentIndex();
  if (currentIndex >= 0 && currentIndex < nav_items.size()) {
    nav_items[currentIndex].panel->refresh();
  }
}

// Handle show events
void BPNavBarView::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  refresh();
  updateActivityState();
}

// Handle hide events
void BPNavBarView::hideEvent(QHideEvent *event) {
  updateActivityState();
  QWidget::hideEvent(event);
}

// Update activity state
void BPNavBarView::updateActivityState() {
  int currentIndex = content_stack->currentIndex();
  if (currentIndex >= 0 && currentIndex < nav_items.size()) {
    nav_items[currentIndex].panel->updateActivitySimulation();
  }
}
