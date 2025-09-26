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

// Standard Library
#include <functional>
#include <memory>

// Forward declarations
class FrameReader;
class MediaControlButton;

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
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>

// Qt Multimedia
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QKeyEvent>

// Custom includes
#include "../panels/bp_panel_controls.h"
#include "../panels/bp_panel_dialogs.h"
#include "selfdrive/ui/qt/widgets/input.h"
#include "selfdrive/ui/sunnypilot/ui.h"

// Video types
#include "bp_video_types.h"

// Forward declarations for video decoder types
class VideoDecoder;
class VisionBuf;

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
    QString displayDate;  // Date for grouping (e.g. "Thursday - September 18, 2025")
    int segments;
    QString size;
    double tripMiles; // Trip distance in miles
    bool hasVideo;
    bool hasRLog;
    bool hasQLog;
    bool hasFrontVideo;      // Legacy - any front camera
    bool hasWideVideo;
    bool hasDriverVideo;
    bool hasLQVideo;         // Legacy - any LQ video
    bool hasFrontHQVideo;    // fcamera.hevc
    bool hasFrontLQVideo;    // qcamera.ts
    bool hasDriverHQVideo;   // dcamera.hevc
    bool isStarred;
    QDateTime dateTime;   // For sorting
  };

  // Public methods needed by video dialog
  QString getRoutesDir() const;
  RouteInfo getRouteInfo(const QString &routePath);
  bool isRouteStarred(const QString &routeBase);
  void setRouteStarred(const QString &routeBase, bool starred);
  void handleRouteStarToggle(const QString &route);
  void handleRefresh();
  QString formatElapsedTime(const QDateTime &routeTime);
  QString getThumbnailPath(const QString &routeBase);
  void initializeThumbnail(QLabel *thumbnailLabel, const QString &routeBase);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
  void onOffroadTransition();

private:
  Params params;
  QTimer *activityTimer = nullptr;
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

  // Disk caching methods
  QString getRouteCacheFile() const;
  void saveRouteCacheToDisk();
  bool loadRouteCacheFromDisk();
  bool shouldRefreshRoutes() const;

  bool isCommaDevice() const {
#ifdef QCOM2
    return true;
#else
    return false;
#endif
  }

  QString getAbsolutePath(const QString &path) const {
    if (path.startsWith('~')) {
      return QDir::homePath() + path.mid(1);
    }
    return path;
  }

  bool isOnRoad() { return params.getBool("IsOnRoad"); }
  QString getSerialNumber() { return QString::fromStdString(params.get("HardwareSerial")); }

  // Constants
  const QString getConcatDir = [this]() { return getAbsolutePath(isCommaDevice() ? "/data/tmp/concat_tmp" : "~/comma_data/tmp/concat_tmp"); }();
  const QString getRoutesDirBackup = [this]() { return getAbsolutePath(isCommaDevice() ? "/data/media/0/realdata_backup" : "~/comma_data/media/0/realdata_backup"); }();
  const QString getThumbnailCacheDir = [this]() {
    QString dir = isCommaDevice() ? "/data/bluepilot/routes/thumbs_cache" : "~/comma_data/bluepilot/routes/thumbs_cache";
    QDir().mkpath(getAbsolutePath(dir)); // Ensure directory exists
    return getAbsolutePath(dir);
  }();
  const int THUMBNAIL_WIDTH = 480;  // Width in pixels (scaled for 6" display)
  const int THUMBNAIL_HEIGHT = 270; // 16:9 ratio
  const int MAX_CONCURRENT_THUMBNAILS = 20; // Allow many concurrent FFmpeg processes for fast generation

  QHash<QString, QFutureWatcher<QString> *> thumbnailWatchers;
  QSet<QString> permanentlyFailedRoutes;  // Track routes without video files
  QString generateThumbnail(const QString &routeBase);
  QString generateThumbnailHardware(const QString &routeBase);  // Hardware-accelerated version
  QString generateThumbnailAuto(const QString &routeBase);      // Auto-select hardware/software
  bool saveYUVasJPEG(VisionBuf *buf, const QString &outputPath, int width, int height);
  void generateAllMissingThumbnails(); // Generate all missing thumbnails at once
  void cleanupThumbnail(const QString &routeBase);
  void cleanupThumbnailCache();
  void playRouteVideoConcatenated(const QString &routeBase, const QString &videoFile);
  void continueRouteProcessing();

  // Activity Simulation
  void simulateActivity();
  void stopActivitySimulation();
  void resetMaxDurationTimer();

  // Onroad Safety
  void showOnroadMessage();
  void clearOnroadMessage();

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
  QPushButton *viewLogButton;
  QWidget *onroadMessageWidget = nullptr;

  // State
  QVector<RouteInfo> routes;
  QVector<RouteInfo> displayedRoutes;  // Currently displayed routes for lazy loading
  int loadedCount = 0;                 // Number of routes currently loaded
  const int ROUTES_PER_LOAD = 15;      // Load 15 routes at a time
  bool isLoading;

  // UI Overlays
  QWidget *loadingOverlay = nullptr;
  QLabel *loadingLabel = nullptr;
  QWidget *statusOverlay = nullptr;
  QLabel *statusLabel = nullptr;

  QHash<QString, bool> expandedRoutes;
  QHash<QString, QWidget*> dateGroupWidgets;  // Track date group widgets
  QString currentSelectedRoute;                // Currently selected route for video playback

  // Setup methods
  void setupUI();
  void setupStyles();
  void loadRoutes();
  void updateRouteList();
  void updateStats();
  void createRouteWidget(const RouteInfo &route);
  void cleanupRoutes(int daysOld);

  // Methods for overlay management
  void showLoadingOverlay(const QString &message);
  void hideLoadingOverlay();
  void showStatusOverlay(const QString &message);
  void hideStatusOverlay();

  // Route Operations
  void handleRouteDetails(const QString &route);
  void handleRouteConcatenation(const QString &route);
  void handleRouteRemoval(const QString &route);
  void handleRouteVideoPlayback(const QString &route, const QString &cameraType = "front");
  void loadMoreRoutes();
  void showSettingsDialog();
  void handleCleanup();

  void updateButtonStates();

  // Concatenation Methods
  void concatRouteMenu(const QString &routeBase);
  bool concatRouteSegments(const QString &routeBase, const QString &concatType, const QString &outputDir, bool keepOriginals = true);
  bool concatRLog(const QString &routeBase, const QString &outputDir);
  bool concatQLog(const QString &routeBase, const QString &outputDir);
  bool concatVideos(const QString &routeBase, const QString &outputDir);

  // Backup Methods
  void backupAllRoutes();
  bool backupRoute(const QString &routeBase);
  bool createBackupLocation();

  // Utility Methods
  QString formatSize(qint64 bytes);
  QString getDirectorySize(const QString &path);
  int countFilesOfType(const QString &path, const QString &extension);
  void showConfirmDialog(const QString &title, const QString &message, const std::function<void()> &onConfirm);
  QString getRouteSegmentPath(const QString &routeBase, int segment);
  int getTotalSegments(const QString &routeBase);
  QString formatRouteTimestamp(const QString &routeDir);
  QString getRouteDuration(const QString &routeBase);
  qint64 calculateDirSize(const QString &path);
  qint64 QStringToSize(const QString &sizeStr);
  QString getStarFilePath(const QString &routeBase);
  QString formatDisplayDate(const QDateTime &dateTime);
  QWidget* createRouteCard(const RouteInfo &route);
  QWidget* createDateGroup(const QString &dateText);

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

