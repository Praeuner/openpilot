#!/usr/bin/env python3
"""
BluePilot Backend Configuration
Centralized configuration for the backend server
"""

import os
import shutil
import logging
from pathlib import Path

logger = logging.getLogger(__name__)

# Directories
ROUTES_DIR = "/data/media/0/realdata" if os.path.exists("/data/media/0/realdata") else os.path.expanduser("~/comma_data/media/0/realdata")
WEBAPP_DIR = Path(__file__).parent.parent / "web" / "public"

# Cache directories
DATA_ROOT = "/data/bluepilot/routes" if os.path.exists("/data") else os.path.expanduser("~/comma_data/bluepilot/routes")
THUMBNAIL_CACHE = os.path.join(DATA_ROOT, "thumbs_cache")
REMUX_CACHE = os.path.join(DATA_ROOT, "remux_cache")
METRICS_CACHE = os.path.join(DATA_ROOT, "metrics_cache")
ROUTE_EXPORT_CACHE = os.path.join(DATA_ROOT, "exports")
VIDEOS_ZIP_CACHE = os.path.join(DATA_ROOT, "videos_zip")
BACKUP_CACHE = os.path.join(DATA_ROOT, "backups")
IMPORT_TEMP_DIR = os.path.join(DATA_ROOT, "import_temp")

# Camera configuration
CAMERA_FILES = {
    'front': 'fcamera.hevc',
    'wide': 'ecamera.hevc',
    'driver': 'dcamera.hevc',
    'lq': 'qcamera.ts'
}
HEVC_CAMERAS = {'front', 'wide', 'driver'}
CAMERA_LABELS = {
    'front': 'front',
    'wide': 'wide',
    'driver': 'driver',
    'lq': 'interior'
}

# WebSocket configuration
WEBSOCKET_HOST = '0.0.0.0'
WEBSOCKET_PORT = 8089  # Different port from HTTP server

# Power management
IDLE_TIMEOUT_SECONDS = 300  # 5 minutes of no remuxing = idle

# FFmpeg settings
MAX_CONCURRENT_FFMPEG = 3  # Max concurrent FFmpeg processes
FFMPEG_RESERVED_FOR_PLAYBACK = 1  # Always keep 1 slot free for playback

# Server configuration
DEFAULT_PORT = 8088
RATE_LIMIT_WINDOW_SECONDS = 60
RATE_LIMIT_MAX_REQUESTS = 100
MAX_REQUESTS_PER_MINUTE_ONROAD = 20  # Stricter limit when driving
MAX_REQUESTS_PER_MINUTE_OFFROAD = RATE_LIMIT_MAX_REQUESTS  # Normal limit when parked

# Cellular access configuration
CELLULAR_ACCESS_TIMEOUT_DEFAULT = 60  # 1 hour default timeout in minutes


# FFmpeg binary detection
def find_ffmpeg_binary():
    """Find ffmpeg binary with fallback paths for different systems"""
    # Try standard PATH lookup first
    ffmpeg_path = shutil.which('ffmpeg')
    if ffmpeg_path:
        logger.info(f"Found ffmpeg in PATH: {ffmpeg_path}")
        return ffmpeg_path

    # Try common installation paths for Comma devices and Ubuntu
    fallback_paths = [
        '/usr/bin/ffmpeg',
        '/usr/local/bin/ffmpeg',
        '/data/openpilot/third_party/ffmpeg',  # Comma device custom install
        '/opt/homebrew/bin/ffmpeg',  # macOS
    ]

    for path in fallback_paths:
        if os.path.exists(path) and os.access(path, os.X_OK):
            logger.info(f"Found ffmpeg at fallback path: {path}")
            return path

    logger.error("FFmpeg binary not found! Video playback will not work.")
    logger.error("Install with: apt-get install ffmpeg")
    return None


def find_ffprobe_binary():
    """Find ffprobe binary with fallback paths"""
    ffprobe_path = shutil.which('ffprobe')
    if ffprobe_path:
        logger.info(f"Found ffprobe in PATH: {ffprobe_path}")
        return ffprobe_path

    # Try common paths
    common_paths = ['/usr/bin/ffprobe', '/usr/local/bin/ffprobe']
    for path in common_paths:
        if os.path.exists(path):
            logger.info(f"Found ffprobe at: {path}")
            return path

    logger.warning("ffprobe not found - HLS playlists will use estimated durations")
    return None


# Initialize FFmpeg binaries
FFMPEG_BINARY = find_ffmpeg_binary()
FFPROBE_BINARY = find_ffprobe_binary()
