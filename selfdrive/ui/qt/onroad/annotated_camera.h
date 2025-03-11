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
