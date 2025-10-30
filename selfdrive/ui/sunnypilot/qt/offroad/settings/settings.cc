/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/sunnypilot/qt/offroad/settings/settings.h"

#include "selfdrive/ui/sunnypilot/qt/widgets/scrollview.h"
#include "selfdrive/ui/qt/offroad/firehose.h"
#include "selfdrive/ui/sunnypilot/qt/network/networking.h"

// #include "selfdrive/ui/sunnypilot/qt/offroad/settings/developer_panel.h"
// #include "selfdrive/ui/sunnypilot/qt/offroad/settings/device_panel.h"
// #include "selfdrive/ui/sunnypilot/qt/offroad/settings/display_panel.h"
// #include "selfdrive/ui/sunnypilot/qt/offroad/settings/models_panel.h"
// #include "selfdrive/ui/sunnypilot/qt/offroad/settings/software_panel.h"
#include "selfdrive/ui/sunnypilot/qt/offroad/settings/sunnylink_panel.h"
// #include "selfdrive/ui/sunnypilot/qt/offroad/settings/lateral_panel.h"
// #include "selfdrive/ui/sunnypilot/qt/offroad/settings/longitudinal_panel.h"
// #include "selfdrive/ui/sunnypilot/qt/offroad/settings/osm_panel.h"
#include "selfdrive/ui/sunnypilot/qt/offroad/settings/trips_panel.h"
// #include "selfdrive/ui/sunnypilot/qt/offroad/settings/vehicle_panel.h"
// #include "selfdrive/ui/sunnypilot/qt/offroad/settings/visuals_panel.h"

#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_base_view.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_nav_bar_view.h"

#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_statistics_panel.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_network_panel.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_software_panel.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_models_panel.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_osm_panel.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/bp_routes_panel.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_controls.h"

TogglesPanelSP::TogglesPanelSP(SettingsWindowSP *parent) : TogglesPanel(parent) {
  QObject::connect(uiStateSP(), &UIStateSP::uiUpdate, this, &TogglesPanelSP::updateState);
}

void TogglesPanelSP::updateState(const UIStateSP &s) {
  TogglesPanel::updateState(s);
}

SettingsWindowSP::SettingsWindowSP(QWidget *parent) : SettingsWindow(parent) {
  // setup two main layouts
  sidebar_widget = new QWidget;
  QVBoxLayout *sidebar_layout = new QVBoxLayout(sidebar_widget);
  panel_widget = new QStackedWidget();

  // setup layout for close button
  QVBoxLayout *close_btn_layout = new QVBoxLayout;
  close_btn_layout->setContentsMargins(0, 0, 0, 20);

  // close button
  QPushButton *close_btn = new QPushButton(tr("×"));
  close_btn->setStyleSheet(R"(
    QPushButton {
      font-size: 140px;
      padding-bottom: 20px;
      border-radius: 76px;
      background-color: #292929;
      font-weight: 400;
    }
    QPushButton:pressed {
      background-color: #3B3B3B;
    }
  )");
  close_btn->setFixedSize(152, 152);
  close_btn_layout->addWidget(close_btn, 0, Qt::AlignLeft);
  QObject::connect(close_btn, &QPushButton::clicked, this, &SettingsWindowSP::closeSettings);

  // setup buttons widget
  QWidget *buttons_widget = new QWidget;
  QVBoxLayout *buttons_layout = new QVBoxLayout(buttons_widget);
  buttons_layout->setMargin(0);
  buttons_layout->addSpacing(10);

  // setup panels
  // DevicePanelSP *device = new DevicePanelSP(this);
  // QObject::connect(device, &DevicePanelSP::reviewTrainingGuide, this, &SettingsWindowSP::reviewTrainingGuide);
  // QObject::connect(device, &DevicePanelSP::showDriverView, this, &SettingsWindowSP::showDriverView);

  // TogglesPanelSP *toggles = new TogglesPanelSP(this);
  // QObject::connect(this, &SettingsWindowSP::expandToggleDescription, toggles, &TogglesPanel::expandToggleDescription);
  // QObject::connect(this, &SettingsWindowSP::scrollToToggle, toggles, &TogglesPanel::scrollToToggle);

  // auto networking = new NetworkingSP(this);
  // QObject::connect(uiState()->prime_state, &PrimeState::changed, networking, &NetworkingSP::setPrimeType);

  // BP Network Panel (Native C++ version)
  auto bpNetworkPanel = new BPNetworkPanel(this);
  QObject::connect(uiState()->prime_state, &PrimeState::changed, bpNetworkPanel, &BPNetworkPanel::setPrimeType);

  BPBaseView *bpVisualsView = new BPBaseView(this);
  bpVisualsView->initialize("/selfdrive/ui/bluepilot/menus/bp_visuals_panel.json");

  BPBaseView *bpDeviceView = new BPBaseView(this);
  bpDeviceView->initialize("/selfdrive/ui/bluepilot/menus/bp_device_panel.json");
  QObject::connect(bpDeviceView, &BPBaseView::showDriverView, this, &SettingsWindowSP::showDriverView);
  QObject::connect(bpDeviceView, &BPBaseView::reviewTrainingGuide, this, &SettingsWindowSP::reviewTrainingGuide);

  BPBaseView *bpDisplayView = new BPBaseView(this);
  bpDisplayView->initialize("/selfdrive/ui/bluepilot/menus/bp_display_panel.json");

  BPBaseView *bpTogglesView = new BPBaseView(this);
  bpTogglesView->initialize("/selfdrive/ui/bluepilot/menus/bp_toggles_panel.json");

  BPBaseView *bpCruiseView = new BPBaseView(this);
  bpCruiseView->initialize("/selfdrive/ui/bluepilot/menus/bp_cruise_panel.json");

  BPBaseView *bpSteeringView = new BPBaseView(this);
  bpSteeringView->initialize("/selfdrive/ui/bluepilot/menus/bp_steering_panel.json");

  BPBaseView *bpDeveloperView = new BPBaseView(this);
  bpDeveloperView->initialize("/selfdrive/ui/bluepilot/menus/bp_developer_panel.json");

  BPBaseView *bpVehicleView = new BPBaseView(this);
  bpVehicleView->initialize("/selfdrive/ui/bluepilot/menus/bp_vehicle_panel.json");

  QList<PanelInfo> panels = {
    // PanelInfo("   " + tr("Device"), device, "../../sunnypilot/selfdrive/assets/offroad/icon_home.svg"),
    PanelInfo("   " + tr("Device"), bpDeviceView, "../../sunnypilot/selfdrive/assets/offroad/icon_home.svg"),
    // PanelInfo("   " + tr("Network"), networking, "../assets/icons/network.png"),
    PanelInfo("   " + tr("Network"), bpNetworkPanel, "../assets/icons/network.png"),
    PanelInfo("   " + tr("Routes"), new BPRoutesPanel(this), "../assets/offroad/icon_routes.png"),
    // PanelInfo("   " + tr("Toggles"), toggles, "../../sunnypilot/selfdrive/assets/offroad/icon_toggle.png"),
    PanelInfo("   " + tr("Toggles"), bpTogglesView, "../../sunnypilot/selfdrive/assets/offroad/icon_toggle.png"),
    // PanelInfo("   " + tr("Steering"), new LateralPanel(this), "../../sunnypilot/selfdrive/assets/offroad/icon_lateral.png"),
    PanelInfo("   " + tr("Steering"), bpSteeringView, "../../sunnypilot/selfdrive/assets/offroad/icon_lateral.png"),
    // PanelInfo("   " + tr("Cruise"), new LongitudinalPanel(this), "../assets/icons/speed_limit.png"),
    PanelInfo("   " + tr("Cruise"), bpCruiseView, "../assets/icons/speed_limit.png"),
    // PanelInfo("   " + tr("Visuals"), new VisualsPanel(this), "../../sunnypilot/selfdrive/assets/offroad/icon_visuals.png"),
    PanelInfo("   " + tr("Visuals"), bpVisualsView, "../../sunnypilot/selfdrive/assets/offroad/icon_visuals.png"),
    // PanelInfo("   " + tr("Display"), new DisplayPanel(this), "../../sunnypilot/selfdrive/assets/offroad/icon_display.png"),
    PanelInfo("   " + tr("Display"), bpDisplayView, "../../sunnypilot/selfdrive/assets/offroad/icon_display.png"),
    // PanelInfo("   " + tr("Software"), new SoftwarePanelSP(this), "../../sunnypilot/selfdrive/assets/offroad/icon_software.png"),
    PanelInfo("   " + tr("Software"), new BPSoftwarePanel(this), "../../sunnypilot/selfdrive/assets/offroad/icon_software.png"),
    // PanelInfo("   " + tr("Models"), new ModelsPanel(this), "../../sunnypilot/selfdrive/assets/offroad/icon_models.png"),
    PanelInfo("   " + tr("Models"), new BPModelsPanel(this), "../../sunnypilot/selfdrive/assets/offroad/icon_models.png"),
    // PanelInfo("   " + tr("OSM"), new OsmPanel(this), "../../sunnypilot/selfdrive/assets/offroad/icon_map.png"),
    PanelInfo("   " + tr("OSM"), new BPOsmPanel(this), "../../sunnypilot/selfdrive/assets/offroad/icon_map.png"),
    PanelInfo("   " + tr("Trips"), new TripsPanel(this), "../../sunnypilot/selfdrive/assets/offroad/icon_trips.png"),
    // PanelInfo("   " + tr("Vehicle"), new VehiclePanel(this), "../../sunnypilot/selfdrive/assets/offroad/icon_vehicle.png"),
    PanelInfo("   " + tr("Vehicle"), bpVehicleView, "../../sunnypilot/selfdrive/assets/offroad/icon_vehicle.png"),
    PanelInfo("   " + tr("Firehose"), new FirehosePanel(this), "../../sunnypilot/selfdrive/assets/offroad/icon_firehose.svg"),
    PanelInfo("   " + tr("sunnylink"), new SunnylinkPanel(this), "../assets/icons/wifi_strength_full.svg"),
    // PanelInfo("   " + tr("Developer"), new DeveloperPanelSP(this), "../assets/icons/shell.png"),
    PanelInfo("   " + tr("Developer"), bpDeveloperView, "../assets/icons/shell.png"),
    PanelInfo("   " + tr("Statistics"), new BPStatisticsPanel(this), "../assets/offroad/icon_statistics.png"),
  };

  nav_btns = new QButtonGroup(this);
  for (auto &[name, panel, icon] : panels) {
    QPushButton *btn = new QPushButton(name);
    btn->setCheckable(true);
    btn->setChecked(nav_btns->buttons().size() == 0);
    btn->setIcon(QIcon(QPixmap(icon)));
    btn->setIconSize(QSize(70, 70));
    btn->setStyleSheet(R"(
      QPushButton {
        border-radius: 20px;
        width: 400px;
        height: 98px;
        color: #bdbdbd;
        border: none;
        background: none;
        font-size: 50px;
        font-weight: 500;
        text-align: left;
        padding-left: 22px;
      }
      QPushButton:checked {
        background-color: #696868;
        color: white;
      }
      QPushButton:pressed {
        color: #ADADAD;
      }
    )");
    btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    nav_btns->addButton(btn);
    buttons_layout->addWidget(btn, 0, Qt::AlignLeft | Qt::AlignBottom);

    const int lr_margin = (name != ("   " + tr("Network"))) ? 50 : 0;  // Network panel handles its own margins
    panel->setContentsMargins(lr_margin, 25, lr_margin, 25);

    // Use BPScrollView for BP panels (touch-friendly scrollbar), ScrollViewSP for others
    QScrollArea *panel_frame;
    if (qobject_cast<BPBaseView*>(panel) || qobject_cast<BPNavBarView*>(panel) ||
        qobject_cast<BPRoutesPanel*>(panel) || qobject_cast<BPStatisticsPanel*>(panel) ||
        qobject_cast<BPNetworkPanel*>(panel)) {
      panel_frame = new BPScrollView(panel, this);
    } else {
      panel_frame = new ScrollViewSP(panel, this);
    }
    panel_widget->addWidget(panel_frame);

    QObject::connect(btn, &QPushButton::clicked, [=, w = panel_frame]() {
      btn->setChecked(true);
      panel_widget->setCurrentWidget(w);
    });
  }
  sidebar_layout->setContentsMargins(50, 50, 25, 50);

  // main settings layout, sidebar + main panel
  QHBoxLayout *main_layout = new QHBoxLayout(this);

  // add layout for close button
  sidebar_layout->addLayout(close_btn_layout);

  // add layout for buttons scrolling
  ScrollViewSP *buttons_scrollview = new ScrollViewSP(buttons_widget, this);
  sidebar_layout->addWidget(buttons_scrollview);

  sidebar_widget->setFixedWidth(500);
  main_layout->addWidget(sidebar_widget);
  main_layout->addWidget(panel_widget);

  setStyleSheet(R"(
    * {
      color: white;
      font-size: 50px;
    }
    SettingsWindow {
      background-color: black;
    }
    QStackedWidget, ScrollViewSP {
      background-color: black;
      border-radius: 30px;
    }
  )");
}
