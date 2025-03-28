#pragma once

#include <QVBoxLayout>
#include <memory>
#include "selfdrive/ui/qt/onroad/driver_monitoring.h"
#include "selfdrive/ui/qt/onroad/model.h"
#include "selfdrive/ui/qt/widgets/cameraview.h"
#include "selfdrive/ui/bluepilot/qt/onroad/widgets/hybrid_drive_gauge.h"

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/qt/onroad/buttons.h"
#include "selfdrive/ui/sunnypilot/qt/onroad/hud.h"
#define ExperimentalButton ExperimentalButtonSP
#else
#include "selfdrive/ui/qt/onroad/buttons.h"
#include "selfdrive/ui/qt/onroad/hud.h"
#endif

class AnnotatedCameraWidget : public CameraWidget {
  Q_OBJECT

public:
  explicit AnnotatedCameraWidget(VisionStreamType type, QWidget *parent = 0);
  virtual ~AnnotatedCameraWidget() = default;
  virtual void updateState(const UIState &s);

private:
  QVBoxLayout *main_layout;
  ExperimentalButton *experimental_btn;
  DriverMonitorRenderer dmon;
  HudRenderer hud;
  ModelRenderer model;
  std::unique_ptr<PubMaster> pm;

  int skip_frame_count = 0;
  bool wide_cam_requested = false;

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

  // Helper methods
  void drawLeftTurnSignal(QPainter &painter, int x, int y, int circle_size, int state);
  void drawRightTurnSignal(QPainter &painter, int x, int y, int circle_size, int state);
  int blinkerPulse(int frame);
  void drawStandstillTimer(QPainter &p, int x, int y);
  void drawColoredText(QPainter &p, int x, int y, const QString &text, QColor color);

  // Hybrid Drive Data
  bool hevDataAvailable;
  float hevThrottleDemandPercent;
  float hevThrottleThresholdPercent;
  QString hevPowerFlowMode;
  QString hevEngineOnReason;

  // Hybrid Battery Data
  bool hevBattDataAvailable;
  float hevBattAmpsActual;
  float hevBattVoltActual;
  float hevBattVoltLowLimit;
  float hevBattVoltHighLimit;
  float hevBattSocActual;
  float hevBattSocMinPerc;
  float hevBattSocMaxPerc;

protected:
  void paintGL() override;
  void initializeGL() override;
  void showEvent(QShowEvent *event) override;
  mat4 calcFrameMatrix() override;

  double prev_draw_t = 0;
  FirstOrderFilter fps_filter;
};
