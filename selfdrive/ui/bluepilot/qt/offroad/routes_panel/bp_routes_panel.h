// bp_routes_panel.h - Web-Based Routes Panel with BP Toggle
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QGroupBox>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "common/params.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_controls.h"

class BPRoutesPanel : public QWidget {
  Q_OBJECT

public:
  explicit BPRoutesPanel(QWidget *parent = nullptr);
  ~BPRoutesPanel();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private slots:
  void updateServerStatus();
  void toggleServer(bool enabled);
  void refreshStats();
  void toggleCellularAccess(bool enabled);
  void updateCellularStatus();

private:
  void setupUI();
  bool isServerRunning();
  QString getServerUrl();
  QString getWiFiIP();
  void fetchRouteStats();
  void generateQRCode(const QString &url);
  void fetchDetailedStatus();

  // UI Elements
  QVBoxLayout *mainLayout;

  // Server control section
  BPToggle *serverToggle;
  QLabel *serverStatusLabel;
  QLabel *urlLabel;
  QLabel *qrCodeLabel;

  // Stats section
  QGroupBox *statsFrame;
  QLabel *totalRoutesLabel;
  QLabel *totalSizeLabel;
  QLabel *newestRouteLabel;
  BPButton *refreshStatsButton;

  // Cellular access section
  QGroupBox *cellularGroup;
  BPToggleControl *cellularToggle;
  BPSelectionControl *cellularTimeoutSelection;
  QLabel *cellularStatusLabel;
  QLabel *cellularWarningLabel;

  // Help text
  QLabel *helpLabel;

  // State
  Params params;
  QTimer *statusTimer;
  QNetworkAccessManager *networkManager;
  bool serverEnabled;
  int routeCount;
  QString totalSize;
};
