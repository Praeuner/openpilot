#!/usr/bin/env python3
"""
BluePilot Web Routes Server
Lightweight HTTP server using only stdlib (no external dependencies)
Complete rewrite matching old Qt panel behavior
"""

import os
import json
import mimetypes
import subprocess
import sys
import tempfile
import shutil
import time
import signal
import atexit
from pathlib import Path
from datetime import datetime
from functools import lru_cache
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse
import logging
import re
import asyncio
import threading
from collections import defaultdict

# Configure logging early for import error handling
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# WebSocket availability - checked dynamically to handle runtime installation
WEBSOCKETS_AVAILABLE = False


# Note: We're now using direct imports throughout the codebase instead of these helper functions

# Add parent directory to path for imports
import sys
sys.path.append(os.path.join(os.path.dirname(__file__), "../.."))

# Import shared route processing functions
from bluepilot.backend.route_processing import (
    haversine_distance,
    reverse_geocode,
    extract_gps_metrics_from_segment,
    get_route_gps_metrics,
    generate_thumbnail,
    check_processing_status,
    process_route,
    kill_existing_process,
)

# Import WebSocket broadcaster
from bluepilot.backend.websocket_broadcaster import WebSocketBroadcaster, WebSocketEvent

try:
    from common.params import Params
    params = Params()
except ImportError:
    # Mock for testing
    class Params:
        def __init__(self):
            self._params = {"IsOnRoad": False, "BPWebServerPort": "8088", "BPWebServerEnabled": True}
        def get_bool(self, key):
            return self._params.get(key, False)
        def get(self, key, encoding=None):
            value = self._params.get(key, "")
            if encoding and isinstance(value, str):
                return value.encode(encoding)
            return value if isinstance(value, str) else str(value)
        def put(self, key, value):
            self._params[key] = value
        def put_bool(self, key, value):
            self._params[key] = value
    params = Params()
except Exception as e:
    # Handle any other params initialization errors
    import logging
    logging.error(f"Failed to initialize params: {e}")
    class Params:
        def __init__(self):
            self._params = {"IsOnRoad": False, "BPWebServerPort": "8088", "BPWebServerEnabled": True}
        def get_bool(self, key):
            return self._params.get(key, False)
        def get(self, key, encoding=None):
            value = self._params.get(key, "")
            if encoding and isinstance(value, str):
                return value.encode(encoding)
            return value if isinstance(value, str) else str(value)
        def put(self, key, value):
            self._params[key] = value
        def put_bool(self, key, value):
            self._params[key] = value
    params = Params()

# Logging already configured above

# WebSocket configuration
WEBSOCKET_HOST = '0.0.0.0'
WEBSOCKET_PORT = 8089  # Different port from HTTP server

# Configuration
ROUTES_DIR = "/data/media/0/realdata" if os.path.exists("/data/media/0/realdata") else os.path.expanduser("~/comma_data/media/0/realdata")
WEBAPP_DIR = Path(__file__).parent.parent / "web" / "public"
THUMBNAIL_CACHE = "/data/bluepilot/routes/thumbs_cache" if os.path.exists("/data") else os.path.expanduser("~/comma_data/bluepilot/routes/thumbs_cache")
REMUX_CACHE = "/data/bluepilot/routes/remux_cache" if os.path.exists("/data") else os.path.expanduser("~/comma_data/bluepilot/routes/remux_cache")
METRICS_CACHE = "/data/bluepilot/routes/metrics_cache" if os.path.exists("/data") else os.path.expanduser("~/comma_data/bluepilot/routes/metrics_cache")

# WebSocket clients and event management
websocket_clients = set()
websocket_events = asyncio.Queue()
loop = None  # Global event loop for WebSocket operations
broadcaster = None  # Global broadcaster instance


def broadcast_websocket_event(event_type, data=None):
    """Broadcast event to all connected WebSocket clients (legacy wrapper)"""
    if broadcaster:
        broadcaster.broadcast(event_type, data)


async def websocket_handler(websocket):
    """Handle WebSocket connections"""
    # Import websockets directly for exception handling
    try:
        import websockets
    except ImportError:
        logger.error("websockets not available in handler")
        return

    try:
        # Add client to active connections
        websocket_clients.add(websocket)
        logger.info(f"WebSocket client connected. Total clients: {len(websocket_clients)}")

        # Send initial status (simplified to avoid potential issues)
        initial_status = {
            'type': 'connection_established',
            'timestamp': datetime.now().isoformat(),
            'data': {
                'status': 'online',
                'routes_count': 0  # Simplified for now
            }
        }
        await websocket.send(json.dumps(initial_status))

        # Keep connection alive with simple loop
        try:
            while True:
                await asyncio.sleep(10.0)  # Send heartbeat every 10 seconds
                heartbeat = {
                    'type': 'heartbeat',
                    'timestamp': datetime.now().isoformat(),
                    'data': {}
                }
                await websocket.send(json.dumps(heartbeat))
        except Exception as e:
            logger.debug(f"Connection closed: {e}")

    except Exception as e:
        logger.error(f"WebSocket error: {e}")
    finally:
        # Remove client from active connections
        websocket_clients.discard(websocket)
        logger.info(f"WebSocket client removed. Total clients: {len(websocket_clients)}")


async def start_websocket_server():
    """Start the WebSocket server"""
    try:
        # Import websockets directly for the serve function
        import websockets
    except ImportError:
        logger.warning("WebSocket server not started - websockets library not available")
        return

    try:
        logger.info(f"Starting WebSocket server on {WEBSOCKET_HOST}:{WEBSOCKET_PORT}")
        server = await websockets.serve(
            websocket_handler,
            WEBSOCKET_HOST,
            WEBSOCKET_PORT,
            ping_interval=30,
            ping_timeout=10,
            close_timeout=5
        )

        logger.info("WebSocket server started successfully")
        await server.wait_closed()

    except Exception as e:
        logger.error(f"Failed to start WebSocket server: {e}")
        raise


def start_websocket_server_thread():
    """Start WebSocket server in a separate thread"""
    try:
        # Import websockets to check availability
        import websockets
    except ImportError:
        logger.warning("WebSocket server thread not started - websockets library not available")
        return

    global loop
    try:
        # Create new event loop for this thread
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)

        # Start the WebSocket server
        loop.run_until_complete(start_websocket_server())
    except Exception as e:
        logger.error(f"WebSocket server thread error: {e}")
    finally:
        try:
            loop.close()
        except:
            pass


# Cache management settings
MAX_CACHE_SIZE_GB = 5  # Maximum cache size in GB (adjustable - 5GB ~= 70 segments)
CACHE_CLEANUP_THRESHOLD = 0.9  # Cleanup when 90% full
CACHE_EXPIRATION_HOURS = 1  # Remove cached files older than 1 hour (unless starred)

# Ensure cache directories exist
os.makedirs(THUMBNAIL_CACHE, exist_ok=True)
os.makedirs(REMUX_CACHE, exist_ok=True)
os.makedirs(METRICS_CACHE, exist_ok=True)


def get_cache_size():
    """Get total size of remux cache in bytes"""
    total = 0
    try:
        for entry in os.scandir(REMUX_CACHE):
            if entry.is_file():
                total += entry.stat().st_size
    except OSError:
        pass
    return total


def get_free_disk_space(path="/data"):
    """Get free disk space in bytes for the given path"""
    try:
        stat = shutil.disk_usage(path)
        return stat.free
    except Exception as e:
        logger.warning(f"Could not check free disk space: {e}")
        return None


def get_system_metrics():
    """Get comprehensive system metrics for monitoring

    Returns:
        dict: System metrics including CPU, memory, disk, temperature
    """
    metrics = {
        'timestamp': datetime.now().isoformat(),
        'cpu': {},
        'memory': {},
        'disk': {},
        'temperature': {},
        'ffmpeg': {},
    }

    # CPU Usage
    try:
        # Check if /proc/stat exists (Linux)
        if os.path.exists('/proc/stat'):
            with open('/proc/loadavg') as f:
                load = f.read().split()
                metrics['cpu']['load_1min'] = float(load[0])
                metrics['cpu']['load_5min'] = float(load[1])
                metrics['cpu']['load_15min'] = float(load[2])

            # Check CPU core status
            online_cores = []
            for cpu in range(8):  # Check up to 8 cores
                online_path = f'/sys/devices/system/cpu/cpu{cpu}/online'
                if os.path.exists(online_path):
                    with open(online_path) as f:
                        if f.read().strip() == '1':
                            online_cores.append(cpu)
                elif cpu == 0:  # CPU0 is always online
                    online_cores.append(cpu)

            metrics['cpu']['online_cores'] = online_cores
            metrics['cpu']['core_count'] = len(online_cores)
    except Exception as e:
        logger.debug(f"Error reading CPU metrics: {e}")

    # Memory Usage
    try:
        if os.path.exists('/proc/meminfo'):
            with open('/proc/meminfo') as f:
                meminfo = {}
                for line in f:
                    parts = line.split(':')
                    if len(parts) == 2:
                        key = parts[0].strip()
                        value = parts[1].strip().split()[0]  # Get number, ignore unit
                        meminfo[key] = int(value) * 1024  # Convert KB to bytes

            total = meminfo.get('MemTotal', 0)
            available = meminfo.get('MemAvailable', 0)
            used = total - available
            percent = (used / total * 100) if total > 0 else 0

            metrics['memory']['total_bytes'] = total
            metrics['memory']['used_bytes'] = used
            metrics['memory']['available_bytes'] = available
            metrics['memory']['percent_used'] = round(percent, 1)
            metrics['memory']['total_gb'] = round(total / 1024 / 1024 / 1024, 2)
            metrics['memory']['available_gb'] = round(available / 1024 / 1024 / 1024, 2)
    except Exception as e:
        logger.debug(f"Error reading memory metrics: {e}")

    # Disk Usage
    try:
        for path_name, path in [('/data', '/data'), ('home', os.path.expanduser('~'))]:
            if os.path.exists(path):
                stat = shutil.disk_usage(path)
                percent = (stat.used / stat.total * 100) if stat.total > 0 else 0
                metrics['disk'][path_name] = {
                    'total_bytes': stat.total,
                    'used_bytes': stat.used,
                    'free_bytes': stat.free,
                    'percent_used': round(percent, 1),
                    'total_gb': round(stat.total / 1024 / 1024 / 1024, 2),
                    'free_gb': round(stat.free / 1024 / 1024 / 1024, 2),
                }
    except Exception as e:
        logger.debug(f"Error reading disk metrics: {e}")

    # Temperature (if available)
    try:
        thermal_zones = [
            '/sys/class/thermal/thermal_zone0/temp',
            '/sys/class/thermal/thermal_zone1/temp',
        ]
        temps = []
        for zone_path in thermal_zones:
            if os.path.exists(zone_path):
                with open(zone_path) as f:
                    temp_millic = int(f.read().strip())
                    temp_c = temp_millic / 1000
                    temps.append(temp_c)

        if temps:
            metrics['temperature']['celsius'] = round(max(temps), 1)
            metrics['temperature']['fahrenheit'] = round(max(temps) * 9/5 + 32, 1)
    except Exception as e:
        logger.debug(f"Error reading temperature: {e}")

    # FFmpeg process info
    global active_ffmpeg_processes
    metrics['ffmpeg']['active_processes'] = active_ffmpeg_processes
    metrics['ffmpeg']['max_processes'] = MAX_CONCURRENT_FFMPEG

    # Cache sizes
    try:
        metrics['cache'] = {
            'remux_cache_bytes': get_cache_size(),
            'remux_cache_gb': round(get_cache_size() / 1024 / 1024 / 1024, 2),
        }
    except Exception as e:
        logger.debug(f"Error reading cache metrics: {e}")

    return metrics


def has_sufficient_disk_space(required_bytes, path="/data", min_free_gb=1):
    """Check if there's sufficient disk space for an operation

    Args:
        required_bytes: Estimated bytes needed for operation
        path: Path to check (usually /data)
        min_free_gb: Minimum GB to keep free after operation

    Returns:
        bool: True if sufficient space available
    """
    free_space = get_free_disk_space(path)
    if free_space is None:
        return True  # Can't check, assume OK

    min_free_bytes = min_free_gb * 1024 * 1024 * 1024
    required_total = required_bytes + min_free_bytes

    if free_space < required_total:
        free_gb = free_space / 1024 / 1024 / 1024
        required_gb = required_total / 1024 / 1024 / 1024
        logger.warning(f"Insufficient disk space: {free_gb:.2f}GB free, {required_gb:.2f}GB required")
        return False

    return True


def is_route_starred(route_base):
    """Check if a route is starred"""
    star_file = os.path.join(ROUTES_DIR, route_base, '.star')
    return os.path.exists(star_file)


def cleanup_old_cache():
    """Remove oldest cached files when cache is too large or expired

    Rules:
    1. Starred routes are never removed due to expiration
    2. Starred routes can be removed if cache size exceeds limit (LRU)
    3. Non-starred routes expire after 1 hour
    """
    import time

    max_bytes = MAX_CACHE_SIZE_GB * 1024 * 1024 * 1024
    current_size = get_cache_size()
    expiration_seconds = CACHE_EXPIRATION_HOURS * 60 * 60  # 1 hour = 3600 seconds
    current_time = time.time()

    # Get all cache files
    cache_files = []
    expired_files = []
    starred_routes = set()

    try:
        for entry in os.scandir(REMUX_CACHE):
            if entry.is_file() and entry.name.endswith('.mp4'):
                stat = entry.stat()

                # Extract route base name from cache filename
                # Format: routebase_segment_camera.mp4
                parts = entry.name.rsplit('_', 2)
                if len(parts) >= 2:
                    route_base = parts[0]

                    # Check if route is starred
                    is_starred = is_route_starred(route_base)
                    if is_starred:
                        starred_routes.add(route_base)

                    # Check if file is expired (only for non-starred routes)
                    file_age = current_time - stat.st_mtime
                    if file_age > expiration_seconds and not is_starred:
                        expired_files.append((entry.path, stat.st_size, route_base))
                    else:
                        cache_files.append((entry.path, stat.st_atime, stat.st_size, is_starred))
    except OSError as e:
        logger.error(f"Error scanning cache: {e}")
        return

    # Remove expired files first (excluding starred routes)
    if expired_files:
        logger.info(f"Removing {len(expired_files)} expired cache files (>{CACHE_EXPIRATION_HOURS}h old, non-starred)")
        for filepath, size, route_base in expired_files:
            try:
                os.remove(filepath)
                current_size -= size
                logger.info(f"Removed expired: {os.path.basename(filepath)}")
            except OSError as e:
                logger.error(f"Error removing expired file {filepath}: {e}")

    # Check if we still need size-based cleanup
    if current_size < max_bytes * CACHE_CLEANUP_THRESHOLD:
        logger.info(f"Cache within limits: {current_size / 1024 / 1024 / 1024:.2f}GB / {MAX_CACHE_SIZE_GB}GB")
        if starred_routes:
            logger.info(f"Protected starred routes: {', '.join(sorted(starred_routes))}")
        return

    logger.info(f"Cache size cleanup triggered: {current_size / 1024 / 1024 / 1024:.2f}GB / {MAX_CACHE_SIZE_GB}GB")

    # Sort files: non-starred first (by access time), then starred (by access time)
    # This ensures starred routes are only removed as a last resort
    non_starred = [(f, a, s) for f, a, s, starred in cache_files if not starred]
    starred = [(f, a, s) for f, a, s, starred in cache_files if starred]

    non_starred.sort(key=lambda x: x[1])  # Sort by access time (oldest first)
    starred.sort(key=lambda x: x[1])

    # Combine: remove non-starred first, starred only if necessary
    sorted_files = non_starred + starred

    # Remove oldest files until we're under 80% of max
    target_size = max_bytes * 0.8
    for filepath, _, size in sorted_files:
        if current_size <= target_size:
            break
        try:
            os.remove(filepath)
            current_size -= size
            is_from_starred = filepath in [f for f, _, _ in starred]
            prefix = "⭐ starred" if is_from_starred else "old"
            logger.info(f"Removed {prefix} cached file: {os.path.basename(filepath)}")
        except OSError as e:
            logger.error(f"Error removing cache file {filepath}: {e}")

    logger.info(f"Cache cleanup complete: {current_size / 1024 / 1024 / 1024:.2f}GB remaining")


# Track last activity time for power save restoration
last_activity_time = None
IDLE_TIMEOUT_SECONDS = 300  # 5 minutes of no remuxing = idle

# FFmpeg process tracking (prevent resource exhaustion)
active_ffmpeg_processes = 0
ffmpeg_lock = threading.Lock()
MAX_CONCURRENT_FFMPEG = 3  # Maximum concurrent FFmpeg processes

# Rate limiting (prevent abuse)
request_counter = defaultdict(list)
rate_limit_lock = threading.Lock()
MAX_REQUESTS_PER_MINUTE = 120  # 120 requests per minute per IP
RATE_LIMIT_WINDOW = 60  # 1 minute window


def check_rate_limit(client_ip):
    """Check if client has exceeded rate limit

    Args:
        client_ip: Client IP address

    Returns:
        tuple: (is_allowed: bool, retry_after: int)
    """
    current_time = time.monotonic()

    with rate_limit_lock:
        # Get request timestamps for this IP
        timestamps = request_counter[client_ip]

        # Remove old timestamps outside the window
        timestamps[:] = [t for t in timestamps if current_time - t < RATE_LIMIT_WINDOW]

        # Check if limit exceeded
        if len(timestamps) >= MAX_REQUESTS_PER_MINUTE:
            # Calculate retry_after based on oldest request
            retry_after = int(RATE_LIMIT_WINDOW - (current_time - timestamps[0])) + 1
            return False, retry_after

        # Add current request
        timestamps.append(current_time)
        return True, 0


def enable_performance_mode():
    """Enable all CPU cores for FFmpeg remuxing"""
    global last_activity_time
    import time

    last_activity_time = time.time()  # Update activity time

    try:
        # Enable CPU cores 4-7 (big cores on Snapdragon 845)
        for cpu in range(4, 8):
            online_path = f'/sys/devices/system/cpu/cpu{cpu}/online'
            if os.path.exists(online_path):
                with open(online_path, 'w') as f:
                    f.write('1')
                logger.info(f"Enabled CPU{cpu}")

        logger.info("Performance mode enabled for video remuxing")
    except (OSError, PermissionError) as e:
        logger.warning(f"Could not enable performance mode: {e}")
        logger.warning("This may slow down video remuxing. Run as root or with proper permissions.")


def restore_power_save():
    """Disable big cores to save power when idle"""
    try:
        # Disable CPU cores 4-7 (big cores)
        disabled_count = 0
        for cpu in range(4, 8):
            online_path = f'/sys/devices/system/cpu/cpu{cpu}/online'
            if os.path.exists(online_path):
                with open(online_path, 'w') as f:
                    f.write('0')
                disabled_count += 1

        if disabled_count > 0:
            logger.info(f"Power save restored - disabled {disabled_count} big cores")
    except (OSError, PermissionError) as e:
        logger.debug(f"Could not restore power save: {e}")


def check_and_restore_power_save():
    """Check if idle and restore power save mode"""
    global last_activity_time
    import time

    if last_activity_time is None:
        return

    idle_time = time.time() - last_activity_time
    if idle_time > IDLE_TIMEOUT_SECONDS:
        restore_power_save()
        last_activity_time = None  # Reset so we don't keep trying


def cleanup_on_shutdown():
    """Critical cleanup on server shutdown - ensure CPU cores are restored"""
    logger.info("Server shutting down - performing cleanup...")

    # Only restore CPU power save mode if we're still offroad
    # If going onroad, leave cores enabled for openpilot processes
    try:
        if not is_onroad():
            restore_power_save()
            logger.info("CPU cores restored to power save mode (device is offroad)")
        else:
            logger.info("Device going onroad - keeping CPU cores enabled for openpilot")
    except Exception:
        logger.exception("Error checking onroad status for power save")

    # Kill any remaining FFmpeg processes
    global active_ffmpeg_processes
    if active_ffmpeg_processes > 0:
        logger.warning(f"Killing {active_ffmpeg_processes} remaining FFmpeg processes")
        try:
            subprocess.run(['pkill', '-9', 'ffmpeg'], timeout=2)
        except Exception:
            logger.exception("Error killing FFmpeg processes")

    logger.info("Cleanup completed")


def signal_handler(signum, frame):
    """Handle termination signals gracefully"""
    signal_name = signal.Signals(signum).name
    logger.info(f"Received signal {signal_name} - initiating graceful shutdown")
    cleanup_on_shutdown()
    sys.exit(0)


def is_onroad():
    """Check if vehicle is currently driving"""
    try:
        return params.get_bool("IsOnRoad")
    except:
        return False

def should_server_run():
    """Check if server should be running (enabled and not onroad)"""
    try:
        enabled = params.get_bool("BPWebServerEnabled")
        onroad = is_onroad()
        return enabled and not onroad
    except:
        return True  # Default to running if we can't check


def get_route_base_name(route_name):
    """Extract base name from route path (remove --segment suffix)
    Example: 2024-09-18--14-30-00--5 -> 2024-09-18--14-30-00
    """
    # Remove segment number (last --N)
    match = re.match(r'(.+)--\d+$', route_name)
    if match:
        return match.group(1)
    return route_name


def get_segment_number(route_name):
    """Extract segment number from route name
    Example: 2024-09-18--14-30-00--5 -> 5
    """
    match = re.search(r'--(\d+)$', route_name)
    if match:
        try:
            return int(match.group(1))
        except:
            return 0
    return 0


def parse_route_datetime(route_base):
    """Parse route base name to extract datetime
    Example: 2024-09-18--14-30-00 -> datetime(2024, 9, 18, 14, 30, 0)
    Returns None for non-standard route names (e.g., dongle IDs)
    """
    try:
        # Split by --
        parts = route_base.split('--')
        if len(parts) >= 2:
            date_part = parts[0]  # 2024-09-18
            time_part = parts[1]  # 14-30-00

            # Check if date_part looks like a date (YYYY-MM-DD format)
            if len(date_part.split('-')) != 3:
                return None

            # Parse date
            year, month, day = map(int, date_part.split('-'))

            # Validate year is reasonable (not hex like 000000ad)
            if year < 2000 or year > 2100:
                return None

            # Parse time
            time_components = time_part.split('-')
            hour = int(time_components[0]) if len(time_components) > 0 else 0
            minute = int(time_components[1]) if len(time_components) > 1 else 0
            second = int(time_components[2]) if len(time_components) > 2 else 0

            return datetime(year, month, day, hour, minute, second)
    except (ValueError, TypeError):
        # Silently return None for non-standard route names
        return None

    return None


def format_time_12hr(dt):
    """Format datetime as 12-hour time with AM/PM
    Example: 14:30 -> 2:30 PM
    """
    return dt.strftime("%-I:%M %p" if os.name != 'nt' else "%#I:%M %p")


def format_display_date(dt):
    """Format date as: Thursday - September 17th, 2025"""
    day_name = dt.strftime("%A")
    month_name = dt.strftime("%B")
    day = dt.day
    year = dt.year

    # Add ordinal suffix
    if day % 10 == 1 and day != 11:
        suffix = "st"
    elif day % 10 == 2 and day != 12:
        suffix = "nd"
    elif day % 10 == 3 and day != 13:
        suffix = "rd"
    else:
        suffix = "th"

    return f"{day_name} - {month_name} {day}{suffix}, {year}"


def format_elapsed_time(dt):
    """Format elapsed time since route
    Example: 2 hours ago, 3 days ago, etc.
    """
    now = datetime.now()
    delta = now - dt

    seconds = delta.total_seconds()

    if seconds < 60:
        return "just now"
    elif seconds < 3600:
        minutes = int(seconds / 60)
        return f"{minutes} minute{'s' if minutes != 1 else ''} ago"
    elif seconds < 86400:
        hours = int(seconds / 3600)
        return f"{hours} hour{'s' if hours != 1 else ''} ago"
    elif seconds < 604800:  # 7 days
        days = int(seconds / 86400)
        return f"{days} day{'s' if days != 1 else ''} ago"
    else:
        weeks = int(seconds / 604800)
        return f"{weeks} week{'s' if weeks != 1 else ''} ago"


@lru_cache(maxsize=100)
def get_route_segments(route_base):
    """Get all segments for a route"""
    if not os.path.exists(ROUTES_DIR):
        return []

    segments = []
    pattern = re.compile(f"^{re.escape(route_base)}--\\d+$")

    for entry in os.listdir(ROUTES_DIR):
        if pattern.match(entry):
            entry_path = os.path.join(ROUTES_DIR, entry)
            if os.path.isdir(entry_path):
                seg_num = get_segment_number(entry)
                segments.append({
                    'path': entry_path,
                    'name': entry,
                    'segment': seg_num
                })

    return sorted(segments, key=lambda x: x['segment'])


# GPS and geocoding functions imported from route_processing module


def get_file_size(path):
    """Get size of file or directory"""
    if os.path.isfile(path):
        return os.path.getsize(path)

    total = 0
    try:
        for dirpath, dirnames, filenames in os.walk(path):
            for filename in filenames:
                filepath = os.path.join(dirpath, filename)
                if os.path.exists(filepath):
                    total += os.path.getsize(filepath)
    except:
        pass
    return total


def format_size(bytes_size):
    """Format bytes to human readable size"""
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if bytes_size < 1024.0:
            return f"{bytes_size:.1f} {unit}"
        bytes_size /= 1024.0
    return f"{bytes_size:.1f} PB"


def get_video_files(segment_path):
    """Get available video files in segment"""
    videos = {}
    video_types = {
        'fcamera.hevc': 'front',
        'ecamera.hevc': 'wide',
        'dcamera.hevc': 'driver',
        'qcamera.ts': 'lq'
    }

    for filename, camera_type in video_types.items():
        filepath = os.path.join(segment_path, filename)
        if os.path.exists(filepath):
            videos[camera_type] = {
                'filename': filename,
                'path': filepath,
                'size': os.path.getsize(filepath)
            }

    return videos


def scan_routes():
    """Scan routes directory and build route metadata

    Returns route list with cached GPS metrics only (fast).
    Does NOT process logs or geocode - that's handled by:
    - Background preprocessor during idle time
    - Individual API endpoints on-demand
    """
    if not os.path.exists(ROUTES_DIR):
        logger.warning(f"Routes directory not found: {ROUTES_DIR}")
        return []

    routes_dict = {}
    processed_bases = set()

    for entry in os.listdir(ROUTES_DIR):
        entry_path = os.path.join(ROUTES_DIR, entry)

        if not os.path.isdir(entry_path):
            continue

        # CRITICAL: Skip non-route directories
        # Routes MUST contain "--" and NOT be in exclusion list
        if entry in ('boot', 'crash') or '--' not in entry:
            continue

        base_name = get_route_base_name(entry)

        # Skip if we've already processed this base route
        if base_name in processed_bases:
            continue

        processed_bases.add(base_name)

        # Parse datetime from base name
        route_dt = parse_route_datetime(base_name)

        # If datetime parsing fails (e.g., dongle ID routes), use fallback
        if route_dt is None:
            # Use directory modification time as fallback (silently)
            try:
                mtime = os.path.getmtime(entry_path)
                route_dt = datetime.fromtimestamp(mtime)
            except:
                logger.warning(f"Could not parse datetime from route: {base_name}, skipping")
                continue

        # Get all segments for this base route
        segments = get_route_segments(base_name)
        if not segments:
            continue

        # Calculate total size across all segments
        total_size = sum(get_file_size(seg['path']) for seg in segments)

        # Check which cameras have footage across all segments
        has_video = {
            'front': False,
            'wide': False,
            'driver': False,
            'lq': False
        }

        for seg in segments:
            videos = get_video_files(seg['path'])
            for camera in videos.keys():
                has_video[camera] = True

        # Check for star file
        star_file = os.path.join(ROUTES_DIR, base_name, '.star')
        is_starred = os.path.exists(star_file)

        # Calculate duration (1 minute per segment)
        duration_seconds = len(segments) * 60
        hours = duration_seconds // 3600
        minutes = (duration_seconds % 3600) // 60
        if hours > 0:
            duration_str = f"{hours}h {minutes}m"
        else:
            duration_str = f"{minutes}m"

        # Check if GPS metrics are already cached (don't process logs during scan)
        cache_file = os.path.join(METRICS_CACHE, f"{base_name}.json")
        if os.path.exists(cache_file):
            try:
                with open(cache_file) as f:
                    gps_metrics = json.load(f)
            except Exception as e:
                logger.debug(f"Error reading GPS cache for {base_name}: {e}")
                gps_metrics = {'has_gps_data': False}
        else:
            # No cache - return placeholder, let background preprocessor handle it
            gps_metrics = {'has_gps_data': False}

        # Format GPS metrics for display based on user's unit preference
        if gps_metrics['has_gps_data']:
            # Get unit preference (False = imperial/mph, True = metric/km/h)
            is_metric = params.get_bool("IsMetric")

            if is_metric:
                # Metric units
                distance_km = gps_metrics['total_distance_meters'] / 1000
                avg_speed_kmh = gps_metrics['avg_speed_ms'] * 3.6
                max_speed_kmh = gps_metrics['max_speed_ms'] * 3.6

                mileage_str = f"{distance_km:.2f} km"
                avg_speed_str = f"{avg_speed_kmh:.1f} km/h"
                max_speed_str = f"{max_speed_kmh:.1f} km/h"
            else:
                # Imperial units
                distance_miles = gps_metrics['total_distance_meters'] / 1609.34
                avg_speed_mph = gps_metrics['avg_speed_ms'] * 2.237
                max_speed_mph = gps_metrics['max_speed_ms'] * 2.237

                mileage_str = f"{distance_miles:.2f} mi"
                avg_speed_str = f"{avg_speed_mph:.1f} mph"
                max_speed_str = f"{max_speed_mph:.1f} mph"

            # Load location names from cache (saved by background preprocessor or /api/geocode)
            start_location = gps_metrics.get('start_location')
            end_location = gps_metrics.get('end_location')
        else:
            mileage_str = None
            avg_speed_str = None
            max_speed_str = None
            start_location = None
            end_location = None

        # Build route info matching old panel structure
        routes_dict[base_name] = {
            'baseName': base_name,
            'displayDate': format_display_date(route_dt),
            'displayTime': format_time_12hr(route_dt),
            'timestamp': route_dt.isoformat(),
            'elapsedTime': format_elapsed_time(route_dt),
            'segments': len(segments),
            'duration': duration_str,
            'size': format_size(total_size),
            'sizeBytes': total_size,
            'hasVideo': has_video,
            'isStarred': is_starred,
            'dateTime': route_dt.isoformat(),  # For sorting
            # GPS metrics
            'mileage': mileage_str,
            'avgSpeed': avg_speed_str,
            'topSpeed': max_speed_str,
            'hasGpsData': gps_metrics['has_gps_data'],
            # Location names (reverse geocoded)
            'startLocation': start_location,
            'endLocation': end_location,
        }

    # Convert to list and sort by datetime (newest first)
    routes = list(routes_dict.values())
    routes.sort(key=lambda x: x['dateTime'], reverse=True)

    logger.info(f"Scanned {len(routes)} routes")
    return routes


# Thumbnail generation function imported from route_processing module


class ReuseAddressHTTPServer(HTTPServer):
    """HTTPServer with SO_REUSEADDR to prevent 'Address already in use' errors"""
    allow_reuse_address = True


class WebRoutesHandler(BaseHTTPRequestHandler):
    """HTTP request handler for web routes server"""

    def log_message(self, format, *args):
        """Override to use logger"""
        logger.info(f"{self.address_string()} - {format % args}")

    def send_cors_headers(self):
        """Send CORS headers for local network access"""
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, DELETE, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')

    def send_json_response(self, data, status=200):
        """Send JSON response"""
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_cors_headers()
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())

    def send_file_response(self, filepath, mime_type=None):
        """Send file response with optional byte-range support"""
        if not os.path.exists(filepath):
            self.send_json_response({'error': 'File not found'}, 404)
            return

        file_size = os.path.getsize(filepath)

        # Parse range header
        range_header = self.headers.get('Range')
        if range_header and range_header.startswith('bytes='):
            try:
                # Parse bytes=start-end format more robustly
                range_spec = range_header.replace('bytes=', '').strip()
                range_parts = range_spec.split('-')

                if len(range_parts) >= 1 and range_parts[0]:
                    start = int(range_parts[0])
                else:
                    start = 0

                if len(range_parts) >= 2 and range_parts[1]:
                    end = int(range_parts[1])
                else:
                    end = file_size - 1

                # Validate range
                if start >= file_size or end >= file_size or start > end:
                    # Invalid range, serve entire file
                    start = 0
                    end = file_size - 1

                length = end - start + 1

                self.send_response(206)  # Partial Content
                self.send_header('Content-Range', f'bytes {start}-{end}/{file_size}')
                self.send_header('Content-Length', str(length))
            except (ValueError, IndexError) as e:
                # Malformed range header, serve entire file
                logger.warning(f"Malformed range header '{range_header}': {e}")
                start = 0
                length = file_size
                self.send_response(200)
                self.send_header('Content-Length', str(file_size))
        else:
            start = 0
            length = file_size
            self.send_response(200)
            self.send_header('Content-Length', str(file_size))

        # Determine MIME type
        if mime_type is None:
            mime_type, _ = mimetypes.guess_type(filepath)
            if mime_type is None:
                if filepath.endswith('.hevc'):
                    # HEVC files - use MP4 container MIME with HEVC codec hint for Safari
                    mime_type = 'video/mp4; codecs="hvc1"'
                elif filepath.endswith('.ts'):
                    # MPEG-TS files - proper MIME type for transport streams
                    mime_type = 'video/mp2t'
                else:
                    mime_type = 'application/octet-stream'

        self.send_header('Content-Type', mime_type)
        self.send_header('Accept-Ranges', 'bytes')
        self.send_cors_headers()
        self.end_headers()

        # Send file data
        try:
            with open(filepath, 'rb') as f:
                f.seek(start)
                remaining = length
                while remaining > 0:
                    chunk_size = min(8192, remaining)
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    remaining -= len(chunk)
        except (IOError, OSError) as e:
            logger.error(f"Error sending file {filepath}: {e}")
            # Connection might be broken, don't try to send error response
            return
        except Exception as e:
            logger.error(f"Unexpected error sending file {filepath}: {e}")
            return

    def send_remuxed_hevc(self, hevc_path, route_base, segment_num, camera):
        """Remux raw HEVC elementary stream to MP4 container for browser playback

        Uses progressive streaming to start playback immediately while remuxing.
        """
        if not os.path.exists(hevc_path):
            self.send_json_response({'error': 'HEVC file not found'}, 404)
            return

        # Create cache filename
        cache_filename = f"{route_base}_{segment_num}_{camera}.mp4"
        cache_path = os.path.join(REMUX_CACHE, cache_filename)

        # Check if cached MP4 exists and is newer than source
        if os.path.exists(cache_path):
            cache_mtime = os.path.getmtime(cache_path)
            source_mtime = os.path.getmtime(hevc_path)
            if cache_mtime >= source_mtime:
                logger.info(f"Serving cached MP4: {cache_filename}")
                # Update access time for LRU cache management
                os.utime(cache_path, None)
                self.send_file_response(cache_path)
                return

        # Check cache size and cleanup if needed
        cleanup_old_cache()

        # Check if too many FFmpeg processes are running (prevent resource exhaustion)
        global active_ffmpeg_processes
        with ffmpeg_lock:
            if active_ffmpeg_processes >= MAX_CONCURRENT_FFMPEG:
                logger.warning(f"Too many concurrent FFmpeg processes ({active_ffmpeg_processes}), rejecting request")
                self.send_json_response({
                    'error': 'Server busy processing other videos',
                    'hint': f'Maximum {MAX_CONCURRENT_FFMPEG} concurrent video streams allowed. Please try again in a moment.'
                }, 503)  # Service Unavailable
                return
            active_ffmpeg_processes += 1
            logger.info(f"FFmpeg processes active: {active_ffmpeg_processes}/{MAX_CONCURRENT_FFMPEG}")

        # Check disk space before remuxing (estimate 2x source file size)
        source_size = os.path.getsize(hevc_path)
        estimated_output_size = source_size * 2  # Conservative estimate
        cache_dir = "/data" if os.path.exists("/data") else os.path.expanduser("~")

        if not has_sufficient_disk_space(estimated_output_size, cache_dir, min_free_gb=0.5):
            logger.error(f"Insufficient disk space to remux {cache_filename}")
            with ffmpeg_lock:
                active_ffmpeg_processes -= 1
            self.send_json_response({
                'error': 'Insufficient disk space for video processing',
                'hint': 'Clear cache or free up disk space'
            }, 507)  # HTTP 507 Insufficient Storage
            return

        # Enable performance mode for fast remuxing
        enable_performance_mode()

        # Remux using FFmpeg with progressive streaming
        logger.info(f"Remuxing HEVC to MP4 (streaming): {cache_filename}")

        try:
            # Use FFmpeg to remux raw HEVC to MP4 container
            # Stream to stdout while also writing to cache file
            cmd = [
                'ffmpeg',
                '-f', 'hevc',
                '-r', '20',  # Comma camera framerate (20 fps)
                '-i', hevc_path,
                '-c', 'copy',
                '-movflags', 'frag_keyframe+empty_moov+faststart+default_base_moof',  # Better HLS compatibility
                '-fflags', '+genpts',  # Generate missing PTS if needed
                '-avoid_negative_ts', 'make_zero',  # Handle negative timestamps
                '-bsf:v', 'hevc_mp4toannexb',  # Convert HEVC bitstream for better compatibility
                '-f', 'mp4',
                '-'  # Output to stdout
            ]

            # Start FFmpeg process
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                bufsize=8192
            )

            # Send HTTP headers
            self.send_response(200)
            self.send_header('Content-Type', 'video/mp4')
            self.send_header('Accept-Ranges', 'bytes')  # Enable range requests for video streaming
            self.send_cors_headers()
            self.end_headers()

            # Stream output while also saving to cache
            try:
                with open(cache_path, 'wb') as cache_file:
                    while True:
                        chunk = process.stdout.read(8192)
                        if not chunk:
                            break

                        # Send to client
                        try:
                            self.wfile.write(chunk)
                        except (BrokenPipeError, ConnectionResetError):
                            logger.warning("Client disconnected during streaming")
                            process.kill()
                            process.wait(timeout=2)  # Wait for process to die
                            break

                        # Save to cache
                        cache_file.write(chunk)

                # Wait for process to complete
                process.wait(timeout=5)
            finally:
                # CRITICAL: Ensure FFmpeg process is always terminated
                if process.poll() is None:  # Process still running
                    logger.warning("FFmpeg process still running, forcing termination")
                    process.kill()
                    try:
                        process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        logger.exception("FFmpeg process would not die, sending SIGKILL")
                        process.kill()  # Force kill
                        process.wait()  # Wait indefinitely for it to die

                # Decrement process counter
                with ffmpeg_lock:
                    active_ffmpeg_processes -= 1
                    logger.info(f"FFmpeg process completed. Active: {active_ffmpeg_processes}/{MAX_CONCURRENT_FFMPEG}")

            if process.returncode != 0:
                stderr = process.stderr.read().decode()
                logger.error(f"FFmpeg remux failed: {stderr[:500]}")
                # Clean up incomplete cache file
                if os.path.exists(cache_path):
                    os.remove(cache_path)
            else:
                logger.info(f"Remux successful: {cache_filename}")

        except subprocess.TimeoutExpired:
            logger.error(f"FFmpeg timeout remuxing {hevc_path}")
            if os.path.exists(cache_path):
                os.remove(cache_path)
            with ffmpeg_lock:
                active_ffmpeg_processes -= 1
        except FileNotFoundError:
            logger.error("FFmpeg not found - install with: apt-get install ffmpeg")
            # Check if we can use ffmpeg.wasm as fallback
            try:
                # Try to use ffmpeg.wasm for client-side remuxing if available
                logger.info("Native FFmpeg not found, checking for ffmpeg.wasm support")
                # For now, we'll still return an error since ffmpeg.wasm integration would require frontend changes
                # TODO: Implement ffmpeg.wasm client-side remuxing as ultimate fallback
            except:
                pass
            with ffmpeg_lock:
                active_ffmpeg_processes -= 1
            self.send_json_response({
                'error': 'FFmpeg not installed',
                'hint': 'Install with: apt-get install ffmpeg',
                'note': 'ffmpeg.wasm client-side support could be added as fallback'
            }, 500)
        except Exception as e:
            logger.error(f"Unexpected error remuxing {hevc_path}: {e}")
            # Clean up incomplete cache file
            if os.path.exists(cache_path):
                os.remove(cache_path)

            # Decrement counter on error
            with ffmpeg_lock:
                active_ffmpeg_processes -= 1

            # If remuxing fails, try to serve the raw HEVC file with different MIME type
            # This might work in some browsers with native HEVC support
            logger.warning(f"Remuxing failed, attempting to serve raw HEVC as fallback")
            try:
                self.send_file_response(hevc_path, 'video/mp4; codecs="hev1"')
                return
            except Exception as fallback_error:
                logger.error(f"Fallback to raw HEVC also failed: {fallback_error}")
                self.send_json_response({
                    'error': 'Video conversion failed',
                    'details': f'FFmpeg error: {e}. Fallback also failed: {fallback_error}'
                }, 500)

    def do_OPTIONS(self):
        """Handle OPTIONS for CORS preflight"""
        self.send_response(200)
        self.send_cors_headers()
        self.end_headers()

    def do_GET(self):
        """Handle GET requests"""
        try:
            parsed = urlparse(self.path)
            path = parsed.path

            # Rate limiting check
            client_ip = self.client_address[0]
            is_allowed, retry_after = check_rate_limit(client_ip)
            if not is_allowed:
                self.send_response(429)  # Too Many Requests
                self.send_header('Retry-After', str(retry_after))
                self.send_cors_headers()
                self.end_headers()
                error_msg = json.dumps({
                    'error': 'Rate limit exceeded',
                    'retry_after': retry_after,
                    'limit': f'{MAX_REQUESTS_PER_MINUTE} requests per minute'
                }).encode()
                self.wfile.write(error_msg)
                return

            # Check if server should be running
            if not should_server_run():
                if is_onroad():
                    self.send_json_response({'error': 'Server disabled while driving for safety'}, 503)
                else:
                    self.send_json_response({'error': 'Server disabled by user'}, 503)
                return

            # Check onroad status for API endpoints (redundant but kept for compatibility)
            if path.startswith('/api/') and path != '/api/status':
                if is_onroad():
                    self.send_json_response({'error': 'Server disabled while driving for safety'}, 503)
                    return

            # Route handlers
            if path == '/' or path == '/index.html':
                self.send_file_response(str(WEBAPP_DIR / 'index.html'), 'text/html')

            elif path.startswith('/api/status'):
                self.send_json_response({
                    'status': 'online',
                    'onroad': is_onroad(),
                    'routes_dir': ROUTES_DIR,
                    'routes_dir_exists': os.path.exists(ROUTES_DIR),
                    'isMetric': params.get_bool("IsMetric")
                })

            elif path.startswith('/api/system/metrics'):
                # System health metrics
                try:
                    metrics = get_system_metrics()
                    self.send_json_response({
                        'success': True,
                        'metrics': metrics
                    })
                except Exception as e:
                    logger.exception("Error getting system metrics")
                    self.send_json_response({
                        'success': False,
                        'error': str(e)
                    }, 500)

            elif path.startswith('/api/routes/') and '/video/' not in path:
                # Get specific route details
                route_base = path.split('/api/routes/')[1].strip('/')
                segments = get_route_segments(route_base)

                if not segments:
                    self.send_json_response({'success': False, 'error': 'Route not found'}, 404)
                    return

                # Parse datetime from base name
                route_dt = parse_route_datetime(route_base)
                if route_dt is None:
                    # Use directory modification time as fallback
                    try:
                        first_seg_path = segments[0]['path']
                        mtime = os.path.getmtime(first_seg_path)
                        route_dt = datetime.fromtimestamp(mtime)
                    except Exception as e:
                        logger.warning(f"Failed to get route datetime for {route_base}: {e}")
                        route_dt = datetime.now()

                # Calculate total size
                total_size = sum(get_file_size(seg['path']) for seg in segments)

                # Calculate duration (1 minute per segment)
                duration_seconds = len(segments) * 60
                hours = duration_seconds // 3600
                minutes = (duration_seconds % 3600) // 60
                if hours > 0:
                    duration_str = f"{hours}h {minutes}m"
                else:
                    duration_str = f"{minutes}m"

                # Check for star file
                star_file = os.path.join(ROUTES_DIR, route_base, '.star')
                is_starred = os.path.exists(star_file)

                # Load GPS metrics from cache if available
                cache_file = os.path.join(METRICS_CACHE, f"{route_base}.json")
                if os.path.exists(cache_file):
                    try:
                        with open(cache_file) as f:
                            gps_metrics = json.load(f)
                    except Exception as e:
                        logger.debug(f"Error reading GPS cache for {route_base}: {e}")
                        gps_metrics = {'has_gps_data': False}
                else:
                    gps_metrics = {'has_gps_data': False}

                # Format GPS metrics based on user preference
                if gps_metrics['has_gps_data']:
                    is_metric = params.get_bool("IsMetric")
                    if is_metric:
                        distance_km = gps_metrics['total_distance_meters'] / 1000
                        mileage_str = f"{distance_km:.2f} km"
                    else:
                        distance_miles = gps_metrics['total_distance_meters'] / 1609.34
                        mileage_str = f"{distance_miles:.2f} mi"
                    start_location = gps_metrics.get('start_location')
                    end_location = gps_metrics.get('end_location')
                else:
                    mileage_str = None
                    start_location = None
                    end_location = None

                # Build segment details
                segments_detail = []
                for seg in segments:
                    videos = get_video_files(seg['path'])
                    segments_detail.append({
                        'number': seg['segment'],
                        'name': seg['name'],
                        'path': seg['path'],
                        'videos': videos
                    })

                self.send_json_response({
                    'success': True,
                    'baseName': route_base,
                    'displayDate': format_display_date(route_dt),
                    'displayTime': format_time_12hr(route_dt),
                    'timestamp': route_dt.isoformat(),
                    'duration': duration_str,
                    'size': format_size(total_size),
                    'sizeBytes': total_size,
                    'mileage': mileage_str,
                    'hasGpsData': gps_metrics['has_gps_data'],
                    'startLocation': start_location,
                    'endLocation': end_location,
                    'isStarred': is_starred,
                    'segments': segments_detail,
                    'totalSegments': len(segments_detail)
                })

            elif path == '/api/routes':
                # Fast route list (cached data only, no processing)
                routes = scan_routes()
                self.send_json_response({
                    'success': True,
                    'routes': routes,
                    'total': len(routes)
                })

            elif path.startswith('/api/geocode/'):
                # Geocode a specific route: /api/geocode/{route_base}
                route_base = path.split('/api/geocode/')[1].strip('/')
                segments = get_route_segments(route_base)

                if not segments:
                    self.send_json_response({'error': 'Route not found'}, 404)
                    return

                # Get GPS coordinates (should be cached)
                gps_metrics = get_route_gps_metrics(route_base, segments, include_coordinates=True)

                if not gps_metrics.get('has_gps_data'):
                    self.send_json_response({
                        'success': True,
                        'baseName': route_base,
                        'startLocation': None,
                        'endLocation': None,
                        'hasGpsData': False
                    })
                    return

                coordinates = gps_metrics.get('coordinates', [])
                if coordinates:
                    start_coord = coordinates[0]
                    end_coord = coordinates[-1]

                    start_location = reverse_geocode(start_coord['lat'], start_coord['lon'])
                    end_location = reverse_geocode(end_coord['lat'], end_coord['lon'])

                    # Save location names to GPS metrics cache for future use
                    cache_file = os.path.join(METRICS_CACHE, f"{route_base}.json")
                    try:
                        if os.path.exists(cache_file):
                            with open(cache_file, 'r') as f:
                                cached_data = json.load(f)

                            cached_data['start_location'] = start_location
                            cached_data['end_location'] = end_location

                            with open(cache_file, 'w') as f:
                                json.dump(cached_data, f)
                    except Exception as e:
                        logger.warning(f"Error saving location names to cache: {e}")
                else:
                    start_location = None
                    end_location = None

                self.send_json_response({
                    'success': True,
                    'baseName': route_base,
                    'startLocation': start_location,
                    'endLocation': end_location,
                    'hasGpsData': True
                })

            elif path.startswith('/api/hls/'):
                # Generate HLS manifest for route playback
                # Parse: /api/hls/{route_base}/{camera}/playlist.m3u8
                parts = path.split('/')[3:]  # Skip '', 'api', 'hls'
                if len(parts) < 3 or not parts[2].endswith('.m3u8'):
                    self.send_json_response({'error': 'Invalid HLS path'}, 400)
                    return

                route_base = parts[0]
                camera = parts[1]

                # Get all segments for this route
                segments = get_route_segments(route_base)
                if not segments:
                    self.send_json_response({'error': 'Route not found'}, 404)
                    return

                # Map camera to filename
                camera_files = {
                    'front': 'fcamera.hevc',
                    'wide': 'ecamera.hevc',
                    'driver': 'dcamera.hevc',
                    'lq': 'qcamera.ts'
                }

                if camera not in camera_files:
                    self.send_json_response({'error': 'Invalid camera type'}, 400)
                    return

                # Generate HLS playlist
                playlist = "#EXTM3U\n"
                playlist += "#EXT-X-VERSION:6\n"  # Use HLS version 6 for better compatibility
                playlist += "#EXT-X-TARGETDURATION:60\n"
                playlist += "#EXT-X-MEDIA-SEQUENCE:0\n"
                playlist += "#EXT-X-PLAYLIST-TYPE:VOD\n"

                for seg in segments:
                    seg_path = os.path.join(seg['path'], camera_files[camera])
                    if os.path.exists(seg_path):
                        # Each segment is ~60 seconds with more precise timing
                        playlist += f"#EXTINF:60.0,\n"
                        playlist += f"/api/video/{route_base}/{seg['segment']}/{camera}\n"

                playlist += "#EXT-X-ENDLIST\n"

                # Send playlist
                self.send_response(200)
                self.send_header('Content-Type', 'application/vnd.apple.mpegurl')
                self.send_cors_headers()
                self.end_headers()
                self.wfile.write(playlist.encode())

            elif path.startswith('/api/video/'):
                # Parse: /api/video/{route_base}/{segment}/{camera}
                parts = path.split('/')[3:]  # Skip '', 'api', 'video'
                if len(parts) < 3:
                    self.send_json_response({'error': 'Invalid video path'}, 400)
                    return

                route_base = parts[0]
                try:
                    segment_num = int(parts[1])
                except ValueError:
                    self.send_json_response({'error': 'Invalid segment number'}, 400)
                    return
                camera = parts[2]

                # Get segment
                segments = get_route_segments(route_base)
                segment_data = next((s for s in segments if s['segment'] == segment_num), None)

                if not segment_data:
                    self.send_json_response({'error': 'Segment not found'}, 404)
                    return

                # Map camera to filename
                camera_files = {
                    'front': 'fcamera.hevc',
                    'wide': 'ecamera.hevc',
                    'driver': 'dcamera.hevc',
                    'lq': 'qcamera.ts'
                }

                if camera not in camera_files:
                    self.send_json_response({'error': 'Invalid camera type'}, 400)
                    return

                video_path = os.path.join(segment_data['path'], camera_files[camera])

                # For HEVC files, remux to MP4 for browser compatibility
                if camera in ['front', 'wide', 'driver']:
                    self.send_remuxed_hevc(video_path, route_base, segment_num, camera)
                else:
                    self.send_file_response(video_path)

            elif path.startswith('/api/download/route/'):
                # Download full route: /api/download/route/{route_base}/{camera}
                parts = path.split('/')[4:]  # Skip '', 'api', 'download', 'route'
                if len(parts) < 2:
                    self.send_json_response({'error': 'Invalid download path'}, 400)
                    return

                route_base = parts[0]
                camera = parts[1]

                # Get all segments for this route
                segments = get_route_segments(route_base)
                if not segments:
                    self.send_json_response({'error': 'Route not found'}, 404)
                    return

                # Validate camera type
                camera_files = {
                    'front': 'fcamera.hevc',
                    'wide': 'ecamera.hevc',
                    'driver': 'dcamera.hevc',
                    'lq': 'qcamera.ts'
                }

                if camera not in camera_files:
                    self.send_json_response({'error': 'Invalid camera type'}, 400)
                    return

                self.download_full_route(route_base, camera, segments)

            elif path.startswith('/api/download/segment/'):
                # Download individual segment: /api/download/segment/{route_base}/{segment}/{camera}
                parts = path.split('/')[4:]  # Skip '', 'api', 'download', 'segment'
                if len(parts) < 3:
                    self.send_json_response({'error': 'Invalid download path'}, 400)
                    return

                route_base = parts[0]
                try:
                    segment_num = int(parts[1])
                except ValueError:
                    self.send_json_response({'error': 'Invalid segment number'}, 400)
                    return
                camera = parts[2]

                # Get segment data
                segments = get_route_segments(route_base)
                segment_data = next((s for s in segments if s['segment'] == segment_num), None)

                if not segment_data:
                    self.send_json_response({'error': 'Segment not found'}, 404)
                    return

                # Validate camera type
                camera_files = {
                    'front': 'fcamera.hevc',
                    'wide': 'ecamera.hevc',
                    'driver': 'dcamera.hevc',
                    'lq': 'qcamera.ts'
                }

                if camera not in camera_files:
                    self.send_json_response({'error': 'Invalid camera type'}, 400)
                    return

                self.download_segment(route_base, segment_num, camera, segment_data)

            elif path.startswith('/api/route-coordinates/'):
                # New endpoint: /api/route-coordinates/{route_base}
                route_base = path.split('/api/route-coordinates/')[1].strip('/')
                segments = get_route_segments(route_base)

                if not segments:
                    self.send_json_response({'error': 'Route not found'}, 404)
                    return

                # Extract GPS coordinates with caching (separate from metrics cache)
                gps_data = get_route_gps_metrics(route_base, segments, include_coordinates=True)

                if gps_data.get('has_gps_data') and 'coordinates' in gps_data:
                    self.send_json_response({
                        'success': True,
                        'baseName': route_base,
                        'coordinates': gps_data['coordinates'],
                        'pointCount': len(gps_data['coordinates'])
                    })
                else:
                    self.send_json_response({
                        'success': False,
                        'error': 'No GPS data available for this route'
                    }, 404)

            elif path.startswith('/api/thumbnail/'):
                route_base = path.split('/api/thumbnail/')[1].strip('/')

                # Try to generate thumbnail if it doesn't exist
                thumbnail_path = generate_thumbnail(route_base)

                if thumbnail_path and os.path.exists(thumbnail_path):
                    self.send_file_response(thumbnail_path, 'image/jpeg')
                else:
                    # Return a placeholder 1x1 transparent PNG instead of 404
                    # This prevents broken image icons in the UI
                    self.send_response(200)
                    self.send_header('Content-Type', 'image/png')
                    self.send_cors_headers()
                    self.end_headers()
                    # 1x1 transparent PNG (67 bytes)
                    self.wfile.write(b'\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\nIDATx\x9cc\x00\x01\x00\x00\x05\x00\x01\r\n-\xb4\x00\x00\x00\x00IEND\xaeB`\x82')

            else:
                # Serve static files
                file_path = WEBAPP_DIR / path.lstrip('/')
                if file_path.exists() and file_path.is_file():
                    self.send_file_response(str(file_path))
                else:
                    self.send_json_response({'error': 'Not found'}, 404)

        except Exception as e:
            logger.error(f"Error handling GET request to {self.path}: {e}")
            try:
                self.send_json_response({'error': 'Internal server error'}, 500)
            except:
                # If we can't send error response, just pass
                pass

    def download_full_route(self, route_base, camera, segments):
        """Download full route by concatenating all segments"""
        try:
            # Create filename for download
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"{route_base}_{camera}_{timestamp}.mp4"

            # Set download headers
            self.send_response(200)
            self.send_header('Content-Type', 'video/mp4')
            self.send_header('Content-Disposition', f'attachment; filename="{filename}"')
            self.send_cors_headers()
            self.end_headers()

            # Get camera file mapping
            camera_files = {
                'front': 'fcamera.hevc',
                'wide': 'ecamera.hevc',
                'driver': 'dcamera.hevc',
                'lq': 'qcamera.ts'
            }

            # Create FFmpeg concat file list
            concat_list = []
            for seg in segments:
                seg_path = os.path.join(seg['path'], camera_files[camera])
                if os.path.exists(seg_path):
                    concat_list.append(seg_path)

            if not concat_list:
                logger.error(f"No segments found for route {route_base} camera {camera}")
                return

            # Create temporary concat file
            concat_file = f"/tmp/concat_{route_base}_{camera}.txt"
            with open(concat_file, 'w') as f:
                for video_file in concat_list:
                    f.write(f"file '{video_file}'\n")

            # Use FFmpeg to concatenate all segments
            # For HEVC raw streams, use concat protocol with proper input format
            if camera in ['front', 'wide', 'driver']:
                # HEVC files are raw elementary streams
                # Use concat protocol: concat:file1|file2|file3
                concat_protocol = 'concat:' + '|'.join(concat_list)

                cmd = [
                    'ffmpeg',
                    '-f', 'hevc',  # Input format is raw HEVC
                    '-r', '20',  # Comma camera framerate (20 fps)
                    '-i', concat_protocol,  # Use concat protocol
                    '-c:v', 'copy',  # Copy without re-encoding
                    '-movflags', '+faststart',  # Optimize for web playback
                    '-f', 'mp4',
                    '-'  # Output to stdout
                ]
            else:
                # LQ files (qcamera.ts) can use concat demuxer
                cmd = [
                    'ffmpeg',
                    '-f', 'concat',
                    '-safe', '0',
                    '-i', concat_file,
                    '-c', 'copy',  # Copy without re-encoding
                    '-movflags', '+faststart',  # Optimize for web playback
                    '-f', 'mp4',
                    '-'  # Output to stdout
                ]

            # Start FFmpeg process
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                bufsize=8192
            )

            # Stream output to client
            try:
                while True:
                    chunk = process.stdout.read(8192)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
            except (BrokenPipeError, ConnectionResetError):
                logger.warning("Client disconnected during download")
                process.kill()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    logger.warning("FFmpeg download process did not terminate, force killing")
                    process.kill()
                    process.wait()
            finally:
                # Cleanup - ensure process is terminated
                if process.poll() is None:
                    logger.warning("FFmpeg download process still running in finally block")
                    process.kill()
                    try:
                        process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        logger.exception("FFmpeg download process would not die")
                        process.kill()
                        process.wait()

                # Remove temp file
                if os.path.exists(concat_file):
                    os.remove(concat_file)

        except Exception as e:
            logger.error(f"Error downloading full route {route_base}: {e}")

    def download_segment(self, route_base, segment_num, camera, segment_data):
        """Download individual segment"""
        try:
            # Create filename for download
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"{route_base}_segment_{segment_num:03d}_{camera}_{timestamp}.mp4"

            # Set download headers
            self.send_response(200)
            self.send_header('Content-Type', 'video/mp4')
            self.send_header('Content-Disposition', f'attachment; filename="{filename}"')
            self.send_cors_headers()
            self.end_headers()

            # Get video file path
            camera_files = {
                'front': 'fcamera.hevc',
                'wide': 'ecamera.hevc',
                'driver': 'dcamera.hevc',
                'lq': 'qcamera.ts'
            }

            video_path = os.path.join(segment_data['path'], camera_files[camera])

            # For HEVC files, remux to MP4 for browser compatibility
            if camera in ['front', 'wide', 'driver']:
                # Check if already cached
                cache_filename = f"{route_base}_{segment_num}_{camera}.mp4"
                cache_path = os.path.join(REMUX_CACHE, cache_filename)

                if os.path.exists(cache_path):
                    # Serve cached version
                    with open(cache_path, 'rb') as f:
                        shutil.copyfileobj(f, self.wfile)
                else:
                    # Remux on-demand for download
                    self.send_remuxed_hevc(video_path, route_base, segment_num, camera)
            else:
                # LQ files are already in correct format
                with open(video_path, 'rb') as f:
                    shutil.copyfileobj(f, self.wfile)

        except Exception as e:
            logger.error(f"Error downloading segment {route_base}/{segment_num}: {e}")

    def do_POST(self):
        """Handle POST requests"""
        try:
            parsed = urlparse(self.path)
            path = parsed.path

            # Rate limiting check (skip for internal endpoints)
            if not path.startswith('/_internal/'):
                client_ip = self.client_address[0]
                is_allowed, retry_after = check_rate_limit(client_ip)
                if not is_allowed:
                    self.send_response(429)
                    self.send_header('Retry-After', str(retry_after))
                    self.send_cors_headers()
                    self.end_headers()
                    error_msg = json.dumps({
                        'error': 'Rate limit exceeded',
                        'retry_after': retry_after
                    }).encode()
                    self.wfile.write(error_msg)
                    return

            # Internal broadcast endpoint (for cross-process communication)
            # No authentication check - only listens on localhost
            if path == '/_internal/broadcast':
                try:
                    content_length = int(self.headers.get('Content-Length', 0))
                    if content_length > 0:
                        body = self.rfile.read(content_length)
                        event_data = json.loads(body.decode('utf-8'))

                        # Broadcast to all connected WebSocket clients
                        if broadcaster:
                            broadcaster.broadcast(event_data.get('type'), event_data.get('data'))

                    self.send_json_response({'success': True})
                except Exception as e:
                    logger.exception("Error handling internal broadcast")
                    self.send_json_response({'error': str(e)}, 500)
                return

            # Check if server should be running
            if not should_server_run():
                if is_onroad():
                    self.send_json_response({'error': 'Server disabled while driving for safety'}, 503)
                else:
                    self.send_json_response({'error': 'Server disabled by user'}, 503)
                return

            if is_onroad():
                self.send_json_response({'error': 'Server disabled while driving for safety'}, 503)
                return

            if path.startswith('/api/star/'):
                route_base = path.split('/api/star/')[1].strip('/')
                star_file = os.path.join(ROUTES_DIR, route_base, '.star')

                if os.path.exists(star_file):
                    os.remove(star_file)
                    is_starred = False
                else:
                    os.makedirs(os.path.dirname(star_file), exist_ok=True)
                    with open(star_file, 'w') as f:
                        f.write('')
                    is_starred = True

                self.send_json_response({
                    'success': True,
                    'isStarred': is_starred
                })

                # Broadcast WebSocket event
                event_type = WebSocketEvent.ROUTE_STARRED if is_starred else WebSocketEvent.ROUTE_UNSTARRED
                broadcast_websocket_event(event_type, {
                    'route_base': route_base,
                    'is_starred': is_starred
                })
            elif path == '/api/clear-cache':
                # Clear all cached data (remuxed videos, thumbnails, GPS metrics)
                import shutil

                cleared = {
                    'remux_cache': 0,
                    'thumbnails': 0,
                    'gps_metrics': 0,
                    'gps_coordinates': 0
                }

                # Clear remuxed video cache
                if os.path.exists(REMUX_CACHE):
                    for filename in os.listdir(REMUX_CACHE):
                        if filename.endswith('.mp4'):
                            try:
                                os.remove(os.path.join(REMUX_CACHE, filename))
                                cleared['remux_cache'] += 1
                            except Exception as e:
                                logger.warning(f"Error deleting remux cache file {filename}: {e}")

                # Clear thumbnail cache
                if os.path.exists(THUMBNAIL_CACHE):
                    for filename in os.listdir(THUMBNAIL_CACHE):
                        if filename.endswith('.jpg'):
                            try:
                                os.remove(os.path.join(THUMBNAIL_CACHE, filename))
                                cleared['thumbnails'] += 1
                            except Exception as e:
                                logger.warning(f"Error deleting thumbnail {filename}: {e}")

                # Clear GPS metrics cache
                if os.path.exists(METRICS_CACHE):
                    for filename in os.listdir(METRICS_CACHE):
                        filepath = os.path.join(METRICS_CACHE, filename)
                        try:
                            if filename.endswith('_coords.json'):
                                os.remove(filepath)
                                cleared['gps_coordinates'] += 1
                            elif filename.endswith('.json') and filename != 'geocoding_cache.json':
                                os.remove(filepath)
                                cleared['gps_metrics'] += 1
                        except Exception as e:
                            logger.warning(f"Error deleting metrics cache file {filename}: {e}")

                # Clear in-memory route cache
                get_route_segments.cache_clear()

                logger.info(f"Cache cleared: {cleared}")

                self.send_json_response({
                    'success': True,
                    'cleared': cleared
                })

                # Broadcast WebSocket event
                broadcast_websocket_event(WebSocketEvent.CACHE_CLEARED, {
                    'cleared': cleared
                })
            else:
                self.send_json_response({'error': 'Not found'}, 404)

        except Exception as e:
            logger.error(f"Error handling POST request to {self.path}: {e}")
            try:
                self.send_json_response({'error': 'Internal server error'}, 500)
            except:
                # If we can't send error response, just pass
                pass

    def do_DELETE(self):
        """Handle DELETE requests"""
        try:
            parsed = urlparse(self.path)
            path = parsed.path

            # Rate limiting check
            client_ip = self.client_address[0]
            is_allowed, retry_after = check_rate_limit(client_ip)
            if not is_allowed:
                self.send_response(429)
                self.send_header('Retry-After', str(retry_after))
                self.send_cors_headers()
                self.end_headers()
                error_msg = json.dumps({
                    'error': 'Rate limit exceeded',
                    'retry_after': retry_after
                }).encode()
                self.wfile.write(error_msg)
                return

            # Check if server should be running
            if not should_server_run():
                if is_onroad():
                    self.send_json_response({'error': 'Server disabled while driving for safety'}, 503)
                else:
                    self.send_json_response({'error': 'Server disabled by user'}, 503)
                return

            if is_onroad():
                self.send_json_response({'error': 'Server disabled while driving for safety'}, 503)
                return

            if path.startswith('/api/delete/'):
                import shutil
                route_base = path.split('/api/delete/')[1].strip('/')
                segments = get_route_segments(route_base)

                if not segments:
                    self.send_json_response({'error': 'Route not found'}, 404)
                    return

                # Delete all segments
                for seg in segments:
                    if os.path.exists(seg['path']):
                        shutil.rmtree(seg['path'])

                # Delete thumbnail
                cache_path = os.path.join(THUMBNAIL_CACHE, f"{route_base}.jpg")
                if os.path.exists(cache_path):
                    os.remove(cache_path)

                # Clear cache
                get_route_segments.cache_clear()

                self.send_json_response({
                    'success': True,
                    'deleted': len(segments)
                })

                # Broadcast WebSocket event
                broadcast_websocket_event(WebSocketEvent.ROUTE_DELETED, {
                    'route_base': route_base,
                    'deleted_segments': len(segments)
                })
            else:
                self.send_json_response({'error': 'Not found'}, 404)

        except Exception as e:
            logger.error(f"Error handling DELETE request to {self.path}: {e}")
            try:
                self.send_json_response({'error': 'Internal server error'}, 500)
            except:
                # If we can't send error response, just pass
                pass


def record_crash():
    """Record a server crash for monitoring (but don't disable server)"""
    import time

    try:
        current_time = int(time.monotonic())

        # Get crash count - handle both real and mock params
        try:
            crash_count_str = params.get("BPWebServerCrashCount")
            crash_count = int(crash_count_str) if crash_count_str else 0
        except (AttributeError, TypeError):
            # Mock params or other error
            crash_count = 0

        # Get last crash time - handle both real and mock params
        try:
            last_crash_str = params.get("BPWebServerLastCrash")
            last_crash = int(last_crash_str) if last_crash_str else 0
        except (AttributeError, TypeError):
            # Mock params or other error
            last_crash = 0

        # Check if this is a recent consecutive crash
        if current_time - last_crash <= 30:  # Within 30 seconds
            crash_count += 1
        else:
            # Reset crash count if more than 30 seconds have passed
            crash_count = 1

        # Update crash tracking parameters for monitoring only
        try:
            params.put("BPWebServerCrashCount", min(crash_count, 10))  # Cap at 10
            params.put("BPWebServerLastCrash", current_time)
        except AttributeError:
            # Mock params object doesn't have put methods, skip
            pass

        logger.warning(f"Server error occurred ({crash_count} recent errors) - continuing operation")

    except Exception as e:
        logger.error(f"Error recording crash: {e}")

def check_and_handle_crashes():
    """Check server status but never disable it automatically"""
    import time

    try:
        # Get crash count for monitoring only - don't disable server
        try:
            crash_count_str = params.get("BPWebServerCrashCount")
            crash_count = int(crash_count_str) if crash_count_str else 0
        except (AttributeError, TypeError):
            crash_count = 0

        # Get last crash time for monitoring only
        try:
            last_crash_str = params.get("BPWebServerLastCrash")
            last_crash = int(last_crash_str) if last_crash_str else 0
        except (AttributeError, TypeError):
            last_crash = 0

        current_time = int(time.monotonic())

        # Log crash statistics for monitoring but don't disable server
        if crash_count > 0:
            logger.info(f"Server has experienced {crash_count} errors recently - continuing operation")

        # Reset crash count if server has been running stably for 10 minutes
        if crash_count > 0 and current_time - last_crash > 600:  # 10 minutes
            logger.info("Server running stably for 10 minutes, resetting error count")
            try:
                params.put("BPWebServerCrashCount", 0)
                params.put("BPWebServerLastCrash", 0)
            except AttributeError:
                # Mock params object doesn't have put methods, skip
                pass

        return True  # Always allow server to start

    except Exception as e:
        logger.error(f"Error checking crash status: {e}")
        return True  # Default to allowing server start

def ensure_dependencies():
    """Ensure all required dependencies are available"""
    global WEBSOCKETS_AVAILABLE

    try:
        # Check if websockets is available (direct import)
        try:
            import websockets
            WEBSOCKETS_AVAILABLE = True
            logger.info("websockets library is available")
            return
        except ImportError:
            WEBSOCKETS_AVAILABLE = False

        logger.info("websockets library not available")

        # Check if we're in a read-only environment (common in embedded systems)
        import os
        try:
            # Try to create a test file to check if filesystem is writable
            test_file = "/tmp/websockets_test"
            with open(test_file, 'w') as f:
                f.write("test")
            os.remove(test_file)
            can_write = True
        except (OSError, IOError):
            can_write = False

        if can_write:
            logger.info("Filesystem is writable, attempting automatic installation...")

            # Try to install websockets if missing
            try:
                import subprocess
                import shutil

                # First try uv sync (preferred method for this project)
                if shutil.which("uv"):
                    logger.info("Attempting to install websockets using uv...")
                    subprocess.check_call(["uv", "add", "websockets"])
                    logger.info("websockets installed successfully via uv")

                    # Check if it's now available
                    if check_websockets_available():
                        WEBSOCKETS_AVAILABLE = True
                        logger.info("WebSocket support enabled after uv installation")
                    else:
                        logger.warning("Installation completed but websockets still not available")
                        logger.info("You may need to restart the web server to use WebSocket features")
                else:
                    logger.info("uv not available, trying pip...")
                    subprocess.check_call([sys.executable, "-m", "pip", "install", "websockets"])
                    logger.info("websockets installed successfully via pip")

                    # Check if it's now available
                    if check_websockets_available():
                        WEBSOCKETS_AVAILABLE = True
                        logger.info("WebSocket support enabled after pip installation")
                    else:
                        logger.warning("Installation completed but websockets still not available")
                        logger.info("You may need to restart the web server to use WebSocket features")

            except (subprocess.CalledProcessError, ImportError) as e:
                logger.warning(f"Could not install websockets: {e}")
                logger.info("WebSocket features will be disabled, but HTTP API will still work")
                logger.info("To enable WebSocket support, run: uv sync --extra dev")
        else:
            logger.info("Read-only filesystem detected - cannot install automatically")
            logger.info("WebSocket features will be disabled, but HTTP API will still work")
            logger.info("To enable WebSocket support:")
            logger.info("  1. Run 'uv sync --extra dev' on a writable system")
            logger.info("  2. Or install manually: pip install websockets")
    except Exception as e:
        logger.warning(f"Error during dependency check: {e}")


def main():
    """Start the server"""
    port = int(params.get("BPWebServerPort") or "8088")

    logger.info(f"Starting BluePilot Web Routes Server on port {port}")
    logger.info(f"Routes directory: {ROUTES_DIR}")
    logger.info(f"Web app directory: {WEBAPP_DIR}")

    # Register cleanup handlers for graceful shutdown
    atexit.register(cleanup_on_shutdown)
    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGINT, signal_handler)
    logger.info("Registered graceful shutdown handlers")

    # Ensure dependencies are available before starting
    ensure_dependencies()

    # Kill any existing server instances first
    kill_existing_process('web_routes_server.py')

    # Enable all CPU cores for better FFmpeg performance
    enable_performance_mode()

    # Check if server should be disabled due to crashes
    # if not check_and_handle_crashes():
    #     logger.error("Server startup aborted due to excessive crashes")
    #     return

    # Initialize WebSocket broadcaster (will be set once loop is ready)
    global broadcaster

    # Start WebSocket server in separate thread if websockets is available
    try:
        import websockets
        websocket_thread = threading.Thread(
            target=start_websocket_server_thread,
            daemon=True,
            name="WebSocketServer"
        )
        websocket_thread.start()
        logger.info(f"WebSocket server thread started on port {WEBSOCKET_PORT}")

        # Give the thread a moment to start and set the loop
        time.sleep(0.5)

        # Initialize broadcaster with in-process WebSocket clients
        broadcaster = WebSocketBroadcaster(websocket_clients=websocket_clients, loop=loop)
        logger.info("WebSocket broadcaster initialized (in-process mode)")
    except ImportError:
        logger.info("WebSocket server not available - HTTP polling will be used")
        # Initialize broadcaster with HTTP fallback for cross-process communication
        broadcaster = WebSocketBroadcaster(http_fallback_port=port)
        logger.info("WebSocket broadcaster initialized (HTTP fallback mode)")
    except Exception as e:
        logger.error(f"Failed to start WebSocket server: {e}")
        logger.warning("Continuing without WebSocket support (HTTP polling will still work)")
        # Initialize broadcaster with HTTP fallback
        broadcaster = WebSocketBroadcaster(http_fallback_port=port)
        logger.info("WebSocket broadcaster initialized (HTTP fallback mode)")

    # Create HTTP server with socket reuse to prevent "Address already in use" errors
    try:
        server = ReuseAddressHTTPServer(('0.0.0.0', port), WebRoutesHandler)
        server.timeout = 30  # Set timeout to prevent hanging connections
    except OSError as e:
        if e.errno == 98:  # Address already in use
            logger.error(f"Port {port} is already in use. Another instance may be running.")
            logger.error("Try: pkill -f web_routes_server or reboot the device")
            return
        raise

    # Track previous onroad status for change detection
    last_onroad_status = None

    # Check periodically if server should stop and restore power save
    def check_stop_condition():
        try:
            global last_onroad_status

            # Check if we should restore power save mode
            check_and_restore_power_save()

            # Check for onroad status changes and broadcast via WebSocket
            current_onroad = is_onroad()
            if last_onroad_status is not None and current_onroad != last_onroad_status:
                status_str = 'onroad' if current_onroad else 'online'
                broadcast_websocket_event(WebSocketEvent.STATUS_CHANGED, {
                    'status': status_str,
                    'onroad': current_onroad
                })
                logger.info(f"Device status changed to: {status_str}")

            last_onroad_status = current_onroad

            return not should_server_run()
        except:
            return False

    # Start HTTP server with periodic stop checks
    try:
        logger.info("Web server starting - will run until disabled or onroad")

        # Override handle_timeout to check power save periodically
        original_handle_timeout = server.handle_timeout

        def custom_handle_timeout():
            check_and_restore_power_save()
            if original_handle_timeout:
                original_handle_timeout()

        server.handle_timeout = custom_handle_timeout

        server.serve_forever()
    except KeyboardInterrupt:
        logger.info("Server stopped by user")
        server.shutdown()
    except Exception as e:
        logger.error(f"Server error: {e}")
        # Record this error for monitoring but don't stop the server
        # record_crash()
        logger.info("Server continuing despite error...")
        # Don't shutdown or re-raise - keep server running


if __name__ == '__main__':
    main()
