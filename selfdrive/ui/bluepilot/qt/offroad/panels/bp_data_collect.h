// bp_data_collect_panel.h
#pragma once

#include "bp_panel_base.h"
#include <QLabel>
#include <QTimer>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QScrollArea>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui.h"
#else
#include "selfdrive/ui/ui.h"
#endif

class BPDataCollectPanel : public BPPanelBase {
  Q_OBJECT

public:
  explicit BPDataCollectPanel(QWidget *parent = nullptr);
  virtual ~BPDataCollectPanel();

private slots:
  void updateStats();
  void refreshDisplay();

private:
  // Utility functions
  QString formatNumber(int value);
  QString getElapsedTimeText(int timestamp);
  QString formatFileSize(double sizeInBytes);
  void updateUploadStatus(); // New method to update upload status

  // UI Components
  QTimer *statsUpdateTimer;
  QProgressBar *syncProgressBar;
  QScrollArea *routesScrollArea;
  QWidget *scrollContent;
  QVBoxLayout *scrollLayout;
  QWidget *statsContainer;
  QLabel *totalRoutesLabel;
  QLabel *routesUploadedLabel;
  QLabel *lastSyncLabel;
  QLabel *syncStatusLabel;
  QLabel *uploadStatusValue; // Label for upload status value

  // Stats display widgets
  QWidget *createStatsCard(QString title, QString value, QString subtitle = "", QString iconPath = "");
  QWidget *createRouteCard(QString routeId, QString timestamp, int segmentCount, bool uploaded, QString uploadTime, QString fingerprint);

  // Stats data
  int totalRoutesFound = 0;
  int totalRoutesUploaded = 0;
  int lastSyncTimestamp = 0;
  QString lastSyncRouteId;
  int successfulSyncs = 0;
  int failedSyncs = 0;

  // Current sync status
  QString syncStatus = "idle";
  QString currentRouteId;
  QMap<QString, QVariant> statusDetails;

  // Routes data
  struct RouteInfo {
    QString routeId;
    int timestamp;
    int segmentCount;
    bool uploaded;
    int uploadTimestamp;
    QString fingerprint;
  };
  QList<RouteInfo> routes;

  // Stats collection
  void fetchSyncStats();
  void fetchSyncStatus();

  // Clean up resources
  void cleanupWidgets();
};