// selfdrive/ui/bluepilot/qt/offroad/panels/bp_network_panel.h
// BluePilot Network Panel - Native implementation with BP styling
// A clone of the stock network panel using BP controls and layout

#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>

#include "selfdrive/ui/qt/network/wifi_manager.h"
#include "selfdrive/ui/qt/network/networking.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_controls.h"
#include "common/params.h"

/**
 * BPNetworkPanel - Native network settings panel with BP styling
 *
 * This panel provides WiFi, tethering, and cellular management using
 * BluePilot controls and styling while following the stock panel's
 * lifecycle management patterns.
 */
class BPNetworkPanel : public QWidget {
  Q_OBJECT

public:
  explicit BPNetworkPanel(QWidget *parent = nullptr);
  void setPrimeType(PrimeState::Type type);
  WifiManager* getWifiManager() { return wifi; }

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void setupUI();
  void createWifiNetworksGroup();
  void createWifiManagementGroup();
  void createTetheringGroup();
  void createCellularGroup();

  // Helper methods
  QGroupBox* createStyledGroupBox(const QString &title);
  void refreshAll();
  void updateTetheringPasswordVisibility();
  void updateIpAddress();

  // Core components (shared WiFi manager)
  WifiManager *wifi;
  Params params;

  // Layout
  QVBoxLayout *mainLayout;

  // Groups
  QGroupBox *wifiNetworksGroup;
  QGroupBox *wifiManagementGroup;
  QGroupBox *tetheringGroup;
  QGroupBox *cellularGroup;

  // WiFi Networks Group
  QLabel *ipAddressLabel;
  QPushButton *scanButton;
  QPushButton *hiddenNetworkBtn;
  BPWifiListControl *wifiList;
  BPWifiMeteredControl *wifiMeteredControl;

  // WiFi Management Group (empty now - can be removed later)

  // Tethering Group
  BPToggleControl *tetheringToggle;
  BPCommandControl *tetheringPasswordBtn;

  // Cellular Group (tici only)
  BPToggleControl *gsmRoamingToggle;
  BPToggleControl *gsmMeteredToggle;
  BPCommandControl *apnSettingBtn;

  // Timers
  QTimer *refreshTimer;

private slots:
  void onTetheringToggled(bool enabled);
  void onGsmRoamingToggled(bool enabled);
  void onGsmMeteredToggled(bool enabled);
};

