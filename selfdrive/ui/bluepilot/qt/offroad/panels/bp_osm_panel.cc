/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 * Copyright (c) 2024-, BluePilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and BluePilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_osm_panel.h"

#include <QEventLoop>
#include <QHBoxLayout>
#include <QSpacerItem>

#include "common/swaglog.h"

BPOsmPanel::BPOsmPanel(QWidget *parent) : QWidget(parent) {
  setupUI();

  // Connect to offroad transition
  connect(uiStateSP(), &UIStateSP::offroadTransition, this, [this](bool offroad) {
    updateLabels();
  });

  // Setup timer for periodic updates
  timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, &BPOsmPanel::updateLabels);
  timer->start(BPOsmConstants::FAST_REFRESH_INTERVAL);

  updateLabels();
}

void BPOsmPanel::setupUI() {
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(40, 40, 40, 40);
  mainLayout->setSpacing(30);

  createMapInfoGroup();
  createCountrySelectionGroup();
  createDatabaseUpdateGroup();

  mainLayout->addStretch();

  setStyleSheet(R"(
    BPOsmPanel {
      background-color: #1B1B1B;
    }
    BPOsmPanel QGroupBox BPButton {
      background-color: transparent;
    }
  )");
}

QGroupBox *BPOsmPanel::createStyledGroupBox(const QString &title) {
  QGroupBox *group = new QGroupBox(title, this);
  group->setStyleSheet(R"(
    QGroupBox {
      background-color: #242424;
      border: none;
      border-radius: 40px;
      margin-top: 50px;
      padding: 5px;
      font-size: 40px;
      font-weight: 500;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      padding: 5px 15px;
      border-top-left-radius: 15px;
      border-top-right-radius: 15px;
      border-bottom: none;
      margin-left: 35px;
      margin-top: 0px;
      background-color: #242424;
      color: #2196F3;
    }
    QGroupBox > QWidget {
      background-color: transparent;
    }
    QGroupBox::indicator {
      width: 0px;
    }
  )");
  group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  return group;
}

void BPOsmPanel::createMapInfoGroup() {
  mapInfoGroup = createStyledGroupBox(tr("Map Information"));
  QVBoxLayout *layout = new QVBoxLayout(mapInfoGroup);
  layout->setSpacing(20);
  layout->setContentsMargins(25, 25, 25, 25);

  // Mapd Version
  QWidget *versionWidget = new QWidget(this);
  QVBoxLayout *versionLayout = new QVBoxLayout(versionWidget);
  versionLayout->setSpacing(5);
  versionLayout->setContentsMargins(0, 0, 0, 0);

  mapdVersionLabel = new QLabel(tr("Mapd Version"), this);
  mapdVersionLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  versionLayout->addWidget(mapdVersionLabel);

  mapdVersionValue = new QLabel(tr("Loading..."), this);
  mapdVersionValue->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  versionLayout->addWidget(mapdVersionValue);

  layout->addWidget(versionWidget);

  // Divider
  layout->addWidget(BPUIHelpers::createDivider());

  // Delete Maps Button
  QWidget *deleteWidget = new QWidget(this);
  QHBoxLayout *deleteLayout = new QHBoxLayout(deleteWidget);
  deleteLayout->setSpacing(20);
  deleteLayout->setContentsMargins(0, 0, 0, 0);

  deleteMapsBtn = new BPButton(tr("DELETE"), this);
  deleteMapsBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  deleteMapsBtn->setMinimumHeight(100);
  deleteMapsBtn->setStyleSheet(R"(
    BPButton {
      background-color: #dc3545;
      border-radius: 40px;
      font-size: 42px;
      font-weight: 600;
      padding: 15px 40px;
    }
    BPButton:hover {
      background-color: #c82333;
    }
    BPButton:pressed {
      background-color: #bd2130;
    }
    BPButton:disabled {
      background-color: #424242;
      color: #888888;
    }
  )");
  connect(deleteMapsBtn, &BPButton::clicked, this, &BPOsmPanel::onDeleteMapsButtonClicked);
  deleteLayout->addWidget(deleteMapsBtn);

  QVBoxLayout *deleteStatusLayout = new QVBoxLayout();
  deleteStatusLayout->setSpacing(5);

  QLabel *deleteTitle = new QLabel(tr("Downloaded Maps"), this);
  deleteTitle->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  deleteStatusLayout->addWidget(deleteTitle);

  deleteMapsValue = new QLabel(tr("Calculating..."), this);
  deleteMapsValue->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  deleteMapsValue->setWordWrap(true);
  deleteStatusLayout->addWidget(deleteMapsValue);

  deleteLayout->addLayout(deleteStatusLayout, 1);
  layout->addWidget(deleteWidget);

  mainLayout->addWidget(mapInfoGroup);
}

void BPOsmPanel::createCountrySelectionGroup() {
  countrySelectionGroup = createStyledGroupBox(tr("Location Selection"));
  QVBoxLayout *layout = new QVBoxLayout(countrySelectionGroup);
  layout->setSpacing(20);
  layout->setContentsMargins(25, 25, 25, 25);

  // Country Selection
  QWidget *countryWidget = new QWidget(this);
  QHBoxLayout *countryLayout = new QHBoxLayout(countryWidget);
  countryLayout->setSpacing(20);
  countryLayout->setContentsMargins(0, 0, 0, 0);

  countryBtn = new BPButton(tr("SELECT"), this);
  countryBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  countryBtn->setMinimumHeight(100);
  countryBtn->setStyleSheet(R"(
    BPButton {
      background-color: #2196F3;
      border-radius: 40px;
      font-size: 42px;
      font-weight: 600;
      padding: 15px 40px;
    }
    BPButton:hover {
      background-color: #1E88E5;
    }
    BPButton:pressed {
      background-color: #1976D2;
    }
    BPButton:disabled {
      background-color: #424242;
      color: #888888;
    }
  )");
  connect(countryBtn, &BPButton::clicked, this, &BPOsmPanel::onCountryButtonClicked);
  countryLayout->addWidget(countryBtn);

  QVBoxLayout *countryStatusLayout = new QVBoxLayout();
  countryStatusLayout->setSpacing(5);

  QLabel *countryTitle = new QLabel(tr("Country"), this);
  countryTitle->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  countryStatusLayout->addWidget(countryTitle);

  countryValue = new QLabel(tr("None selected"), this);
  countryValue->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  countryValue->setWordWrap(true);
  countryStatusLayout->addWidget(countryValue);

  countryLayout->addLayout(countryStatusLayout, 1);
  layout->addWidget(countryWidget);

  // Divider
  countryStateDivider = BPUIHelpers::createDivider();
  layout->addWidget(countryStateDivider);

  // State Selection (initially hidden)
  stateWidget = new QWidget(this);
  QHBoxLayout *stateLayout = new QHBoxLayout(stateWidget);
  stateLayout->setSpacing(20);
  stateLayout->setContentsMargins(0, 0, 0, 0);

  stateBtn = new BPButton(tr("SELECT"), this);
  stateBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  stateBtn->setMinimumHeight(100);
  stateBtn->setStyleSheet(R"(
    BPButton {
      background-color: #2196F3;
      border-radius: 40px;
      font-size: 42px;
      font-weight: 600;
      padding: 15px 40px;
    }
    BPButton:hover {
      background-color: #1E88E5;
    }
    BPButton:pressed {
      background-color: #1976D2;
    }
    BPButton:disabled {
      background-color: #424242;
      color: #888888;
    }
  )");
  connect(stateBtn, &BPButton::clicked, this, &BPOsmPanel::onStateButtonClicked);
  stateLayout->addWidget(stateBtn);

  QVBoxLayout *stateStatusLayout = new QVBoxLayout();
  stateStatusLayout->setSpacing(5);

  QLabel *stateTitle = new QLabel(tr("State"), this);
  stateTitle->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  stateStatusLayout->addWidget(stateTitle);

  stateValue = new QLabel(tr("None selected"), this);
  stateValue->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  stateValue->setWordWrap(true);
  stateStatusLayout->addWidget(stateValue);

  stateLayout->addLayout(stateStatusLayout, 1);
  stateWidget->setVisible(false);
  layout->addWidget(stateWidget);

  mainLayout->addWidget(countrySelectionGroup);
}

void BPOsmPanel::createDatabaseUpdateGroup() {
  databaseUpdateGroup = createStyledGroupBox(tr("Database Update"));
  QVBoxLayout *layout = new QVBoxLayout(databaseUpdateGroup);
  layout->setSpacing(20);
  layout->setContentsMargins(25, 25, 25, 25);

  // Update Button and Status
  QWidget *updateWidget = new QWidget(this);
  QHBoxLayout *updateLayout = new QHBoxLayout(updateWidget);
  updateLayout->setSpacing(20);
  updateLayout->setContentsMargins(0, 0, 0, 0);

  updateBtn = new BPButton(tr("UPDATE"), this);
  updateBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  updateBtn->setMinimumHeight(100);
  updateBtn->setStyleSheet(R"(
    BPButton {
      background-color: #28a745;
      border-radius: 40px;
      font-size: 42px;
      font-weight: 600;
      padding: 15px 40px;
    }
    BPButton:hover {
      background-color: #218838;
    }
    BPButton:pressed {
      background-color: #1e7e34;
    }
    BPButton:disabled {
      background-color: #424242;
      color: #888888;
    }
  )");
  connect(updateBtn, &BPButton::clicked, this, &BPOsmPanel::onUpdateButtonClicked);
  updateLayout->addWidget(updateBtn);

  QVBoxLayout *updateStatusLayout = new QVBoxLayout();
  updateStatusLayout->setSpacing(5);

  QLabel *updateTitle = new QLabel(tr("Database Status"), this);
  updateTitle->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  updateStatusLayout->addWidget(updateTitle);

  updateStatusValue = new QLabel(tr("Ready"), this);
  updateStatusValue->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  updateStatusValue->setWordWrap(true);
  updateStatusLayout->addWidget(updateStatusValue);

  updateLayout->addLayout(updateStatusLayout, 1);
  layout->addWidget(updateWidget);

  // Divider
  updateEtaDivider = BPUIHelpers::createDivider();
  layout->addWidget(updateEtaDivider);

  // ETA (initially hidden)
  etaWidget = new QWidget(this);
  QVBoxLayout *etaLayout = new QVBoxLayout(etaWidget);
  etaLayout->setSpacing(5);
  etaLayout->setContentsMargins(0, 0, 0, 0);

  etaLabel = new QLabel(tr("Estimated Time Remaining"), this);
  etaLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  etaLayout->addWidget(etaLabel);

  etaValue = new QLabel(tr("Calculating..."), this);
  etaValue->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  etaLayout->addWidget(etaValue);

  etaWidget->setVisible(false);
  layout->addWidget(etaWidget);

  // Divider
  etaElapsedDivider = BPUIHelpers::createDivider();
  layout->addWidget(etaElapsedDivider);

  // Elapsed Time (initially hidden)
  elapsedWidget = new QWidget(this);
  QVBoxLayout *elapsedLayout = new QVBoxLayout(elapsedWidget);
  elapsedLayout->setSpacing(5);
  elapsedLayout->setContentsMargins(0, 0, 0, 0);

  elapsedLabel = new QLabel(tr("Time Elapsed"), this);
  elapsedLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  elapsedLayout->addWidget(elapsedLabel);

  elapsedValue = new QLabel(tr("Calculating..."), this);
  elapsedValue->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  elapsedLayout->addWidget(elapsedValue);

  elapsedWidget->setVisible(false);
  layout->addWidget(elapsedWidget);

  mainLayout->addWidget(databaseUpdateGroup);
}

void BPOsmPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  updateLabels();
  if (!timer->isActive()) {
    timer->start(BPOsmConstants::FAST_REFRESH_INTERVAL);
  }
}

void BPOsmPanel::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  if (timer->isActive()) {
    timer->stop();
  }
}

void BPOsmPanel::updateLabels() {
  if (!isVisible()) {
    return;
  }

  // Update mapd version
  mapd_version = params.get("MapdVersion");
  mapdVersionValue->setText(QString::fromStdString(mapd_version));

  // Update map size
  updateMapSize();
  deleteMapsValue->setText(formatSize(mapsDirSize));

  // Check download status
  osm_download_locations = mem_params.get("OSMDownloadLocations");
  osm_download_in_progress = !osm_download_locations.empty();

  // Adjust timer interval based on download status
  timer->setInterval(osm_download_in_progress ? BPOsmConstants::FAST_REFRESH_INTERVAL : BPOsmConstants::SLOW_REFRESH_INTERVAL);

  // Update last download time
  const std::string osmLastDownloadTimeStr = params.get("OsmDownloadedDate");
  if (!lastDownloadedTimePoint.has_value() && !osmLastDownloadTimeStr.empty()) {
    const double osmLastDownloadTime = std::stod(osmLastDownloadTimeStr);
    lastDownloadedTimePoint = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(osmLastDownloadTime));
  }

  // Hide location selection group when downloads are in progress
  countrySelectionGroup->setVisible(!osm_download_in_progress);

  // Enable/disable buttons based on download status
  countryBtn->setEnabled(!osm_download_in_progress);
  stateBtn->setEnabled(!osm_download_in_progress);

  // Update download progress
  updateDownloadProgress();

  // Update country/state visibility (only when not downloading)
  const QString locationName = QString::fromStdString(params.get("OsmLocationName"));
  const bool isUs = !locationName.isEmpty() && locationName == "US";
  if (!osm_download_in_progress) {
    stateWidget->setVisible(isUs);
  }

  if (!locationName.isEmpty()) {
    if (!isUs) {
      params.remove("OsmStateName");
      params.remove("OsmStateTitle");
    }
    databaseUpdateGroup->setVisible(true);
  } else {
    params.remove("OsmLocal");
    params.remove("OsmLocationName");
    params.remove("OsmLocationTitle");
    params.remove("OsmStateName");
    params.remove("OsmStateTitle");
    databaseUpdateGroup->setVisible(false);
    stateWidget->setVisible(false);
  }

  // Update displayed values
  countryValue->setText(QString::fromStdString(params.get("OsmLocationTitle")));
  stateValue->setText(QString::fromStdString(params.get("OsmStateTitle")));

  updateDividerVisibility();
  update();
}

void BPOsmPanel::updateDownloadProgress() {
  const auto pending_update_check = params.getBool("OsmDbUpdatesCheck");
  const QJsonObject osmDownloadProgress = QJsonDocument::fromJson(params.get("OSMDownloadProgress").c_str()).object();

  // Show/hide ETA and elapsed time based on download status
  if (osm_download_in_progress && lastDownloadedTimePoint.has_value()) {
    etaWidget->setVisible(true);
    elapsedWidget->setVisible(true);
    etaValue->setText(calculateETA(osmDownloadProgress, lastDownloadedTimePoint.value()));
    elapsedValue->setText(calculateElapsedTime(osmDownloadProgress, lastDownloadedTimePoint.value()));
  } else {
    etaWidget->setVisible(false);
    elapsedWidget->setVisible(false);
  }
  updateDividerVisibility();

  const int total_files = extractIntFromJson(osmDownloadProgress, "total_files");
  const int downloaded_files = extractIntFromJson(osmDownloadProgress, "downloaded_files");
  download_failed_state = total_files && osm_download_in_progress && !lastDownloadedTimePoint.has_value() && downloaded_files < total_files;

  QString updateButtonText = processUpdateStatus(pending_update_check, total_files, downloaded_files, osmDownloadProgress, download_failed_state);
  updateStatusValue->setText(updateButtonText);
  updateBtn->setText(osm_download_in_progress && !download_failed_state ? tr("REFRESH") : tr("UPDATE"));
}

bool BPOsmPanel::showBPConfirmation(const QString &message, const QString &confirmText) {
  const auto _is_metered = isMetered();
  const QString warning_message = _is_metered ? tr("\n\nWarning: You are on a metered connection!") : QString();
  QString final_message = message + warning_message;
  const QString final_confirmText = confirmText.isEmpty() ? (_is_metered ? tr("Continue on Metered") : tr("Start Download")) : confirmText;

  BPConfirmationDialog::ConfirmConfig config;
  config.title = tr("Confirm Download");
  config.prompt = final_message;
  config.confirmText = final_confirmText;
  config.cancelText = tr("Cancel");
  config.richText = true;

  auto *dialog = BPConfirmationDialog::showConfirmation(config, this);

  bool result = false;
  QEventLoop loop;
  QObject::connect(dialog, &BPConfirmationDialog::confirmed, [&](bool accepted) {
    result = accepted;
    loop.quit();
  });
  loop.exec();

  return result;
}

void BPOsmPanel::onCountryButtonClicked() {
  countryBtn->setEnabled(false);
  countryValue->setText(tr("Fetching list..."));

  const std::vector<std::tuple<QString, QString, QString, QString>> locations = getOsmLocations();

  countryBtn->setEnabled(true);
  countryValue->setText("");

  const QString initTitle = QString::fromStdString(params.get("OsmLocationTitle"));
  const QString currentTitle = ((initTitle == "== None ==") || (initTitle.isEmpty())) ? "== None ==" : initTitle;

  QStringList locationTitles;
  for (const auto &loc : locations) {
    locationTitles.push_back(std::get<0>(loc));
  }

  // Use BP Selection Dialog (blocking call)
  QString selection = BPSelectionDialog::getSelection(tr("Select Country"), locationTitles, currentTitle, this);

  if (!selection.isEmpty()) {
    params.put("OsmLocal", "1");
    params.put("OsmLocationTitle", selection.toStdString());
    for (const auto &loc : locations) {
      if (std::get<0>(loc) == selection) {
        params.put("OsmLocationName", std::get<1>(loc).toStdString());
        break;
      }
    }

    if (params.get("OsmLocationName") == "US") {
      onStateButtonClicked();
      return;
    } else if (selection != "== None ==") {
      if (showBPConfirmation(tr("This will start the download process and it might take a while to complete."), "")) {
        osm_download_in_progress = true;
        params.putBool("OsmDbUpdatesCheck", true);
        updateLabels();
      }
    }
  }
  updateLabels();
}

void BPOsmPanel::onStateButtonClicked() {
  const std::tuple<QString, QString> allStatesOption = std::make_tuple("All States (~4.8 GB)", "All");

  stateBtn->setEnabled(false);
  stateValue->setText(tr("Fetching list..."));

  const std::vector<std::tuple<QString, QString, QString, QString>> locations = getUsStatesLocations(allStatesOption);

  stateBtn->setEnabled(true);
  stateValue->setText("");

  const QString initTitle = QString::fromStdString(params.get("OsmStateTitle"));
  const QString currentTitle = ((initTitle == std::get<0>(allStatesOption)) || (initTitle.isEmpty())) ? tr("All") : initTitle;

  QStringList locationTitles;
  for (const auto &loc : locations) {
    locationTitles.push_back(std::get<0>(loc));
  }

  // Use BP Selection Dialog (blocking call)
  QString selection = BPSelectionDialog::getSelection(tr("Select State"), locationTitles, currentTitle, this);

  if (!selection.isEmpty()) {
    params.put("OsmStateTitle", selection.toStdString());
    for (const auto &loc : locations) {
      if (std::get<0>(loc) == selection) {
        params.put("OsmStateName", std::get<1>(loc).toStdString());
        break;
      }
    }
    stateValue->setText(selection);
    if (showBPConfirmation(tr("This will start the download process and it might take a while to complete."), "")) {
      osm_download_in_progress = true;
      params.putBool("OsmDbUpdatesCheck", true);
      updateLabels();
    }
  }
  updateLabels();
}

void BPOsmPanel::onUpdateButtonClicked() {
  if (osm_download_in_progress && !download_failed_state) {
    updateLabels();
  } else if (showBPConfirmation(tr("This will start the download process and it might take a while to complete."), "")) {
    osm_download_in_progress = true;
    params.putBool("OsmDbUpdatesCheck", true);
    updateLabels();
  }
}

void BPOsmPanel::onDeleteMapsButtonClicked() {
  if (showBPConfirmation(tr("This will delete ALL downloaded maps\n\nAre you sure you want to delete all the maps?"), tr("Yes, delete all the maps."))) {
    QtConcurrent::run([this]() {
      QDir dir(BPOsmConstants::MAP_PATH);
      deleteMapsBtn->setEnabled(false);
      deleteMapsBtn->setText("⌛");
      dir.removeRecursively();
      updateMapSize();
      deleteMapsBtn->setEnabled(true);
      deleteMapsBtn->setText(tr("DELETE"));
    });
    updateLabels();
  }
}

// Helper Functions

int BPOsmPanel::extractIntFromJson(const QJsonObject &json, const QString &key) {
  return (json.contains(key)) ? json[key].toInt() : 0;
}

QString BPOsmPanel::processUpdateStatus(bool pending_update, int total_files, int downloaded_files,
                                        const QJsonObject &json, bool failed_state) {
  if (pending_update && !osm_download_in_progress && !total_files) {
    lastDownloadedTimePoint.reset();
    return tr("Download starting...");
  } else if (failed_state) {
    return tr("Error: Invalid download. Retry.");
  } else if (osm_download_in_progress && total_files > downloaded_files) {
    return formatDownloadStatus(json);
  } else if (osm_download_in_progress && downloaded_files >= total_files) {
    osm_download_in_progress = false;
    lastDownloadedTimePoint.reset();
    return tr("Download complete!");
  }

  if (lastDownloadedTimePoint.has_value()) {
    QDateTime dateTime = QDateTime::fromTime_t(std::chrono::system_clock::to_time_t(lastDownloadedTimePoint.value()));
    dateTime = dateTime.toLocalTime();
    return QString("%1").arg(dateTime.toString("yyyy-MM-dd HH:mm:ss"));
  }

  return "";
}

QString BPOsmPanel::formatSize(quint64 size) const {
  if (size == 0 && (!mapSizeFuture.has_value() || mapSizeFuture.value().isRunning())) {
    return tr("Calculating...");
  }

  constexpr qint64 kb = 1024;
  constexpr qint64 mb = 1024 * kb;
  constexpr qint64 gb = 1024 * mb;

  if (size < gb) {
    const double sizeMB = size / static_cast<double>(mb);
    return QString::number(sizeMB, 'f', 2) + " MB";
  } else {
    const double sizeGB = size / static_cast<double>(gb);
    return QString::number(sizeGB, 'f', 2) + " GB";
  }
}

QString BPOsmPanel::formatDownloadStatus(const QJsonObject &json) {
  if (!json.contains("total_files") || !json.contains("downloaded_files"))
    return "";

  const int total_files = json["total_files"].toInt();
  const int downloaded_files = json["downloaded_files"].toInt();

  if (total_files <= 0) return tr("Ready");
  if (downloaded_files >= total_files) return tr("Downloaded");

  const int percentage = static_cast<int>(100.0 * downloaded_files / total_files);
  return QString::asprintf("%d/%d (%d%%)", downloaded_files, total_files, percentage);
}

QString BPOsmPanel::formatTime(const long timeInSeconds) {
  const long minutes = timeInSeconds / 60;
  const long seconds = timeInSeconds % 60;

  QString formattedTime;
  if (minutes > 0) {
    formattedTime = QString::number(minutes) + tr("m ");
  }
  formattedTime += QString::number(seconds) + tr("s");
  return formattedTime;
}

QString BPOsmPanel::calculateElapsedTime(const QJsonObject &jsonData,
                                         const std::chrono::system_clock::time_point &startTime) {
  using namespace std::chrono;
  if (!jsonData.contains("total_files") || !jsonData.contains("downloaded_files"))
    return tr("Calculating...");

  const int totalFiles = jsonData["total_files"].toInt();
  const int downloadedFiles = jsonData["downloaded_files"].toInt();

  if (downloadedFiles >= totalFiles || totalFiles <= 0) return tr("Downloaded");

  const long elapsed = duration_cast<seconds>(system_clock::now() - startTime).count();

  if (elapsed == 0 || downloadedFiles == 0) return tr("Calculating...");

  return formatTime(elapsed);
}

QString BPOsmPanel::calculateETA(const QJsonObject &jsonData,
                                 const std::chrono::system_clock::time_point &startTime) {
  using namespace std::chrono;
  static steady_clock::time_point lastUpdateTime = steady_clock::now();
  static std::deque<double> rateHistory;

  constexpr int minDataPoints = 3;
  constexpr int historySize = 10;

  static QString lastETA = tr("Calculating ETA...");

  if (duration_cast<seconds>(steady_clock::now() - lastUpdateTime).count() < 1) {
    return lastETA;
  }

  if (!jsonData.contains("total_files") || !jsonData.contains("downloaded_files"))
    return lastETA;

  const int totalFiles = jsonData["total_files"].toInt();
  const int downloadedFiles = jsonData["downloaded_files"].toInt();

  if (totalFiles <= 0 || downloadedFiles >= totalFiles) {
    return totalFiles <= 0 ? tr("Ready") : tr("Downloaded");
  }

  const long elapsed = duration_cast<seconds>(system_clock::now() - startTime).count();
  if (elapsed == 0 || downloadedFiles == 0) return lastETA;

  const double rate = downloadedFiles / static_cast<double>(elapsed);
  if (rateHistory.size() >= historySize) rateHistory.pop_front();
  rateHistory.push_back(rate);

  if (rateHistory.size() < minDataPoints) return lastETA;

  double weightedSum = 0;
  for (int i = 0, weight = 1; i < rateHistory.size(); ++i, ++weight) {
    weightedSum += rateHistory[i] * weight;
  }
  const double avgRate = 2 * weightedSum / (rateHistory.size() * (rateHistory.size() + 1));

  const long remainingTime = static_cast<long>((totalFiles - downloadedFiles) / avgRate);
  if (remainingTime <= 0) return lastETA;

  lastETA = tr("Time remaining: ") + formatTime(remainingTime);
  lastUpdateTime = steady_clock::now();
  return lastETA;
}

quint64 BPOsmPanel::getDirSize(QString dirPath) {
  quint64 size = 0;
  const QString actualDirPath = dirPath.startsWith("~") ? dirPath.replace(0, 1, QDir::homePath()) : dirPath;
  QDirIterator it(actualDirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    if (it.fileInfo().isFile()) {
      size += it.fileInfo().size();
    }
  }
  return size;
}

void BPOsmPanel::updateMapSize() {
  if (mapSizeFuture.has_value() && mapSizeFuture.value().isFinished()) {
    mapsDirSize = mapSizeFuture.value().result();
  }

  if (!mapSizeFuture.has_value() || !mapSizeFuture.value().isRunning()) {
    mapSizeFuture = QtConcurrent::run(getDirSize, BPOsmConstants::MAP_PATH);
  }
}

void BPOsmPanel::updateDividerVisibility() {
  // Hide dividers if the widget they precede is hidden (or if they're the last element)
  countryStateDivider->setVisible(stateWidget->isVisible());
  updateEtaDivider->setVisible(etaWidget->isVisible());
  etaElapsedDivider->setVisible(elapsedWidget->isVisible());
}
