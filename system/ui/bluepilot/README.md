# BluePilot Raylib UI

This directory contains the BluePilot UI components ported from Qt to Raylib (pyray).

## Overview

OpenPilot is migrating from Qt to Raylib for the device UI. This port maintains BluePilot/SunnyPilot-specific UI customizations while using the new Raylib rendering framework.

## Directory Structure

```
bluepilot/ui/
├── lib/              # Utilities and constants
│   ├── colors.py     # Color definitions matching Qt sidebar.h
│   ├── constants.py  # Layout constants, dimensions, carrier mappings
│   └── ui_state_bp.py # BluePilot UI state extensions
├── widgets/          # Reusable UI components
│   ├── sidebar.py    # BluePilot sidebar with metric cards
│   ├── metric_card.py # Individual metric display cards
│   ├── network_card.py # Cellular/WiFi network status card
│   ├── icon_button.py # Icon buttons and fan animation widget
│   └── badge_widget.py # Version badge widgets for header
├── layouts/          # Main layout components
│   └── main_bp.py    # Main BluePilot layout (replaces stock MainLayout)
├── settings/         # Settings panels
│   ├── __init__.py
│   ├── settings_bp.py # BluePilot settings window with modern styling
│   └── panels/       # JSON-driven panel widgets
│       ├── __init__.py
│       └── bp_base_panel.py # JSON panel renderer for dynamic settings
├── offroad/          # Offroad-specific screens
│   ├── home_bp.py    # BluePilot offroad home screen
│   ├── drive_stats.py # Drive statistics widget (ALL TIME / PAST WEEK)
│   └── model_info.py # Current driving model display widget
└── onroad/           # Onroad-specific screens (future)
    └── __init__.py
```

## Qt to Raylib Porting Notes

### Source Reference Hierarchy

When porting Qt widgets, check for overrides in this order:
1. **openpilot** (`selfdrive/ui/qt/`) - Base implementation
2. **sunnypilot** (`selfdrive/ui/sunnypilot/qt/`) - SunnyPilot overrides
3. **bluepilot** (`selfdrive/ui/bluepilot/qt/`) - BluePilot-specific overrides

### Key Qt Source Files

| Raylib File | Qt Source |
|-------------|-----------|
| `widgets/sidebar.py` | `selfdrive/ui/sunnypilot/qt/sidebar.h/cc` |
| `offroad/drive_stats.py` | `selfdrive/ui/sunnypilot/qt/widgets/drive_stats.h/cc` |
| `offroad/model_info.py` | `selfdrive/ui/sunnypilot/qt/widgets/model_info.h/cc` |
| `offroad/home_bp.py` | `selfdrive/ui/sunnypilot/qt/offroad/offroad_home.h/cc` |
| `widgets/badge_widget.py` | `selfdrive/ui/bluepilot/qt/offroad/offroad_home_bp.h/cc` |
| `settings/settings_bp.py` | `selfdrive/ui/bluepilot/qt/offroad/settings.h/cc` |
| `settings/panels/bp_base_panel.py` | `selfdrive/ui/bluepilot/qt/offroad/panels/bp_base_view.h/cc` |

### Color Matching

Colors are defined in `lib/colors.py` and match Qt's `sidebar.h`:

```python
BACKGROUND = rl.Color(32, 33, 35, 255)   # background_color
ACCENT = rl.Color(24, 144, 255, 255)     # accent_color (BluePilot blue)
GOOD = rl.Color(42, 199, 122, 255)       # good_color (green)
WARNING = rl.Color(255, 195, 0, 255)     # warning_color (yellow)
DANGER = rl.Color(242, 72, 85, 255)      # danger_color (red)
PROGRESS = rl.Color(3, 132, 252, 255)    # progress_color (blue)
```

### Font Size Reference

From Qt implementations:

| Widget | Element | Qt Size | Notes |
|--------|---------|---------|-------|
| DriveStats | Title | 48px | Semi-bold, white |
| DriveStats | Number | 66px | Bold, cyan #18b4ff |
| DriveStats | Unit | 42px | Normal, gray #b0b0b0 |
| ModelInfo | Title | 48px | Semi-bold, white |
| ModelInfo | Model Name | 38px | Medium, cyan #18b4ff, scales to fit |
| Badge | Text | 32px | DemiBold, white |

### Layout Dimensions

From Qt `offroad_home.cc`:
- Widget spacing: 30px
- DriveStats base height: 550px (for 1.0 font scale)
- ModelInfo height: ~260px
- Badge height: 58px

## Widget Base Class

All widgets inherit from `openpilot.system.ui.widgets.Widget` which provides:
- `render(rect)` - Main entry point, calls `_render(rect)`
- `_render(rect)` - Override this to implement rendering
- `_rect` - Current render rectangle

## Key Differences from Qt

1. **No QSS Stylesheets**: Styling is done directly in Python with raylib draw calls
2. **Manual Layout**: No Qt layout managers, positions calculated manually
3. **Font Rendering**: Uses `gui_app.font(FontWeight.X)` and `measure_text_cached()`
4. **Input Handling**: Uses `rl.is_mouse_button_pressed()` and `rl.check_collision_point_rec()`

## Testing

Run the UI locally:
```bash
source .venv/bin/activate
python selfdrive/ui/ui.py
```

Note: D-Bus errors on macOS are expected and can be ignored.

## JSON-Based Settings Panels

The settings system uses JSON configuration files to dynamically generate UI panels. This mirrors the Qt `BPBaseView` system.

### JSON Panel Files

Located at `selfdrive/ui/bluepilot/menus/`:

- `bp_device_panel.json` - Device settings
- `bp_toggles_panel.json` - Toggle switches
- `bp_steering_panel.json` - Steering/lateral control settings
- `bp_cruise_panel.json` - Cruise/longitudinal control settings
- `bp_visuals_panel.json` - Visual display settings
- `bp_display_panel.json` - Display preferences
- `bp_vehicle_panel.json` - Vehicle-specific settings
- `bp_developer_panel.json` - Developer options

### JSON Structure

```json
{
  "type": "base",
  "menuName": "Panel Name",
  "groups": [
    {
      "groupName": "groupId",
      "title": "Group Title",
      "controls": [
        {
          "type": "toggle",
          "param": "ParamName",
          "title": "Control Title",
          "desc": "Description text"
        }
      ]
    }
  ]
}
```

### Supported Control Types

| Type | Description |
|------|-------------|
| `toggle` | Boolean on/off switch |
| `segmented_control` | Multi-option button group |
| `selection` | Dropdown-style selection |
| `integer` | Integer input with +/- buttons |
| `float` | Float input with +/- buttons |

### Settings Panel Types

| Panel | Description |
|-------|-------------|
| `DEVICE` | Device configuration |
| `NETWORK` | WiFi/cellular settings |
| `TOGGLES` | General toggles |
| `STEERING` | MADS, lane change, torque control |
| `CRUISE` | Speed limit, DEC, ICBM settings |
| `VISUALS` | UI visual preferences |
| `DISPLAY` | Display settings |
| `SOFTWARE` | Software updates |
| `VEHICLE` | Vehicle-specific options |
| `FIREHOSE` | Firehose settings |
| `DEVELOPER` | Developer tools |

## Future Work

- [ ] Onroad UI components
- [ ] Additional SunnyPilot widgets (community features, etc.)
- [x] Settings panels (completed)
