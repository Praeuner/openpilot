# BluePilot → Raylib Migration Plan

> Objective: recreate the complete BluePilot (BP) Qt UI, widgets, overlays, panels, and supporting utilities under the Raylib UI stack while preserving behavior, styling, and developer tooling.

---

## 1. Goals, Non-Goals, and Assumptions
- **Goals**
  - Mirror every UI surface currently defined under `selfdrive/ui/bluepilot/qt` (home, sidebar, settings panels, onroad renderer, overlays, developer panels, menus, dialogs).
  - Provide a structured Python/Raylib package that can replace the Qt UI at runtime via a feature toggle without regressing upstream SunnyPilot/OpenPilot UI behavior.
  - Maintain functional parity for all BP-specific behaviors: BluePilot overlays, `bp_controls` interactions, `bp_panels` features, statistics dashboards, updater/network utilities, brightness management, and debug tooling.
  - Keep telemetry, params writes, and CAN/messaging subscriptions identical so higher-level processes remain unaware of the UI rewrite.
  - Deliver a regression test suite (unit + rendering smoke tests) and documentation for QA/manual validation passes.
- **Non-Goals**
  - No simultaneous redesign of UI visuals; only Raylib-specific adjustments (font metrics, pixel snapping) allowed.
  - No behavioral changes to upstream non-BP Raylib layouts, unless required for abstraction sharing.
  - No elimination of legacy Qt assets until Raylib replacement reaches full feature parity.
- **Assumptions**
  - Pyray bindings and `system/ui/lib/application.py` already operate on-device with sufficient performance.
  - All BP Params keys, log streams, and menus remain available; Raylib port consumes them as-is.
  - Assets referenced by Qt (PNGs, SVGs) can be converted to textures accessible to Raylib without licensing conflicts.

---

## 2. Source Inventory (Qt Implementations to Port)
- **Top-Level Directories**
  - `selfdrive/ui/bluepilot/bp_logging.{cc,h}`, `performance_logger.h`, `concurrent_tracker.h`, `ui_scene_bp.h`, scripts (`scripts/symbolize_core.sh`).
  - `selfdrive/ui/bluepilot/menus/*.json` (`bp_*.json`, `statistics_menu.json`, `utilities_menu.json`).
  - `selfdrive/ui/bluepilot/qt/*` (home/sidebar/offroad/onroad/widgets/developer/overlays).
- **Onroad Renderer (`selfdrive/ui/bluepilot/qt/onroad/`)**
  - Core surfaces: `bluepilot_renderer.*`, `hud_bp.*`, `buttons_bp.*`, `model_bp.*`, `alerts_bp.*`, `annotated_camera_bp.h`.
  - Debug panels: `lateral_debug_panel.*`, `long_debug_panel.*`, `onroad_controls_debug_panel.*`, `other_debug_panel.*`.
  - Developer UI: `developer_ui/*` (ui_elements, layout containers).
  - Overlays: `overlays/*` (gforce, hybrid_gauges, radar, standstill_timer, stop_sign).
  - Widgets: `widgets/debug/*` (AccelGraphWidget, LateralGraphWidget, LongControlGraphWidget, SpeedGraphWidget).
- **Offroad/Home (`selfdrive/ui/bluepilot/qt/offroad/`)**
  - Screens: `offroad_home_bp.*`, `settings.*`, `home.*`, `sidebar.*`, Crash hooks (`CrashHooks.*`), notifications.
  - Panels (`offroad/panels/*`): nav bar, nested views, models panel, network panel, OSM panel, panel actions/conditions/controls/dialogs/utils, statistics/recent changes, updater/web manager/software manager.
  - Software utilities (`offroad/software/*`): git manager, command dialogs, updater glue.
- **Menus/Data (`selfdrive/ui/bluepilot/menus`)**
  - JSON definitions for toggles/panels (device/network/display/steering/visuals/vehicle, etc.), developer menus, statistics/utilities.
- **Logging/Utilities**
  - `bp_logging` macros, `performance_logger`, `concurrent_tracker` usage across Qt code.
  - BluePilot-specific Params reads/writes, env var toggles (BP_DEBUG*, overlay toggles, brightness overrides).

---

## 3. Target Raylib Package Layout
```
bluepilot/ui/raylib/
  __init__.py
  app.py                       # entrypoint + Raylib MainLayout wiring
  assets/__init__.py           # texture/font manifest + conversion helpers
  bp_logging.py                # python port of bp_logging macros
  bp_state.py                  # UIState extension mirroring UISceneBP
  data/menu_loader.py          # JSON schema + renderer factory for menus
  widgets/                     # shared building blocks (buttons, toggles, sliders, dialogs)
  layouts/
    main.py                    # BluePilotMainLayout orchestrator
    sidebar.py                 # BP sidebar clone (flag button, status metrics)
    home/                      # offroad home cards, crash hooks, recent changes
    settings/                  # panel stack + dynamic menu rendering
  onroad/
    view.py                    # camera compositing + overlay orchestration
    hud.py / buttons.py / alerts.py / model.py
    overlays/                  # gforce, radar, hybrid gauges, stop timer, etc.
    panels/                    # debug panels & developer UI
    widgets/                   # graph widgets, developer controls
```
- Add `tests/` (unit + rendering), `docs/bluepilot_raylib_port.md`, and SCons integration to package assets.
- Provide a feature flag (Param `BluePilotRaylibUI` or similar) to toggle between upstream Raylib layouts and the BP package through `selfdrive/ui/ui.py` and `system/manager/process_config.py`.

---

## 4. Migration Phases and Detailed Tasks

### Phase 0 – Discovery & Tooling
1. **Code Mapping:** Document every Qt class, their responsibilities, and dependencies (signals/slots, shared state, params) in a spreadsheet referencing files listed above.
2. **Asset Audit:** Catalog images/fonts referenced in Qt resources (buttons, icons, overlays) and confirm source files exist; plan conversions to PNG/atlas for Raylib.
3. **Menu/Panel Schema Review:** Parse `menus/*.json` and `offroad/panels/*.cc` to extract field types, conditional logic, actions (scripts, params writes, sockets). Define schema invariants for Raylib loader.
4. **UI State Requirements:** Extract all BP-specific variables from `uiscene_bp.h`, onroad widgets, and offroad panels (brightness, overlay toggles, data caches). Note message topics and Params keys.
5. **Performance Baselines:** Record FPS, CPU, GPU usage for Qt UI to set acceptance thresholds for Raylib (target equal or better).

### Phase 1 – Foundational Infrastructure
1. **Entry/Process Selection**
   - Update `system/manager/process_config.py` to allow `raylib_ui` to launch either upstream `MainLayout` or the BP Raylib port based on Param/env var.
   - Provide CLI override for developers (e.g., `UI=bp_raylib`).
2. **Application Shell (`app.py`)**
   - Create `BluePilotGuiApp` that initializes `gui_app`, sets window title, loads fonts/textures, and registers pre-render callbacks for brightness/timeouts.
   - Mirror existing `selfdrive/ui/ui.py` loop but instantiate `BluePilotMainLayout`.
3. **BP State Layer (`bp_state.py`)**
   - Subclass or wrap `UIState` to add BP fields (hybrid overlay flags, brightness manual/auto state, overlay-specific data).
   - Subscribe to additional `cereal` messages referenced by Qt components (g-force inputs, radar, nav data, statistics) and compute derived values (unit conversions, smoothing filters).
   - Provide helper methods for toggles (e.g., `bp_state.show_sidebar`, `bp_state.brake_percent`).
4. **Logging & Profiling**
   - Port `bp_logging` to Python with env-controlled log levels (`BP_DEBUG`, `BP_DEBUG_VIDEO`, etc.) and integrate with `cloudlog`.
   - Implement `performance_logger` hooks to measure render timing (Raylib frame debug overlays) for parity tracking.
5. **Concurrency/Task Helpers**
   - Convert `concurrent_tracker.h` patterns into Python helpers (async tasks or threading) for data fetchers (e.g., menu actions hitting network).
   - Ensure thread-safe interaction with Params via `openpilot.common.params`.

### Phase 2 – Shared Widgets + Layout Primitives
1. **Widget Toolkit**
   - Extend `openpilot.system.ui.widgets` with BP-specific components: card containers, metric tiles, graph canvas, toggle rows, slider rows, list/table views, dialog frameworks, developer buttons.
   - Ensure multi-touch support, scrollable areas, tooltips, context menus.
2. **Theming & Assets**
   - Implement `assets` module that preloads textures (home/flag/settings buttons, overlay backgrounds) using `gui_app.texture`.
   - Provide color palette + typography constants replicating Qt styling (font weights, sizes) and adapt to Raylib text metrics.
3. **Layout Management**
   - Add layout helper classes (FlexBox, Grid, Stack) to emulate Qt layout constraints for consistent spacing.
   - Implement animation/tween utilities for overlay fades and sidebar transitions.

### Phase 3 – Onroad Core Rendering
1. **Camera & Scene**
   - Port `bluepilot_renderer` into `onroad/view.py`, using `AugmentedRoadView` for video feed but layering BP-specific surfaces (HUD, buttons, overlays, developer UI).
   - Manage z-order and dynamic layout rectangles (sidebar open/closed, debug panels).
2. **HUD Components**
   - `hud_bp`: speed, set speed, lane state, lead info, braking bar, lane departure, turn signals. Translate Qt drawing commands to Raylib primitives (`draw_rectangle_rounded`, `draw_text_ex`, `draw_texture_pro`).
   - `buttons_bp`: replicate tap targets (engage, dis/eng, menu toggles) with Raylib hit detection; integrate with `device.reset_interactive_timeout`.
   - `model_bp`: path render, car models, lane lines, predicted path shading via Raylib `DrawMesh`/`DrawLineStrip` equivalents or reuse existing `model_renderer`.
   - `alerts_bp`: port alert logic (priority stacking, icon handling, text) and ensure compatibility with `cereal` `alerts`.
3. **State Synchronization**
   - Feed all components from `bp_state` to avoid redundant `SubMaster` reads; ensure each widget caches derived values for minimal per-frame computation.
4. **Input Handling**
   - Translate Qt mouse/gesture handling (tap-to-toggle sidebar, long-press developer panel) into Raylib `MousePos` events via `Widget` base class.
5. **Performance Optimization**
   - Profile `BluePilotOnroadView` to maintain 20+ FPS, batching draw calls where possible and precomputing text measurement via `measure_text_cached`.

### Phase 4 – Overlays & Developer Panels
1. **Overlays (`onroad/overlays/`)**
   - `hybrid_gauges_overlay`: replicates gauge arcs, battery meter, charge/discharge animations.
   - `radar_overlay`: draws lead icons, relative speed lines, car identifiers.
   - `standstill_timer_overlay`: shows countdown for stop situations.
   - `stop_sign_overlay`: icon + text cues for detected stop signs.
   - `gforce_overlay`: dynamic dot with axes, history trail.
   - Ensure toggles map to Params (and developer menu) exactly as Qt.
2. **Developer Panels (`onroad/panels/`)**
   - `lateral_debug_panel`, `long_debug_panel`, `onroad_controls_debug_panel`, `other_debug_panel`: each becomes a Raylib pane with tables, toggles, text logs.
   - Maintain `bp_controls` interactions (param gating, command dispatch) accessible via these panels.
3. **Developer UI Toolkit**
   - Port `developer_ui` components: multi-column layout, nested tabs, developer shortcuts.
4. **Graph Widgets (`onroad/widgets/debug`)**
   - Implement Raylib graph renderers for `AccelGraph`, `LateralGraph`, `LongControlGraph`, `SpeedGraph`, matching data smoothing and colors.
   - Provide data feeders reading from `controlsState`, `carState`, `longitudinalPlan`, etc., with time-series buffers.

### Phase 5 – Sidebar & Global Chrome
1. **Sidebar Layout (`layouts/sidebar.py`)**
   - Recreate Qt sidebar (flag/home button, network dots, temperature/panda/connect metrics, settings shortcuts).
   - Manage show/hide animations, interactive timeouts, brightness icons, developer badges.
   - Expose callbacks for settings toggle, bookmark button, developer actions.
2. **Global Buttons/Status**
   - Implement top status bars (GPS strength, storage usage, update indicator) if present in Qt `home.cc`/`sidebar`.
   - Integrate `bp_logging` debug overlay toggles accessible via gestures.

### Phase 6 – Offroad Home Experience
1. **Home Screen (`layouts/home/`)**
   - Port `home.cc` components: device status cards, drive stats, photo/video tiles, disclaimers, update prompts.
   - Integrate CrashHooks (error logs) and route browser using Raylib scrollable lists.
2. **Offroad Landing (`offroad_home_bp.*`)**
   - Recreate onboarding/warnings, linking to settings panels or dialogs.
3. **Crash/Log Hooks**
   - Re-implement `CrashHooks` functionality (stack traces, log viewer) with Raylib dialog windows pulling from local files.
4. **Recent Changes / Release Notes**
   - Port `bp_recent_changes` view to Raylib list, referencing `BP_CHANGES.json`.
5. **Updater Integration**
   - Mirror `bp_updater_panel`, `bp_software_panel`, `bp_web_manager_panel`, `bp_git_manager`, `bp_command_dialog` behavior: run commands/scripts, stream output, display progress bars.
   - Ensure concurrency management for long-running scripts (threading + log streaming).

### Phase 7 – Settings Stack & Panels
1. **Panel Framework**
   - Translate `bp_panel_base`, `bp_nested_view`, `bp_nav_bar_view` concepts into Python classes managing navigation stacks, breadcrumbs, and panel switching animations.
   - Implement conditional visibility (`bp_panel_conditions`), shared actions (`bp_panel_actions`), and UI helpers (`bp_ui_helpers`).
2. **Panel Types**
   - Device/Network/Display panels: replicate toggle rows, slider adjustments, input dialogs, referencing `bp_device_panel.json`, `bp_network_panel.json`, `bp_display_panel.json`.
   - Vehicle/Steering/Visuals/Cruise panels: map toggles to Params or pub messages.
   - Statistics panel: show metrics using new Raylib chart widgets; integrate `statistics_menu.json`.
   - Models panel: show available ML models, allow downloads/switching.
   - OSM panel: display map layers, statuses.
   - Utilities/Developer panels: expose scripts (e.g., `symbolize_core.sh`), diag buttons.
3. **Dialogs**
   - Port `bp_panel_dialogs` (confirmations, warnings) into Raylib overlay windows with consistent button styling.
4. **Controls Integration**
   - Recreate `bp_panel_controls` so toggles/sliders share binding logic (enable/disable dependencies, apply button states).

### Phase 8 – Menu JSON Loader & `bp_controls`
1. **Schema Definition**
   - Define Python dataclasses representing each JSON element (Toggle, Slider, Dropdown, ActionGroup, Section, Condition).
   - Validate JSON files at startup; surface errors to developer console.
2. **Renderer Factory**
   - Build mapping from schema nodes to Raylib `Widget` instances, applying layout rules (two-column, metric cards, inline help).
3. **Action Binding**
   - Implement `bp_controls` service that executes actions (set Params, run scripts, send `PubMaster` messages) with permission prompts.
4. **Live Sync**
   - Add watchers so UI widgets reflect external Param changes and disable themselves when conditions fail.
5. **Customization Hooks**
   - Provide developer extension points to inject new menu definitions without editing core code (auto-discovery from `menus/`).

### Phase 9 – Assets, Fonts, and Localization
1. **Asset Conversion**
   - Extract Qt resources (PNGs/SVGs) and convert to optimized PNGs sized for Raylib; update `assets/manifest.json`.
   - Provide script to rebuild textures if assets change.
2. **Font Strategy**
   - Load required font weights (regular, semi-bold, mono) via `gui_app.font`; ensure glyph coverage for localization.
3. **Localization**
   - Identify any translated strings in Qt UI (via `translations/`); integrate with Raylib text pipeline (string resource file) and ensure layout handles varying lengths.

### Phase 10 – Testing, Tooling, and Validation
1. **Automated Tests**
   - Extend `selfdrive/ui/tests/test_raylib_ui.py` to instantiate `BluePilotMainLayout` with mocked `ui_state`.
   - Add unit tests for menu loader, panel actions, overlay toggles, graph data buffers.
   - Write golden-image or pixel-diff tests for HUD overlays using offscreen render targets where feasible.
2. **Manual QA Playbooks**
   - Document scenario-based validation (offroad boot, toggling overlays, developer panel use, network tests, updates) in `docs/bluepilot_raylib_port.md`.
3. **Performance Monitoring**
   - Add optional frame timing overlay triggered via developer menu to profile Raylib drawing cost per subsystem.
4. **Telemetry Hooks**
   - Emit stats (FPS, panel load time) to `selfdriveState` or `cloudlog` for regression tracking.

### Phase 11 – Rollout & Cleanup
1. **Feature Flag Rollout**
   - Default to Raylib BP UI when Param `BluePilotRaylibUI` is enabled; allow fallback to Qt for user opt-out.
2. **Release Checklist**
   - Validate on-road usage, ensure overlays behave, run test suite, confirm CPU/mem budgets.
3. **Documentation**
   - Update READMEs (`README_SP.md`, `docs/`) with instructions for enabling Raylib BP UI and contributing to new code.
4. **Deprecation Plan**
   - Once Raylib UI fully validated, mark Qt modules deprecated and schedule removal (documented timeline).

---

## 5. Risk Mitigation & Dependencies
- **Performance Risk:** Mitigate by iterative profiling, caching textures, and leveraging existing Raylib renderers (avoid Python loops per pixel).
- **Behavioral Parity Risk:** Maintain side-by-side screenshot comparisons and automated checks using recorded logs to replay identical frames.
- **Menu Schema Drift:** Convert JSON loader to fail-fast with clear errors; add unit tests per JSON file.
- **Concurrency/Threading Differences:** Wrap long operations (git pulls, downloads) in background tasks with thread-safe UI updates.
- **Asset Inconsistencies:** Store asset conversion script in repo to prevent manual drift; verify with hash checks at build time.

---

## 6. Deliverables Checklist
- [ ] `bluepilot/ui/raylib/` package fully implemented.
- [ ] New `BluePilotMainLayout`, `BluePilotOnroadView`, and associated widgets ported.
- [ ] Sidebar, settings menu, `bp_controls`, `bp_panels`, overlays, and developer panels implemented in Raylib with parity.
- [ ] Menu JSON loader + schema validation + renderer.
- [ ] BP logging/state utilities ported, Params sync verified.
- [ ] Asset pipeline + font loading script documented.
- [ ] Automated tests (unit + render) and manual QA guide.
- [ ] Feature flag + manager process gating + documentation updates.
- [ ] Deprecation note for Qt UI with migration timeline.

---

## 7. Next Steps
1. Approve architecture/layout described above.
2. Spin up implementation trackers per phase with owners and estimates.
3. Begin Phase 0 audits, followed by simultaneous Phase 1/2 development.
