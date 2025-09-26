#pragma once

#include <QDialog>
#include <QTimer>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QWidget>
#include <QButtonGroup>
#include <QKeyEvent>
#include <QEvent>
#include <QVBoxLayout>
#include <QDateTime>
#include <memory>
#include <QFuture>
#include <QPixmap>
#include <QFutureWatcher>
#include <QProgressBar>
#include <QCache>
#include <QThreadPool>
#include <QMutex>
#include <QPointer>
#include <atomic>
#include <queue>
#include <thread>

// Custom includes
#include "../panels/bp_panel_dialogs.h"
#include "bp_frame_reader.h"

// Forward declarations
class FrameReader;
class VisionBuf;
class BPVideoWidget;
class BPRoutesPanel;

struct DecodedFrame;

// Player state enumeration for UI feedback
enum class PlayerState {
    Idle,
    Playing,
    Paused,
    Buffering,
    Seeking,
    Loading
};

// Playback state for precise pause/resume
struct PlaybackState {
    int frameIndex = 0;
    int segment = 0;
    qint64 position = 0;
    bool wasPlaying = false;
    QString cameraType;
};

// Frame buffer pool for memory optimization
class FrameBufferPool {
public:
    FrameBufferPool(size_t maxBuffers = 10);
    ~FrameBufferPool();

    VisionBuf* acquire(size_t frameSize);
    void release(VisionBuf* buffer);
    void clear();

private:
    std::queue<std::unique_ptr<VisionBuf>> available;
    std::mutex mutex;
    size_t maxBuffers;
    size_t frameSize = 0;
};

// Segment cache for intelligent preloading
class SegmentCache {
public:
    SegmentCache(int maxCacheSize = 5);
    ~SegmentCache();

    std::shared_ptr<FrameReader> getSegment(int segmentIndex);
    void preloadSegment(int segmentIndex, const QString& videoPath, CameraType cameraType);
    void clearCache();
    bool hasSegment(int segmentIndex) const;
    void cancelPendingLoads();

private:
    QCache<int, std::shared_ptr<FrameReader>> cache;
    QThreadPool* loaderPool;
    std::atomic<bool> shutdown{false};
    mutable QMutex cacheMutex;
};

class BPRouteVideoDialog : public BPDialogBase {
  Q_OBJECT

public:
  explicit BPRouteVideoDialog(const QString &routeBase, QWidget *parent = nullptr);
  ~BPRouteVideoDialog();

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void togglePlayback();
  void seekForward();
  void seekBackward();
  void toggleFullscreen();
  void switchCamera(const QString &cameraType);
  void deleteRoute();
  void toggleStar();
  void onSegmentFinished();
  void updatePlaybackPosition();
  void keepDisplayAwake();
  void hideOverlayControls();
  void showOverlayControls();
  void preloadNextSegment();
  void onSegmentTransitionStart();
  void onVideoTap();
  void onSeekCompleted();
  void onSegmentPreloaded();
  void updatePlayerState(PlayerState state);
  void showLoadingIndicator(const QString& message);
  void hideLoadingIndicator();

private:
  void setupUI();
  void setupFullWidthHeader();
  void setupVideoDisplay();
  void showDebugPlayerOutput(const QString &message);
  void setupCameraPanel();
  void setupOverlayControls();
  void setupStatusOverlay();
  void setupActionButtons(QVBoxLayout *parentLayout);
  QString buttonStyle(const QString &size);
  void updateOverlayPosition();
  void loadVideoSegments();
  void loadThumbnail();
  void playCurrentSegment();
  void playbackVideoFrames();
  void onFrameDecoded(const DecodedFrame &frame, const VisionBuf *buf);
  void updateCameraButtonStates();
  QString getVideoPath(const QString &cameraType, int segment);
  void seekToPosition(qint64 positionMs);
  void updateVideoFrame();
  void handleCameraError(const QString& failedCamera);

  // Route data
  QString routeBaseName;
  struct {
    QString baseName, timestamp, endTimestamp, duration, elapsedTime, displayDate, size, humanTime;
    int segments;
    double tripMiles;
    bool hasVideo, hasRLog, hasQLog, hasFrontVideo, hasWideVideo, hasDriverVideo, hasLQVideo;
    bool hasFrontHQVideo, hasFrontLQVideo, hasDriverHQVideo, isStarred;
    QDateTime dateTime;
  } routeInfo;  // Matches actual BPRoutesPanel::RouteInfo by layout

  // Video playback
  std::shared_ptr<FrameReader> frameReader;
  QStringList currentPlaylist;
  QString currentCameraType = "front";
  int currentSegment = 0;
  bool isPlaying = false;
  bool isFullscreen = false;
  bool isSeeking = false;
  QTimer *playbackTimer;
  QTimer *positionTimer;
  QTimer *keepAwakeTimer;
  QFuture<void> playbackFuture;
  qint64 totalDuration = 0;
  qint64 currentPosition = 0;

  // Timer-based playback state
  size_t currentFrameIndex = 0;
  size_t totalFrames = 0;

  // Overlay control management
  bool controlsVisible = true;
  QTimer *overlayFadeTimer;

  // Enhanced segment preloading system
  std::unique_ptr<SegmentCache> segmentCache;
  std::unique_ptr<FrameBufferPool> bufferPool;
  std::unique_ptr<FrameReader> nextSegmentReader;
  int preloadedSegment = -1;

  // Enhanced playback state management
  PlayerState currentPlayerState = PlayerState::Idle;
  PlaybackState pausedState;
  qint64 lastValidPosition = 0;
  bool isPausedSnapshot = false;
  std::atomic<bool> stopAllOperations{false};

  // Async operation management
  QPointer<QFutureWatcher<void>> activeSeekWatcher;
  QPointer<QFutureWatcher<std::shared_ptr<FrameReader>>> segmentLoadWatcher;
  std::atomic<int> loadingSegment{-1};

  // UI Components
  QWidget *headerWidget;
  QWidget *videoContainer;
  class BPVideoWidget *videoDisplay;
  QWidget *cameraPanel;
  QWidget *controlsWidget;
  QLabel *routeTitle;
  QPushButton *starButton;
  QPushButton *fullscreenToggleButton;
  QPushButton *closeButton;
  QPushButton *frontCamButton;
  QPushButton *wideCamButton;
  QPushButton *driverCamButton;
  QPushButton *lqCamButton;
  QPushButton *deleteButton;
  class MediaControlButton *playPauseButton;
  QSlider *positionSlider;
  QLabel *timeLabel;
  QLabel *segmentLabel;

  // Status overlay components
  QWidget *statusOverlay;
  QLabel *statusIndicator;
  QProgressBar *seekProgress;
  QProgressBar *loadingProgress;
  QLabel *loadingLabel;

  // Fullscreen SVG icons
  QPixmap fullscreenIcon;
  QPixmap minimizeIcon;
};
