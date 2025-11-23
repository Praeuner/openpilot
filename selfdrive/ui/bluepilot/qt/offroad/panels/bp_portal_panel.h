// bp_portal_panel.h - BluePilot Portal Panel
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

class BPPortalPanel : public QWidget {
  Q_OBJECT

public:
  explicit BPPortalPanel(QWidget *parent = nullptr);
  ~BPPortalPanel();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private slots:
  void updateServerStatus();
  void toggleServer(bool enabled);
  void refreshStats();
  void fetchServerErrors();
  void updateWebSocketStatus();

private:
  void setupUI();
  bool isServerRunning();
  QString getServerUrl();
  QString getWiFiIP();
  void fetchRouteStats();
  void generateQRCode(const QString &url);

  // UI Elements
  QVBoxLayout *mainLayout;

  // Server control section
  BPToggle *serverToggle;
  QLabel *serverStatusLabel;
  QLabel *urlLabel;
  QLabel *qrCodeLabel;
  QLabel *websocketStatusBadge;

  // Stats section
  QGroupBox *statsFrame;
  QLabel *totalRoutesLabel;
  QLabel *totalSizeLabel;
  QLabel *newestRouteLabel;
  BPButton *refreshStatsButton;

  // WiFi hotspot shortcut section
  QGroupBox *hotspotGroup;
  BPToggleControl *hotspotToggle;

  // Server errors section (NEW)
  QGroupBox *errorsFrame;
  QLabel *errorStatusLabel;
  QLabel *errorListLabel;
  QPushButton *clearErrorsButton;

  // Help text
  QLabel *helpLabel;

  // State
  Params params;
  QTimer *statusTimer;
  QTimer *errorTimer;  // Separate timer for error checking
  QNetworkAccessManager *networkManager;
  bool serverEnabled;
  int routeCount;
  QString totalSize;
  int recentErrorCount;
  qint64 lastToggleOnTimestamp;  // Timestamp when server was last enabled (for startup grace period)
};
