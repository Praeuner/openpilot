/**
 * BluePilot UI Scene Extension
 *
 * This file extends SunnyPilot's UISceneSP to add BluePilot-specific UI state.
 * It preserves the developer UI functionality while adding BluePilot overlays.
 */

#pragma once

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui_scene.h"
#else
#include "selfdrive/ui/ui.h"
#endif

typedef struct UISceneBP :
#ifdef SUNNYPILOT
  UISceneSP
#else
  UIScene
#endif
{
  // BluePilot-specific UI state fields

  // Hybrid Drive Data Overlay
  bool show_hybrid_drive_overlay = false;
  int hybrid_drive_gauge_size = 0;
  int radar_overlay_size = 0;
  bool show_hybrid_battery_overlay = false;
  bool show_animated_wheel_angle = false;
  bool show_bp_radar_overlay = false;
  bool stand_still_timer = false;
  bool show_blindspot_indicators = false;
  bool show_stop_indicator_overlay = false;
  bool show_gforce_meter = false;
  bool sidebar_visible = false;

  // Display and brightness state
  int onroad_display_brightness = 0;
  float display_brightness_auto = 0.0f;
  bool display_brightness_manual_enabled = false;
  int display_brightness_manual = 0;
  bool display_brightness_auto_enabled = false;

} UISceneBP;

// Redefine UIScene to use BluePilot version when BLUEPILOT is defined
#ifdef BLUEPILOT
#define UIScene UISceneBP
#endif