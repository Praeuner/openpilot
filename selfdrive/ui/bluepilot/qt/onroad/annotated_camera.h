/**
 * Bluepilot Annotated Camera Widget
 * Extends sunnypilot's AnnotatedCameraWidget with bluepilot features
 */

#pragma once

#include "selfdrive/ui/sunnypilot/qt/onroad/annotated_camera.h"
#include "selfdrive/ui/bluepilot/qt/onroad/widgets/hybrid_drive_gauge.h"

class AnnotatedCameraWidgetBP : public AnnotatedCameraWidgetSP {
  Q_OBJECT

public:
  explicit AnnotatedCameraWidgetBP(VisionStreamType type, QWidget *parent = nullptr);
  void updateState(const UIState &s) override;

protected:
  void paintGL() override;
  void showEvent(QShowEvent *event) override;

private:
  // Bluepilot-specific drawing methods
  void drawColoredText(QPainter &p, int x, int y, const QString &text, QColor color);
  void drawLeftTurnSignal(QPainter &painter, int x, int y, int circle_size, int state);
  void drawRightTurnSignal(QPainter &painter, int x, int y, int circle_size, int state);
  void drawStandstillTimer(QPainter &p, int x, int y);
  int blinkerPulse(int frame);

  // Blinker related
  bool left_blinker = false;
  bool right_blinker = false;
  bool left_blindspot = false;
  bool right_blindspot = false;
  bool lane_change_edge_block = false;
  int blinker_frame = 0;
  int blinker_state = 0;
  static const int blinker_size = 120;

  // Stand still timer related
  bool standStill = false;
  bool prev_standStill = false;
  double standstill_start_time = 0.0;
  float standstillElapsedTime = 0.0;
  double standstill_exit_time = 0.0;
  float vehicle_speed = 0.0;
  static constexpr float STANDSTILL_THRESHOLD = 0.1f;
  static constexpr float STANDSTILL_DEBOUNCE_TIME = 0.5f;

  // Performance monitoring
  double last_frame_time = 0.0;

  // Hybrid Drive Data
  bool hevDataAvailable = false;
  float hevThrottleDemandPercent = 0.0f;
  float hevThrottleThresholdPercent = 0.0f;
  QString hevPowerFlowMode;
  QString hevEngineOnReason;

  // Hybrid Battery Data
  bool hevBattDataAvailable = false;
  float hevBattAmpsActual = 0.0f;
  float hevBattVoltActual = 0.0f;
  float hevBattVoltLowLimit = 0.0f;
  float hevBattVoltHighLimit = 0.0f;
  float hevBattSocActual = 0.0f;
  float hevBattSocMinPerc = 0.0f;
  float hevBattSocMaxPerc = 0.0f;
};