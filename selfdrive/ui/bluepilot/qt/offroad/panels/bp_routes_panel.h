// bp_routes_panel.h
#pragma once

// Qt Core
#include <QDateTime>
#include <QFileInfo>
#include <QFuture>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QTime>
#include <QTimer>
#include <QUrl>

// Qt Concurrent
#include <QtConcurrent>

// Qt GUI
#include <QGuiApplication>
#include <QScreen>

// Qt Widgets
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>
#include <QDate>

// Qt Multimedia
#include <QMediaPlayer>

// Custom includes
#include "bp_panel_controls.h"
#include "bp_panel_dialogs.h"
#include "bp_hardware_video_decoder.h"
#include "selfdrive/ui/qt/widgets/input.h"

class BPRoutesPanel : public QWidget {
  Q_OBJECT

public:
  explicit BPRoutesPanel(QWidget *parent = nullptr);
  ~BPRoutesPanel();

  struct RouteInfo {
    QString baseName;
    QString timestamp;    // Start time
    QString endTimestamp; // End time
    QString duration;     // Total duration
    QString elapsedTime;  // Human readable elapsed time
    int segments;
    QString size;
    double tripMiles; // Trip distance in miles
    bool hasVideo;
    bool hasRLog;
    bool hasQLog;
    QDate date; // Date for grouping
    QString thumbnailPath; // Path to thumbnail image
  };

  struct SyncConfig {
    bool enabled = false;
    int startupDelay = 30;
    int retentionDays = 30;
    bool autoConcat = false;
    QString networkLocation;
    QString protocol;
    QString username;
    QString password;
  };

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  Params params;
  QTimer *activityTimer = nullptr;
  QTimer *scrollCheckTimer = nullptr;
  QMutex fileMutex;
  int routeIndex = 0;

  struct RouteCache {
    QHash<QString, RouteInfo> routeInfoCache; // Now RouteInfo is properly defined
    QDateTime lastUpdated;
    static const int CACHE_LIFETIME_SECS = 300;

    bool isValid() const { return !lastUpdated.isNull() && lastUpdated.secsTo(QDateTime::currentDateTime()) < CACHE_LIFETIME_SECS; }

    void update() { lastUpdated = QDateTime::currentDateTime(); }

    void clear() {
      routeInfoCache.clear();
      lastUpdated = QDateTime();
    }
  };

  RouteCache routeCache;

  struct SyncStatus {
    int totalRoutes = 0;
    int processedRoutes = 0;
    int successfulSyncs = 0;
    int failedSyncs = 0;
    qint64 totalBytes = 0;
    qint64 transferredBytes = 0;
    QString currentRoute;
  };

  bool isCommaDevice() {
#ifdef QCOM2
    return true;
#else
    return false;
#endif
  }

  QString getAbsolutePath(const QString &path) {
    if (path.startsWith('~')) {
      return QDir::homePath() + path.mid(1);
    }
    return path;
  }

  bool isOnRoad() { return params.getBool("IsOnRoad"); }
  QString getSerialNumber() { return QString::fromStdString(params.get("HardwareSerial")); }

  // Constants
  const QString getConcatDir = [this]() { return getAbsolutePath(isCommaDevice() ? "/data/tmp/concat_tmp" : "~/comma_data/tmp/concat_tmp"); }();
  const QString getRoutesDir = [this]() { return getAbsolutePath(isCommaDevice() ? "/data/media/0/realdata" : "~/comma_data/media/0/realdata"); }();
  const QString getRoutesDirBackup = [this]() { return getAbsolutePath(isCommaDevice() ? "/data/media/0/realdata_backup" : "~/comma_data/media/0/realdata_backup"); }();
  const QString getThumbnailCacheDir = [this]() { return getAbsolutePath(isCommaDevice() ? "/data/media/0/realdata_thumbnails" : "~/comma_data/media/0/realdata_thumbnails"); }();
  const int THUMBNAIL_WIDTH = 240;  // Width in pixels
  const int THUMBNAIL_HEIGHT = 135; // 16:9 ratio

  QHash<QString, QFutureWatcher<QString> *> thumbnailWatchers;
  void initializeThumbnail(QLabel *thumbnailLabel, const QString &routeBase);
  QString generateThumbnailAsync(const QString &routeBase);
  QString getThumbnailPath(const QString &routeBase);
  void cleanupThumbnail(const QString &routeBase);
  void cleanupThumbnailCache();
  void playRouteVideoConcatenated(const QString &routeBase, const QString &videoFile);
  void continueRouteProcessing();

  // Modern UI methods
  void groupRoutesByDate();
  void createModernRouteWidget(const RouteInfo &route);
  void createDateGroupHeader(const QDate &date, int routeCount);
  void setupModernStyles();
  void loadMoreRoutes();
  void updatePaginationInfo();
  void checkScrollPosition();
  void createRouteCard(const RouteInfo &route, QWidget *parent);
  void setupRouteCardActions(QWidget *card, const RouteInfo &route);
  void showVideoSelectionMenu(const RouteInfo &route);

  // Activity Simulation
  void simulateActivity();
  void stopActivitySimulation();
  void resetMaxDurationTimer();

  // UI Elements
  QVBoxLayout *mainLayout;
  QWidget *contentWidget;
  QLabel *titleLabel;
  QLabel *statsLabel;
  BPScrollView *scrollArea;
  QWidget *routesContainer;
  QVBoxLayout *routesLayout;
  QPushButton *refreshButton;
  QPushButton *cleanupButton;
  QPushButton *settingsButton;
  QPushButton *syncAllButton;
  QPushButton *viewLogButton;
  QProgressDialog *syncProgressDialog;

  // State
  QVector<RouteInfo> routes;
  QHash<QDate, QVector<RouteInfo>> routesByDate; // Routes grouped by date
  bool isLoading;
  bool isSyncing;
  SyncConfig syncConfig;
  SyncStatus syncStatus;
  QTimer *syncTimer;

  // Pagination
  int currentPage = 0;
  int routesPerPage = 10;
  int totalPages = 0;
  QPushButton *loadMoreButton = nullptr;
  QLabel *paginationLabel = nullptr;

  // UI Overlays
  QWidget *loadingOverlay = nullptr;
  QLabel *loadingLabel = nullptr;
  QWidget *statusOverlay = nullptr;
  QLabel *statusLabel = nullptr;

  QHash<QString, bool> expandedRoutes;

  // Setup methods
  void setupUI();
  void setupStyles();
  void setupNetworkSync();
  void loadRoutes();
  void updateRouteList();
  void updateStats();
  void createRouteWidget(const RouteInfo &route);
  void cleanupRoutes(int daysOld);
  void showSyncProgressDialog();

  // Methods for overlay management
  void showLoadingOverlay(const QString &message);
  void hideLoadingOverlay();
  void showStatusOverlay(const QString &message);
  void hideStatusOverlay();

  // Route Operations
  void handleRouteDetails(const QString &route);
  void handleRouteConcatenation(const QString &route);
  void handleRouteRemoval(const QString &route);
  void showSettingsDialog();
  void handleRouteSync();
  void handleCleanup();
  void handleRefresh();
  void viewSyncLog();

  // Platform-specific ffmpeg detection
  QString findFFmpegExecutable();

  void updateButtonStates();

  // Concatenation Methods
  void concatRouteMenu(const QString &routeBase);
  bool concatRouteSegments(const QString &routeBase, const QString &concatType, const QString &outputDir, bool keepOriginals = true);
  bool concatRLog(const QString &routeBase, const QString &outputDir);
  bool concatQLog(const QString &routeBase, const QString &outputDir);
  bool concatVideos(const QString &routeBase, const QString &outputDir);

  // Sync and Backup Methods
  bool syncRoutes();
  void backupAllRoutes();
  bool backupRoute(const QString &routeBase);
  bool createBackupLocation();
  void loadSyncConfig();
  void saveSyncConfig();
  void updateSyncStatus();
  void updateSyncProgress(const QString &status);
  bool validateSyncSettings();
  void logSyncEvent(const QString &message);
  QString getSyncErrorMessage(const QString &route, const QString &error);

  // Utility Methods
  RouteInfo getRouteInfo(const QString &routePath);
  QString formatSize(qint64 bytes);
  QString getDirectorySize(const QString &path);
  int countFilesOfType(const QString &path, const QString &extension);
  void showConfirmDialog(const QString &title, const QString &message, const std::function<void()> &onConfirm);
  QString getRouteSegmentPath(const QString &routeBase, int segment);
  int getTotalSegments(const QString &routeBase);
  QString formatRouteTimestamp(const QString &routeDir);
  QString getRouteDuration(const QString &routeBase);
  QString getSyncLogPath() const { return "/data/media/0/route_sync.log"; }
  qint64 calculateDirSize(const QString &path);
  qint64 QStringToSize(const QString &sizeStr);

  QMessageBox *createStyledMessageBox(const QString &title, const QString &text, QMessageBox::Icon icon) {
    QMessageBox *msgBox = new QMessageBox(this);
    msgBox->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    msgBox->setIcon(icon);
    msgBox->setText(text);
    msgBox->setWindowTitle(title);

    // Set width to match parent
    msgBox->setMinimumWidth(width() * 0.9);

    // Style the message box
    msgBox->setStyleSheet(R"(
        QMessageBox {
            background-color: #212121;
            border: none;
        }
        QMessageBox QLabel {
            color: white;
            font-size: 48px;
            padding: 20px;
        }
        QPushButton {
            background-color: #2196F3;
            color: white;
            font-size: 40px;
            font-weight: 500;
            border: none;
            border-radius: 10px;
            padding: 15px 30px;
            margin: 20px;
            min-width: 150px;
            min-height: 60px;
        }
        QPushButton:pressed {
            background-color: #1976D2;
        }
    )");

    return msgBox;
  }
};

// Dialog class definition
class BPRouteSyncSettingsDialog : public BPDialogBase {
  Q_OBJECT

public:
  explicit BPRouteSyncSettingsDialog(const BPRoutesPanel::SyncConfig &config, QWidget *parent = nullptr);
  BPRoutesPanel::SyncConfig getConfig() const { return currentConfig; }

signals:
  void configurationUpdated(const BPRoutesPanel::SyncConfig &config);

private slots:
  void showLocationKeyboard();
  void showUsernameKeyboard();
  void showPasswordKeyboard();
  void validateAndSave();
  void loadSavedSettings();
  void testConnection();
  bool isCommaDevice() {
#ifdef OCOM2
    return true;
#else
    return false;
#endif
  }

private:
  Params params;
  void setupUI();

  QString getAbsolutePath(const QString &path) {
    if (path.startsWith('~')) {
      return QDir::homePath() + path.mid(1);
    }
    return path;
  }

  QLineEdit *locationEdit;
  QComboBox *protocolCombo;
  QSpinBox *delaySpinner;
  QSpinBox *retentionSpinner;
  BPToggle *enabledToggle;    // Changed from QCheckBox
  BPToggle *autoConcatToggle; // Changed from QCheckBox

  QPushButton *locationButton;
  QPushButton *usernameButton;
  QPushButton *passwordButton;
  QWidget *credentialsContainer;

  BPRoutesPanel::SyncConfig currentConfig;
};

class BPVideoDialog : public BPDialogBase {
  Q_OBJECT
public:
  bool m_fullscreenApplied = false;
  explicit BPVideoDialog(const QString &videoPath, QWidget *parent) : BPDialogBase(parent) {

    setWindowTitle(tr("Video Playback"));
    // this->showFullScreen();

    // Create a main vertical layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header (100px height)
    QWidget *header = new QWidget;
    header->setFixedHeight(100);
    header->setStyleSheet("background-color: #202020;");

    QHBoxLayout *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(30, 20, 30, 20);

    BPBackButton *backBtn = new BPBackButton;
    connect(backBtn, &BPBackButton::clicked, this, &QDialog::reject);

    QLabel *titleLabel = new QLabel(tr("Video Playback"));
    titleLabel->setStyleSheet("font-size: 48px; font-weight: 600; color: white;");
    titleLabel->setAlignment(Qt::AlignCenter);

    headerLayout->addWidget(backBtn);
    headerLayout->addWidget(titleLabel, 1);
    headerLayout->addSpacing(backBtn->width());

    // Video widget (takes remaining space)
    videoWidget = new QVideoWidget;
    videoWidget->setMinimumHeight(200);

    // Controls (100px height)
    QWidget *controls = new QWidget;
    controls->setFixedHeight(100);
    controls->setStyleSheet("background-color: #333333;");

    QHBoxLayout *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(20, 10, 20, 10);

    playPauseButton = new QPushButton(tr("Pause"));
    playPauseButton->setFixedSize(100, 80);

    slider = new QSlider(Qt::Horizontal);
    timeLabel = new QLabel("00:00 / 00:00");
    timeLabel->setStyleSheet("color: white; font-size: 28px;");

    controlsLayout->addWidget(playPauseButton);
    controlsLayout->addWidget(slider, 1);
    controlsLayout->addWidget(timeLabel);

    // Add widgets to main layout
    mainLayout->addWidget(header);
    mainLayout->addWidget(videoWidget, 1);
    mainLayout->addWidget(controls);

    // Set up media player
    player = new QMediaPlayer(this);
    player->setVideoOutput(videoWidget);
    player->setMedia(QUrl::fromLocalFile(videoPath));
    player->play();

    // Connect controls
    connect(playPauseButton, &QPushButton::clicked, this, &BPVideoDialog::togglePlayback);
    connect(player, &QMediaPlayer::positionChanged, this, &BPVideoDialog::updatePosition);
    connect(player, &QMediaPlayer::durationChanged, this, &BPVideoDialog::updateDuration);
    connect(slider, &QSlider::sliderMoved, player, &QMediaPlayer::setPosition);
  }

private slots:
  void togglePlayback() {
    if (player->state() == QMediaPlayer::PlayingState) {
      player->pause();
      playPauseButton->setText(tr("Play"));
    } else {
      player->play();
      playPauseButton->setText(tr("Pause"));
    }
  }

  void updatePosition(qint64 position) {
    slider->setValue(position);
    updateTimeLabel();
  }

  void updateDuration(qint64 duration) {
    slider->setRange(0, duration);
    updateTimeLabel();
  }

  void updateTimeLabel() {
    int duration = player->duration() / 1000;
    int position = player->position() / 1000;
    QString format = duration >= 3600 ? "hh:mm:ss" : "mm:ss";
    timeLabel->setText(QTime(0, 0).addSecs(position).toString(format) + " / " + QTime(0, 0).addSecs(duration).toString(format));
  }

  void setupFullscreen() {
    // If we have already done it, do nothing
    if (m_fullscreenApplied)
      return;
    m_fullscreenApplied = true;

    setFixedSize(2160, 1080);
    show();
#ifdef QCOM2
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    if (native && windowHandle()) {
      wl_surface *s = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow("surface", windowHandle()));
      if (s) {
        wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
        wl_surface_commit(s);
      }
      setWindowState(Qt::WindowFullScreen);
      layout()->activate();
    }
#endif
  }

protected:
  void showEvent(QShowEvent *event) override {
    QDialog::showEvent(event);
    if (videoWidget) {
      setupFullscreen();
    }
  }

private:
  QMediaPlayer *player;
  QVideoWidget *videoWidget;
  QPushButton *playPauseButton;
  QSlider *slider;
  QLabel *timeLabel;
};

Q_DECLARE_METATYPE(BPRoutesPanel::RouteInfo)
