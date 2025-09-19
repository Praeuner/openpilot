#pragma once

#include <QWidget>
#include <QDialog>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QDate>
#include <QDateTime>
#include <QFutureWatcher>
#include <QTimer>
#include <QProcess>
#include <QSet>
#include <QMap>
#include <QVector>
#include <QFile>
#include <QTime>
#include <memory>
#include <vector>

// Forward declarations
class VideoDecoder;
struct DecodedFrame;

// Route information structure
struct RouteInfo {
  QString baseName;
  QString timestamp;
  QString duration;
  QString elapsedTime;
  int segments;
  QString size;
  qint64 totalBytes;
  bool hasVideo;
  bool hasFCamera;
  bool hasDCamera;
  bool hasECamera;
  bool hasQCamera;
  bool hasRLog;
  bool hasQLog;
  bool isStarred;
  QDate date;
  QString thumbnailPath;
  QString fullPath;
};

// Enhanced Video Modal with right-side camera panel
class BPEnhancedVideoModal : public QDialog {
  Q_OBJECT

public:
  explicit BPEnhancedVideoModal(const QString &routeBase, const RouteInfo &route, QWidget *parent = nullptr);
  ~BPEnhancedVideoModal();

  void setRoute(const RouteInfo &route);

signals:
  void routeDeleted(const QString &routeBaseName);
  void routeStarredChanged(const QString &routeBaseName, bool starred);

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;

private slots:
  void onCloseClicked();
  void onFullscreenToggle();
  void togglePlayback();
  void onPlaybackTimer();
  void onDecodeChunk();
  void deleteRoute();
  void onStarClicked();
  void switchCamera(const QString &cameraFile);

private:
  void setupUI();
  void updateStarButton();
  void createCameraButton(const QString &label, const QString &file, bool isDefault);
  void loadVideo(const QString &videoFile);
  void setupDecoder();
  void startPlayback();
  void stopPlayback();
  void setupFullscreen();
  void applyQCOM2Rotation();
  void updateTimeLabel();
  void onFrameDecoded(const DecodedFrame &frame);
  void convertYUVToRGB(const DecodedFrame &frame);
  QVector<QString> getAvailableSegments(const QString &videoFile);
  
  // File I/O and decoding
  bool openSegmentFile(int segmentIndex);
  void processHEVCBuffer();
  void processTSBuffer();
  bool isHEVCKeyframe(uint8_t* data, int size);
  void seekToPosition(qint64 positionMs);
  
  // UI Components
  QWidget *header;
  QLabel *routeInfoLabel;
  QPushButton *closeButton;
  QPushButton *fullscreenButton;
  QPushButton *starButton;
  
  QWidget *videoContainer;
  QLabel *videoDisplay;
  
  QWidget *controlsWidget;
  QPushButton *playPauseButton;
  QSlider *positionSlider;
  QLabel *timeLabel;
  
  QWidget *cameraPanel;
  QVBoxLayout *cameraButtonLayout;
  QMap<QString, QPushButton*> cameraButtons;
  QPushButton *deleteButton;
  
  // Video decoder
  std::unique_ptr<VideoDecoder> decoder;
  
  // State
  QString m_routeBase;
  RouteInfo m_route;
  QString m_currentCamera;
  bool m_isFullscreen = false;
  bool m_fullscreenApplied = false;
  bool isPlaying = false;
  bool isDecoding = false;
  
  // Playback state
  QVector<QString> m_segmentPaths;
  int m_currentSegmentIndex = 0;
  qint64 m_totalDuration = 0;
  qint64 m_currentPosition = 0;
  
  // Timers
  QTimer *playbackTimer = nullptr;
  QTimer *decodeTimer = nullptr;
  
  // Frame data
  QPixmap currentPixmap;
  std::vector<uint8_t> rgbBuffer;
  
  // File handling
  QFile *currentVideoFile = nullptr;
  QByteArray readBuffer;
  static const int CHUNK_SIZE = 64 * 1024; // 64KB chunks
  int frameCount = 0;
  qint64 m_segmentStartTime = 0;
};

// Route card widget with star functionality
class RouteCardWidget : public QWidget {
  Q_OBJECT

public:
  explicit RouteCardWidget(const RouteInfo &route, QWidget *parent = nullptr);
  void setThumbnail(const QPixmap &pixmap);
  void setStarred(bool starred);
  RouteInfo getRoute() const { return route; }

signals:
  void clicked(const RouteInfo &route);
  void starToggled(const QString &routeBaseName, bool starred);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void enterEvent(QEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private slots:
  void onStarButtonClicked();

private:
  void setupUI();
  void updateStarButton();

  RouteInfo route;
  QLabel *thumbnailLabel;
  QLabel *timestampLabel;
  QLabel *durationLabel;
  QLabel *sizeLabel;
  QLabel *segmentsLabel;
  QPushButton *starButton;
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
  void onRouteDeleted(const QString &routeBaseName);
  void onRouteStarredChanged(const QString &routeBaseName, bool starred);
  void onCardStarToggled(const QString &routeBaseName, bool starred);
  void onScrollPositionChanged();
  void loadMoreRoutes();

private:
  void setupUI();
  void applyStyles();
  void loadRoutes();
  void scanRoutes();
  void updateStats();
  RouteInfo parseRoute(const QString &routePath);
  void addRouteCard(const RouteInfo &route);
  void addDateSection(const QDate &date);
  QString generateThumbnailAsync(const QString &routeBase);
  void initializeThumbnail(QLabel *thumbnailLabel, const QString &routeBase);
  QString getThumbnailPath(const QString &routeBase) const;
  void cleanupThumbnailCache();
  QString formatDuration(int seconds) const;
  QString formatSize(qint64 bytes) const;
  QString getDurationFromRoute(const QString &routePath) const;
  bool hasVideoFiles(const QString &routePath);
  QString findFFmpegPath() const;
  void saveRouteStarStatus(const QString &routeBaseName, bool starred);
  bool loadRouteStarStatus(const QString &routeBaseName);
  void removeRouteCard(const QString &routeBaseName);

  // UI Components
  QScrollArea *scrollArea;
  QWidget *scrollContent;
  QVBoxLayout *contentLayout;
  QPushButton *refreshBtn;
  QPushButton *clearCacheBtn;
  QLabel *statusLabel;
  QLabel *statsLabel;
  std::unique_ptr<BPEnhancedVideoModal> videoModal;

  // Data
  std::vector<RouteInfo> allRoutes;
  std::vector<RouteInfo> displayedRoutes;
  QMap<QDate, QWidget*> dateSections;
  QSet<QString> loadedDates;
  QMap<QString, RouteCardWidget*> routeCards;

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
  QString routesPath;
};
