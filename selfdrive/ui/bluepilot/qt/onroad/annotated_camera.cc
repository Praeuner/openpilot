/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/bluepilot/qt/onroad/annotated_camera.h"
#include "selfdrive/ui/qt/util.h"

AnnotatedCameraWidgetBP::AnnotatedCameraWidgetBP(VisionStreamType type, QWidget *parent)
    : AnnotatedCameraWidgetSP(type, parent) {
}

void AnnotatedCameraWidgetBP::updateState(const UIState &s) {
  // Call parent implementation
  AnnotatedCameraWidgetSP::updateState(s);

  // Update bluepilot-specific model renderer
  model_bp.updateState(s);

  // Update wide camera preference
  use_wide_camera_at_low_speed = s.scene.wide_camera_low_speed;
}

mat4 AnnotatedCameraWidgetBP::calcFrameMatrix() {
  // Use parent implementation but potentially modify for bluepilot-specific needs
  auto matrix = AnnotatedCameraWidgetSP::calcFrameMatrix();

  // Add any bluepilot-specific matrix transformations here if needed

  return matrix;
}

void AnnotatedCameraWidgetBP::paintGL() {
  UIState *s = uiState();
  SubMaster &sm = *(s->sm);
  const double start_draw_t = millis_since_boot();

  // draw camera frame
  {
    std::lock_guard lk(frame_lock);

    if (frames.empty()) {
      if (skip_frame_count > 0) {
        skip_frame_count--;
        qDebug() << "skipping frame, not ready";
        return;
      }
    } else {
      skip_frame_count = 5;
    }

    // Wide or narrow cam dependent on speed - with bluepilot modifications
    bool has_wide_cam = available_streams.count(VISION_STREAM_WIDE_ROAD);
    if (has_wide_cam) {
      float v_ego = sm["carState"].getCarState().getVEgo();

      // Bluepilot-specific logic for wide camera switching
      if (use_wide_camera_at_low_speed) {
        // Use wide camera at low speeds if enabled
        if ((v_ego < 12) || available_streams.size() == 1) {
          wide_cam_requested = true;
        } else if (v_ego > 18) {
          wide_cam_requested = false;
        }
      } else {
        // Default behavior
        if ((v_ego < 10) || available_streams.size() == 1) {
          wide_cam_requested = true;
        } else if (v_ego > 15) {
          wide_cam_requested = false;
        }
      }

      wide_cam_requested = wide_cam_requested && sm["selfdriveState"].getSelfdriveState().getExperimentalMode();
    }

    CameraWidget::setStreamType(wide_cam_requested ? VISION_STREAM_WIDE_ROAD : VISION_STREAM_ROAD);
    CameraWidget::setFrameId(sm["modelV2"].getModelV2().getFrameId());
    CameraWidget::paintGL();
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);

  // Use bluepilot model renderer instead of default
  model_bp.draw(painter, rect());

  // Draw other overlays (driver monitoring, HUD, etc.)
  dmon.draw(painter, rect());
  hud.updateState(*s);
  hud.draw(painter, rect());

  double cur_draw_t = millis_since_boot();
  double dt = cur_draw_t - prev_draw_t;
  double fps = fps_filter.update(1. / dt * 1000);
  if (fps < 15) {
    LOGW("slow frame rate: %.2f fps", fps);
  }
  prev_draw_t = cur_draw_t;

  // publish debug msg
  MessageBuilder msg;
  auto m = msg.initEvent().initUiDebug();
  m.setDrawTimeMillis(cur_draw_t - start_draw_t);
  pm->send("uiDebug", msg);
}