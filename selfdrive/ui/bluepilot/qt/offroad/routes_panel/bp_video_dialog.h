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

// Custom includes
#include "../panels/bp_panel_dialogs.h"

// Forward declarations
class FrameReader;
class VisionBuf;
class BPVideoWidget;
class BPRoutesPanel;

struct DecodedFrame;

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

private:
  void setupUI();
  void setupFullWidthHeader();
  void setupVideoDisplay();
  void showDebugPlayerOutput(const QString &message);
  void setupCameraPanel();
  void setupOverlayControls();
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

  // Route data
  QString routeBaseName;
  struct {
    QString baseName, timestamp, endTimestamp, duration, elapsedTime, displayDate, size;
    int segments;
    double tripMiles;
    bool hasVideo, hasRLog, hasQLog, hasFrontVideo, hasWideVideo, hasDriverVideo, hasLQVideo;
    bool hasFrontHQVideo, hasFrontLQVideo, hasDriverHQVideo, isStarred;
    QDateTime dateTime;
  } routeInfo;  // Matches actual BPRoutesPanel::RouteInfo by layout

  // Video playback
  std::unique_ptr<FrameReader> frameReader;
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

  // Segment preloading system
  std::unique_ptr<FrameReader> nextSegmentReader;
  int preloadedSegment = -1;

  // Playback state management
  qint64 lastValidPosition = 0;
  bool isPausedSnapshot = false;

  // UI Components
  QWidget *headerWidget;
  QWidget *videoContainer;
  class BPVideoWidget *videoDisplay;
  QWidget *cameraPanel;
  QWidget *controlsWidget;
  QLabel *routeTitle;
  QPushButton *starButton;
  QPushButton *fullscreenExitButton;
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
};
