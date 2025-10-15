#pragma once

#include <QVBoxLayout>
#include <memory>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/qt/onroad/annotated_camera.h"
#include "selfdrive/ui/bluepilot/qt/onroad/buttons_bp.h"
#include "selfdrive/ui/sunnypilot/qt/onroad/hud.h"
#define AnnotatedCameraWidgetBase AnnotatedCameraWidgetSP
#define ExperimentalButton ExperimentalButtonBP
#define HudRenderer HudRendererSP
#else
#include "selfdrive/ui/qt/onroad/annotated_camera.h"
#include "selfdrive/ui/qt/onroad/buttons.h"
#include "selfdrive/ui/qt/onroad/hud.h"
#define AnnotatedCameraWidgetBase AnnotatedCameraWidget
#endif

#include "selfdrive/ui/qt/onroad/driver_monitoring.h"
#include "selfdrive/ui/bluepilot/qt/onroad/model_bp.h"
#include "selfdrive/ui/bluepilot/qt/onroad/bluepilot_renderer.h"
#include "selfdrive/ui/qt/widgets/cameraview.h"

// BluePilot enhanced annotated camera widget with visual enhancements
class AnnotatedCameraWidgetBP : public AnnotatedCameraWidgetBase {
  Q_OBJECT

public:
  explicit AnnotatedCameraWidgetBP(VisionStreamType type, QWidget* parent = nullptr);
  virtual ~AnnotatedCameraWidgetBP() = default;
  void updateState(const UIState &s) override;

private:
  QVBoxLayout *main_layout;
  ExperimentalButton *experimental_btn;
  DriverMonitorRenderer dmon;
  HudRenderer hud;
  ModelRendererBP model_bp;  // Use BluePilot enhanced model renderer
  BluepilotRenderer bluepilot_renderer;
  std::unique_ptr<PubMaster> pm;

  int skip_frame_count = 0;
  bool wide_cam_requested = false;

protected:
  void paintGL() override;
  void initializeGL() override;
  void showEvent(QShowEvent *event) override;
  mat4 calcFrameMatrix() override;

  double prev_draw_t = 0;
  FirstOrderFilter fps_filter;

private slots:
  void updateModelData();
  void updateBluepilotOverlays();

private:
  // BluePilot-specific state
  bool bp_visual_enhancements_enabled = true;
  bool bp_glow_effects_enabled = true;
  float bp_glow_intensity = 1.0f;

  // Performance monitoring
  int frame_count = 0;
  double last_performance_check = 0.0;
  double avg_frame_time = 0.0;

  void updatePerformanceMetrics(double current_time);
  bool shouldReduceVisualEffects() const;
};

// Factory function to create the appropriate widget type
#ifdef BLUEPILOT_UI_ENABLED
using AnnotatedCameraWidgetFinal = AnnotatedCameraWidgetBP;
#else
using AnnotatedCameraWidgetFinal = AnnotatedCameraWidgetBase;
#endif
