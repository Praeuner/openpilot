// selfdrive/ui/bluepilot/qt/offroad/panels/bp_models_panel.cc

#include "bp_models_panel.h"

#include <algorithm>
#include <QScrollArea>
#include <QJsonDocument>
#include <QEventLoop>
#include <QDir>
#include <QtConcurrent/QtConcurrent>
#include <QRegularExpression>

#include "common/model.h"
#include "selfdrive/ui/qt/widgets/input.h"
#include "selfdrive/ui/sunnypilot/qt/util.h"
#include "selfdrive/ui/sunnypilot/qt/widgets/controls.h"
#include "selfdrive/ui/sunnypilot/ui.h"
#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_dialogs.h"
#include "system/hardware/hw.h"

// Progress bar styles
static const QString progressStyleActive = "QProgressBar {"
    "  font-size: 40px;"
    "  font-weight: 200;"
    "  padding: 1px;"
    "  border: 3px solid black;"
    "  border-radius: 10px;"
    "}"
    "QProgressBar::chunk {"
    "  background-color: #1e79e8;"
    "  border-radius: 10px;"
    "}";

static const QString progressStyleInactive = progressStyleActive +
    "QProgressBar::chunk {"
    "  background-color: transparent;"
    "}";

static const QString progressStyleDone = progressStyleActive +
    "QProgressBar {"
    "  color: #33ab4c;"
    "}"
    "QProgressBar::chunk {"
    "  background-color: transparent;"
    "}";

static const QString progressStyleError = progressStyleActive +
    "QProgressBar {"
    "  color: red;"
    "}"
    "QProgressBar::chunk {"
    "  background-color: transparent;"
    "}";

// Helper function for synchronous BP confirmation
static bool showBPConfirmation(const QString &title, const QString &message, const QString &confirmText, const QString &cancelText, QWidget *parent) {
  BPConfirmationDialog::ConfirmConfig config;
  config.title = title;
  config.prompt = message;
  config.confirmText = confirmText;
  config.cancelText = cancelText;
  config.richText = true;

  auto *dialog = BPConfirmationDialog::showConfirmation(config, parent);

  bool result = false;
  QEventLoop loop;
  QObject::connect(dialog, &BPConfirmationDialog::confirmed, [&](bool accepted) {
    result = accepted;
    loop.quit();
  });
  loop.exec();

  return result;
}

BPModelsPanel::BPModelsPanel(QWidget *parent) : QWidget(parent) {
  setupUI();

  // Refresh timer for periodic updates (only active when visible)
  refreshTimer = new QTimer(this);
  connect(refreshTimer, &QTimer::timeout, this, &BPModelsPanel::refreshAll);

  // Connect to offroad transition
  connect(uiState(), &UIState::offroadTransition, this, [this](bool offroad) {
    is_onroad = !offroad;
    updateLabels();
  });

  // Connect to UI updates
  connect(uiStateSP(), &UIStateSP::uiUpdate, this, &BPModelsPanel::updateLabels);
}

void BPModelsPanel::setupUI() {
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(40, 40, 40, 40);
  mainLayout->setSpacing(30);

  // Create all groups
  createModelSelectionGroup();
  createDownloadProgressGroup();
  createLaneTurnGroup();
  createSteerDelayGroup();

  mainLayout->addStretch();

  setStyleSheet(R"(
    BPModelsPanel {
      background-color: #1B1B1B;
    }
    BPModelsPanel QGroupBox BPToggleControl,
    BPModelsPanel QGroupBox BPOptionControl {
      background-color: transparent;
    }
  )");
}

QGroupBox* BPModelsPanel::createStyledGroupBox(const QString &title) {
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

void BPModelsPanel::createModelSelectionGroup() {
  modelSelectionGroup = createStyledGroupBox(tr("Model Selection"));
  QVBoxLayout *layout = new QVBoxLayout(modelSelectionGroup);
  layout->setSpacing(20);
  layout->setContentsMargins(25, 25, 25, 25);

  // Current model selector
  QWidget *currentModelWidget = new QWidget(this);
  QHBoxLayout *currentModelLayout = new QHBoxLayout(currentModelWidget);
  currentModelLayout->setSpacing(20);
  currentModelLayout->setContentsMargins(0, 0, 0, 0);

  currentModelBtn = new BPButton(tr("SELECT"), this);
  currentModelBtn->setMinimumWidth(250);
  currentModelBtn->setMinimumHeight(100);
  currentModelBtn->setStyleSheet(R"(
    BPButton {
      background-color: #2196F3;
      border-radius: 40px;
      font-size: 42px;
      font-weight: 600;
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
  connect(currentModelBtn, &QPushButton::clicked, this, &BPModelsPanel::onModelSelectionClicked);
  currentModelLayout->addWidget(currentModelBtn);

  QVBoxLayout *modelStatusLayout = new QVBoxLayout();
  modelStatusLayout->setSpacing(5);

  QLabel *modelTitle = new QLabel(tr("Current Model"), this);
  modelTitle->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  modelStatusLayout->addWidget(modelTitle);

  currentModelLabel = new QLabel(getActiveModelInternalName(), this);
  currentModelLabel->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  currentModelLabel->setWordWrap(true);
  modelStatusLayout->addWidget(currentModelLabel);

  currentModelLayout->addLayout(modelStatusLayout, 1);
  layout->addWidget(currentModelWidget);

  // Refresh models button
  QWidget *refreshWidget = new QWidget(this);
  QHBoxLayout *refreshLayout = new QHBoxLayout(refreshWidget);
  refreshLayout->setSpacing(20);
  refreshLayout->setContentsMargins(0, 0, 0, 0);

  refreshModelsBtn = new BPButton(tr("REFRESH"), this);
  refreshModelsBtn->setMinimumWidth(250);
  refreshModelsBtn->setMinimumHeight(100);
  refreshModelsBtn->setStyleSheet(R"(
    BPButton {
      background-color: #FF9800;
      border-radius: 40px;
      font-size: 42px;
      font-weight: 600;
    }
    BPButton:hover {
      background-color: #FB8C00;
    }
    BPButton:pressed {
      background-color: #F57C00;
    }
  )");
  connect(refreshModelsBtn, &QPushButton::clicked, this, &BPModelsPanel::onRefreshModelsClicked);
  refreshLayout->addWidget(refreshModelsBtn);

  QLabel *refreshLabel = new QLabel(tr("Refresh Model List"), this);
  refreshLabel->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  refreshLayout->addWidget(refreshLabel, 1);
  layout->addWidget(refreshWidget);

  // Clear cache button
  QWidget *cacheWidget = new QWidget(this);
  QHBoxLayout *cacheLayout = new QHBoxLayout(cacheWidget);
  cacheLayout->setSpacing(20);
  cacheLayout->setContentsMargins(0, 0, 0, 0);

  clearCacheBtn = new BPButton(tr("CLEAR"), this);
  clearCacheBtn->setMinimumWidth(250);
  clearCacheBtn->setMinimumHeight(100);
  clearCacheBtn->setStyleSheet(R"(
    BPButton {
      background-color: #F44336;
      border-radius: 40px;
      font-size: 42px;
      font-weight: 600;
    }
    BPButton:hover {
      background-color: #E53935;
    }
    BPButton:pressed {
      background-color: #D32F2F;
    }
  )");
  connect(clearCacheBtn, &QPushButton::clicked, this, &BPModelsPanel::onClearCacheClicked);
  cacheLayout->addWidget(clearCacheBtn);

  QVBoxLayout *cacheStatusLayout = new QVBoxLayout();
  cacheStatusLayout->setSpacing(5);

  QLabel *cacheTitle = new QLabel(tr("Clear Model Cache"), this);
  cacheTitle->setStyleSheet("font-size: 40px; color: white; font-weight: 500;");
  cacheStatusLayout->addWidget(cacheTitle);

  cacheSizeLabel = new QLabel("0.00 MB", this);
  cacheSizeLabel->setStyleSheet("font-size: 34px; color: #AAAAAA;");
  cacheStatusLayout->addWidget(cacheSizeLabel);

  cacheLayout->addLayout(cacheStatusLayout, 1);
  layout->addWidget(cacheWidget);

  mainLayout->addWidget(modelSelectionGroup);
}

void BPModelsPanel::createDownloadProgressGroup() {
  downloadProgressGroup = createStyledGroupBox(tr("Download Progress"));
  QVBoxLayout *layout = new QVBoxLayout(downloadProgressGroup);
  layout->setSpacing(15);
  layout->setContentsMargins(25, 25, 25, 25);

  // Create progress bars for each model type
  supercomboProgressBar = createProgressBar(this);
  supercomboFrame = createModelDetailFrame(this, tr("Driving Model"), supercomboProgressBar);
  layout->addWidget(supercomboFrame);

  navigationProgressBar = createProgressBar(this);
  navigationFrame = createModelDetailFrame(this, tr("Navigation Model"), navigationProgressBar);
  layout->addWidget(navigationFrame);

  visionProgressBar = createProgressBar(this);
  visionFrame = createModelDetailFrame(this, tr("Vision Model"), visionProgressBar);
  layout->addWidget(visionFrame);

  policyProgressBar = createProgressBar(this);
  policyFrame = createModelDetailFrame(this, tr("Policy Model"), policyProgressBar);
  layout->addWidget(policyFrame);

  // Initially hide all frames
  supercomboFrame->setVisible(false);
  navigationFrame->setVisible(false);
  visionFrame->setVisible(false);
  policyFrame->setVisible(false);

  mainLayout->addWidget(downloadProgressGroup);
}

void BPModelsPanel::createLaneTurnGroup() {
  laneTurnGroup = createStyledGroupBox(tr("Lane Turn Settings"));
  QVBoxLayout *layout = new QVBoxLayout(laneTurnGroup);
  layout->setSpacing(15);
  layout->setContentsMargins(10, 10, 10, 10);

  // Lane Turn Desire toggle
  laneTurnDesireToggle = new BPToggleControl(
    "LaneTurnDesire",
    tr("Use Lane Turn Desires"),
    tr("If you're driving at 20 mph (32 km/h) or below and have your blinker on, "
       "the car will plan a turn in that direction at the nearest drivable path. "
       "This prevents situations (like at red lights) where the car might plan the wrong turn direction."),
    this
  );
  connect(laneTurnDesireToggle, &BPToggleControl::toggleFlipped, this, &BPModelsPanel::onLaneTurnDesireToggled);
  laneTurnDesireToggle->setStyleSheet("BPToggleControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(laneTurnDesireToggle);

  // Lane Turn Value control
  bool is_metric = params.getBool("IsMetric");
  double per_value_change_scaled = is_metric ? (1.0 / 1.609344) : 1.0;

  laneTurnValueControl = new BPNumericControl(
    "LaneTurnValue",
    tr("Adjust Lane Turn Speed"),
    tr("Set the maximum speed for lane turn desires. Default is 19 %1.").arg(is_metric ? "km/h" : "mph"),
    5,   // min
    20,  // max
    per_value_change_scaled,  // increment
    false,  // isFloat
    1.0,    // division
    this
  );
  laneTurnValueControl->setStyleSheet("BPNumericControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(laneTurnValueControl);

  // Initially hide lane turn value control if lane turn desire is disabled
  laneTurnValueControl->setVisible(params.getBool("LaneTurnDesire"));

  mainLayout->addWidget(laneTurnGroup);
}

void BPModelsPanel::createSteerDelayGroup() {
  steerDelayGroup = createStyledGroupBox(tr("Steering Delay Settings"));
  QVBoxLayout *layout = new QVBoxLayout(steerDelayGroup);
  layout->setSpacing(15);
  layout->setContentsMargins(10, 10, 10, 10);

  // Live Learning Steer Delay toggle
  lagdToggleControl = new BPToggleControl(
    "LagdToggle",
    tr("Live Learning Steer Delay"),
    tr("Enable this for the car to learn and adapt its steering response time. "
       "Disable to use a fixed steering response time. Keeping this on provides the stock openpilot experience."),
    this
  );
  connect(lagdToggleControl, &BPToggleControl::toggleFlipped, this, &BPModelsPanel::onLagdToggled);
  lagdToggleControl->setStyleSheet("BPToggleControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(lagdToggleControl);

  // Software delay control
  delayControl = new BPNumericControl(
    "LagdToggleDelay",
    tr("Adjust Software Delay"),
    tr("Adjust the software delay when Live Learning Steer Delay is toggled off. The default software delay value is 0.2"),
    5,    // min (0.05 when divided by 100)
    50,   // max (0.50 when divided by 100)
    1,    // increment
    true, // isFloat
    100.0,  // division (to get 0.01 precision)
    this
  );
  delayControl->setStyleSheet("BPNumericControl { background-color: transparent; border-radius: 0px; }");
  layout->addWidget(delayControl);

  // Initially hide delay control if lagd is enabled (default is on)
  delayControl->setVisible(!params.getBool("LagdToggle"));

  mainLayout->addWidget(steerDelayGroup);
}

QProgressBar* BPModelsPanel::createProgressBar(QWidget *parent) {
  QProgressBar *progressBar = new QProgressBar(parent);
  progressBar->setRange(0, 100);
  progressBar->setValue(0);
  progressBar->setTextVisible(true);
  progressBar->setAlignment(Qt::AlignVCenter);
  progressBar->setStyleSheet(progressStyleInactive);
  return progressBar;
}

QFrame* BPModelsPanel::createModelDetailFrame(QWidget *parent, const QString &typeName, QProgressBar *progressBar) {
  QFrame *frame = new QFrame(parent);
  frame->setStyleSheet("QFrame { background-color: transparent; border: none; }");

  QHBoxLayout *layout = new QHBoxLayout(frame);
  layout->setContentsMargins(0, 10, 0, 10);
  layout->setSpacing(20);

  QLabel *typeLabel = new QLabel(typeName, parent);
  typeLabel->setStyleSheet("font-size: 38px; color: white; font-weight: 500; min-width: 300px;");
  layout->addWidget(typeLabel);

  layout->addWidget(progressBar, 1);

  frame->setVisible(false);
  return frame;
}

void BPModelsPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  // Defer initialization
  QTimer::singleShot(0, this, [this]() {
    is_onroad = uiState()->scene.started;

    // Start refresh timer
    refreshTimer->start(1000);

    // Initial update
    updateLabels();
  });
}

void BPModelsPanel::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  refreshTimer->stop();
}

void BPModelsPanel::refreshAll() {
  updateLabels();
}

void BPModelsPanel::updateLabels() {
  if (!isVisible()) {
    return;
  }

  updateModelManagerState();
  handleBundleDownloadProgress();

  // Update current model
  currentModelBtn->setEnabled(!is_onroad && !isDownloading());
  currentModelLabel->setText(getActiveModelInternalName());

  // Update cache size
  cacheSizeLabel->setText(QString::number(calculateCacheSize(), 'f', 2) + " MB");

  // Update steer delay description
  updateSteerDelayDescription();

  // Update lane turn value
  refreshLaneTurnValueControl();

  // Update delay control visibility and value
  delayControl->setVisible(!params.getBool("LagdToggle"));
  if (delayControl->isVisible()) {
    refreshDelayControl();
  }
}

void BPModelsPanel::updateModelManagerState() {
  const SubMaster &sm = *(uiStateSP()->sm);
  model_manager = sm["modelManagerSP"].getModelManagerSP();
}

void BPModelsPanel::handleBundleDownloadProgress() {
  supercomboFrame->setVisible(false);
  visionFrame->setVisible(false);
  policyFrame->setVisible(false);
  navigationFrame->setVisible(false);

  using DS = cereal::ModelManagerSP::DownloadStatus;
  if (!model_manager.hasSelectedBundle() && !model_manager.hasActiveBundle()) {
    downloadProgressGroup->setVisible(false);
    return;
  }

  const bool showSelectedBundle = model_manager.hasSelectedBundle() && (isDownloading() || model_manager.getSelectedBundle().getStatus() == DS::FAILED);
  const auto &bundle = showSelectedBundle ? model_manager.getSelectedBundle() : model_manager.getActiveBundle();
  const auto &models = bundle.getModels();
  download_status = bundle.getStatus();
  const auto download_status_changed = prev_download_status != download_status;

  // Show group if we have any visible frames
  bool hasVisibleFrames = false;

  for (const auto &model: models) {
    QString modelName = QString::fromStdString(bundle.getDisplayName());

    QProgressBar *progressBar = nullptr;
    QFrame *modelFrame = nullptr;

    switch (model.getType()) {
      case cereal::ModelManagerSP::Model::Type::SUPERCOMBO:
        progressBar = supercomboProgressBar;
        modelFrame = supercomboFrame;
        break;
      case cereal::ModelManagerSP::Model::Type::NAVIGATION:
        progressBar = navigationProgressBar;
        modelFrame = navigationFrame;
        break;
      case cereal::ModelManagerSP::Model::Type::VISION:
        progressBar = visionProgressBar;
        modelFrame = visionFrame;
        break;
      case cereal::ModelManagerSP::Model::Type::POLICY:
        progressBar = policyProgressBar;
        modelFrame = policyFrame;
        break;
    }

    const auto &progress = model.getArtifact().getDownloadProgress();

    if (progress.getStatus() == cereal::ModelManagerSP::DownloadStatus::DOWNLOADING) {
      progressBar->setStyleSheet(progressStyleActive);
      progressBar->setValue(progress.getProgress());
      progressBar->setFormat(QString("  %1% - %2").arg(static_cast<int>(progress.getProgress())).arg(modelName));
      device()->resetInteractiveTimeout();
    } else if (progress.getStatus() == cereal::ModelManagerSP::DownloadStatus::DOWNLOADED) {
      progressBar->setStyleSheet(progressStyleDone);
      progressBar->setFormat(tr("  %1 - %2").arg(modelName, download_status_changed ? tr("downloaded") : tr("ready")));
    } else if (progress.getStatus() == cereal::ModelManagerSP::DownloadStatus::CACHED) {
      progressBar->setStyleSheet(progressStyleDone);
      progressBar->setFormat(tr("  %1 - %2").arg(modelName, download_status_changed ? tr("from cache") : tr("ready")));
    } else if (progress.getStatus() == cereal::ModelManagerSP::DownloadStatus::FAILED) {
      progressBar->setStyleSheet(progressStyleError);
      progressBar->setFormat(tr("  download failed - %1").arg(modelName));
    } else {
      progressBar->setStyleSheet(progressStyleInactive);
      progressBar->setFormat(tr("  pending - %1").arg(modelName));
    }

    // Keep navigation hidden for now to avoid confusion
    if (model.getType() != cereal::ModelManagerSP::Model::Type::NAVIGATION) {
      modelFrame->setVisible(true);
      hasVisibleFrames = true;
    }
  }

  // Hide the entire download progress group if no frames are visible
  downloadProgressGroup->setVisible(hasVisibleFrames);
  prev_download_status = download_status;
}

QString BPModelsPanel::getActiveModelName() {
  if (model_manager.hasActiveBundle()) {
    return QString::fromStdString(model_manager.getActiveBundle().getDisplayName());
  }
  return DEFAULT_MODEL;
}

QString BPModelsPanel::getActiveModelInternalName() {
  if (model_manager.hasActiveBundle()) {
    return QString::fromStdString(model_manager.getActiveBundle().getInternalName());
  }
  return DEFAULT_MODEL;
}

QString BPModelsPanel::getActiveModelRef() {
  if (model_manager.hasActiveBundle()) {
    return QString::fromStdString(model_manager.getActiveBundle().getRef());
  }
  return DEFAULT_MODEL;
}

void BPModelsPanel::refreshLaneTurnValueControl() {
  if (!laneTurnValueControl) return;
  laneTurnValueControl->setVisible(params.getBool("LaneTurnDesire"));
}

void BPModelsPanel::refreshDelayControl() {
  // BPNumericControl handles its own label updates
}

void BPModelsPanel::updateSteerDelayDescription() {
  QString desc = tr("Enable this for the car to learn and adapt its steering response time. "
                   "Disable to use a fixed steering response time. Keeping this on provides the stock openpilot experience.");

  bool lagdEnabled = params.getBool("LagdToggle");
  if (lagdEnabled) {
    auto liveDelayBytes = params.get("LiveDelay");
    if (!liveDelayBytes.empty()) {
      auto LD = loadCerealEvent(params, "LiveDelay");
      float lateralDelay = LD->getLiveDelay().getLateralDelay();
      desc += QString("<br><br><b><span style=\"color:#e0e0e0\">%1</span></b> <span style=\"color:#e0e0e0\">%2 s</span>")
              .arg(tr("Live Steer Delay:")).arg(QString::number(lateralDelay, 'f', 3));
    }
  } else {
    auto carParamsBytes = params.get("CarParamsPersistent");
    if (!carParamsBytes.empty()) {
      AlignedBuffer aligned_buf_cp;
      capnp::FlatArrayMessageReader cmsg(aligned_buf_cp.align(carParamsBytes.data(), carParamsBytes.size()));
      cereal::CarParams::Reader CP = cmsg.getRoot<cereal::CarParams>();

      float steerDelay = CP.getSteerActuatorDelay();
      float softwareDelay = QString::fromStdString(params.get("LagdToggleDelay")).toFloat();
      float totalLag = steerDelay + softwareDelay;
      desc += QString("<br><br><span style=\"color:#e0e0e0\">"
                      "<b>%1</b> %2 s + <b>%3</b> %4 s = <b>%5</b> %6 s</span>")
             .arg(tr("Actuator Delay:"), QString::number(steerDelay, 'f', 2),
                  tr("Software Delay:"), QString::number(softwareDelay, 'f', 2),
                  tr("Total Delay:"), QString::number(totalLag, 'f', 2));
    }
  }
  lagdToggleControl->setDescription(desc);
}

void BPModelsPanel::onModelSelectionClicked() {
  currentModelBtn->setEnabled(false);
  currentModelLabel->setText(tr("Fetching models..."));

  QList<TreeNode> sortedModels;
  QSet<QString> modelFolders;
  QRegularExpression re("\\(([^)]*)\\)[^(]*$");
  const auto bundles = model_manager.getAvailableBundles();

  for (const auto &bundle : bundles) {
    auto overrides = bundle.getOverrides();
    QString folder;
    for (const auto &override : overrides) {
      if (override.getKey() == "folder") {
        folder = QString::fromStdString(override.getValue().cStr());
      }
    }

    modelFolders.insert(folder);
    sortedModels.append(TreeNode{
      folder,
      QString::fromStdString(bundle.getDisplayName()),
      QString::fromStdString(bundle.getRef()),
      static_cast<int>(bundle.getIndex())
    });
  }

  std::sort(sortedModels.begin(), sortedModels.end(),
    [](const TreeNode &a, const TreeNode &b) {
      return a.index > b.index;
    });

  // Create folder-maxIndex pairs for sorting
  QList<QPair<QString, int>> folderMaxIndices;
  for (const auto &folder : modelFolders) {
    int maxIndex = -1;
    for (const auto &model : sortedModels) {
      if (model.folder == folder) {
        maxIndex = std::max(maxIndex, model.index);
      }
    }
    folderMaxIndices.append(qMakePair(folder, maxIndex));
  }

  // Sort folders by highest model index
  std::sort(folderMaxIndices.begin(), folderMaxIndices.end(),
      [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
          return a.second > b.second;
      });

  // Create final items list
  QList<TreeFolder> items;
  for (const auto &folderPair : folderMaxIndices) {
    QList<TreeNode> folderModels;
    QString folder = folderPair.first;
    for (const auto &model : sortedModels) {
      if (model.folder == folderPair.first) {
        if (model.index == folderPair.second) {
          QRegularExpressionMatch match = re.match(model.displayName);
          if (match.hasMatch()) {
            folder.append(" - (Updated: ").append(match.captured(1)).append(")");
          }
        }
        folderModels.append(model);
      }
    }
    items.append(TreeFolder{folder, folderModels});
  }

  items.insert(0, TreeFolder{"", {
    TreeNode{"", DEFAULT_MODEL, DEFAULT_MODEL, -1}
  }});

  currentModelLabel->setText(getActiveModelInternalName());

  const QString selectedBundleRef = TreeOptionDialog::getSelection(
    tr("Select a Model"), items, getActiveModelRef(), QString("ModelManager_Favs"), this);

  if (selectedBundleRef.isEmpty() || !canContinueOnMeteredDialog()) {
    currentModelBtn->setEnabled(true);
    return;
  }

  // Handle "Stock" selection
  if (selectedBundleRef == DEFAULT_MODEL) {
    params.remove("ModelManager_ActiveBundle");
    currentModelLabel->setText(tr("Default"));
    showResetParamsDialog();
  } else {
    // Find selected bundle and initiate download
    for (const auto &bundle: bundles) {
      if (QString::fromStdString(bundle.getRef()) == selectedBundleRef) {
        params.put("ModelManager_DownloadIndex", std::to_string(bundle.getIndex()));
        if (bundle.getGeneration() != model_manager.getActiveBundle().getGeneration()) {
          showResetParamsDialog();
        }
        break;
      }
    }
  }

  updateLabels();
  currentModelBtn->setEnabled(true);
}

void BPModelsPanel::showResetParamsDialog() {
  const auto confirmMsg = QString("%1<br><br><b>%2</b><br><br><b>%3</b>")
                          .arg(tr("Model download has started in the background."))
                          .arg(tr("We STRONGLY suggest you to reset calibration."))
                          .arg(tr("Would you like to do that now?"));

  QString content("<body><h2 style=\"text-align: center;\">" + tr("Driving Model Selector") + "</h2><br>"
                  "<p style=\"text-align: center; margin: 0 128px; font-size: 50px;\">" + confirmMsg + "</p></body>");

  if (showBPConfirmation(tr("Driving Model Selector"), content, tr("Reset Calibration"), tr("Cancel"), this)) {
    params.remove("CalibrationParams");
    params.remove("LiveTorqueParameters");
  }
}

bool BPModelsPanel::canContinueOnMeteredDialog() {
  if (!is_metered) return true;

  const QString warning_message = tr("Warning: You are on a metered connection!");
  const QString final_message = warning_message;
  const QString final_buttonText = tr("Continue on Metered");

  return showBPConfirmation(tr("Network Warning"), final_message, final_buttonText, tr("Cancel"), this);
}

bool BPModelsPanel::showConfirmationDialog(const QString &message, const QString &confirmButtonText, bool show_metered_warning) {
  const QString warning_message = show_metered_warning ? tr("Warning: You are on a metered connection!") : QString();
  const QString final_message = QString("%1%2").arg(!message.isEmpty() ? message + "\n" : QString(), warning_message);
  const QString final_buttonText = !confirmButtonText.isEmpty() ? confirmButtonText : QString(tr("Continue") + " %1").arg(show_metered_warning ? tr("on Metered") : "");

  return showBPConfirmation("", final_message, final_buttonText, tr("Cancel"), this);
}

void BPModelsPanel::onRefreshModelsClicked() {
  params.put("ModelManager_LastSyncTime", "0");

  BPConfirmationDialog::ConfirmConfig config;
  config.title = "Model List";
  config.prompt = tr("Fetching Latest Models");
  config.confirmText = "OK";
  config.cancelText = "";

  BPConfirmationDialog::showMessage(config, this);
}

void BPModelsPanel::onClearCacheClicked() {
  QString confirmMsg = tr("This will delete ALL downloaded models from the cache"
                            "<br/><u>except the currently active model</u>."
                            "<br/><br/>Are you sure you want to continue?");
  QString content("<body><h2 style=\"text-align: center;\">" + tr("Driving Model Selector") + "</h2><br>"
                "<p style=\"text-align: center; margin: 0 128px; font-size: 50px;\">" + confirmMsg + "</p></body>");

  if (showBPConfirmation(tr("Clear Model Cache"), content, tr("Clear Cache"), tr("Cancel"), this)) {
    params.putBool("ModelManager_ClearCache", true);
  }
}

void BPModelsPanel::clearModelCache() {
  params.putBool("ModelManager_ClearCache", true);
}

double BPModelsPanel::calculateCacheSize() {
  QFuture<qint64> future_ModelCacheSize = QtConcurrent::run([=]() {
    QDir model_dir(QString::fromStdString(Path::model_root()));
    QFileInfoList model_files = model_dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    qint64 totalSize = 0;
    for (const QFileInfo &model_file : model_files) {
        if (model_file.isFile()) {
            totalSize += model_file.size();
        }
    }
    return totalSize;
  });
  return static_cast<double>(future_ModelCacheSize.result()) / (1024.0 * 1024.0);
}

void BPModelsPanel::onLaneTurnDesireToggled(bool enabled) {
  refreshLaneTurnValueControl();
}

void BPModelsPanel::onLagdToggled(bool enabled) {
  delayControl->setVisible(!enabled);
  updateSteerDelayDescription();
}
