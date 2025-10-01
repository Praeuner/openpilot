#pragma once

#include <QButtonGroup>
#include <QDateTime>
#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>

// Custom includes
#include "../panels/bp_panel_dialogs.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/video_controller.h"
#include "selfdrive/ui/bluepilot/qt/offroad/routes_panel/watchdog_detector.h"

// Forward declarations
class VisionBuf;
class BPVideoWidget;
class BPRoutesPanel;

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
  void keepDisplayAwake();
  void hideOverlayControls();
  void showOverlayControls();
  void onVideoTap();

private:
  void setupUI();
  void setupFullWidthHeader();
  void setupVideoDisplay();
  void setupCameraPanel();
  void setupOverlayControls();
  void setupActionButtons(QVBoxLayout *parentLayout);
  QString buttonStyle(const QString &size);
  void updateOverlayPosition();
  void updateCameraButtonStates();
  void showLoadingIndicator();
  void hideLoadingIndicator();
  void handlePlaybackState(VideoState state);
  void handleRouteLoaded();
  void handleFrameReady(VisionBuf *buf, int width, int height, int64_t timestamp_ms);
  void handlePositionChanged(int64_t position_ms, int64_t duration_ms);
  void handleBufferLevel(float level);
  void handleSegmentChanged(int current_segment, int total_segments);
  void handleError(const QString &message);
  void handleSeekRequested(int64_t position_ms);
  void handlePlaybackMetrics(int64_t buffered_ms, int total_frames);
  void attachInputHandlers();
  void resetOverlayTimers();
  void updatePlaybackControls();
  void resetPlaybackUi();
  void updateTimeLabel(qint64 position_ms, qint64 total_ms);
  void updatePlayStatus(VideoState state);

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
  QString currentCameraType = "front";
  bool isPlaying = false;
  bool isFullscreen = false;
  bool isSeeking = false;
  QTimer *keepAwakeTimer;
  qint64 totalDuration = 0;
  qint64 currentPosition = 0;

  // Overlay control management
  bool controlsVisible = true;
  QTimer *overlayFadeTimer;

  // Async playback pipeline
  std::unique_ptr<VideoController> videoController;
  std::unique_ptr<WatchdogDetector> watchdog;

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
  QLabel *playStatusLabel;
  QLabel *segmentLabel;
  QWidget *sliderContainer;

  int currentSegmentIndex = -1;
  int totalSegments = 0;

  // Fullscreen SVG icons
  QPixmap fullscreenIcon;
  QPixmap minimizeIcon;
};
