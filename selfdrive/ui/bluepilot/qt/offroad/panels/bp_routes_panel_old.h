#pragma once

#include <QWidget>
#include <QDialog>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QSlider>
#include <QDate>
#include <QDateTime>
#include <QFutureWatcher>
#include <QTimer>
#include <QProcess>
#include <QSet>
#include <QMap>
#include <memory>
#include <vector>

// Forward declarations
class QMediaPlayer;
class QVideoWidget;
class QSlider;
class QTimer;

// Route information structure
struct RouteInfo {
  QString baseName;        // Route identifier (timestamp-based)
  QString timestamp;       // Start time (human readable)
  QString endTimestamp;    // End time
  QString duration;        // Total trip duration
  QString elapsedTime;     // Human-friendly elapsed time
  int segments;           // Number of recording segments
  QString size;           // Total data size
  double tripMiles;       // Distance traveled in miles
  bool hasVideo;          // Video files available
  bool hasRLog;           // Road log available
  bool hasQLog;           // Quality log available
  QDate date;             // Date for grouping
  QString thumbnailPath;  // Cached thumbnail image path
  QString fullPath;       // Full path to route directory
};

// Video types available
enum class VideoType {
  FCamera,    // Primary road-facing camera
  DCamera,    // Driver monitoring camera
  ECamera,    // Wide-angle road camera
  QCamera     // Low-resolution preview
};

// Enhanced video modal for full-screen playback
class BPEnhancedVideoModal : public QDialog {
  Q_OBJECT

public:
  explicit BPEnhancedVideoModal(QWidget *parent = nullptr);
  ~BPEnhancedVideoModal();

  void setRoute(const RouteInfo &route);
  void play();
  void pause();

signals:
  void cameraChanged(VideoType type);

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void onPlayPauseClicked();
  void onCameraButtonClicked();
  void onSeekSliderMoved(int value);
  void onPositionChanged(qint64 position);
  void onDurationChanged(qint64 duration);

private:
  void setupUI();
  void loadVideo(VideoType type);
  QString getVideoPath(VideoType type) const;
  void updateControlsVisibility();
  void concatenateSegments(const QString &outputPath);

  // UI Components
  QVideoWidget *videoWidget;
  QMediaPlayer *mediaPlayer;
  QWidget *controlsWidget;
  QPushButton *playPauseBtn;
  QPushButton *fcameraBtn;
  QPushButton *dcameraBtn;
  QPushButton *ecameraBtn;
  QPushButton *qcameraBtn;
  QSlider *seekSlider;
  QLabel *timeLabel;
  QLabel *durationLabel;

  // State
  RouteInfo currentRoute;
  VideoType currentVideoType;
  bool isPlaying;
  QTimer *hideControlsTimer;
  QString tempVideoPath;
};

// Route card widget
class RouteCardWidget : public QWidget {
  Q_OBJECT

public:
  explicit RouteCardWidget(const RouteInfo &route, QWidget *parent = nullptr);

  void setThumbnail(const QPixmap &pixmap);
  RouteInfo getRoute() const { return route; }

signals:
  void clicked(const RouteInfo &route);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void enterEvent(QEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  void setupUI();

  RouteInfo route;
  QLabel *thumbnailLabel;
  QLabel *timestampLabel;
  QLabel *durationLabel;
  QLabel *sizeLabel;
  QLabel *segmentsLabel;
  QLabel *distanceLabel;

  bool isPressed;
  bool isHovered;
};

// Main routes panel
class BPRoutesPanel : public QWidget {
  Q_OBJECT

public:
  explicit BPRoutesPanel(QWidget *parent = nullptr);
  ~BPRoutesPanel();

public slots:
  void refresh();
  void clearCache();

signals:
  void backPressed();

protected:
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
  void onRouteCardClicked(const RouteInfo &route);
  void onScrollPositionChanged();
  void loadMoreRoutes();

private:
  void setupUI();
  void applyStyles();
  void loadRoutes();
  void scanRoutes();
  RouteInfo parseRoute(const QString &routePath);
  void addRouteCard(const RouteInfo &route);
  void addDateSection(const QDate &date);
  QString generateThumbnailAsync(const QString &routeBase);
  void initializeThumbnail(QLabel *thumbnailLabel, const QString &routeBase);
  QString getThumbnailPath(const QString &routeBase) const;
  void cleanupThumbnailCache();
  QString formatDuration(int seconds) const;
  QString formatSize(qint64 bytes) const;
  QString getRouteStartTime(const QString &routePath);
  QString getRouteEndTime(const QString &routePath);
  double calculateTripDistance(const QString &routePath);
  bool hasVideoFiles(const QString &routePath);
  QString findFFmpegPath() const;

  // UI Components
  QScrollArea *scrollArea;
  QWidget *scrollContent;
  QVBoxLayout *contentLayout;
  QPushButton *refreshBtn;
  QPushButton *clearCacheBtn;
  QLabel *statusLabel;
  std::unique_ptr<BPEnhancedVideoModal> videoModal;

  // Data
  std::vector<RouteInfo> allRoutes;
  std::vector<RouteInfo> displayedRoutes;
  QMap<QDate, QWidget*> dateSections;
  QSet<QString> loadedDates;

  // Pagination
  int currentPage;
  int routesPerPage;
  bool isLoading;
  bool allRoutesLoaded;

  // Thumbnail generation
  QMap<QString, QFutureWatcher<QString>*> thumbnailWatchers;
  QProcess *ffmpegProcess;
  QString ffmpegPath;
  QString thumbnailCachePath;

  // Platform detection
  bool isQCOM2;
  QString routesPath;
};
