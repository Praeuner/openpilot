# BluePilot Web Routes Panel

Modern web-based interface for viewing and playing route videos directly in your browser, eliminating the Qt video playback issues.

**Dependencies:** `websockets` (Python package for real-time updates, with HTTP polling fallback)

**Note:** For read-only environments, install dependencies before deployment: `uv sync --extra dev`

## Quick Start

### 1. Enable the Web Server

In the BluePilot UI, go to the Routes panel and tap "Start Server". The server will:
- Start automatically when enabled
- Stop automatically when you start driving (safety feature)
- Restart when you stop driving

### 2. Access the Web Interface

**On your phone/tablet:**
1. Connect to the same WiFi network as your Comma device
2. Open the URL shown in the Routes panel (e.g., `http://192.168.1.100:8088`)
3. Bookmark it for easy access

**Or use the QR code** (coming soon) to quickly open on mobile devices.

## Features

### Route Browsing
- View all your routes organized by date
- See duration, size, and segment count
- Star your favorite routes
- Search and filter routes
- Delete routes you don't need

### Video Playback
- Native HEVC playback in modern browsers (Safari recommended)
- Low Quality (LQ) H.264 fallback for maximum compatibility
- Switch between cameras (Front, Wide, Driver, LQ)
- Automatic segment transitions
- Scrub through timeline
- Keyboard shortcuts (Space, Arrow keys, F for fullscreen)
- Touch-friendly controls for mobile

### Safety
- Server automatically stops when driving starts
- No performance impact on driving
- Lightweight and efficient

## Browser Compatibility

| Browser | HEVC Support | LQ (H.264) | Status |
|---------|--------------|------------|--------|
| Safari (iOS/macOS) | Native | ✅ | ✅ Full support |
| Chrome (Android) | Varies | ✅ | ⚠️ HEVC device-dependent |
| Edge (Windows) | Varies | ✅ | ⚠️ HEVC device-dependent |
| Firefox | Limited | ✅ | ⚠️ Use LQ camera |

**Recommended:** Safari on iPhone/iPad for best HEVC experience, or use LQ camera for universal H.264 compatibility.

## Architecture

```
bluepilot/
├── backend/
│   └── web_routes_server.py   # Python stdlib HTTP server (zero dependencies)
├── web/
│   ├── src/                    # Source files
│   │   ├── index.html
│   │   ├── styles.css
│   │   └── app.js
│   └── public/                 # Deployed files (committed)
└── test_web_routes.py          # Local testing script
```

**Key Design:**
- Python stdlib only (`http.server`, no Flask/external deps)
- Byte-range request support for video seeking
- Automatic route filtering and metadata parsing
- Qt panel integration for server control

## API Endpoints

### Routes
- `GET /api/routes` - List all routes
- `GET /api/routes/:baseName` - Get route details
- `GET /api/thumbnail/:baseName` - Get route thumbnail
- `POST /api/star/:baseName` - Toggle star status
- `DELETE /api/delete/:baseName` - Delete route

### Video Streaming
- `GET /api/video/:baseName/:segment/:camera` - Stream video segment
  - Supports byte-range requests for seeking
  - Camera types: `front`, `wide`, `driver`, `lq`

### Server Status
- `GET /api/status` - Check server health

## Development

### Testing Locally

No dependencies to install - uses Python standard library only:

```bash
cd /data/openpilot  # or your openpilot directory
python3 bluepilot/test_web_routes.py
```

Then open http://localhost:8088 in your browser.

### Building Web App

```bash
cd bluepilot/web
./build.sh
```

This copies files from `src/` to `public/` (which is committed to the repo).

### Modifying the UI

1. Edit files in `bluepilot/web/src/`
2. Run `./build.sh` to update `public/`
3. Refresh browser to see changes
4. Commit both `src/` and `public/` directories

## Configuration

Parameters (set via Comma device UI or params):

- `BPWebServerEnabled` (bool) - Enable/disable server
- `BPWebServerPort` (int) - Server port (default: 8088)

## Troubleshooting

### Server won't start
- Check that port 8088 is not in use: `lsof -i :8088`
- Check process manager logs: `journalctl -u manager -f | grep bp_web`
- Verify BPWebServerEnabled param is true

### Can't access from phone
- Ensure phone and Comma device are on same WiFi network
- Check firewall settings
- Try accessing from Comma device first: `curl http://localhost:8088/api/status`

### Video won't play
- Try switching to LQ camera (uses H.264 instead of HEVC)
- Check browser HEVC support (Safari on iOS recommended)
- Verify video files exist in `/data/media/0/realdata/`
- Check browser console for errors (F12)

### Routes showing "boot" or other invalid directories
- This should be filtered automatically by the backend
- Check backend is running latest version
- Verify routes directory permissions

### Server uses too much memory
- Server is very lightweight (stdlib only, no framework overhead)
- Check active connections: `netstat -an | grep 8088`
- Restart server via UI toggle

## Security Notes

- Server only accessible on local network (not exposed to internet)
- No authentication by default (coming soon as optional feature)
- Onroad safety check prevents use while driving
- Read-only access to route files (except delete)

## Future Enhancements

- [x] Zero-dependency Python stdlib implementation
- [x] LQ camera H.264 fallback for compatibility
- [x] Thumbnail support from cache
- [x] Route filtering and proper date/time formatting
- [ ] Thumbnail generation if cache is empty
- [ ] HLS/DASH playlist support
- [ ] Progressive Web App (PWA) installation
- [ ] Remote access via Comma Connect
- [ ] Optional authentication
- [ ] Drive analytics and statistics
- [ ] Export routes to external storage

## Credits

Built with:
- Python standard library (`http.server`)
- Vanilla JavaScript (zero dependencies)
- Modern CSS (BluePilot dark theme)

Developed for BluePilot (Ford-specific OpenPilot fork)
