/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 * Copyright (c) 2024-, BluePilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and BluePilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include <chrono>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_controls.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_dialogs.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/sunnypilot/qt/offroad/settings/osm/locations_fetcher.h"
#include "selfdrive/ui/sunnypilot/ui.h"
#include "system/hardware/hw.h"

// Use unique names to avoid conflicts with osm_panel.h
namespace BPOsmConstants {
  constexpr int FAST_REFRESH_INTERVAL = 1000;  // ms
  constexpr int SLOW_REFRESH_INTERVAL = 5000;  // ms
  static const QString MAP_PATH = Hardware::PC() ? QDir::homePath() + "/.comma/media/0/osm/offline/" : "/data/media/0/osm/offline/";
}

class BPOsmPanel : public QWidget {
  Q_OBJECT

public:
  explicit BPOsmPanel(QWidget *parent = nullptr);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void setupUI();
  void createMapInfoGroup();
  void createCountrySelectionGroup();
  void createDatabaseUpdateGroup();

  QGroupBox *createStyledGroupBox(const QString &title);

  void updateLabels();
  void updateDownloadProgress();
  void updateMapSize();

  // Helper functions
  static int extractIntFromJson(const QJsonObject &json, const QString &key);
  QString processUpdateStatus(bool pending_update_check, int total_files, int downloaded_files,
                              const QJsonObject &json, bool failed_state);
  QString formatSize(quint64 size) const;
  static QString formatDownloadStatus(const QJsonObject &json);
  static QString formatTime(long timeInSeconds);
  static QString calculateElapsedTime(const QJsonObject &jsonData,
                                     const std::chrono::system_clock::time_point &startTime);
  static QString calculateETA(const QJsonObject &jsonData,
                             const std::chrono::system_clock::time_point &startTime);
  static quint64 getDirSize(QString dirPath);

  bool showBPConfirmation(const QString &message, const QString &confirmText);

  // Location fetcher methods
  std::vector<std::tuple<QString, QString, QString, QString>> getOsmLocations(
    const std::tuple<QString, QString> &customLocation = defaultLocation) const {
    return locationsFetcher.getOsmLocations(customLocation);
  }

  std::vector<std::tuple<QString, QString, QString, QString>> getUsStatesLocations(
    const std::tuple<QString, QString> &customLocation = defaultLocation) const {
    return locationsFetcher.getUsStatesLocations(customLocation);
  }

  bool isWifi() const {
    return sm["deviceState"].getDeviceState().getNetworkType() == cereal::DeviceState::NetworkType::WIFI;
  }
  bool isMetered() const {
    return sm["deviceState"].getDeviceState().getNetworkMetered();
  }

private slots:
  void onCountryButtonClicked();
  void onStateButtonClicked();
  void onUpdateButtonClicked();
  void onDeleteMapsButtonClicked();

private:
  QVBoxLayout *mainLayout;

  // Map Info Group
  QGroupBox *mapInfoGroup;
  QLabel *mapdVersionLabel;
  QLabel *mapdVersionValue;
  BPButton *deleteMapsBtn;
  QLabel *deleteMapsValue;

  // Country Selection Group
  QGroupBox *countrySelectionGroup;
  BPButton *countryBtn;
  QLabel *countryValue;
  BPButton *stateBtn;
  QLabel *stateValue;
  QWidget *stateWidget;  // Container for state button/value pair

  // Database Update Group
  QGroupBox *databaseUpdateGroup;
  BPButton *updateBtn;
  QLabel *updateStatusValue;
  QLabel *etaLabel;
  QLabel *etaValue;
  QLabel *elapsedLabel;
  QLabel *elapsedValue;
  QWidget *etaWidget;       // Container for ETA label/value pair
  QWidget *elapsedWidget;   // Container for Elapsed label/value pair

  Params params;
  Params mem_params{Hardware::PC() ? "" : "/dev/shm/params"};
  const SubMaster &sm = *uiStateSP()->sm;

  QTimer *timer;
  bool osm_download_in_progress = false;
  bool download_failed_state = false;
  quint64 mapsDirSize = 0;
  std::string mapd_version;
  std::string osm_download_locations;
  std::optional<QFuture<quint64>> mapSizeFuture;
  std::optional<std::chrono::system_clock::time_point> lastDownloadedTimePoint;

  LocationsFetcher locationsFetcher;
};
