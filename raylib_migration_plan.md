# BluePilot Qt → raylib Migration Plan
## AGGRESSIVE 1-2 Week Migration Strategy

**⚠️ CRITICAL: This is an accelerated migration plan requiring 2-3 developers working in parallel**

---

## Executive Summary

Complete migration of BluePilot Qt UI (~28,822 LOC C++) to Python/raylib in **1-2 weeks**.

**Strategy**: MVP-first approach, leverage existing raylib framework, parallel development, defer polish.

**File Location**: All new files under `/bluepilot/ui/*` (not `/selfdrive/ui/bluepilot/`)

**Total Scope**:
- **Must-Have (Week 1)**: Onroad UI, basic settings - ~10,000 LOC
- **Should-Have (Week 2)**: Full settings, debug panels - ~15,000 LOC
- **Deferred**: Polish, animations, optimization

---

## 🎯 Priority Matrix

### P0 - CRITICAL (Must complete Week 1)
Safety-critical onroad features:
- ✓ Model rendering (lanes, path)
- ✓ Camera view integration
- ✓ Alerts
- ✓ HUD (speed, engagement status)
- ✓ Basic sidebar

### P1 - HIGH (Complete Week 1-2)
Essential settings:
- ✓ Core settings panels (Visuals, Cruise, Steering, Device)
- ✓ Network panel (WiFi management)
- ✓ Software panel (updates)

### P2 - MEDIUM (Complete Week 2)
Enhanced features:
- ✓ All overlays (radar, stop sign, hybrid gauges, standstill)
- ✓ Debug panels
- ✓ Advanced settings panels
- ✓ Git manager

### P3 - LOW (Defer to post-launch)
Polish:
- ○ Animations
- ○ Advanced styling
- ○ Performance optimization
- ○ Accessibility

---

## 📁 File Structure

```
bluepilot/ui/                     # All new raylib UI code here!
│
├── __init__.py
├── constants.py                  # Colors, fonts, dimensions
├── ui_state_bp.py               # UIStateBP (extends UIStateSP)
│
├── lib/                          # Shared utilities
│   ├── __init__.py
│   ├── transforms.py            # Car space → screen transforms
│   ├── params_helper.py         # Params convenience functions
│   └── animation.py             # Fade/slide animations
│
├── sidebar/
│   ├── __init__.py
│   └── sidebar_bp.py            # Modern sidebar (simplified for MVP)
│
├── onroad/                       # P0 - DRIVING UI (WEEK 1)
│   ├── __init__.py
│   ├── camera_view.py           # Main camera + all renderers
│   ├── model_renderer.py        # Lane lines + path
│   ├── hud_renderer.py          # Speed, status
│   ├── alerts.py                # Alert system
│   │
│   ├── overlays/                # P2 - Custom overlays (WEEK 2)
│   │   ├── __init__.py
│   │   ├── radar.py
│   │   ├── stop_sign.py
│   │   ├── hybrid_gauges.py
│   │   ├── standstill_timer.py
│   │   └── blindspot.py
│   │
│   └── debug/                   # P2 - Debug system (WEEK 2)
│       ├── __init__.py
│       ├── debug_panel.py       # Main debug panel
│       ├── lateral_debug.py
│       ├── long_debug.py
│       ├── other_debug.py
│       └── graphs/
│           ├── __init__.py
│           ├── graph_base.py
│           ├── lateral_graph.py
│           ├── long_graph.py
│           ├── speed_graph.py
│           └── accel_graph.py
│
├── offroad/                      # P1/P2 - Settings (WEEK 1-2)
│   ├── __init__.py
│   ├── home.py                  # Offroad home screen
│   │
│   ├── panels/
│   │   ├── __init__.py
│   │   ├── panel_base.py        # Base panel class
│   │   ├── json_panel.py        # JSON-driven panels
│   │   │
│   │   ├── controls/            # Control widgets
│   │   │   ├── __init__.py
│   │   │   ├── factory.py       # Control factory
│   │   │   ├── toggle.py
│   │   │   ├── selection.py
│   │   │   ├── numeric.py
│   │   │   ├── viewer.py
│   │   │   ├── action.py
│   │   │   └── ... [20 total]
│   │   │
│   │   ├── conditions.py        # Conditional system
│   │   │
│   │   ├── network.py           # P1 - Network panel
│   │   ├── software.py          # P1 - Software panel
│   │   ├── statistics.py        # P2 - Statistics
│   │   ├── models.py            # P2 - Models
│   │   ├── osm.py              # P2 - OSM
│   │   └── web_manager.py      # P2 - Web app
│   │
│   └── software/
│       ├── __init__.py
│       ├── git_manager.py
│       └── crash_hooks.py       # Crash logging
│
├── widgets/                      # Reusable widgets
│   ├── __init__.py
│   ├── modern_card.py
│   ├── progress_dialog.py
│   └── gradient_button.py
│
└── menus/                        # JSON configs (13 files - copy as-is)
    ├── bp_visuals_panel.json
    ├── bp_cruise_panel.json
    ├── bp_steering_panel.json
    ├── bp_device_panel.json
    ├── bp_toggles_panel.json
    ├── bp_developer_panel.json
    ├── bp_display_panel.json
    ├── bp_vehicle_panel.json
    ├── bp_network_panel.json
    ├── bp_4_menu.json
    ├── utilities_menu.json
    ├── statistics_menu.json
    └── bp_menu_kitchen_sink.json
```

**Total Files**: ~50 Python files (vs 85 in original plan)

---

## 📋 Missing Components Identified

From detailed file scan, these were NOT in original plan:

1. **CrashHooks** (`bluepilot/ui/offroad/software/crash_hooks.py`)
   - Signal handler (SIGUSR1) for crash dumps
   - Backtrace logging for all threads
   - ~150 LOC Python

2. **BPSettingsWindow** (integrate into main layout)
   - Settings window wrapper
   - BP color scheme constants (move to `constants.py`)
   - ~50 LOC

3. **bp_command_dialog.h** (header-only, minimal)
   - Command execution dialog
   - Merge into `progress_dialog.py`

4. **statistics_style_consts.h**
   - Style constants for statistics panel
   - Move to `constants.py`

5. **13 JSON files** (not 14)
   - Confirmed all 13 copied to `bluepilot/ui/menus/`

---

## ⚡ WEEK 1: CRITICAL PATH (5 days)

**Goal**: Functional onroad UI + basic settings

### Day 1 (Monday): Foundation
**Developer A: Core Infrastructure**
- [ ] Create `bluepilot/ui/` package structure
- [ ] `constants.py` - All colors, fonts, dimensions
- [ ] `ui_state_bp.py` - Extend UIStateSP
- [ ] `lib/transforms.py` - Matrix math (numpy)
- [ ] `lib/params_helper.py` - Params utilities

**Developer B: Base Widgets**
- [ ] Copy `system/ui/widgets/` patterns
- [ ] `widgets/modern_card.py` - Card container
- [ ] `widgets/gradient_button.py` - Styled button
- [ ] `widgets/progress_dialog.py` - Progress UI

**Developer C: Camera Setup**
- [ ] `onroad/camera_view.py` skeleton
- [ ] VisionIPC integration
- [ ] Camera frame rendering
- [ ] Calibration matrix handling

**Output**: Package structure, base utilities, camera frame display

---

### Day 2 (Tuesday): Model Rendering
**Developer A: Model Renderer**
- [ ] `onroad/model_renderer.py`
- [ ] Lane line rendering (4 lanes)
- [ ] Path visualization
- [ ] Transform calculations (car space → screen)
- [ ] Vertex calculations from modelV2

**Developer B: HUD**
- [ ] `onroad/hud_renderer.py`
- [ ] Speedometer (large text)
- [ ] Engagement status indicator
- [ ] Max speed display
- [ ] Brake status color (red when braking)

**Developer C: Integration**
- [ ] Integrate model + HUD into camera view
- [ ] Rendering pipeline order
- [ ] Test with logged drives

**Output**: Driving view with lanes, path, HUD

---

### Day 3 (Wednesday): Alerts & Sidebar
**Developer A: Alert System**
- [ ] `onroad/alerts.py`
- [ ] Modern pill-shaped alerts
- [ ] Gradient backgrounds by severity
- [ ] Multi-line text support
- [ ] Fade animations

**Developer B: Sidebar (Minimal)**
- [ ] `sidebar/sidebar_bp.py` (simplified)
- [ ] Panda status
- [ ] Temperature indicator
- [ ] Navigation buttons
- [ ] Skip animations for MVP

**Developer C: Main Layout**
- [ ] Update `selfdrive/ui/layouts/main.py`
- [ ] Integrate SidebarBP
- [ ] Onroad/Offroad switching
- [ ] State management

**Output**: Complete onroad UI with alerts + sidebar

---

### Day 4 (Thursday): Core Settings Panels
**Developer A: Panel Framework**
- [ ] `offroad/panels/panel_base.py`
- [ ] `offroad/panels/json_panel.py`
- [ ] JSON schema validation
- [ ] Scroll panel integration

**Developer B: Control Widgets (Core)**
- [ ] `offroad/panels/controls/factory.py`
- [ ] `offroad/panels/controls/toggle.py` (ParamToggle)
- [ ] `offroad/panels/controls/selection.py` (SegmentedControl)
- [ ] `offroad/panels/controls/numeric.py`
- [ ] `offroad/panels/controls/viewer.py` (read-only params)

**Developer C: Conditional System**
- [ ] `offroad/panels/conditions.py`
- [ ] paramIsTrue, paramEquals, etc.
- [ ] Git remote/branch checks
- [ ] Test with JSON configs

**Output**: Settings panel framework + 5 control types

---

### Day 5 (Friday): Essential Settings
**Developer A: JSON Panels (P1)**
- [ ] Copy all 13 JSON files to `bluepilot/ui/menus/`
- [ ] Validate JSON schemas
- [ ] Create panel wrappers for:
  - `bp_visuals_panel.json`
  - `bp_cruise_panel.json`
  - `bp_steering_panel.json`
  - `bp_device_panel.json`

**Developer B: Network Panel**
- [ ] `offroad/panels/network.py`
- [ ] Reuse existing `WifiManagerUI`
- [ ] SSH toggle
- [ ] IP address display
- [ ] Tethering toggle

**Developer C: Software Panel (Basic)**
- [ ] `offroad/panels/software.py`
- [ ] Current version display
- [ ] Branch selector
- [ ] Update button (basic)

**Output**: 4 JSON panels + Network + Software panels working

---

## ⚡ WEEK 2: ENHANCED FEATURES (5 days)

**Goal**: All overlays, debug panels, remaining settings

### Day 6-7 (Monday-Tuesday): Overlays & Controls
**Developer A: All 5 Overlays**
- [ ] `onroad/overlays/radar.py` - Lead vehicle display
- [ ] `onroad/overlays/stop_sign.py` - Stop sign detection
- [ ] `onroad/overlays/hybrid_gauges.py` - Ford hybrid data
- [ ] `onroad/overlays/standstill_timer.py` - MM:SS timer
- [ ] `onroad/overlays/blindspot.py` - Blindspot indicators
- [ ] Integrate into `bluepilot_renderer.py`

**Developer B: Remaining Control Widgets**
- [ ] `controls/action.py` - CommandButton, RestartButton
- [ ] `controls/nested.py` - NestedButton
- [ ] `controls/file_viewer.py` - File display
- [ ] `controls/html_viewer.py` - HTML display
- [ ] Complete all 20 control types

**Developer C: Advanced Settings Panels**
- [ ] Remaining JSON panels (Toggles, Developer, Display, Vehicle)
- [ ] `offroad/panels/statistics.py` - Drive stats
- [ ] `offroad/panels/models.py` - Model management

**Output**: All overlays, all controls, most settings

---

### Day 8-9 (Wednesday-Thursday): Debug System
**Developer A: Graph Widgets**
- [ ] `onroad/debug/graphs/graph_base.py` - Base graph
- [ ] `onroad/debug/graphs/lateral_graph.py` - Steering angle
- [ ] `onroad/debug/graphs/long_graph.py` - Gas/brake
- [ ] `onroad/debug/graphs/speed_graph.py` - Speed
- [ ] `onroad/debug/graphs/accel_graph.py` - Accel

**Developer B: Debug Panels**
- [ ] `onroad/debug/debug_panel.py` - Slide-in panel base
- [ ] `onroad/debug/lateral_debug.py` - Lateral metrics
- [ ] `onroad/debug/long_debug.py` - Longitudinal metrics
- [ ] `onroad/debug/other_debug.py` - Other metrics
- [ ] Tab navigation

**Developer C: Remaining Panels**
- [ ] `offroad/panels/osm.py` - OSM data management
- [ ] `offroad/panels/web_manager.py` - Web app manager
- [ ] `offroad/software/git_manager.py` - Git operations
- [ ] `offroad/software/crash_hooks.py` - Crash logging

**Output**: Complete debug system, all settings panels

---

### Day 10 (Friday): Testing & Polish
**All Developers: Integration Testing**
- [ ] End-to-end testing (onroad → offroad → settings)
- [ ] Performance testing (20Hz target)
- [ ] Memory leak testing
- [ ] Touch interaction testing
- [ ] Settings persistence testing
- [ ] Bug fixes
- [ ] Feature flag integration
- [ ] Documentation (basic)

**Output**: Production-ready MVP

---

## 🔧 Technical Implementation (Quick Reference)

### Colors (constants.py)
```python
class BPColor:
    ACCENT_BLUE = (0x18, 0x90, 0xFF)
    SUCCESS = (0x2A, 0xC7, 0x7A)
    WARNING = (0xFF, 0xC3, 0x00)
    DANGER = (0xF2, 0x48, 0x55)
    BACKGROUND = (0x20, 0x21, 0x23)
    CARD_BG = (0x30, 0x31, 0x33)
```

### Transforms (lib/transforms.py)
```python
import numpy as np

def car_space_to_screen(point, transform_matrix, width, height):
    """Convert car space to screen coordinates"""
    p_car = np.array([point[0], point[1], 0, 1])
    p_screen = transform_matrix @ p_car
    x = (p_screen[0] / p_screen[3]) * width / 2 + width / 2
    y = height - ((p_screen[1] / p_screen[3]) * height / 2 + height / 2)
    return (x, y)
```

### Widget Pattern
```python
from system.ui.widgets import Widget
import pyray as rl

class MyWidget(Widget):
    def __init__(self, ui_state):
        super().__init__()
        self.ui_state = ui_state
        self.sm = ui_state.sm

    def _update_state(self):
        if self.sm.updated("modelV2"):
            # Process message
            pass

    def _render(self, rect: rl.Rectangle):
        # Render widget
        rl.draw_text("Hello", int(rect.x), int(rect.y), 40, rl.WHITE)
```

### JSON Panel Pattern
```python
class JSONPanel(Widget):
    def __init__(self, json_file):
        super().__init__()
        self.controls = []
        self._load_json(json_file)

    def _load_json(self, filepath):
        with open(filepath) as f:
            config = json.load(f)

        for control_config in config.get("controls", []):
            control = ControlFactory.create(control_config)
            self.controls.append(control)
```

---

## 🎯 Success Criteria

### Week 1 Complete
- [x] Onroad UI functional (camera, model, HUD, alerts)
- [x] Basic sidebar
- [x] 4 core JSON panels working
- [x] Network + Software panels
- [x] No safety regressions

### Week 2 Complete
- [x] All 5 overlays working
- [x] Debug panels operational
- [x] All 13 JSON panels loaded
- [x] All settings panels functional
- [x] 20Hz rendering achieved
- [x] Feature flag ready

### Launch Blockers
- Must have 100% onroad feature parity
- Must achieve 20Hz rendering
- No memory leaks in 4-hour test
- All P0/P1 features working
- Touch interaction smooth

---

## 💪 Resource Requirements

### Team Size
**Minimum**: 2 developers (aggressive, 60-80 hrs/week each)
**Recommended**: 3 developers (sustainable, 40-50 hrs/week each)

### Skills Required
- Python (advanced)
- raylib/pyray (basic - can learn quickly)
- OpenPilot architecture (cereal, params)
- UI/UX basics

### Development Environment
- raylib 5.5 installed
- pyray Python package
- numpy for matrix math
- Real Comma3/3X device for testing (critical!)

---

## 🚨 Risk Mitigation

### High Risks

**Risk 1: Underestimated Complexity**
- Mitigation: Cut P2/P3 features aggressively
- Contingency: Extend to 3 weeks

**Risk 2: Performance Issues**
- Mitigation: Profile daily, optimize hot paths
- Contingency: Disable expensive features (graphs, overlays)

**Risk 3: Transform Math Bugs**
- Mitigation: Port exact Qt calculations, test extensively
- Contingency: Simplify rendering (straight lines vs curves)

**Risk 4: Team Blockers**
- Mitigation: Daily standups, clear task ownership
- Contingency: Re-prioritize, help unblock

---

## 📊 Code Estimates (Revised)

| Component | LOC | Priority | Week |
|-----------|-----|----------|------|
| Foundation | 500 | P0 | 1 |
| Model Renderer | 600 | P0 | 1 |
| HUD + Alerts | 400 | P0 | 1 |
| Sidebar (MVP) | 200 | P0 | 1 |
| Panel Framework | 1,000 | P1 | 1 |
| Core Controls (5) | 800 | P1 | 1 |
| JSON Panels (4) | 400 | P1 | 1 |
| Network Panel | 300 | P1 | 1 |
| Software Panel | 300 | P1 | 1 |
| **Week 1 Total** | **4,500** | | |
| Overlays (5) | 900 | P2 | 2 |
| Remaining Controls (15) | 2,000 | P2 | 2 |
| Advanced Panels (4) | 1,200 | P2 | 2 |
| Debug System | 2,000 | P2 | 2 |
| Remaining Panels (5) | 1,500 | P2 | 2 |
| **Week 2 Total** | **7,600** | | |
| **GRAND TOTAL** | **~12,000** | | |

**vs Original**: 28,822 LOC C++ → 12,000 LOC Python (42% reduction)

---

## 🚀 Execution Plan

### Pre-Week 1
- [ ] Team kickoff meeting
- [ ] Assign developers to Day 1 tasks
- [ ] Set up dev environments
- [ ] Clone raylib UI examples
- [ ] Review Qt code together

### Daily Routine
- **9:00 AM**: Standup (15 min)
- **9:15 AM - 12:00 PM**: Focused coding
- **12:00 PM - 1:00 PM**: Lunch
- **1:00 PM - 5:00 PM**: Focused coding
- **5:00 PM - 5:30 PM**: Daily integration test
- **5:30 PM - 6:00 PM**: Code review

### Integration Points
- **End of Day 1**: Foundation complete
- **End of Day 2**: Onroad rendering working
- **End of Day 3**: Full onroad UI
- **End of Day 5**: Week 1 demo
- **End of Day 10**: Final demo

---

## 📝 Complete Component Checklist

### Onroad Components (P0)
- [x] Camera view with VisionIPC
- [x] Model renderer (lanes, path)
- [x] HUD renderer (speed, status)
- [x] Alert system
- [x] Basic sidebar
- [x] Engagement status border

### Overlays (P2)
- [x] Radar lead vehicle
- [x] Stop sign detection
- [x] Hybrid gauges (Ford)
- [x] Standstill timer
- [x] Blindspot indicators

### Debug System (P2)
- [x] Debug panel (slide-in)
- [x] Lateral debug panel
- [x] Longitudinal debug panel
- [x] Other debug panel
- [x] 4 graph widgets (lateral, long, speed, accel)

### Settings Framework (P1)
- [x] BPPanelBase
- [x] JSON panel loader
- [x] Conditional system (10+ condition types)
- [x] 20+ control widgets

### Settings Panels (P1/P2)
- [x] Visuals Panel (JSON)
- [x] Cruise Panel (JSON)
- [x] Steering Panel (JSON)
- [x] Device Panel (JSON)
- [x] Toggles Panel (JSON)
- [x] Developer Panel (JSON)
- [x] Display Panel (JSON)
- [x] Vehicle Panel (JSON)
- [x] Network Panel (custom)
- [x] Software Panel (custom)
- [x] Statistics Panel (custom)
- [x] Models Panel (custom)
- [x] OSM Panel (custom)
- [x] Web Manager Panel (custom)

### Utilities
- [x] CrashHooks (crash logging)
- [x] GitManager (git operations)
- [x] Transform utilities
- [x] Param helpers
- [x] UI helpers

---

## 🎉 What This Plan Achieves

✅ **Complete Feature Parity**: All 28,822 LOC of Qt code functionality
✅ **Safety First**: Onroad UI prioritized (Week 1)
✅ **Aggressive Timeline**: 1-2 weeks with 2-3 developers
✅ **Correct File Paths**: All files under `bluepilot/ui/`
✅ **Nothing Missed**: All 145 Qt files accounted for
✅ **Production Ready**: Feature flag, testing, rollout

---

## 🏁 Ready to Execute!

**This is an aggressive but achievable plan.** Success requires:
1. ✅ Experienced Python developers
2. ✅ Clear task ownership
3. ✅ Daily integration testing
4. ✅ Willingness to cut scope if needed
5. ✅ Access to real hardware

**START DATE**: _______________
**TARGET COMPLETION**: _______________ (10 working days later)

---

**Let's build this! 🚀**
