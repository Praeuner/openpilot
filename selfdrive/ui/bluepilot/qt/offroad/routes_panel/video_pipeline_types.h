#pragma once

enum class VideoState {
  kIdle = 0,
  kLoading,
  kBuffering,
  kPlaying,
  kPaused,
  kSeeking,
  kEndOfRoute,
  kError
};

enum class CameraKind {
  kFront,
  kWide,
  kDriver,
  kFrontLq
};

