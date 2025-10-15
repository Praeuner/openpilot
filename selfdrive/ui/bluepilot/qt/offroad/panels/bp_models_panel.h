// selfdrive/ui/bluepilot/qt/offroad/panels/bp_models_panel.h
// BluePilot Models Panel - Native implementation with BP styling

#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QTimer>
#include <QProgressBar>
#include <QFrame>

#include "selfdrive/ui/bluepilot/qt/offroad/panels/bp_panel_controls.h"
#include "common/params.h"
#include "common/util.h"

/**
 * BPModelsPanel - Native models management panel with BP styling
 *
 * This panel provides model selection, download management, and model-related settings
 * using BluePilot controls and styling.
 */
class BPModelsPanel : public QWidget {
  Q_OBJECT

public:
  explicit BPModelsPanel(QWidget *parent = nullptr);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void setupUI();
  void createModelSelectionGroup();
  void createDownloadProgressGroup();
  void createLaneTurnGroup();
  void createSteerDelayGroup();

  // Helper methods
  QGroupBox* createStyledGroupBox(const QString &title);
  void refreshAll();
  void updateLabels();
  void updateModelManagerState();
  void handleBundleDownloadProgress();
  void refreshLaneTurnValueControl();
  void refreshDelayControl();
  void updateSteerDelayDescription();

  // Model selection
  QString getActiveModelName();
  QString getActiveModelInternalName();
  QString getActiveModelRef();
  void handleModelSelectionClicked();
  void showResetParamsDialog();

  // Model cache management
  void clearModelCache();
  double calculateCacheSize();

  // Progress bar helpers
  QProgressBar* createProgressBar(QWidget *parent);
  QFrame* createModelDetailFrame(QWidget *parent, const QString &typeName, QProgressBar *progressBar);

  bool isDownloading() const {
    if (!model_manager.hasSelectedBundle()) {
        return false;
    }
    const auto &selected_bundle = model_manager.getSelectedBundle();
    return selected_bundle.getStatus() == cereal::ModelManagerSP::DownloadStatus::DOWNLOADING;
  }

  // Confirmation dialog helper
  bool canContinueOnMeteredDialog();
  bool showConfirmationDialog(const QString &message = QString(), const QString &confirmButtonText = QString(), bool show_metered_warning = false);

  // Core components
  Params params;

  // Layout
  QVBoxLayout *mainLayout;

  // Groups
  QGroupBox *modelSelectionGroup;
  QGroupBox *downloadProgressGroup;
  QGroupBox *laneTurnGroup;
  QGroupBox *steerDelayGroup;

  // Model Selection Group
  QPushButton *currentModelBtn;
  QLabel *currentModelLabel;
  QPushButton *refreshModelsBtn;
  QPushButton *clearCacheBtn;
  QLabel *cacheSizeLabel;

  // Download Progress Group
  QFrame *supercomboFrame;
  QProgressBar *supercomboProgressBar;
  QFrame *navigationFrame;
  QProgressBar *navigationProgressBar;
  QFrame *visionFrame;
  QProgressBar *visionProgressBar;
  QFrame *policyFrame;
  QProgressBar *policyProgressBar;

  // Lane Turn Group
  BPToggleControl *laneTurnDesireToggle;
  BPNumericControl *laneTurnValueControl;

  // Steer Delay Group
  BPToggleControl *lagdToggleControl;
  BPNumericControl *delayControl;

  // State tracking
  bool is_onroad = false;
  bool is_metered = false;
  bool is_wifi = false;
  cereal::ModelManagerSP::Reader model_manager;
  cereal::ModelManagerSP::DownloadStatus download_status{};
  cereal::ModelManagerSP::DownloadStatus prev_download_status{};

  // Timers
  QTimer *refreshTimer;

private slots:
  void onModelSelectionClicked();
  void onRefreshModelsClicked();
  void onClearCacheClicked();
  void onLaneTurnDesireToggled(bool enabled);
  void onLagdToggled(bool enabled);
};
