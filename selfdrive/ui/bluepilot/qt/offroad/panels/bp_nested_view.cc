// bp_nested_view.cc

#include "bp_nested_view.h"
#include <QScreen>
#include <QGraphicsDropShadowEffect>
#include <iostream>

BPNestedView::BPNestedView(QWidget *parent) : QDialog(parent), panel(nullptr) {

  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAttribute(Qt::WA_DeleteOnClose);
  setAttribute(Qt::WA_NoSystemBackground);

  setupDialogStyle();
  setupLayout();
}

BPNestedView::~BPNestedView() {
  std::cout << "BPNestedView destructor called" << std::endl;
  if (panel) {
    // Clear conditions for all controls in this panel
    QList<QWidget *> controls = panel->findChildren<QWidget *>();
    for (QWidget *control : controls) {
      PanelConditions::getInstance().controlConditions.erase(control);
    }

    // Then cleanup panel
    panel->disconnect();
    panel->setParent(nullptr);
    delete panel; // This will trigger BPPanelBase destructor
    panel = nullptr;
  }
}

void BPNestedView::setupDialogStyle() {
  setStyleSheet(R"(
        BPNestedView {
            background-color: #000000;
        }
        QLabel {
            color: white;
        }
        QPushButton {
            background-color: #2196F3;
            border-radius: 20px;
            font-size: 40px;
            font-weight: 600;
            color: white;
            padding: 5px 20px;
        }
        QPushButton:hover {
            background-color: #1E88E5;
        }
        QPushButton:pressed {
            background-color: #1976D2;
        }
    )");

  setFixedSize(DIALOG_WIDTH, DIALOG_HEIGHT);
}

void BPNestedView::setupLayout() {
  main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);
}

void BPNestedView::setupHeader(const QString &title) {
  // Create header widget
  QWidget *header_widget = new QWidget(this);
  header_widget->setFixedHeight(HEADER_HEIGHT);
  header_widget->setFixedWidth(DIALOG_WIDTH);
  header_widget->setStyleSheet("background-color: #202020;");

  QHBoxLayout *header_layout = new QHBoxLayout(header_widget);
  header_layout->setContentsMargins(30, 20, 30, 20);

  // Back button
  back_button = new QPushButton(tr("< Back"), this);
  back_button->setFixedSize(220, 90);
  back_button->setStyleSheet(R"(
    QPushButton {
        background-color: #2196F3;
        border-radius: 20px;
        font-size: 40px;
        font-weight: 600;
        color: white;
        padding: 5px 20px;
    }
    QPushButton:hover {
        background-color: #1E88E5;
    }
    QPushButton:pressed {
        background-color: #1976D2;
    }
  )");

  connect(back_button, &QPushButton::clicked, this, &QDialog::accept);

  // Title
  title_label = new QLabel(title, this);
  title_label->setStyleSheet("font-size: 50px; font-weight: 600; color: white;");
  title_label->setAlignment(Qt::AlignCenter);

  // Create symmetric layout
  header_layout->addWidget(back_button);
  header_layout->addStretch(1);
  header_layout->addWidget(title_label);
  header_layout->addStretch(1);

  // Add spacer to match back button width
  QWidget *spacer = new QWidget();
  spacer->setFixedSize(back_button->size());
  header_layout->addWidget(spacer);

  main_layout->addWidget(header_widget);
}

bool BPNestedView::setupView(const QString &title, const QJsonObject &config) {
  std::cout << "Setting up nested view with title: " << title.toStdString() << std::endl;

  setupHeader(title);

  // Create container for panel with margins
  QWidget *container = new QWidget(this);
  container->setFixedWidth(DIALOG_WIDTH);
  container->setStyleSheet("background-color: #000000;");

  QHBoxLayout *containerLayout = new QHBoxLayout(container);
  containerLayout->setContentsMargins(50, 0, 50, 0);

  // Create scroll area first
  QScrollArea *scroll = new QScrollArea(container);
  scroll->setStyleSheet(R"(
        QScrollArea {
            background-color: black;
            border: none;
        }
        QScrollArea > QWidget {
            background-color: black;
            border: none;
        }
        QScrollArea > QWidget > QWidget {
            background-color: transparent;
        }
        QScrollBar:vertical {
            width: 8px;
            margin: 0px;
            background: transparent;
        }
        QScrollBar::handle:vertical {
            background: #666666;
            min-height: 30px;
            border-radius: 4px;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar::add-page:vertical,
        QScrollBar::sub-page:vertical {
            background: none;
        }
    )");

  // Create and set up panel
  panel = new BPPanelBase(scroll);
  panel->setStyleSheet("background-color: transparent;");
  panel->setFixedWidth(DIALOG_WIDTH - 100); // Account for margins
  panel->setContentsMargins(0, 50, 0, 50);

  // Process groups from config
  QJsonArray groups = config["groups"].toArray();
  for (const auto &groupValue : groups) {
    panel->createGroup(groupValue.toObject());
  }

  scroll->setWidget(panel);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  containerLayout->addWidget(scroll);
  main_layout->addWidget(container, 1);

  std::cout << "BPNestedView::setupView completed" << std::endl;
  return true;
}

bool BPNestedView::setupView(const QString &title, const QString &configPath) {
  ConfigManager &config = ConfigManager::getInstance();
  QString actualConfigPath = FileUtils::getProjectRootPath() + configPath;
  if (!config.loadConfig(actualConfigPath)) {
    std::cerr << "Failed to load nested view configuration" << std::endl;
    return false;
  }

  return setupView(title, config.getConfig());
}

void BPNestedView::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);

// Handle fullscreen setup for different platforms
#ifdef QCOM2
  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  if (native && windowHandle()) {
    wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", windowHandle()));
    if (s) {
      wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
      wl_surface_commit(s);
    }
    setWindowState(Qt::WindowFullScreen);
  }
#endif

  if (panel) {
    panel->refresh();
  }
}

void BPNestedView::hideEvent(QHideEvent *event) {
  std::cout << "BPNestedView::hideEvent called" << std::endl;
  QDialog::hideEvent(event);
}

void BPNestedView::closeEvent(QCloseEvent *event) {
  // Ensure panel is cleaned up before closing
  if (panel) {
    panel->disconnect();
    panel->setParent(nullptr);
    delete panel;
    panel = nullptr;
  }
  QDialog::closeEvent(event);
}
