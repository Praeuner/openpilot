#include "selfdrive/ui/bluepilot/qt/onroad/other_debug_panel.h"
#include <QLinearGradient>
#include <QFont>
#include <QScrollBar>
#include <QHeaderView>
#include <QRegularExpression>
#include <QGraphicsDropShadowEffect>
#include <iostream>

OtherDebugPanel::OtherDebugPanel(QWidget *parent) : QWidget(parent) {
  // Set up material styling first
  setupMaterialStyle();
  setupLabelStyles();

  // Set up the main layout
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(10);

  // Title
  QLabel *title = new QLabel("Vehicle Debug Monitor", this);
  title->setStyleSheet(R"(
    font-size: 34px;
    font-weight: bold;
    color: white;
    margin: 0px;
    background: transparent;
  )");
  title->setFont(QFont("Arial", 34, QFont::Bold));
  title->setFixedHeight(60);
  title->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(title);

  // Create Tab Widget
  m_tabWidget = new QTabWidget(this);

  // Set tab position to bottom
  m_tabWidget->setTabPosition(QTabWidget::South);

  mainLayout->addWidget(m_tabWidget);

  // Setup all tabs
  setupTabs();
}

void OtherDebugPanel::setupMaterialStyle() {
  // Set up the main layout with material design spacing
  setStyleSheet(R"(
    QWidget {
      background-color: #121212;
      color: white;
      font-family: Inter, Arial, sans-serif;
    }

    QLabel {
      color: white;
    }

    QTabWidget::pane {
      border: none;
      background: #242424;
      border-radius: 12px;
    }

    QTabBar {
      alignment: center;
    }

    QTabBar::tab {
      background: #363636;
      color: white;
      padding: 15px 30px;
      margin: 5px 8px 0px 8px;
      border-top-left-radius: 10px;
      border-top-right-radius: 10px;
      font-size: 32px;
      min-width: 150px;
      min-height: 50px;
      border-bottom: 3px solid transparent;
    }

    QTabWidget::tab-bar {
      alignment: center;
    }


    QTabBar::tab:selected {
      background: #2196F3;
      border-bottom: 3px solid #64B5F6;
    }

    QTabBar::tab:hover:!selected {
      background: #424242;
      border-bottom: 3px solid #555555;
    }

    QTabBar::tab:disabled {
      background: #242424;
      color: #757575;
    }

    QScrollBar:vertical {
      width: 12px;
      background: transparent;
      margin: 0px;
    }

    QScrollBar::handle:vertical {
      background: #666666;
      min-height: 20px;
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
}

void OtherDebugPanel::setupLabelStyles() {
  nameStyle = R"(
    font-size: 36px;  // Increased from 24px
    color: #BBBBBB;
    padding-right: 10px;
    font-weight: normal;
  )";

  valueStyle = R"(
    font-size: 40px;  // Increased from 26px
    color: #2196F3;
    font-weight: 500;
    padding-right: 20px;
  )";
}

void OtherDebugPanel::setupTableStyle() {
  m_firmwareTable->setStyleSheet(R"(
    QTableWidget {
      background-color: #242424;
      color: white;
      border: none;
      border-radius: 10px;
      font-size: 32px;
      gridline-color: #444444;
    }

    QTableWidget::item {
      padding: 12px;
      border-bottom: 1px solid #393939;
    }

    QTableWidget::item:selected {
      background-color: rgba(33, 150, 243, 120);
      color: white;
    }

    QHeaderView::section {
      background-color: #303030;
      color: white;
      font-weight: 500;
      padding: 15px;
      border: none;
      border-bottom: 2px solid #2196F3;
      font-size: 28px;
      height: 60px;
    }
  )");

  m_firmwareTable->setShowGrid(false);
  m_firmwareTable->setAlternatingRowColors(true);
  m_firmwareTable->setStyleSheet(m_firmwareTable->styleSheet() + "QTableWidget { alternate-background-color: #2A2A2A; }");
}

void OtherDebugPanel::setupTabs() {
  // Setup each tab
  setupMainTab();
  setupRadarTab();
  setupTuningTab();
  setupFirmwareTab();
  setupDeviceTab();

  // Add tabs to tab widget
  m_tabWidget->tabBar()->setShape(QTabBar::RoundedSouth);
  m_tabWidget->tabBar()->setExpanding(true); // This helps with centering
  m_tabWidget->tabBar()->setDocumentMode(true);
  m_tabWidget->tabBar()->setDrawBase(false);
  m_tabWidget->addTab(m_mainTab, "Main");
  m_tabWidget->addTab(m_radarTab, "Radar");
  m_tabWidget->addTab(m_tuningTab, "Tuning");
  m_tabWidget->addTab(m_firmwareTab, "Firmware");
  m_tabWidget->addTab(m_deviceTab, "Device");
}

void OtherDebugPanel::setupMainTab() {
  // Create Main tab
  m_mainTab = new QWidget(m_tabWidget);
  m_mainTab->setStyleSheet("background: transparent;");

  // Create scroll area for main tab
  m_mainScrollArea = new QScrollArea(m_mainTab);
  m_mainScrollArea->setWidgetResizable(true);
  m_mainScrollArea->setFrameShape(QFrame::NoFrame);
  m_mainScrollArea->setStyleSheet("background: transparent;");
  m_mainScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_mainScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_mainScrollArea->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
  m_mainScrollArea->setProperty("kinetic_scrolling", true);
  m_mainScrollArea->setProperty("overshoot", true);

  // Create content widget for scroll area
  m_mainScrollContent = new QWidget(m_mainScrollArea);
  m_mainScrollContent->setStyleSheet("background: transparent;");
  m_mainScrollArea->setWidget(m_mainScrollContent);

  // Create layout for scrollable content
  m_mainLayout = new QGridLayout(m_mainScrollContent);
  m_mainLayout->setContentsMargins(0, 10, 10, 10);
  m_mainLayout->setSpacing(15);

  // Main tab layout
  QVBoxLayout *mainTabLayout = new QVBoxLayout(m_mainTab);
  mainTabLayout->setContentsMargins(0, 0, 0, 0);
  mainTabLayout->addWidget(m_mainScrollArea);

  // Create layouts for each group first
  QGridLayout *dynamicsLayout = new QGridLayout();
  dynamicsLayout->setSpacing(5);

  QGridLayout *steeringLayout = new QGridLayout();
  steeringLayout->setSpacing(5);

  QGridLayout *pedalsLayout = new QGridLayout();
  pedalsLayout->setSpacing(5);

  QGridLayout *systemsLayout = new QGridLayout();
  systemsLayout->setSpacing(5);

  QGridLayout *safetyLayout = new QGridLayout();
  safetyLayout->setSpacing(5);

  QGridLayout *paramsLayout = new QGridLayout();
  paramsLayout->setSpacing(5);

  QGridLayout *cruiseLayout = new QGridLayout();
  cruiseLayout->setSpacing(5);

  QGridLayout *outputsLayout = new QGridLayout();
  outputsLayout->setSpacing(5);

  // Create frames for each layout
  QFrame *dynamicsFrame = createLabelFrame(dynamicsLayout, "Vehicle Dynamics");
  QFrame *steeringFrame = createLabelFrame(steeringLayout, "Steering");
  QFrame *pedalsFrame = createLabelFrame(pedalsLayout, "Pedals & Controls");
  QFrame *systemsFrame = createLabelFrame(systemsLayout, "Vehicle Systems");
  QFrame *safetyFrame = createLabelFrame(safetyLayout, "Safety Systems");
  QFrame *paramsFrame = createLabelFrame(paramsLayout, "Vehicle Parameters");
  QFrame *cruiseFrame = createLabelFrame(cruiseLayout, "Cruise Control");
  QFrame *outputsFrame = createLabelFrame(outputsLayout, "Actuator Outputs");

  // Add frames to layout in a 2-column grid
  // Row 0: Vehicle Dynamics | Steering
  m_mainLayout->addWidget(dynamicsFrame, 0, 0);
  m_mainLayout->addWidget(steeringFrame, 0, 1);

  // Row 1: Pedals & Controls | Cruise Control
  m_mainLayout->addWidget(pedalsFrame, 1, 0);
  m_mainLayout->addWidget(cruiseFrame, 1, 1);

  // Row 2: Vehicle Systems | Safety Systems
  m_mainLayout->addWidget(systemsFrame, 2, 0);
  m_mainLayout->addWidget(safetyFrame, 2, 1);

  // Row 3: Actuator Outputs | Vehicle Parameters (spans 2 columns)
  m_mainLayout->addWidget(outputsFrame, 3, 0);
  m_mainLayout->addWidget(paramsFrame, 3, 1);

  // Initialize the rows for each group
  int dynamicsRow = 1; // Row 0 is for the heading
  int dynamicsCol = 0;

  int steeringRow = 1;
  int steeringCol = 0;

  int pedalsRow = 1;
  int pedalsCol = 0;

  int systemsRow = 1;
  int systemsCol = 0;

  int safetyRow = 1;
  int safetyCol = 0;

  int paramsRow = 1;
  int paramsCol = 0;

  int cruiseRow = 1;
  int cruiseCol = 0;

  int outputsRow = 1;
  int outputsCol = 0;

  // Helper to add a label pair to the appropriate layout
  auto addLabel = [&](const QString &group, const QString &name, const QString &initialValue) {
    QLabel *nameLabel = new QLabel(name, m_mainScrollContent);
    nameLabel->setStyleSheet(nameStyle);
    nameLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    QLabel *valueLabel = new QLabel(initialValue, m_mainScrollContent);
    valueLabel->setStyleSheet(valueStyle);
    valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QGridLayout *targetLayout;
    int *targetRow = nullptr;
    int *targetCol = nullptr;

    if (group == "Vehicle Dynamics") {
      targetLayout = dynamicsLayout;
      targetRow = &dynamicsRow;
      targetCol = &dynamicsCol;
    } else if (group == "Steering") {
      targetLayout = steeringLayout;
      targetRow = &steeringRow;
      targetCol = &steeringCol;
    } else if (group == "Pedals") {
      targetLayout = pedalsLayout;
      targetRow = &pedalsRow;
      targetCol = &pedalsCol;
    } else if (group == "Vehicle Systems") {
      targetLayout = systemsLayout;
      targetRow = &systemsRow;
      targetCol = &systemsCol;
    } else if (group == "Safety") {
      targetLayout = safetyLayout;
      targetRow = &safetyRow;
      targetCol = &safetyCol;
    } else if (group == "Vehicle Parameters") {
      targetLayout = paramsLayout;
      targetRow = &paramsRow;
      targetCol = &paramsCol;
    } else if (group == "Cruise Control") {
      targetLayout = cruiseLayout;
      targetRow = &cruiseRow;
      targetCol = &cruiseCol;
    } else if (group == "Actuator Outputs") {
      targetLayout = outputsLayout;
      targetRow = &outputsRow;
      targetCol = &outputsCol;
    } else {
      return; // Unknown group, skip
    }

    targetLayout->addWidget(nameLabel, *targetRow, *targetCol * 2);
    targetLayout->addWidget(valueLabel, *targetRow, *targetCol * 2 + 1);

    // Store in groups map
    LabelPair pair;
    pair.nameLabel = nameLabel;
    pair.valueLabel = valueLabel;
    m_groups[group].append(pair);

    // Move to next position
    (*targetCol)++;
    if (*targetCol >= 2) { // 2 columns of pairs
      *targetCol = 0;
      (*targetRow)++;
    }
  };

  // Vehicle Dynamics labels
  addLabel("Vehicle Dynamics", "Speed:", "0.0 m/s");
  addLabel("Vehicle Dynamics", "Raw Speed:", "0.0 m/s");
  addLabel("Vehicle Dynamics", "Accel:", "0.0 m/s²");
  addLabel("Vehicle Dynamics", "Yaw Rate:", "0.0 rad/s");
  addLabel("Vehicle Dynamics", "Standstill:", "No");
  addLabel("Vehicle Dynamics", "Engine RPM:", "0");

  // Steering labels
  addLabel("Steering", "Angle:", "0.0°");
  addLabel("Steering", "Rate:", "0.0°/s");
  addLabel("Steering", "Torque:", "0.0");
  addLabel("Steering", "EPS Torque:", "0.0");
  addLabel("Steering", "Pressed:", "No");
  addLabel("Steering", "Temp Fault:", "No");
  addLabel("Steering", "Perm Fault:", "No");

  // Pedals labels
  addLabel("Pedals", "Gas:", "0.0");
  addLabel("Pedals", "Gas Pressed:", "No");
  addLabel("Pedals", "Brake:", "0.0");
  addLabel("Pedals", "Brake Pressed:", "No");
  addLabel("Pedals", "Regen Braking:", "No");
  addLabel("Pedals", "Clutch Pressed:", "No");
  addLabel("Pedals", "Parking Brake:", "No");
  addLabel("Pedals", "Brake Hold:", "No");

  // Systems labels
  addLabel("Vehicle Systems", "ESP Disabled:", "No");
  addLabel("Vehicle Systems", "ESP Active:", "No");
  addLabel("Vehicle Systems", "Left Blinker:", "No");
  addLabel("Vehicle Systems", "Right Blinker:", "No");
  addLabel("Vehicle Systems", "Gear:", "Unknown");
  addLabel("Vehicle Systems", "Fuel Level:", "0.0%");
  addLabel("Vehicle Systems", "Charging:", "No");

  // Safety labels
  addLabel("Safety", "Stock AEB:", "No");
  addLabel("Safety", "Stock FCW:", "No");
  addLabel("Safety", "LKAS Invalid:", "No");
  addLabel("Safety", "Door Open:", "No");
  addLabel("Safety", "Seatbelt:", "Latched");
  addLabel("Safety", "Bad Sensors:", "No");

  // Parameters labels
  addLabel("Vehicle Parameters", "Mass:", "0.0 kg");
  addLabel("Vehicle Parameters", "Wheelbase:", "0.0 m");
  addLabel("Vehicle Parameters", "Steer Ratio:", "0.0");
  addLabel("Vehicle Parameters", "Steer Delay:", "0.0s");
  addLabel("Vehicle Parameters", "Long Delay:", "0.0s");
  addLabel("Vehicle Parameters", "vEgo Stop:", "0.0 m/s");
  addLabel("Vehicle Parameters", "vEgo Start:", "0.0 m/s");
  addLabel("Vehicle Parameters", "Tire Stiffness:", "0.0");

  // Cruise labels
  addLabel("Cruise Control", "Enabled:", "No");
  addLabel("Cruise Control", "Speed:", "0.0 m/s");
  addLabel("Cruise Control", "Available:", "No");
  addLabel("Cruise Control", "Standstill:", "No");
  addLabel("Cruise Control", "Non-Adaptive:", "No");
  addLabel("Cruise Control", "Speed Limit:", "0.0 m/s");

  // Actuator Output labels
  addLabel("Actuator Outputs", "Steer Angle:", "0.0°");
  addLabel("Actuator Outputs", "Torque:", "0.0");
  addLabel("Actuator Outputs", "Curvature:", "0.0");
  addLabel("Actuator Outputs", "Accel:", "0.0 m/s²");
  addLabel("Actuator Outputs", "Gas Output:", "0.0");
  addLabel("Actuator Outputs", "Brake Output:", "0.0");
  addLabel("Actuator Outputs", "CAN Torque:", "0.0");
  addLabel("Actuator Outputs", "LongState:", "Off");

  // Set column stretch factors to ensure even sizing
  m_mainLayout->setColumnStretch(0, 1);
  m_mainLayout->setColumnStretch(1, 1);
}

void OtherDebugPanel::setupRadarTab() {
  m_radarTab = new QWidget(m_tabWidget);
  m_radarTab->setStyleSheet("background: transparent;");

  // Create scroll area for radar tab
  m_radarScrollArea = new QScrollArea(m_radarTab);
  m_radarScrollArea->setWidgetResizable(true);
  m_radarScrollArea->setFrameShape(QFrame::NoFrame);
  m_radarScrollArea->setStyleSheet("background: transparent;");
  m_radarScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // Create content widget for scroll area
  m_radarScrollContent = new QWidget(m_radarScrollArea);
  m_radarScrollContent->setStyleSheet("background: transparent;");
  m_radarScrollArea->setWidget(m_radarScrollContent);

  // Create layout for scrollable content
  m_radarLayout = new QGridLayout(m_radarScrollContent);
  m_radarLayout->setContentsMargins(0, 10, 10, 10);
  m_radarLayout->setSpacing(15);

  // Radar tab layout
  QVBoxLayout *radarTabLayout = new QVBoxLayout(m_radarTab);
  radarTabLayout->setContentsMargins(0, 0, 0, 0);
  radarTabLayout->addWidget(m_radarScrollArea);

  // Create layouts for each group
  QGridLayout *radarStatusLayout = new QGridLayout();
  radarStatusLayout->setSpacing(5);

  QGridLayout *lead1Layout = new QGridLayout();
  lead1Layout->setSpacing(5);

  QGridLayout *lead2Layout = new QGridLayout();
  lead2Layout->setSpacing(5);

  // Create frames for each layout
  QFrame *radarStatusFrame = createLabelFrame(radarStatusLayout, "Radar Status");
  QFrame *lead1Frame = createLabelFrame(lead1Layout, "Primary Lead Vehicle");
  QFrame *lead2Frame = createLabelFrame(lead2Layout, "Secondary Lead Vehicle");

  // Add frames to layout
  m_radarLayout->addWidget(radarStatusFrame, 0, 0, 1, 2);
  m_radarLayout->addWidget(lead1Frame, 1, 0);
  m_radarLayout->addWidget(lead2Frame, 1, 1);

  // Initialize the rows for each group
  int statusRow = 1; // Row 0 is for the heading
  int statusCol = 0;

  int lead1Row = 1;
  int lead1Col = 0;

  int lead2Row = 1;
  int lead2Col = 0;

  // Helper to add a label pair to the appropriate layout
  auto addRadarLabel = [&](const QString &group, const QString &name, const QString &initialValue) {
    QLabel *nameLabel = new QLabel(name, m_radarScrollContent);
    nameLabel->setStyleSheet(nameStyle);
    nameLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    QLabel *valueLabel = new QLabel(initialValue, m_radarScrollContent);
    valueLabel->setStyleSheet(valueStyle);
    valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QGridLayout *targetLayout;
    int *targetRow = nullptr;
    int *targetCol = nullptr;

    if (group == "Radar Status") {
      targetLayout = radarStatusLayout;
      targetRow = &statusRow;
      targetCol = &statusCol;
    } else if (group == "Lead1") {
      targetLayout = lead1Layout;
      targetRow = &lead1Row;
      targetCol = &lead1Col;
    } else if (group == "Lead2") {
      targetLayout = lead2Layout;
      targetRow = &lead2Row;
      targetCol = &lead2Col;
    } else {
      return; // Unknown group, skip
    }

    targetLayout->addWidget(nameLabel, *targetRow, *targetCol * 2);
    targetLayout->addWidget(valueLabel, *targetRow, *targetCol * 2 + 1);

    // Store in groups map
    LabelPair pair;
    pair.nameLabel = nameLabel;
    pair.valueLabel = valueLabel;
    m_radarGroups[group].append(pair);

    // Move to next position
    (*targetCol)++;
    if (*targetCol >= 2) { // 2 columns of pairs
      *targetCol = 0;
      (*targetRow)++;
    }
  };

  // Radar Status labels
  addRadarLabel("Radar Status", "CAN Error:", "No");
  addRadarLabel("Radar Status", "Radar Fault:", "No");
  addRadarLabel("Radar Status", "Wrong Config:", "No");
  addRadarLabel("Radar Status", "Temp Unavailable:", "No");
  addRadarLabel("Radar Status", "Params Unavailable:", "No");

  // Lead1 Vehicle labels
  addRadarLabel("Lead1", "Distance:", "0.0 m");
  addRadarLabel("Lead1", "Lateral Pos:", "0.0 m");
  addRadarLabel("Lead1", "Rel Velocity:", "0.0 m/s");
  addRadarLabel("Lead1", "Rel Accel:", "0.0 m/s²");
  addRadarLabel("Lead1", "Lead Velocity:", "0.0 m/s");
  addRadarLabel("Lead1", "Path Distance:", "0.0 m");
  addRadarLabel("Lead1", "Lat Velocity:", "0.0 m/s");
  addRadarLabel("Lead1", "Lead Velocity K:", "0.0 m/s");
  addRadarLabel("Lead1", "Lead Accel K:", "0.0 m/s²");
  addRadarLabel("Lead1", "FCW:", "No");
  addRadarLabel("Lead1", "Status:", "Not Valid");
  addRadarLabel("Lead1", "Accel Tau:", "0.0 s");
  addRadarLabel("Lead1", "Model Prob:", "0.0");
  addRadarLabel("Lead1", "Radar Detection:", "No");
  addRadarLabel("Lead1", "Track ID:", "-1");

  // Lead2 Vehicle labels
  addRadarLabel("Lead2", "Distance:", "0.0 m");
  addRadarLabel("Lead2", "Lateral Pos:", "0.0 m");
  addRadarLabel("Lead2", "Rel Velocity:", "0.0 m/s");
  addRadarLabel("Lead2", "Rel Accel:", "0.0 m/s²");
  addRadarLabel("Lead2", "Lead Velocity:", "0.0 m/s");
  addRadarLabel("Lead2", "Path Distance:", "0.0 m");
  addRadarLabel("Lead2", "Lat Velocity:", "0.0 m/s");
  addRadarLabel("Lead2", "Lead Velocity K:", "0.0 m/s");
  addRadarLabel("Lead2", "Lead Accel K:", "0.0 m/s²");
  addRadarLabel("Lead2", "FCW:", "No");
  addRadarLabel("Lead2", "Status:", "Not Valid");
  addRadarLabel("Lead2", "Accel Tau:", "0.0 s");
  addRadarLabel("Lead2", "Model Prob:", "0.0");
  addRadarLabel("Lead2", "Radar Detection:", "No");
  addRadarLabel("Lead2", "Track ID:", "-1");

  // Set column stretch factors
  m_radarLayout->setColumnStretch(0, 1);
  m_radarLayout->setColumnStretch(1, 1);
}

void OtherDebugPanel::setupTuningTab() {
  m_tuningTab = new QWidget(m_tabWidget);
  m_tuningTab->setStyleSheet("background: transparent;");

  // Create scroll area for tuning tab
  m_tuningScrollArea = new QScrollArea(m_tuningTab);
  m_tuningScrollArea->setWidgetResizable(true);
  m_tuningScrollArea->setFrameShape(QFrame::NoFrame);
  m_tuningScrollArea->setStyleSheet("background: transparent;");
  m_tuningScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // Create content widget for scroll area
  m_tuningScrollContent = new QWidget(m_tuningScrollArea);
  m_tuningScrollContent->setStyleSheet("background: transparent;");
  m_tuningScrollArea->setWidget(m_tuningScrollContent);

  // Create layout for scrollable content
  m_tuningLayout = new QGridLayout(m_tuningScrollContent);
  m_tuningLayout->setContentsMargins(0, 10, 10, 10);
  m_tuningLayout->setSpacing(15);

  // Tuning tab layout
  QVBoxLayout *tuningTabLayout = new QVBoxLayout(m_tuningTab);
  tuningTabLayout->setContentsMargins(0, 0, 0, 0);
  tuningTabLayout->addWidget(m_tuningScrollArea);

  // Create layouts for each group
  QGridLayout *longitudinalLayout = new QGridLayout();
  longitudinalLayout->setSpacing(5);

  QGridLayout *lateralLayout = new QGridLayout();
  lateralLayout->setSpacing(5);

  QGridLayout *safetyLayout = new QGridLayout();
  safetyLayout->setSpacing(5);

  QGridLayout *paramsLayout = new QGridLayout();
  paramsLayout->setSpacing(5);

  // Create frames for each layout
  QFrame *longitudinalFrame = createLabelFrame(longitudinalLayout, "Longitudinal Tuning");
  QFrame *lateralFrame = createLabelFrame(lateralLayout, "Lateral Tuning");
  QFrame *safetyFrame = createLabelFrame(safetyLayout, "Safety Model");
  QFrame *paramsFrame = createLabelFrame(paramsLayout, "Car Parameters");

  // Add frames to layout
  m_tuningLayout->addWidget(longitudinalFrame, 0, 0);
  m_tuningLayout->addWidget(lateralFrame, 0, 1);
  m_tuningLayout->addWidget(safetyFrame, 1, 0);
  m_tuningLayout->addWidget(paramsFrame, 1, 1);

  // Initialize the rows for each group
  int longRow = 1; // Row 0 is for the heading
  int longCol = 0;

  int latRow = 1;
  int latCol = 0;

  int safetyRow = 1;
  int safetyCol = 0;

  int paramsRow = 1;
  int paramsCol = 0;

  // Helper to add a label pair to the appropriate layout
  auto addTuningLabel = [&](const QString &group, const QString &name, const QString &initialValue) {
    QLabel *nameLabel = new QLabel(name, m_tuningScrollContent);
    nameLabel->setStyleSheet(nameStyle);
    nameLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    QLabel *valueLabel = new QLabel(initialValue, m_tuningScrollContent);
    valueLabel->setStyleSheet(valueStyle);
    valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QGridLayout *targetLayout;
    int *targetRow = nullptr;
    int *targetCol = nullptr;

    if (group == "Longitudinal Tuning") {
      targetLayout = longitudinalLayout;
      targetRow = &longRow;
      targetCol = &longCol;
    } else if (group == "Lateral Tuning") {
      targetLayout = lateralLayout;
      targetRow = &latRow;
      targetCol = &latCol;
    } else if (group == "Safety Model") {
      targetLayout = safetyLayout;
      targetRow = &safetyRow;
      targetCol = &safetyCol;
    } else if (group == "Car Parameters") {
      targetLayout = paramsLayout;
      targetRow = &paramsRow;
      targetCol = &paramsCol;
    } else {
      return; // Unknown group, skip
    }

    targetLayout->addWidget(nameLabel, *targetRow, *targetCol * 2);
    targetLayout->addWidget(valueLabel, *targetRow, *targetCol * 2 + 1);

    // Store in groups map
    LabelPair pair;
    pair.nameLabel = nameLabel;
    pair.valueLabel = valueLabel;
    m_tuningGroups[group].append(pair);

    // Move to next position
    (*targetCol)++;
    if (*targetCol >= 2) { // 2 columns of pairs
      *targetCol = 0;
      (*targetRow)++;
    }
  };

  // Longitudinal Tuning labels
  addTuningLabel("Longitudinal Tuning", "KpBP:", "[ ]");
  addTuningLabel("Longitudinal Tuning", "KpV:", "[ ]");
  addTuningLabel("Longitudinal Tuning", "KiBP:", "[ ]");
  addTuningLabel("Longitudinal Tuning", "KiV:", "[ ]");
  addTuningLabel("Longitudinal Tuning", "Kf:", "0.0");

  // Lateral Tuning labels
  addTuningLabel("Lateral Tuning", "Type:", "Unknown");
  addTuningLabel("Lateral Tuning", "Kp:", "0.0");
  addTuningLabel("Lateral Tuning", "Ki:", "0.0");
  addTuningLabel("Lateral Tuning", "Kf:", "0.0");
  addTuningLabel("Lateral Tuning", "Friction:", "0.0");
  addTuningLabel("Lateral Tuning", "LatAccelFactor:", "0.0");
  addTuningLabel("Lateral Tuning", "LatAccelOffset:", "0.0");

  // Safety Model labels
  addTuningLabel("Safety Model", "Model:", "Unknown");
  addTuningLabel("Safety Model", "Param:", "0x0000");
  addTuningLabel("Safety Model", "Alt Experience:", "0");

  // Car Parameters labels
  addTuningLabel("Car Parameters", "Radar Unavailable:", "No");
  addTuningLabel("Car Parameters", "Rate Cost:", "0.0");
  addTuningLabel("Car Parameters", "Limit Timer:", "0.0 s");
  addTuningLabel("Car Parameters", "Steer Delay:", "0.0 s");
  addTuningLabel("Car Parameters", "Long Delay:", "0.0 s");

  // Set column stretch factors
  m_tuningLayout->setColumnStretch(0, 1);
  m_tuningLayout->setColumnStretch(1, 1);
}

void OtherDebugPanel::setupFirmwareTab() {
  m_firmwareTab = new QWidget(m_tabWidget);
  m_firmwareTab->setStyleSheet("background: transparent;");

  // Create scroll area for firmware tab
  m_firmwareScrollArea = new QScrollArea(m_firmwareTab);
  m_firmwareScrollArea->setWidgetResizable(true);
  m_firmwareScrollArea->setFrameShape(QFrame::NoFrame);
  m_firmwareScrollArea->setStyleSheet("background: transparent;");
  m_firmwareScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // Create content widget for scroll area
  m_firmwareScrollContent = new QWidget(m_firmwareScrollArea);
  m_firmwareScrollContent->setStyleSheet("background: transparent;");
  m_firmwareScrollArea->setWidget(m_firmwareScrollContent);

  // Create layout for scrollable content
  m_firmwareLayout = new QVBoxLayout(m_firmwareScrollContent);
  m_firmwareLayout->setContentsMargins(10, 10, 10, 10);
  m_firmwareLayout->setSpacing(15);

  // Firmware tab layout
  QVBoxLayout *firmwareTabLayout = new QVBoxLayout(m_firmwareTab);
  firmwareTabLayout->setContentsMargins(0, 0, 0, 0);
  firmwareTabLayout->addWidget(m_firmwareScrollArea);

  // Car info section
  QGridLayout *carInfoLayout = new QGridLayout();
  carInfoLayout->setSpacing(10); // Increased spacing
  QFrame *carInfoFrame = createLabelFrame(carInfoLayout, "Car Information");
  m_firmwareLayout->addWidget(carInfoFrame);

  // Initialize the rows for car info
  int carInfoRow = 1; // Row 0 is for the heading
  int carInfoCol = 0;

  // Helper to add a car info label pair
  auto addCarInfoLabel = [&](const QString &name, const QString &initialValue) {
    QLabel *nameLabel = new QLabel(name, m_firmwareScrollContent);
    nameLabel->setStyleSheet(nameStyle);

    QLabel *valueLabel = new QLabel(initialValue, m_firmwareScrollContent);
    valueLabel->setStyleSheet(valueStyle);
    valueLabel->setFixedWidth(400); // Wider for VIN and fingerprint

    carInfoLayout->addWidget(nameLabel, carInfoRow, carInfoCol * 2);
    carInfoLayout->addWidget(valueLabel, carInfoRow, carInfoCol * 2 + 1);

    // Store in groups map
    LabelPair pair;
    pair.nameLabel = nameLabel;
    pair.valueLabel = valueLabel;
    m_firmwareGroups["Car Info"].append(pair);

    // Move to next position
    carInfoCol++;
    if (carInfoCol >= 2) { // 2 columns of pairs
      carInfoCol = 0;
      carInfoRow++;
    }
  };

  // Add car info labels
  addCarInfoLabel("Fingerprint:", "Not Available");
  addCarInfoLabel("VIN:", "Not Available");
  addCarInfoLabel("Brand:", "Unknown");
  addCarInfoLabel("Transmission:", "Unknown");
  addCarInfoLabel("Fuzzy Fingerprint:", "No");
  addCarInfoLabel("Fingerprint Source:", "Unknown");
  addCarInfoLabel("Network Location:", "Unknown");

  // Create table for firmware info with larger title
  QLabel *firmwareTitle = new QLabel("ECU Firmware Information", m_firmwareScrollContent);
  firmwareTitle->setStyleSheet("font-size: 34px; font-weight: bold; color: #00AAFF; margin-top: 20px;");
  firmwareTitle->setAlignment(Qt::AlignLeft);
  m_firmwareLayout->addWidget(firmwareTitle);

  m_firmwareTable = new QTableWidget(0, 4, m_firmwareScrollContent);
  m_firmwareTable->setHorizontalHeaderLabels({"ECU", "FW Version", "Address", "Bus"});

  // Increased header font size and padding
  m_firmwareTable->horizontalHeader()->setStyleSheet(
      "QHeaderView::section { background-color: rgba(60, 60, 60, 200); color: white; font-weight: bold; padding: 15px; border: 1px solid #555; font-size: 28px; height: 60px; }");

  // Increased row height, text size and padding for cells
  m_firmwareTable->setStyleSheet("QTableWidget { background-color: rgba(40, 40, 40, 150); color: white; border: 2px solid #333; font-size: 32px; }"
                                 "QTableWidget::item { padding: 12px; border-bottom: 1px solid #555; }"
                                 "QTableWidget::item:selected { background-color: rgba(0, 170, 255, 150); color: white; }");

  // Configure table
  m_firmwareTable->setColumnWidth(0, 400); // ECU name - wider
  m_firmwareTable->setColumnWidth(1, 500); // FW Version - much wider
  m_firmwareTable->setColumnWidth(2, 200); // Address - wider
  m_firmwareTable->setColumnWidth(3, 120); // Bus - wider

  // Make rows taller
  m_firmwareTable->verticalHeader()->setDefaultSectionSize(70);

  m_firmwareTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_firmwareTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_firmwareTable->verticalHeader()->setVisible(false);

  // Set size policy to expand both horizontally and vertically
  m_firmwareTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  // Make table take the full width
  m_firmwareLayout->addWidget(m_firmwareTable, 1); // The 1 argument gives it a stretch factor

  // Add empty space at bottom
  m_firmwareLayout->addStretch();

  setupTableStyle();
}

void OtherDebugPanel::setupDeviceTab() {
  m_deviceTab = new QWidget(m_tabWidget);
  m_deviceTab->setStyleSheet("background: transparent;");

  // Create scroll area for device tab
  m_deviceScrollArea = new QScrollArea(m_deviceTab);
  m_deviceScrollArea->setWidgetResizable(true);
  m_deviceScrollArea->setFrameShape(QFrame::NoFrame);
  m_deviceScrollArea->setStyleSheet("background: transparent;");
  m_deviceScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // Create content widget for scroll area
  m_deviceScrollContent = new QWidget(m_deviceScrollArea);
  m_deviceScrollContent->setStyleSheet("background: transparent;");
  m_deviceScrollArea->setWidget(m_deviceScrollContent);

  // Create layout for scrollable content
  m_deviceLayout = new QGridLayout(m_deviceScrollContent);
  m_deviceLayout->setContentsMargins(0, 10, 10, 10);
  m_deviceLayout->setSpacing(10);

  // Device tab layout
  QVBoxLayout *deviceTabLayout = new QVBoxLayout(m_deviceTab);
  deviceTabLayout->setContentsMargins(0, 0, 0, 0);
  deviceTabLayout->addWidget(m_deviceScrollArea);

  // Create layouts for each group
  QGridLayout *deviceStatusLayout = new QGridLayout();
  deviceStatusLayout->setSpacing(2);

  QGridLayout *powerLayout = new QGridLayout();
  powerLayout->setSpacing(2);

  QGridLayout *networkLayout = new QGridLayout();
  networkLayout->setSpacing(2);

  QGridLayout *systemAndTempLayout = new QGridLayout();
  systemAndTempLayout->setSpacing(2);

  // Create frames for each layout
  QFrame *deviceStatusFrame = createLabelFrame(deviceStatusLayout, "Device Status");
  deviceStatusFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  QFrame *powerFrame = createLabelFrame(powerLayout, "Power");
  powerFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  QFrame *networkFrame = createLabelFrame(networkLayout, "Network");
  networkFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  QFrame *systemAndTempFrame = createLabelFrame(systemAndTempLayout, "System & Temperatures");
  systemAndTempFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  // Add frames to layout
  // Row 0: Device Status | Power
  m_deviceLayout->addWidget(deviceStatusFrame, 0, 0);
  m_deviceLayout->addWidget(powerFrame, 0, 1);

  // Row 1: Network (spans 2 columns)
  m_deviceLayout->addWidget(networkFrame, 1, 0, 1, 2);

  // Row 2: System & Temperatures (spans 2 columns)
  m_deviceLayout->addWidget(systemAndTempFrame, 2, 0, 1, 2);

  // Set column stretch factors to ensure even distribution
  m_deviceLayout->setColumnStretch(0, 1);
  m_deviceLayout->setColumnStretch(1, 1);

  // Initialize the rows for each group
  int deviceStatusRow = 1; // Row 0 is for the heading
  int deviceStatusCol = 0;

  int powerRow = 1;
  int powerCol = 0;

  int networkRow = 1;
  int networkCol = 0;

  int systemAndTempRow = 1;
  int systemAndTempCol = 0;

  // Helper to add a label pair to the appropriate layout
  auto addDeviceLabel = [&](const QString &group, const QString &name, const QString &initialValue) {
    QLabel *nameLabel = new QLabel(name, m_deviceScrollContent);
    nameLabel->setStyleSheet(nameStyle);
    nameLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    QLabel *valueLabel = new QLabel(initialValue, m_deviceScrollContent);
    valueLabel->setStyleSheet(valueStyle);
    valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QGridLayout *targetLayout;
    int *targetRow = nullptr;
    int *targetCol = nullptr;

    if (group == "Device Status") {
      targetLayout = deviceStatusLayout;
      targetRow = &deviceStatusRow;
      targetCol = &deviceStatusCol;
    } else if (group == "Power") {
      targetLayout = powerLayout;
      targetRow = &powerRow;
      targetCol = &powerCol;
    } else if (group == "Network") {
      targetLayout = networkLayout;
      targetRow = &networkRow;
      targetCol = &networkCol;
    } else if (group == "System & Temperatures") {
      targetLayout = systemAndTempLayout;
      targetRow = &systemAndTempRow;
      targetCol = &systemAndTempCol;
    } else {
      return; // Unknown group, skip
    }

    targetLayout->addWidget(nameLabel, *targetRow, *targetCol * 2);
    targetLayout->addWidget(valueLabel, *targetRow, *targetCol * 2 + 1);

    // Store in groups map
    LabelPair pair;
    pair.nameLabel = nameLabel;
    pair.valueLabel = valueLabel;
    m_deviceGroups[group].append(pair);

    // Move to next position
    (*targetCol)++;
    if (*targetCol >= 2) { // 2 columns of pairs
      *targetCol = 0;
      (*targetRow)++;
    }
  };

  // Device Status labels
  addDeviceLabel("Device Status", "Device Type:", "Unknown");
  addDeviceLabel("Device Status", "Started:", "No");
  addDeviceLabel("Device Status", "Thermal Status:", "Unknown");
  addDeviceLabel("Device Status", "Fan Speed:", "0%");
  addDeviceLabel("Device Status", "Brightness:", "0%");
  // addDeviceLabel("Device Status", "Last Ping:", "N/A");

  // Power labels
  addDeviceLabel("Power", "Power Draw:", "0.0 W");
  addDeviceLabel("Power", "SOM Power Draw:", "0.0 W");
  addDeviceLabel("Power", "Offroad Power:", "0 µWh");
  addDeviceLabel("Power", "Car Battery:", "0 µWh");

  // Network labels
  addDeviceLabel("Network", "Type:", "None");
  addDeviceLabel("Network", "Strength:", "Unknown");
  addDeviceLabel("Network", "Metered:", "No");
  addDeviceLabel("Network", "Technology:", "N/A");
  addDeviceLabel("Network", "Operator:", "N/A");
  addDeviceLabel("Network", "Band:", "N/A");
  addDeviceLabel("Network", "Channel:", "0");
  addDeviceLabel("Network", "State:", "N/A");
  addDeviceLabel("Network", "TX Data:", "0 B");
  addDeviceLabel("Network", "RX Data:", "0 B");

  // System & Temperatures labels (merged)
  // System Usage labels first
  addDeviceLabel("System & Temperatures", "Free Space:", "0%");
  addDeviceLabel("System & Temperatures", "Memory Usage:", "0%");
  addDeviceLabel("System & Temperatures", "GPU Usage:", "0%");
  addDeviceLabel("System & Temperatures", "CPU Usage:", "N/A");

  // Temperature labels
  addDeviceLabel("System & Temperatures", "CPU Temp:", "N/A");
  addDeviceLabel("System & Temperatures", "GPU Temp:", "N/A");
  addDeviceLabel("System & Temperatures", "Memory Temp:", "0.0°C");
  addDeviceLabel("System & Temperatures", "NVME Temp:", "N/A");
  addDeviceLabel("System & Temperatures", "Modem Temp:", "N/A");
  addDeviceLabel("System & Temperatures", "PMIC Temp:", "N/A");
  addDeviceLabel("System & Temperatures", "Intake Temp:", "0.0°C");
  addDeviceLabel("System & Temperatures", "Exhaust Temp:", "0.0°C");
  addDeviceLabel("System & Temperatures", "Case Temp:", "0.0°C");
  addDeviceLabel("System & Temperatures", "Max Temp:", "0.0°C");
}

QFrame *OtherDebugPanel::createLabelFrame(QGridLayout *layout, QString title) {
  QFrame *frame = new QFrame();
  frame->setLayout(layout);

  // Add a heading to the frame with modern styling
  QLabel *heading = new QLabel(title, frame);
  heading->setStyleSheet(R"(
    font-size: 28px;
    font-weight: 500;
    color: #2196F3;
    padding: 5px 0px;
    border-bottom: 2px solid #555555;
  )");
  heading->setAlignment(Qt::AlignLeft);

  layout->setContentsMargins(20, 15, 20, 20);
  layout->addWidget(heading, 0, 0, 1, 4, Qt::AlignLeft);

  // Material Design card-like styling with elevation
  frame->setStyleSheet(R"(
    QFrame {
      background-color: #242424;
      border-radius: 12px;
      margin: 5px;
    }
  )");

  // Add subtle shadow effect for elevation
  QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(frame);
  shadow->setColor(QColor(0, 0, 0, 80));
  shadow->setBlurRadius(15);
  shadow->setOffset(0, 3);
  frame->setGraphicsEffect(shadow);

  return frame;
}

void OtherDebugPanel::updateState(const UIState &s) {
  try {
    if (!isVisible() || !s.scene.started || !s.sm)
      return;

    auto &sm = *(s.sm);

    // Update CarState values
    try {
      if (sm.valid("carState")) {
        auto car = sm["carState"].getCarState();
        m_carValues.vEgo = car.getVEgo();
        m_carValues.aEgo = car.getAEgo();
        m_carValues.vEgoRaw = car.getVEgoRaw();
        m_carValues.yawRate = car.getYawRate();
        m_carValues.standstill = car.getStandstill();
        m_carValues.engineRpm = car.getEngineRpm();

        m_carValues.steeringAngleDeg = car.getSteeringAngleDeg();
        m_carValues.steeringRateDeg = car.getSteeringRateDeg();
        m_carValues.steeringTorque = car.getSteeringTorque();
        m_carValues.steeringTorqueEps = car.getSteeringTorqueEps();
        m_carValues.steeringPressed = car.getSteeringPressed();
        m_carValues.steerFaultTemporary = car.getSteerFaultTemporary();
        m_carValues.steerFaultPermanent = car.getSteerFaultPermanent();

        m_carValues.brake = car.getBrake();
        m_carValues.gas = car.getGas();
        m_carValues.gasPressed = car.getGasPressed();
        m_carValues.brakePressed = car.getBrakePressed();
        m_carValues.regenBraking = car.getRegenBraking();
        m_carValues.clutchPressed = car.getClutchPressed();
        m_carValues.parkingBrake = car.getParkingBrake();
        m_carValues.brakeHoldActive = car.getBrakeHoldActive();

        m_carValues.espDisabled = car.getEspDisabled();
        m_carValues.espActive = car.getEspActive();
        m_carValues.leftBlinker = car.getLeftBlinker();
        m_carValues.rightBlinker = car.getRightBlinker();

        // Handle gearShifter as an int
        m_carValues.gearShifter = static_cast<int>(car.getGearShifter());

        m_carValues.fuelGauge = car.getFuelGauge();
        m_carValues.charging = car.getCharging();

        m_carValues.stockAeb = car.getStockAeb();
        m_carValues.stockFcw = car.getStockFcw();
        m_carValues.invalidLkasSetting = car.getInvalidLkasSetting();
        m_carValues.doorOpen = car.getDoorOpen();
        m_carValues.seatbeltUnlatched = car.getSeatbeltUnlatched();
        m_carValues.vehicleSensorsInvalid = car.getVehicleSensorsInvalid();

        // Get cruise state
        if (car.hasCruiseState()) {
          auto cruise = car.getCruiseState();
          m_carValues.cruiseEnabled = cruise.getEnabled();
          m_carValues.cruiseSpeed = cruise.getSpeed();
          m_carValues.cruiseAvailable = cruise.getAvailable();
          m_carValues.cruiseStandstill = cruise.getStandstill();
          m_carValues.cruiseNonAdaptive = cruise.getNonAdaptive();
          m_carValues.cruiseSpeedLimit = cruise.getSpeedLimit();
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating CarState:" << e.what() << std::endl;
    }

    // Update Radar values
    try {
      if (sm.valid("radarState")) {
        auto radar = sm["radarState"].getRadarState();

        m_radarValues.errors.canError = radar.getRadarErrors().getCanError();
        m_radarValues.errors.radarFault = radar.getRadarErrors().getRadarFault();
        m_radarValues.errors.wrongConfig = radar.getRadarErrors().getWrongConfig();
        m_radarValues.errors.radarUnavailableTemporary = radar.getRadarErrors().getRadarUnavailableTemporary();

        if (radar.hasLeadOne()) {
          auto lead = radar.getLeadOne();
          m_radarValues.leadOne.dRel = lead.getDRel();
          m_radarValues.leadOne.yRel = lead.getYRel();
          m_radarValues.leadOne.vRel = lead.getVRel();
          m_radarValues.leadOne.aRel = lead.getARel();
          m_radarValues.leadOne.vLead = lead.getVLead();
          m_radarValues.leadOne.dPath = lead.getDPath();
          m_radarValues.leadOne.vLat = lead.getVLat();
          m_radarValues.leadOne.vLeadK = lead.getVLeadK();
          m_radarValues.leadOne.aLeadK = lead.getALeadK();
          m_radarValues.leadOne.status = lead.getStatus();
          m_radarValues.leadOne.fcw = lead.getFcw();
          m_radarValues.leadOne.radar = lead.getRadar();
          m_radarValues.leadOne.radarTrackId = lead.getRadarTrackId();
        }

        if (radar.hasLeadTwo()) {
          auto lead = radar.getLeadTwo();
          m_radarValues.leadTwo.dRel = lead.getDRel();
          m_radarValues.leadTwo.yRel = lead.getYRel();
          m_radarValues.leadTwo.vRel = lead.getVRel();
          m_radarValues.leadTwo.aRel = lead.getARel();
          m_radarValues.leadTwo.vLead = lead.getVLead();
          m_radarValues.leadTwo.dPath = lead.getDPath();
          m_radarValues.leadTwo.vLat = lead.getVLat();
          m_radarValues.leadTwo.vLeadK = lead.getVLeadK();
          m_radarValues.leadTwo.aLeadK = lead.getALeadK();
          m_radarValues.leadTwo.status = lead.getStatus();
          m_radarValues.leadTwo.fcw = lead.getFcw();
          m_radarValues.leadTwo.radar = lead.getRadar();
          m_radarValues.leadTwo.radarTrackId = lead.getRadarTrackId();
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating RadarState:" << e.what() << std::endl;
    }

    // Update CarOutput values
    try {
      // Set default values in case of error
      m_outputValues.accel = 0.0f;
      m_outputValues.gas = 0.0f;
      m_outputValues.brake = 0.0f;
      m_outputValues.speed = 0.0f;
      m_outputValues.steeringAngleDeg = 0.0f;
      m_outputValues.torque = 0.0f;
      m_outputValues.curvature = 0.0f;
      m_outputValues.torqueOutputCan = 0.0f;
      m_outputValues.longControlState = 0;

      if (sm.valid("carOutput")) {
        // std::cout << "carOutput is valid, attempting to access it..." << std::endl;

        // Check if we can get the carOutput message
        try {
          auto output = sm["carOutput"].getCarOutput();
          // std::cout << "Successfully got carOutput message" << std::endl;

          // Check if actuatorsOutput exists
          if (output.hasActuatorsOutput()) {
            // std::cout << "output has actuatorsOutput field" << std::endl;

            try {
              auto actuators = output.getActuatorsOutput();
              try {
                m_outputValues.accel = actuators.getAccel();
              } catch (...) {
                m_outputValues.accel = 0.0f;
              }

              try {
                m_outputValues.gas = actuators.getGas();
              } catch (...) {
                m_outputValues.gas = 0.0f;
              }

              try {
                m_outputValues.brake = actuators.getBrake();
              } catch (...) {
                m_outputValues.brake = 0.0f;
              }

              try {
                m_outputValues.speed = actuators.getSpeed();
              } catch (...) {
                m_outputValues.speed = 0.0f;
              }

              try {
                m_outputValues.steeringAngleDeg = actuators.getSteeringAngleDeg();
              } catch (...) {
                m_outputValues.steeringAngleDeg = 0.0f;
              }

              try {
                m_outputValues.torque = actuators.getTorque();
              } catch (...) {
                m_outputValues.torque = 0.0f;
              }

              try {
                m_outputValues.curvature = actuators.getCurvature();
              } catch (...) {
                m_outputValues.curvature = 0.0f;
              }

              try {
                m_outputValues.torqueOutputCan = actuators.getTorqueOutputCan();
              } catch (...) {
                m_outputValues.torqueOutputCan = 0.0f;
              }

              try {
                auto longState = actuators.getLongControlState();
                m_outputValues.longControlState = static_cast<int>(longState);
              } catch (...) {
                m_outputValues.longControlState = 0;
              }
            } catch (const std::exception &e) {
              std::cerr << "Error accessing actuatorsOutput: " << e.what() << std::endl;
            }
          } else {
            std::cout << "output does not have actuatorsOutput field" << std::endl;
          }
        } catch (const std::exception &e) {
          std::cerr << "Error accessing carOutput message: " << e.what() << std::endl;
        }
      } else {
        // std::cout << "carOutput is not valid in state manager" << std::endl;
      }
    } catch (const std::exception &e) {
      // std::cerr << "Error with carOutput handling at " << __FILE__ << ":" << __LINE__ << ": " << e.what() << std::endl;
    }

    // Update CarParams values
    try {
      if (sm.valid("carParams")) {
        auto params = sm["carParams"].getCarParams();
        m_paramValues.mass = params.getMass();
        m_paramValues.wheelbase = params.getWheelbase();
        m_paramValues.steerRatio = params.getSteerRatio();
        m_paramValues.steerActuatorDelay = params.getSteerActuatorDelay();
        m_paramValues.longitudinalActuatorDelay = params.getLongitudinalActuatorDelay();
        m_paramValues.vEgoStopping = params.getVEgoStopping();
        m_paramValues.vEgoStarting = params.getVEgoStarting();
        m_paramValues.tireStiffnessFactor = params.getTireStiffnessFactor();
        m_paramValues.radarUnavailable = params.getRadarUnavailable();
        m_paramValues.carFingerprint = QString::fromStdString(params.getCarFingerprint());
        m_paramValues.carVin = QString::fromStdString(params.getCarVin());
        m_paramValues.brand = QString::fromStdString(params.getBrand());
        m_paramValues.fuzzyFingerprint = params.getFuzzyFingerprint();
        m_paramValues.fingerprintSource = static_cast<int>(params.getFingerprintSource());
        m_paramValues.networkLocation = static_cast<int>(params.getNetworkLocation());
        m_paramValues.transmissionType = static_cast<int>(params.getTransmissionType());
        m_paramValues.steerLimitTimer = params.getSteerLimitTimer();

        // Get lateral tuning parameters
        if (params.getLateralTuning().which() == cereal::CarParams::LateralTuning::PID) {
          m_paramValues.lateralTuningType = LateralTuningType::PID;
          auto pid = params.getLateralTuning().getPid();

          // Get the values from the arrays at appropriate indices
          if (pid.getKpBP().size() > 0 && pid.getKpV().size() > 0) {
            m_paramValues.pidKp = pid.getKpV()[0];
          }

          if (pid.getKiBP().size() > 0 && pid.getKiV().size() > 0) {
            m_paramValues.pidKi = pid.getKiV()[0];
          }

          m_paramValues.pidKf = pid.getKf();
        } else if (params.getLateralTuning().which() == cereal::CarParams::LateralTuning::TORQUE) {
          m_paramValues.lateralTuningType = LateralTuningType::TORQUE;
          auto torque = params.getLateralTuning().getTorque();

          m_paramValues.torqueUseSteeringAngle = torque.getUseSteeringAngle();
          m_paramValues.torqueKp = torque.getKp();
          m_paramValues.torqueKi = torque.getKi();
          m_paramValues.torqueKf = torque.getKf();
          m_paramValues.torqueFriction = torque.getFriction();
          m_paramValues.torqueLatAccelFactor = torque.getLatAccelFactor();
          m_paramValues.torqueLatAccelOffset = torque.getLatAccelOffset();
        }

        // Get longitudinal tuning parameters
        auto longTuning = params.getLongitudinalTuning();

        // Convert from capnp::List to QList for BP and V values
        m_paramValues.longKpBP.clear();
        m_paramValues.longKpV.clear();
        m_paramValues.longKiBP.clear();
        m_paramValues.longKiV.clear();

        for (auto v : longTuning.getKpBP()) {
          m_paramValues.longKpBP.append(v);
        }

        for (auto v : longTuning.getKpV()) {
          m_paramValues.longKpV.append(v);
        }

        for (auto v : longTuning.getKiBP()) {
          m_paramValues.longKiBP.append(v);
        }

        for (auto v : longTuning.getKiV()) {
          m_paramValues.longKiV.append(v);
        }

        m_paramValues.longKf = longTuning.getKf();

        // Get safety configs
        if (params.getSafetyConfigs().size() > 0) {
          auto safety = params.getSafetyConfigs()[0];
          m_paramValues.safetyModel = static_cast<int>(safety.getSafetyModel());
          m_paramValues.safetyParam = safety.getSafetyParam();
        }

        m_paramValues.alternativeExperience = params.getAlternativeExperience();

        // Get car firmware information
        m_paramValues.carFw.clear();

        for (auto fw : params.getCarFw()) {
          CarParameterValues::CarFirmware carFw;
          carFw.ecu = static_cast<int>(fw.getEcu());

          // Handle capnp::Data conversion to string
          auto fwVersionData = fw.getFwVersion();
          std::string fwVersionStr(reinterpret_cast<const char *>(fwVersionData.begin()), fwVersionData.size());
          carFw.fwVersion = QString::fromStdString(fwVersionStr);

          carFw.address = fw.getAddress();
          carFw.subAddress = fw.getSubAddress();
          carFw.bus = fw.getBus();

          m_paramValues.carFw.append(carFw);
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating CarParams:" << e.what() << std::endl;
    }

    // Update Device State values
    try {
      if (sm.valid("deviceState")) {
        auto device = sm["deviceState"].getDeviceState();

        m_deviceValues.deviceType = static_cast<int>(device.getDeviceType());
        m_deviceValues.freeSpacePercent = device.getFreeSpacePercent();
        m_deviceValues.memoryUsagePercent = device.getMemoryUsagePercent();
        m_deviceValues.gpuUsagePercent = device.getGpuUsagePercent();

        // CPU usage
        m_deviceValues.cpuUsagePercent.clear();
        for (auto usage : device.getCpuUsagePercent()) {
          m_deviceValues.cpuUsagePercent.append(usage);
        }

        // Power
        m_deviceValues.offroadPowerUsageUwh = device.getOffroadPowerUsageUwh();
        m_deviceValues.carBatteryCapacityUwh = device.getCarBatteryCapacityUwh();
        m_deviceValues.powerDrawW = device.getPowerDrawW();
        m_deviceValues.somPowerDrawW = device.getSomPowerDrawW();

        // Temperatures
        m_deviceValues.cpuTempC.clear();
        for (auto temp : device.getCpuTempC()) {
          m_deviceValues.cpuTempC.append(temp);
        }

        m_deviceValues.gpuTempC.clear();
        for (auto temp : device.getGpuTempC()) {
          m_deviceValues.gpuTempC.append(temp);
        }

        m_deviceValues.memoryTempC = device.getMemoryTempC();

        m_deviceValues.nvmeTempC.clear();
        for (auto temp : device.getNvmeTempC()) {
          m_deviceValues.nvmeTempC.append(temp);
        }

        m_deviceValues.modemTempC.clear();
        for (auto temp : device.getModemTempC()) {
          m_deviceValues.modemTempC.append(temp);
        }

        m_deviceValues.pmicTempC.clear();
        for (auto temp : device.getPmicTempC()) {
          m_deviceValues.pmicTempC.append(temp);
        }

        m_deviceValues.intakeTempC = device.getIntakeTempC();
        m_deviceValues.exhaustTempC = device.getExhaustTempC();
        m_deviceValues.caseTempC = device.getCaseTempC();
        m_deviceValues.maxTempC = device.getMaxTempC();

        // Status
        m_deviceValues.thermalStatus = static_cast<int>(device.getThermalStatus());
        m_deviceValues.fanSpeedPercentDesired = device.getFanSpeedPercentDesired();
        m_deviceValues.screenBrightnessPercent = device.getScreenBrightnessPercent();
        m_deviceValues.started = device.getStarted();
        m_deviceValues.startedMonoTime = device.getStartedMonoTime();

        // Network
        m_deviceValues.networkType = static_cast<int>(device.getNetworkType());
        m_deviceValues.networkStrength = static_cast<int>(device.getNetworkStrength());
        m_deviceValues.networkMetered = device.getNetworkMetered();
        m_deviceValues.lastAthenaPingTime = device.getLastAthenaPingTime();

        if (device.hasNetworkInfo()) {
          auto netInfo = device.getNetworkInfo();
          m_deviceValues.networkInfo.technology = QString::fromStdString(netInfo.getTechnology());
          m_deviceValues.networkInfo.operator_ = QString::fromStdString(netInfo.getOperator());
          m_deviceValues.networkInfo.band = QString::fromStdString(netInfo.getBand());
          m_deviceValues.networkInfo.channel = netInfo.getChannel();
          m_deviceValues.networkInfo.state = QString::fromStdString(netInfo.getState());
        }

        if (device.hasNetworkStats()) {
          auto netStats = device.getNetworkStats();
          m_deviceValues.networkStats.wwanTx = netStats.getWwanTx();
          m_deviceValues.networkStats.wwanRx = netStats.getWwanRx();
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating DeviceState:" << e.what() << std::endl;
    }

    // Update UI labels
    updateLabels();
    update();
  } catch (const std::exception &e) {
    std::cerr << "Error updating state:" << e.what() << std::endl;
  }
}

void OtherDebugPanel::updateLabels() {
  try {
    // Safety check to ensure all maps are initialized
    if (m_groups.isEmpty() || m_radarGroups.isEmpty() || m_tuningGroups.isEmpty() || m_deviceGroups.isEmpty()) {
      std::cout << "One or more groups are empty in updateLabels" << std::endl;
      return;
    }

    // Helper to format boolean values
    auto formatBool = [](bool value, const QString &trueText = "Yes", const QString &falseText = "No") { return value ? trueText : falseText; };

    // Helper to format gear shifter enum
    auto formatGear = [](int gear) {
      switch (gear) {
      case 1:
        return "Park";
      case 2:
        return "Drive";
      case 3:
        return "Neutral";
      case 4:
        return "Reverse";
      case 5:
        return "Sport";
      case 6:
        return "Low";
      case 7:
        return "Brake";
      case 8:
        return "Eco";
      case 9:
        return "Manumatic";
      default:
        return "Unknown";
      }
    };

    //
    // Main Tab
    //

    // Update Vehicle Dynamics group
    try {
      if (m_groups.contains("Vehicle Dynamics")) {
        int idx = 0;
        auto &group = m_groups["Vehicle Dynamics"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_carValues.vEgo, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_carValues.vEgoRaw, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_carValues.aEgo, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 rad/s").arg(m_carValues.yawRate, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_carValues.standstill));
      if (idx < group.size())
        group[idx++].valueLabel->setText(m_carValues.engineRpm > 0 ? QString("%1").arg(m_carValues.engineRpm, 0, 'f', 0) : "N/A");
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Vehicle Dynamics group:" << e.what() << std::endl;
    }

    // Update Steering group
    try {
      if (m_groups.contains("Steering")) {
        int idx = 0;
        auto &group = m_groups["Steering"];
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1°").arg(m_carValues.steeringAngleDeg, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1°/s").arg(m_carValues.steeringRateDeg, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_carValues.steeringTorque, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_carValues.steeringTorqueEps, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.steeringPressed));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.steerFaultTemporary));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.steerFaultPermanent));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Steering group:" << e.what() << std::endl;
    }

    // Update Pedals group
    try {
      if (m_groups.contains("Pedals")) {
        int idx = 0;
        auto &group = m_groups["Pedals"];
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_carValues.gas, 0, 'f', 3));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.gasPressed));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_carValues.brake, 0, 'f', 3));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.brakePressed));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.regenBraking));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.clutchPressed));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.parkingBrake));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.brakeHoldActive));
      }
    } catch (const std::exception &e) {
      std::cout << "Error updating Pedals group:" << e.what() << std::endl;
    }

    // Update Vehicle Systems group
    try {
      if (m_groups.contains("Vehicle Systems")) {
        int idx = 0;
        auto &group = m_groups["Vehicle Systems"];
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.espDisabled));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.espActive));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.leftBlinker));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.rightBlinker));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatGear(m_carValues.gearShifter));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1%").arg(m_carValues.fuelGauge * 100.0, 0, 'f', 1));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.charging));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Vehicle Systems group:" << e.what() << std::endl;
    }

    // Update Safety group
    try {
      if (m_groups.contains("Safety")) {
        int idx = 0;
        auto &group = m_groups["Safety"];
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.stockAeb));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.stockFcw));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.invalidLkasSetting));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.doorOpen));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.seatbeltUnlatched, "Unlatched", "Latched"));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.vehicleSensorsInvalid));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Safety group:" << e.what() << std::endl;
    }

    // Update Vehicle Parameters group
    try {
      if (m_groups.contains("Vehicle Parameters")) {
        int idx = 0;
        auto &group = m_groups["Vehicle Parameters"];
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 kg").arg(m_paramValues.mass, 0, 'f', 0));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m").arg(m_paramValues.wheelbase, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.steerRatio, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1s").arg(m_paramValues.steerActuatorDelay, 0, 'f', 3));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1s").arg(m_paramValues.longitudinalActuatorDelay, 0, 'f', 3));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_paramValues.vEgoStopping, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_paramValues.vEgoStarting, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.tireStiffnessFactor, 0, 'f', 2));
      }
    } catch (const std::exception &e) {
        std::cerr << "Error updating Vehicle Parameters group:" << e.what() << std::endl;
    }

    // Update Cruise Control group
    try {
      if (m_groups.contains("Cruise Control")) {
        int idx = 0;
        auto &group = m_groups["Cruise Control"];
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.cruiseEnabled));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_carValues.cruiseSpeed, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.cruiseAvailable));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.cruiseStandstill));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_carValues.cruiseNonAdaptive));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_carValues.cruiseSpeedLimit, 0, 'f', 2));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Cruise Control group:" << e.what() << std::endl;
    }

    // Update Actuator Outputs group
    try {
      if (m_groups.contains("Actuator Outputs")) {
        int idx = 0;
        auto &group = m_groups["Actuator Outputs"];
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1°").arg(m_outputValues.steeringAngleDeg, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_outputValues.torque, 0, 'f', 3));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_outputValues.curvature, 0, 'f', 6));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_outputValues.accel, 0, 'f', 3));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_outputValues.gas, 0, 'f', 3));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_outputValues.brake, 0, 'f', 3));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_outputValues.torqueOutputCan, 0, 'f', 3));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatLongControlState(m_outputValues.longControlState));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Actuator Outputs group:" << e.what() << std::endl;
    }

    //
    // Radar Tab
    //

    // Update Radar Status
    try {
      if (m_radarGroups.contains("Radar Status")) {
        int idx = 0;
        auto &group = m_radarGroups["Radar Status"];
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_radarValues.errors.canError));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_radarValues.errors.radarFault));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_radarValues.errors.wrongConfig));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_radarValues.errors.radarUnavailableTemporary));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_paramValues.radarUnavailable));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Radar Status group:" << e.what() << std::endl;
    }

    // Update Lead1 group
    try {
      if (m_radarGroups.contains("Lead1")) {
        int idx = 0;
        auto &group = m_radarGroups["Lead1"];
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m").arg(m_radarValues.leadOne.dRel, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m").arg(m_radarValues.leadOne.yRel, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_radarValues.leadOne.vRel, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_radarValues.leadOne.aRel, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_radarValues.leadOne.vLead, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m").arg(m_radarValues.leadOne.dPath, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_radarValues.leadOne.vLat, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_radarValues.leadOne.vLeadK, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_radarValues.leadOne.aLeadK, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_radarValues.leadOne.fcw));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_radarValues.leadOne.status, "Valid", "Not Valid"));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 s").arg(m_radarValues.leadOne.aLeadTau, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_radarValues.leadOne.modelProb, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_radarValues.leadOne.radar));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_radarValues.leadOne.radarTrackId));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Lead1 group:" << e.what() << std::endl;
    }

    // Update Lead2 group
    try {
      if (m_radarGroups.contains("Lead2")) {
        int idx = 0;
        auto &group = m_radarGroups["Lead2"];
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m").arg(m_radarValues.leadTwo.dRel, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m").arg(m_radarValues.leadTwo.yRel, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_radarValues.leadTwo.vRel, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_radarValues.leadTwo.aRel, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_radarValues.leadTwo.vLead, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m").arg(m_radarValues.leadTwo.dPath, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_radarValues.leadTwo.vLat, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_radarValues.leadTwo.vLeadK, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_radarValues.leadTwo.aLeadK, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_radarValues.leadTwo.fcw));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_radarValues.leadTwo.status, "Valid", "Not Valid"));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1 s").arg(m_radarValues.leadTwo.aLeadTau, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_radarValues.leadTwo.modelProb, 0, 'f', 2));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_radarValues.leadTwo.radar));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_radarValues.leadTwo.radarTrackId));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Lead2 group:" << e.what() << std::endl;
    }

    //
    // Tuning Tab
    //

    // Update Lateral Tuning
    try {
      if (m_tuningGroups.contains("Lateral Tuning")) {
        int idx = 0;
        auto &group = m_tuningGroups["Lateral Tuning"];

        if (m_paramValues.lateralTuningType == LateralTuningType::PID) {
          if (idx < group.size())
            group[idx++].valueLabel->setText("PID");
          if (idx < group.size())
            group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.pidKp, 0, 'f', 4));
          if (idx < group.size())
            group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.pidKi, 0, 'f', 4));
          if (idx < group.size())
            group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.pidKf, 0, 'f', 4));
          if (idx < group.size())
            group[idx++].valueLabel->setText("N/A"); // Friction
          if (idx < group.size())
            group[idx++].valueLabel->setText("N/A"); // LatAccelFactor
          if (idx < group.size())
            group[idx++].valueLabel->setText("N/A"); // LatAccelOffset
        } else if (m_paramValues.lateralTuningType == LateralTuningType::TORQUE) {
          if (idx < group.size())
            group[idx++].valueLabel->setText("Torque");
          if (idx < group.size())
            group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.torqueKp, 0, 'f', 4));
          if (idx < group.size())
            group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.torqueKi, 0, 'f', 4));
          if (idx < group.size())
            group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.torqueKf, 0, 'f', 4));
          if (idx < group.size())
            group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.torqueFriction, 0, 'f', 4));
          if (idx < group.size())
            group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.torqueLatAccelFactor, 0, 'f', 4));
          if (idx < group.size())
            group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.torqueLatAccelOffset, 0, 'f', 4));
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Lateral Tuning group:" << e.what() << std::endl;
    }

    // Update Longitudinal Tuning
    try {
      if (m_tuningGroups.contains("Longitudinal Tuning")) {
        int idx = 0;
        auto &group = m_tuningGroups["Longitudinal Tuning"];

        QString kpBPStr = "[";
        for (int i = 0; i < m_paramValues.longKpBP.size(); i++) {
          kpBPStr += QString("%1").arg(m_paramValues.longKpBP[i], 0, 'f', 1);
          if (i < m_paramValues.longKpBP.size() - 1)
            kpBPStr += ", ";
        }
        kpBPStr += "]";
        if (idx < group.size())
          group[idx++].valueLabel->setText(kpBPStr);

        QString kpVStr = "[";
        for (int i = 0; i < m_paramValues.longKpV.size(); i++) {
          kpVStr += QString("%1").arg(m_paramValues.longKpV[i], 0, 'f', 3);
          if (i < m_paramValues.longKpV.size() - 1)
            kpVStr += ", ";
        }
        kpVStr += "]";
        if (idx < group.size())
          group[idx++].valueLabel->setText(kpVStr);

        QString kiBPStr = "[";
        for (int i = 0; i < m_paramValues.longKiBP.size(); i++) {
          kiBPStr += QString("%1").arg(m_paramValues.longKiBP[i], 0, 'f', 1);
          if (i < m_paramValues.longKiBP.size() - 1)
            kiBPStr += ", ";
        }
        kiBPStr += "]";
        if (idx < group.size())
          group[idx++].valueLabel->setText(kiBPStr);

        QString kiVStr = "[";
        for (int i = 0; i < m_paramValues.longKiV.size(); i++) {
          kiVStr += QString("%1").arg(m_paramValues.longKiV[i], 0, 'f', 3);
          if (i < m_paramValues.longKiV.size() - 1)
            kiVStr += ", ";
        }
        kiVStr += "]";
        if (idx < group.size())
          group[idx++].valueLabel->setText(kiVStr);

        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.longKf, 0, 'f', 4));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Longitudinal Tuning group:" << e.what() << std::endl;
    }

    // Update Safety Model section
    if (m_tuningGroups.contains("Safety Model")) {
      int idx = 0;
      auto &group = m_tuningGroups["Safety Model"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatSafetyModel(m_paramValues.safetyModel));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("0x%1").arg(m_paramValues.safetyParam, 4, 16, QChar('0')));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.alternativeExperience));
    }

    // Update Car Parameters section
    if (m_tuningGroups.contains("Car Parameters")) {
      int idx = 0;
      auto &group = m_tuningGroups["Car Parameters"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_paramValues.radarUnavailable));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_paramValues.steerRateCost, 0, 'f', 4));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 s").arg(m_paramValues.steerLimitTimer, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 s").arg(m_paramValues.steerActuatorDelay, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 s").arg(m_paramValues.longitudinalActuatorDelay, 0, 'f', 3));
    }

    //
    // Firmware Tab
    //

    // Update car info section in the firmware tab
    if (m_firmwareGroups.contains("Car Info") && !m_firmwareGroups["Car Info"].isEmpty()) {
      int idx = 0;
      auto &group = m_firmwareGroups["Car Info"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(m_paramValues.carFingerprint.isEmpty() ? "Not Available" : m_paramValues.carFingerprint);
      if (idx < group.size())
        group[idx++].valueLabel->setText(m_paramValues.carVin.isEmpty() ? "Not Available" : m_paramValues.carVin);
      if (idx < group.size())
        group[idx++].valueLabel->setText(m_paramValues.brand.isEmpty() ? "Unknown" : m_paramValues.brand);
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatTransmissionType(m_paramValues.transmissionType));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_paramValues.fuzzyFingerprint));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatFingerprintSource(m_paramValues.fingerprintSource));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatNetworkLocation(m_paramValues.networkLocation));
    }

    // Update firmware table
    if (m_firmwareTable) {
      m_firmwareTable->setRowCount(0); // Clear existing rows
      static const QRegularExpression fordPattern("^[A-Za-z0-9]{4}-[A-Za-z0-9]{5,6}-[A-Za-z0-9]{2,4}$");

      for (int i = 0; i < m_paramValues.carFw.size(); i++) {
        const auto &fw = m_paramValues.carFw[i];

        // Clean and validate the version string
        QString cleanVersion;
        for (QChar c : fw.fwVersion) {
          if (c != QChar(0) && c.isPrint()) {
            cleanVersion.append(c);
          }
        }
        cleanVersion = cleanVersion.trimmed();

        // Skip if not matching Ford firmware pattern
        if (!fordPattern.match(cleanVersion).hasMatch()) {
          continue;
        }

        // Add a new row
        int row = m_firmwareTable->rowCount();
        m_firmwareTable->insertRow(row);

        // Create and add each item
        QTableWidgetItem *ecuItem = new QTableWidgetItem(formatEcu(fw.ecu));
        QTableWidgetItem *versionItem = new QTableWidgetItem(fw.fwVersion);
        // std::cout << "fw.ecu: " << fw.ecu << " fw.fwVersion: " << fw.fwVersion.toStdString() << " fw.address: " << fw.address << " fw.bus: " << fw.bus << std::endl;
        QTableWidgetItem *addressItem = new QTableWidgetItem(QString("0x%1").arg(fw.address, 0, 16));
        QTableWidgetItem *busItem = new QTableWidgetItem(QString("%1").arg(fw.bus));

        // Set large font for all items
        QFont largeFont("Arial", 28, QFont::Normal);
        ecuItem->setFont(largeFont);
        versionItem->setFont(largeFont);
        addressItem->setFont(largeFont);
        busItem->setFont(largeFont);

        // Special highlight for Ford ECUs
        if (m_paramValues.safetyModel == 6) {        // 6 is the Ford safety model
          ecuItem->setForeground(QColor(0, 255, 0)); // Bright green
          // Still use large font, just make it bold
          ecuItem->setFont(QFont("Arial", 28, QFont::Bold));
        }

        m_firmwareTable->setItem(row, 0, ecuItem);
        m_firmwareTable->setItem(row, 1, versionItem);
        m_firmwareTable->setItem(row, 2, addressItem);
        m_firmwareTable->setItem(row, 3, busItem);
      }
    }

    //
    // Device Tab
    //

    // Update Device Status section
    try {
      if (m_deviceGroups.contains("Device Status")) {
        int idx = 0;
        auto &group = m_deviceGroups["Device Status"];
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatDeviceType(m_deviceValues.deviceType));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatBool(m_deviceValues.started));
        if (idx < group.size())
          group[idx++].valueLabel->setText(formatThermalStatus(m_deviceValues.thermalStatus));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1%").arg(m_deviceValues.fanSpeedPercentDesired));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1%").arg(m_deviceValues.screenBrightnessPercent));

        // Format last ping time
        // QString pingTimeStr = "N/A";
        // if (m_deviceValues.lastAthenaPingTime > 0) {
        //   // Convert from nanoseconds to milliseconds for QDateTime
        //   QDateTime pingTime = QDateTime::fromMSecsSinceEpoch(m_deviceValues.lastAthenaPingTime / 1000000);
        //   pingTimeStr = pingTime.toString("yyyy-MM-dd hh:mm:ss");
        // }
        // if (idx < group.size())
        //   group[idx++].valueLabel->setText(pingTimeStr);
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating Device Status section:" << e.what() << std::endl;
    }

    // Update Power section
    if (m_deviceGroups.contains("Power")) {
      int idx = 0;
      auto &group = m_deviceGroups["Power"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 W").arg(m_deviceValues.powerDrawW, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 W").arg(m_deviceValues.somPowerDrawW, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 µWh").arg(m_deviceValues.offroadPowerUsageUwh));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 µWh").arg(m_deviceValues.carBatteryCapacityUwh));
    }

    // Update Network section
    if (m_deviceGroups.contains("Network")) {
      int idx = 0;
      auto &group = m_deviceGroups["Network"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatNetworkType(m_deviceValues.networkType));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatNetworkStrength(m_deviceValues.networkStrength));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_deviceValues.networkMetered));

      // If we have network, show the details
      if (m_deviceValues.networkType != 0) { // 0 = None
        if (idx < group.size())
          group[idx++].valueLabel->setText(m_deviceValues.networkInfo.technology);
        if (idx < group.size())
          group[idx++].valueLabel->setText(m_deviceValues.networkInfo.operator_);
        if (idx < group.size())
          group[idx++].valueLabel->setText(m_deviceValues.networkInfo.band);
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_deviceValues.networkInfo.channel));
        if (idx < group.size())
          group[idx++].valueLabel->setText(m_deviceValues.networkInfo.state);
      } else {
        // Set N/A for all network details when there's no network
        for (int i = 0; i < 6; i++) {
          if (idx < group.size())
            group[idx++].valueLabel->setText("N/A");
        }
      }

      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBytes(m_deviceValues.networkStats.wwanTx));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBytes(m_deviceValues.networkStats.wwanRx));
    }

    // Update System Usage section
    if (m_deviceGroups.contains("System Usage")) {
      int idx = 0;
      auto &group = m_deviceGroups["System Usage"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1%").arg(m_deviceValues.freeSpacePercent, 0, 'f', 1));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1%").arg(m_deviceValues.memoryUsagePercent));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1%").arg(m_deviceValues.gpuUsagePercent));

      // Create CPU usage string showing all cores with line breaks every 4 cores
      QString cpuUsageStr = "";
      for (int i = 0; i < m_deviceValues.cpuUsagePercent.size(); i++) {
        cpuUsageStr += QString("C%1:%2% ").arg(i).arg(m_deviceValues.cpuUsagePercent[i]);
        // Add a line break after every 4 cores (except at the end)
        if ((i + 1) % 2 == 0 && i < m_deviceValues.cpuUsagePercent.size() - 1) {
          cpuUsageStr += "<br>";
        }
      }
      if (idx < group.size()) {
        QLabel *cpuUsageLabel = group[idx++].valueLabel;
        cpuUsageLabel->setText(cpuUsageStr);
        cpuUsageLabel->setTextFormat(Qt::RichText); // Enable rich text formatting to support HTML tags
      }
    }

    // Update System & Temperatures section
    try {
      if (m_deviceGroups.contains("System & Temperatures")) {
        int idx = 0;
        auto &group = m_deviceGroups["System & Temperatures"];

        // System usage information
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1%").arg(m_deviceValues.freeSpacePercent, 0, 'f', 1));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1%").arg(m_deviceValues.memoryUsagePercent));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1%").arg(m_deviceValues.gpuUsagePercent));

        // Create CPU usage string showing all cores with line breaks every 4 cores
        QString cpuUsageStr = "";
        for (int i = 0; i < m_deviceValues.cpuUsagePercent.size(); i++) {
          cpuUsageStr += QString("C%1:%2% ").arg(i).arg(m_deviceValues.cpuUsagePercent[i]);
          // Add a line break after every 4 cores (except at the end)
          if ((i + 1) % 4 == 0 && i < m_deviceValues.cpuUsagePercent.size() - 1) {
            cpuUsageStr += "<br>";
          }
        }
        if (idx < group.size()) {
          QLabel *cpuUsageLabel = group[idx++].valueLabel;
          cpuUsageLabel->setText(cpuUsageStr);
          cpuUsageLabel->setTextFormat(Qt::RichText); // Enable rich text formatting to support HTML tags
        }

        // CPU temps - add line breaks for better display
        QString cpuTempStr = "";
        for (int i = 0; i < m_deviceValues.cpuTempC.size(); i++) {
          if (i > 0 && i % 4 == 0) { // Add a line break after every 4 values
            cpuTempStr += "<br>";
          } else if (i > 0) {
            cpuTempStr += " ";
          }
          cpuTempStr += QString("%1°C").arg(m_deviceValues.cpuTempC[i], 0, 'f', 1);
        }

        if (idx < group.size()) {
          QLabel *cpuTempLabel = group[idx++].valueLabel;
          cpuTempLabel->setText(cpuTempStr.isEmpty() ? "N/A" : cpuTempStr);
          cpuTempLabel->setTextFormat(Qt::RichText); // Enable rich text formatting
        }

        // GPU temps
        QString gpuTempStr = "";
        for (int i = 0; i < m_deviceValues.gpuTempC.size(); i++) {
          if (i > 0)
            gpuTempStr += " ";
          gpuTempStr += QString("%1°C").arg(m_deviceValues.gpuTempC[i], 0, 'f', 1);
        }
        if (idx < group.size())
          group[idx++].valueLabel->setText(gpuTempStr.isEmpty() ? "N/A" : gpuTempStr);

        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1°C").arg(m_deviceValues.memoryTempC, 0, 'f', 1));

        // NVME temps
        QString nvmeTempStr = "";
        for (int i = 0; i < m_deviceValues.nvmeTempC.size(); i++) {
          if (i > 0)
            nvmeTempStr += " ";
          nvmeTempStr += QString("%1°C").arg(m_deviceValues.nvmeTempC[i], 0, 'f', 1);
        }
        if (idx < group.size())
          group[idx++].valueLabel->setText(nvmeTempStr.isEmpty() ? "N/A" : nvmeTempStr);

        // Modem temps
        QString modemTempStr = "";
        for (int i = 0; i < m_deviceValues.modemTempC.size(); i++) {
          if (i > 0)
            modemTempStr += " ";
          modemTempStr += QString("%1°C").arg(m_deviceValues.modemTempC[i], 0, 'f', 1);
        }
        if (idx < group.size())
          group[idx++].valueLabel->setText(modemTempStr.isEmpty() ? "N/A" : modemTempStr);

        // PMIC temps
        QString pmicTempStr = "";
        for (int i = 0; i < m_deviceValues.pmicTempC.size(); i++) {
          if (i > 0)
            pmicTempStr += " ";
          pmicTempStr += QString("%1°C").arg(m_deviceValues.pmicTempC[i], 0, 'f', 1);
        }
        if (idx < group.size())
          group[idx++].valueLabel->setText(pmicTempStr.isEmpty() ? "N/A" : pmicTempStr);

        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1°C").arg(m_deviceValues.intakeTempC, 0, 'f', 1));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1°C").arg(m_deviceValues.exhaustTempC, 0, 'f', 1));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1°C").arg(m_deviceValues.caseTempC, 0, 'f', 1));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1°C").arg(m_deviceValues.maxTempC, 0, 'f', 1));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error updating System & Temperatures section:" << e.what() << std::endl;
    }
  } catch (const std::exception &e) {
    std::cout << "Error updating labels:" << e.what() << std::endl;
  }
}

// Helper formatting functions
QString OtherDebugPanel::formatBool(bool value, const QString &trueText, const QString &falseText) { return value ? trueText : falseText; }

QString OtherDebugPanel::formatSafetyModel(int model) {
  QString modelText;
  switch (model) {
  case 0:
    modelText = "Silent";
    break;
  case 1:
    modelText = "Honda Nidec";
    break;
  case 2:
    modelText = "Toyota";
    break;
  case 3:
    modelText = "ELM327";
    break;
  case 4:
    modelText = "GM";
    break;
  case 5:
    modelText = "Honda Bosch Giraffe";
    break;
  case 6:
    modelText = "Ford";
    break;
  case 7:
    modelText = "Cadillac";
    break;
  case 8:
    modelText = "Hyundai";
    break;
  case 9:
    modelText = "Chrysler";
    break;
  case 10:
    modelText = "Tesla";
    break;
  case 11:
    modelText = "Subaru";
    break;
  case 12:
    modelText = "GM Passive";
    break;
  case 13:
    modelText = "Mazda";
    break;
  case 14:
    modelText = "Nissan";
    break;
  case 15:
    modelText = "Volkswagen";
    break;
  case 16:
    modelText = "Toyota IPAS";
    break;
  case 17:
    modelText = "All Output";
    break;
  case 18:
    modelText = "GM ASCM";
    break;
  case 19:
    modelText = "No Output";
    break;
  case 20:
    modelText = "Honda Bosch";
    break;
  case 21:
    modelText = "Volkswagen PQ";
    break;
  case 22:
    modelText = "Subaru Pre-Global";
    break;
  case 23:
    modelText = "Hyundai Legacy";
    break;
  case 24:
    modelText = "Hyundai Community";
    break;
  case 25:
    modelText = "Volkswagen MLB";
    break;
  case 26:
    modelText = "Hongqi";
    break;
  case 27:
    modelText = "Body";
    break;
  case 28:
    modelText = "Hyundai CanFD";
    break;
  case 29:
    modelText = "Volkswagen MQB Evo";
    break;
  case 30:
    modelText = "Chrysler CUSW";
    break;
  case 31:
    modelText = "PSA";
    break;
  case 32:
    modelText = "FCA Giorgio";
    break;
  case 33:
    modelText = "Rivian";
    break;
  case 34:
    modelText = "Volkswagen MEB";
    break;
  default:
    modelText = "Unknown";
    break;
  }

  // Highlight Ford safety model
  if (model == 6) { // Ford
    modelText = "<span style='color: #00FF00; font-weight: bold;'>" + modelText + "</span>";
  }

  return modelText;
}

QString OtherDebugPanel::formatEcu(int ecu) {
  switch (ecu) {
  case 0:
    return "EPS";
  case 1:
    return "ABS";
  case 2:
    return "FWD Radar";
  case 3:
    return "FWD Camera";
  case 4:
    return "Engine";
  case 5:
    return "Unknown";
  case 6:
    return "DSU";
  case 7:
    return "Parking ADAS";
  case 8:
    return "Transmission";
  case 9:
    return "SRS";
  case 10:
    return "Gateway";
  case 11:
    return "HUD";
  case 12:
    return "Combination Meter";
  case 13:
    return "VSA";
  case 14:
    return "PFI";
  case 15:
    return "Electric Brake Booster";
  case 16:
    return "Shift By Wire";
  case 17:
    return "Debug";
  case 18:
    return "Hybrid";
  case 19:
    return "ADAS";
  case 20:
    return "HVAC";
  case 21:
    return "Corner Radar";
  case 22:
    return "EPB";
  case 23:
    return "Telematics";
  case 24:
    return "Body";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatTransmissionType(int type) {
  switch (type) {
  case 0:
    return "Unknown";
  case 1:
    return "Automatic";
  case 2:
    return "Manual";
  case 3:
    return "Direct";
  case 4:
    return "CVT";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatFingerprintSource(int source) {
  switch (source) {
  case 0:
    return "CAN";
  case 1:
    return "FW";
  case 2:
    return "Fixed";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatNetworkLocation(int location) {
  switch (location) {
  case 0:
    return "FWD Camera";
  case 1:
    return "Gateway";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatLongControlState(int state) {
  switch (state) {
  case 0:
    return "Off";
  case 1:
    return "PID";
  case 2:
    return "Stopping";
  case 3:
    return "Starting";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatNetworkType(int type) {
  switch (type) {
  case 0:
    return "None";
  case 1:
    return "WiFi";
  case 2:
    return "Cell 2G";
  case 3:
    return "Cell 3G";
  case 4:
    return "Cell 4G";
  case 5:
    return "Cell 5G";
  case 6:
    return "Ethernet";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatNetworkStrength(int strength) {
  switch (strength) {
  case 0:
    return "Unknown";
  case 1:
    return "Poor";
  case 2:
    return "Moderate";
  case 3:
    return "Good";
  case 4:
    return "Great";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatDeviceType(int type) {
  switch (type) {
  case 0:
    return "Unknown";
  case 1:
    return "Neo";
  case 2:
    return "Chffr Android";
  case 3:
    return "Chffr iOS";
  case 4:
    return "Tici";
  case 5:
    return "PC";
  case 6:
    return "Tizi";
  case 7:
    return "Mici";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatThermalStatus(int status) {
  switch (status) {
  case 0:
    return "Green";
  case 1:
    return "Yellow";
  case 2:
    return "Red";
  case 3:
    return "Danger";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatBytes(qint64 bytes) {
  const QStringList suffixes = {"B", "KB", "MB", "GB", "TB"};

  float num = bytes;
  int level = 0;

  while (num >= 1024.0 && level < suffixes.size() - 1) {
    num /= 1024.0;
    level++;
  }

  return QString("%1 %2").arg(num, 0, 'f', (level > 0) ? 2 : 0).arg(suffixes[level]);
}

void OtherDebugPanel::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Draw background with material design styling
  QPainterPath path;
  path.addRoundedRect(rect().adjusted(5, 5, -5, -5), 15, 15);

  // Material design background with slight gradient
  QLinearGradient gradient(0, 0, 0, height());
  gradient.setColorAt(0, QColor(30, 30, 30, 230));
  gradient.setColorAt(1, QColor(20, 20, 20, 230));

  p.fillPath(path, gradient);

  // Material design subtle border
  p.setPen(QPen(QColor(60, 60, 60, 150), 1));
  p.drawPath(path);

  QWidget::paintEvent(event);
}
