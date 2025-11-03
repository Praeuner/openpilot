# Backend Server Migration Guide

## Migration from web_routes_server to bp_backend_server

**Status**: ✅ Phase 1 Complete
**Date**: November 2024

### Overview

The BluePilot backend server has been renamed from `web_routes_server` to `bp_backend_server` to reflect its expanding scope beyond just route management.

### What Changed

#### Process Name
- **Old**: `bp_web_routes_server`
- **New**: `bp_backend_server`

The process manager configuration in `system/manager/process_config.py` has been updated.

#### Module Structure
- **Old**: Single monolithic file (`web_routes_server.py` ~6000 lines)
- **New**: Modular structure with focused modules

```
bluepilot/backend/
├── bp_backend_server.py      # New entry point
├── config.py                  # Configuration constants
├── web_routes_server.py       # Legacy (preserved for reference)
├── routes/                    # Route processing
│   ├── processing.py          # Route analysis and GPS metrics
│   └── preprocessor.py        # Background preprocessing
├── realtime/                  # Real-time communications
│   └── websocket.py           # WebSocket broadcaster
├── core/                      # Core functionality
├── utils/                     # Utilities
├── video/                     # Video processing (placeholder)
└── handlers/                  # HTTP endpoint handlers
```

### Compatibility

✅ **Fully backwards compatible** - No API changes
✅ **No configuration changes** - Uses same params and ports
✅ **No data migration needed** - Uses same cache directories
✅ **Process manager handles transition** - Automatic on next update

### Testing

Run the test suite to verify the migration:

```bash
cd /data/openpilot  # or your openpilot directory
python3 bluepilot/backend/test_modules_only.py
```

Expected output:
```
Testing new backend modules...
============================================================
✓ config.py
✓ core/logging_handler.py
✓ utils/file_ops.py
✓ utils/power.py
✓ Package __init__.py files
============================================================
Tests passed: 5
Tests failed: 0

All new modules working correctly! ✓
```

### For Developers

#### Importing the Server

**Old way:**
```python
from bluepilot.backend.web_routes_server import main
```

**New way:**
```python
from bluepilot.backend.bp_backend_server import main
```

Note: For now, `bp_backend_server` delegates to `web_routes_server` internally, so both still work.

#### Adding New Features

New features should be added to the modular structure:

1. **New HTTP endpoints**: Add to `handlers/` directory
2. **New utilities**: Add to `core/`, `utils/`, or `video/`
3. **Configuration**: Add to `config.py`

See [README.md](README.md) for detailed guidelines.

### Migration Phases

#### Phase 1: Renaming & Structure ✅ COMPLETE

- [x] Create `bp_backend_server.py` entry point
- [x] Update `process_config.py`
- [x] Create modular directory structure
- [x] Extract initial modules (config, logging, file_ops, power)
- [x] Create comprehensive documentation
- [x] Verify all imports work

#### Phase 2: Module Organization ✅ COMPLETE

- [x] Organize route files into `routes/` directory
  - [x] `routes/processing.py` - Route analysis and GPS metrics
  - [x] `routes/preprocessor.py` - Background preprocessing
- [x] Organize realtime files into `realtime/` directory
  - [x] `realtime/websocket.py` - WebSocket broadcaster
- [x] Update all import paths
- [x] Update process_config.py paths
- [x] Remove old files

#### Phase 3: Further Modularization (PLANNED)

- [ ] Extract ServerState to `core/server_state.py`
- [ ] Extract FFmpeg handling to `video/ffmpeg.py`
- [ ] Extract remuxing to `video/remux.py`
- [ ] Extract prefetching to `video/prefetch.py`
- [ ] Extract route export to `video/export.py`
- [ ] Extract cache management to `utils/cache.py`
- [ ] Extract disk utilities to `utils/disk.py`
- [ ] Extract network utilities to `utils/network.py`

#### Phase 4: Handler Separation (PLANNED)

- [ ] Create `handlers/routes.py` for route endpoints
- [ ] Create `handlers/video.py` for video streaming
- [ ] Create `handlers/cache.py` for cache management
- [ ] Create `handlers/system.py` for system metrics
- [ ] Create `handlers/timeline.py` for statistics

#### Phase 5: Deprecation (FUTURE)

- [ ] Remove dependency on `web_routes_server.py`
- [ ] Mark `web_routes_server.py` as deprecated
- [ ] Eventually remove after stable operation

### Rollback Plan

If issues arise, rollback is simple:

1. Edit `system/manager/process_config.py`:
   ```python
   # Revert to old process name
   PythonProcess("bp_web_routes_server", "bluepilot.backend.web_routes_server", web_server_enabled),
   ```

2. Restart the manager or reboot

The old `web_routes_server.py` is fully preserved and functional.

### Process Manager Integration

The process is managed by openpilot's process manager with these settings:

- **Process Name**: `bp_backend_server`
- **Module**: `bluepilot.backend.bp_backend_server`
- **Enabled**: When `BPWebServerEnabled` param is true
- **Type**: PythonProcess (daemon)
- **Auto-restart**: Yes (managed by process manager)

### FAQ

**Q: Will this break my existing setup?**
A: No, it's fully backwards compatible. The new entry point delegates to the existing implementation.

**Q: Do I need to reconfigure anything?**
A: No, all params, ports, and settings remain the same.

**Q: What about my cached data?**
A: All cache directories remain the same. No data migration needed.

**Q: When will the modularization be complete?**
A: It's being done incrementally to minimize risk. Phase 2 is planned but not scheduled yet.

**Q: Can I still reference web_routes_server.py?**
A: Yes, it's preserved for reference and is still functional. The new entry point imports from it.

### Related Files

- [README.md](README.md) - Backend server documentation
- [config.py](config.py) - Configuration constants
- [bp_backend_server.py](bp_backend_server.py) - New entry point
- [web_routes_server.py](web_routes_server.py) - Legacy implementation
- [test_modules_only.py](test_modules_only.py) - Test suite for new modules

### Support

For issues or questions:
- Check the [README.md](README.md)
- Review process manager logs
- Test with `test_modules_only.py`
- File an issue on GitHub
