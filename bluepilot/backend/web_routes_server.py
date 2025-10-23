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
from datetime import datetime, timedelta
from functools import lru_cache
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse
import logging
import re
import asyncio
import threading
from collections import defaultdict

# Configure logging early for import error handling
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(name)s:%(lineno)d - %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger(__name__)


# ============================================================================
# Custom Logging Handler to Capture Errors for Web Display
# ============================================================================
class ErrorBufferHandler(logging.Handler):
    """Custom logging handler that stores errors in ServerState for web retrieval"""

    def __init__(self, server_state):
        super().__init__(level=logging.WARNING)  # Capture WARNING and above
        self.server_state = server_state

    def emit(self, record):
        """Store log record in server state error buffer"""
        try:
            # Only log WARNING, ERROR, CRITICAL
            if record.levelno >= logging.WARNING:
                level = record.levelname
                message = record.getMessage()

                # Extract exception info if present
                exception_info = None
                if record.exc_info:
                    import traceback
                    exception_info = ''.join(traceback.format_exception(*record.exc_info))

                # Extract details from the log record
                details = {
                    'module': record.module,
                    'function': record.funcName,
                    'line': record.lineno,
                    'thread': record.thread,
                }

                self.server_state.log_error(level, message, details, exception_info)
        except Exception:
            # Don't let logging errors crash the server
            pass

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

# Import xattr for preserve marker support
try:
    from openpilot.system.loggerd.xattr_cache import getxattr, setxattr
    from openpilot.system.loggerd.deleter import PRESERVE_ATTR_NAME, PRESERVE_ATTR_VALUE
    import xattr as xattr_module
    XATTR_AVAILABLE = True
except ImportError:
    XATTR_AVAILABLE = False
    logger.warning("xattr not available - star/preserve functionality will be limited")

# Import disk space utilities
try:
    from openpilot.system.loggerd.config import get_available_bytes, get_available_percent
    from openpilot.system.loggerd.deleter import MIN_BYTES, MIN_PERCENT
    DISK_SPACE_UTILS_AVAILABLE = True
except ImportError:
    DISK_SPACE_UTILS_AVAILABLE = False
    logger.warning("Disk space utilities not available - will use fallback methods")
    MIN_BYTES = 5 * 1024 * 1024 * 1024  # 5 GB
    MIN_PERCENT = 10

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

# Cellular access configuration
CELLULAR_ACCESS_TIMEOUT_DEFAULT = 60  # 1 hour default timeout in minutes

# ============================================================================
# Thread-Safe State Management
# ============================================================================
class ServerState:
    """Thread-safe container for server state to prevent race conditions"""
    def __init__(self):
        self._lock = threading.RLock()
        self._cellular_access_enabled_time = None
        self._websocket_clients = set()
        self._websocket_loop = None
        self._broadcaster = None
        self._websocket_ready = threading.Event()  # Signal when WebSocket is ready
        self._ffmpeg_processes = {}  # Track by PID: {pid: {'start_time': ..., 'route': ...}}
        self._active_ffmpeg_count = 0
        self._error_log = []  # Circular buffer of recent errors
        self._max_errors = 50  # Keep last 50 errors
        self._server_start_time = time.time()

    def get_cellular_enabled_time(self):
        with self._lock:
            return self._cellular_access_enabled_time

    def set_cellular_enabled_time(self, value):
        with self._lock:
            self._cellular_access_enabled_time = value

    def add_websocket_client(self, client):
        with self._lock:
            self._websocket_clients.add(client)
            return len(self._websocket_clients)

    def remove_websocket_client(self, client):
        with self._lock:
            self._websocket_clients.discard(client)
            return len(self._websocket_clients)

    def get_websocket_clients(self):
        with self._lock:
            return list(self._websocket_clients)  # Return copy for safe iteration

    def set_websocket_loop(self, loop):
        with self._lock:
            self._websocket_loop = loop
            if loop:
                self._websocket_ready.set()

    def get_websocket_loop(self):
        with self._lock:
            return self._websocket_loop

    def wait_for_websocket(self, timeout=2.0):
        """Wait for WebSocket to be ready, return True if ready"""
        return self._websocket_ready.wait(timeout)

    def set_broadcaster(self, broadcaster):
        with self._lock:
            self._broadcaster = broadcaster

    def get_broadcaster(self):
        with self._lock:
            return self._broadcaster

    def register_ffmpeg_process(self, pid, route_info):
        """Register FFmpeg process with tracking info"""
        with self._lock:
            self._ffmpeg_processes[pid] = {
                'start_time': time.time(),
                'route': route_info
            }
            self._active_ffmpeg_count += 1
            return self._active_ffmpeg_count

    def unregister_ffmpeg_process(self, pid):
        """Unregister FFmpeg process, return remaining count"""
        with self._lock:
            if pid in self._ffmpeg_processes:
                del self._ffmpeg_processes[pid]
                self._active_ffmpeg_count = max(0, self._active_ffmpeg_count - 1)
            return self._active_ffmpeg_count

    def get_ffmpeg_count(self):
        with self._lock:
            return self._active_ffmpeg_count

    def get_ffmpeg_processes(self):
        """Get copy of FFmpeg process info for monitoring"""
        with self._lock:
            return dict(self._ffmpeg_processes)

    def log_error(self, level, message, details=None, exception_info=None):
        """Log an error to the circular buffer for later retrieval"""
        with self._lock:
            error_entry = {
                'timestamp': datetime.now().isoformat(),
                'unix_time': time.time(),
                'level': level,  # 'ERROR', 'WARNING', 'CRITICAL'
                'message': message,
                'details': details,
                'exception': exception_info
            }
            self._error_log.append(error_entry)

            # Keep only last N errors (circular buffer)
            if len(self._error_log) > self._max_errors:
                self._error_log = self._error_log[-self._max_errors:]

    def get_recent_errors(self, limit=None, level=None):
        """Get recent errors, optionally filtered by level"""
        with self._lock:
            errors = list(self._error_log)  # Copy

            # Filter by level if specified
            if level:
                errors = [e for e in errors if e['level'] == level]

            # Limit results
            if limit:
                errors = errors[-limit:]

            return errors

    def get_error_summary(self):
        """Get summary of errors by level"""
        with self._lock:
            summary = {'ERROR': 0, 'WARNING': 0, 'CRITICAL': 0, 'total': len(self._error_log)}
            for error in self._error_log:
                level = error.get('level', 'ERROR')
                summary[level] = summary.get(level, 0) + 1
            return summary

    def get_server_uptime(self):
        """Get server uptime in seconds"""
        return time.time() - self._server_start_time

    def clear_error_log(self):
        """Clear the error log"""
        with self._lock:
            self._error_log = []

# Global server state instance
server_state = ServerState()

# Attach error buffer handler to logger after server_state is created
_error_handler = ErrorBufferHandler(server_state)
logger.addHandler(_error_handler)

# Legacy global variables (kept for compatibility, but use server_state internally)
cellular_access_enabled_time = None  # Deprecated: use server_state
websocket_clients = set()  # Deprecated: use server_state
loop = None  # Deprecated: use server_state
broadcaster = None  # Deprecated: use server_state


def broadcast_websocket_event(event_type, data=None):
    """Broadcast event to all connected WebSocket clients (thread-safe)"""
    broadcaster = server_state.get_broadcaster()
    if broadcaster:
        try:
            broadcaster.broadcast(event_type, data)
        except Exception as e:
            logger.error(f"Error broadcasting WebSocket event {event_type}: {e}")


async def websocket_handler(websocket):
    """Handle WebSocket connections (thread-safe)"""
    try:
        import websockets
    except ImportError:
        logger.error("websockets not available in handler")
        return

    try:
        # Thread-safe: Add client to active connections
        client_count = server_state.add_websocket_client(websocket)
        logger.info(f"WebSocket client connected. Total clients: {client_count}")

        # Send initial status
        try:
            initial_status = {
                'type': 'connection_established',
                'timestamp': datetime.now().isoformat(),
                'data': {
                    'status': 'online',
                    'routes_count': 0
                }
            }
            await websocket.send(json.dumps(initial_status))
        except Exception as e:
            logger.warning(f"Failed to send initial status: {e}")

        # Keep connection alive with heartbeat
        while True:
            try:
                await asyncio.sleep(10.0)
                heartbeat = {
                    'type': 'heartbeat',
                    'timestamp': datetime.now().isoformat(),
                    'data': {}
                }
                await websocket.send(json.dumps(heartbeat))
            except (websockets.exceptions.ConnectionClosed, ConnectionResetError, BrokenPipeError) as e:
                logger.debug(f"WebSocket connection closed: {e}")
                break
            except Exception as e:
                logger.error(f"Unexpected error in WebSocket heartbeat: {e}")
                break

    except Exception as e:
        logger.error(f"WebSocket handler error: {e}", exc_info=True)
    finally:
        # Thread-safe: Remove client from active connections
        client_count = server_state.remove_websocket_client(websocket)
        logger.info(f"WebSocket client disconnected. Remaining clients: {client_count}")


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
    """Start WebSocket server in a separate thread (thread-safe)"""
    try:
        import websockets
    except ImportError:
        logger.warning("WebSocket server thread not started - websockets library not available")
        return

    try:
        # Create new event loop for this thread
        ws_loop = asyncio.new_event_loop()
        asyncio.set_event_loop(ws_loop)

        # Register loop with server state (thread-safe)
        server_state.set_websocket_loop(ws_loop)
        logger.info("WebSocket event loop registered")

        # Start the WebSocket server
        ws_loop.run_until_complete(start_websocket_server())
    except Exception as e:
        logger.error(f"WebSocket server thread error: {e}", exc_info=True)
    finally:
        try:
            ws_loop = server_state.get_websocket_loop()
            if ws_loop:
                ws_loop.close()
        except Exception as e:
            logger.debug(f"Error closing WebSocket loop: {e}")


# ============================================================================
# Atomic File Operations
# ============================================================================
def atomic_write(filepath, content, mode='w'):
    """
    Write file atomically to prevent corruption from crashes.
    Writes to temp file first, then renames.

    Args:
        filepath: Target file path
        content: Content to write (str or bytes)
        mode: Write mode ('w' for text, 'wb' for binary)

    Returns:
        bool: True if successful, False otherwise
    """
    try:
        # Create directory if needed
        os.makedirs(os.path.dirname(filepath), exist_ok=True)

        # Write to temp file first
        temp_fd, temp_path = tempfile.mkstemp(
            dir=os.path.dirname(filepath),
            prefix='.tmp_',
            suffix=os.path.basename(filepath)
        )

        try:
            if 'b' in mode:
                os.write(temp_fd, content if isinstance(content, bytes) else content.encode())
            else:
                os.write(temp_fd, content.encode() if isinstance(content, str) else content)
            os.close(temp_fd)

            # Atomic rename (overwrites target on POSIX)
            os.replace(temp_path, filepath)
            return True

        except Exception as e:
            os.close(temp_fd)
            if os.path.exists(temp_path):
                os.remove(temp_path)
            raise e

    except Exception as e:
        logger.error(f"Atomic write failed for {filepath}: {e}")
        return False


def safe_json_write(filepath, data):
    """Write JSON file atomically"""
    try:
        json_str = json.dumps(data, indent=2)
        return atomic_write(filepath, json_str, mode='w')
    except Exception as e:
        logger.error(f"JSON write failed for {filepath}: {e}")
        return False


# ============================================================================
# FFmpeg Process Manager
# ============================================================================
class FFmpegProcess:
    """Context manager for FFmpeg processes with guaranteed cleanup"""

    def __init__(self, route_info, max_concurrent=3):
        self.route_info = route_info
        self.max_concurrent = max_concurrent
        self.process = None
        self.pid = None

    def __enter__(self):
        # Check if we can start another process
        current_count = server_state.get_ffmpeg_count()
        if current_count >= self.max_concurrent:
            raise RuntimeError(
                f"Too many FFmpeg processes ({current_count}/{self.max_concurrent}). "
                "Please wait and try again."
            )
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Guaranteed cleanup - always runs"""
        if self.process:
            try:
                # Try graceful termination first
                if self.process.poll() is None:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        # Force kill if graceful termination fails
                        logger.warning(f"FFmpeg process {self.pid} did not terminate gracefully, force killing")
                        self.process.kill()
                        self.process.wait()
            except Exception as e:
                logger.error(f"Error cleaning up FFmpeg process {self.pid}: {e}")
            finally:
                # Always unregister
                if self.pid:
                    remaining = server_state.unregister_ffmpeg_process(self.pid)
                    logger.info(f"FFmpeg process {self.pid} cleaned up. Remaining: {remaining}")
        return False  # Don't suppress exceptions

    def start(self, cmd):
        """Start FFmpeg process and register it"""
        try:
            self.process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                bufsize=8192
            )
            self.pid = self.process.pid

            # Register with server state
            count = server_state.register_ffmpeg_process(self.pid, self.route_info)
            logger.info(f"Started FFmpeg process {self.pid} for {self.route_info}. Active: {count}")

            return self.process

        except Exception as e:
            logger.error(f"Failed to start FFmpeg: {e}")
            raise


# ============================================================================
# Segment Prefetch System
# ============================================================================
def remux_segment_to_cache(hevc_path, route_base, segment_num, camera):
    """
    Remux a single segment to cache without streaming to client.
    Used for background prefetching.

    Returns True if successful, False otherwise.
    """
    if not os.path.exists(hevc_path):
        return False

    cache_filename = f"{route_base}_{segment_num}_{camera}.mp4"
    cache_path = os.path.join(REMUX_CACHE, cache_filename)

    # Check if already cached
    if os.path.exists(cache_path):
        cache_mtime = os.path.getmtime(cache_path)
        source_mtime = os.path.getmtime(hevc_path)
        if cache_mtime >= source_mtime:
            logger.debug(f"Prefetch: {cache_filename} already cached")
            return True

    # Check disk space
    source_size = os.path.getsize(hevc_path)
    estimated_output_size = source_size * 2
    cache_dir = "/data" if os.path.exists("/data") else os.path.expanduser("~")

    if not has_sufficient_disk_space(estimated_output_size, cache_dir, min_free_gb=0.5):
        logger.debug(f"Prefetch: Insufficient disk space for {cache_filename}")
        return False

    # Remux to cache using FFmpeg
    logger.info(f"Prefetch: Remuxing {cache_filename} to cache")

    route_info = f"prefetch:{route_base}:{segment_num}:{camera}"
    try:
        with FFmpegProcess(route_info, max_concurrent=MAX_CONCURRENT_FFMPEG) as ffmpeg_mgr:
            cmd = [
                'ffmpeg',
                '-f', 'hevc',
                '-r', '20',
                '-i', hevc_path,
                '-c', 'copy',
                '-movflags', 'frag_keyframe+empty_moov+faststart+default_base_moof',
                '-fflags', '+genpts',
                '-avoid_negative_ts', 'make_zero',
                '-bsf:v', 'hevc_mp4toannexb',
                '-f', 'mp4',
                cache_path
            ]

            process = ffmpeg_mgr.start(cmd)

            # Wait for completion (background process)
            try:
                process.wait(timeout=60)  # 60 second timeout for prefetch
            except subprocess.TimeoutExpired:
                logger.warning(f"Prefetch timeout for {cache_filename}")
                return False

            if process.returncode == 0:
                logger.info(f"Prefetch: Successfully cached {cache_filename}")
                return True
            else:
                stderr = process.stderr.read().decode('utf-8', errors='ignore')
                logger.warning(f"Prefetch: FFmpeg failed for {cache_filename}: {stderr[:200]}")
                # Clean up incomplete file
                if os.path.exists(cache_path):
                    try:
                        os.remove(cache_path)
                    except OSError:
                        pass
                return False

    except RuntimeError as e:
        # Too many concurrent processes - this is expected, just skip prefetch
        logger.debug(f"Prefetch: Skipping {cache_filename} - {e}")
        return False
    except Exception as e:
        logger.warning(f"Prefetch: Error remuxing {cache_filename}: {e}")
        return False


def _prefetch_worker(route_base, current_segment, camera, segments_to_prefetch=2):
    """
    Background worker to prefetch upcoming segments.
    Called in a daemon thread.
    """
    logger.debug(f"Prefetch worker starting for {route_base} segment {current_segment} camera {camera}")

    try:
        # Get all segments for this route
        segments = get_route_segments(route_base)
        if not segments:
            return

        # Find segments after current
        future_segments = [s for s in segments if s['segment'] > current_segment]
        future_segments.sort(key=lambda x: x['segment'])

        # Prefetch next N segments
        prefetched = 0
        for seg in future_segments[:segments_to_prefetch]:
            segment_num = seg['segment']
            segment_path = seg['path']

            # Map camera to filename
            camera_files = {
                'front': 'fcamera.hevc',
                'wide': 'ecamera.hevc',
                'driver': 'dcamera.hevc'
            }

            if camera not in camera_files:
                continue

            hevc_path = os.path.join(segment_path, camera_files[camera])

            if remux_segment_to_cache(hevc_path, route_base, segment_num, camera):
                prefetched += 1
            else:
                # If one fails (e.g., too many processes), stop trying
                break

        logger.info(f"Prefetch: Completed {prefetched}/{segments_to_prefetch} segments for {route_base}:{camera}")

    except Exception as e:
        logger.warning(f"Prefetch worker error: {e}", exc_info=True)


def prefetch_next_segments(route_base, current_segment, camera, count=2):
    """
    Trigger background prefetch of upcoming segments.
    Non-blocking - spawns a daemon thread.

    Args:
        route_base: Route base name
        current_segment: Current segment number being played
        camera: Camera type ('front', 'wide', 'driver')
        count: Number of segments to prefetch (default: 2)
    """
    thread = threading.Thread(
        target=_prefetch_worker,
        args=(route_base, current_segment, camera, count),
        daemon=True,
        name=f"prefetch-{route_base}-{current_segment}-{camera}"
    )
    thread.start()
    logger.debug(f"Triggered prefetch for {route_base} segment {current_segment}+{count} camera {camera}")


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

    # FFmpeg process info (thread-safe)
    metrics['ffmpeg']['active_processes'] = server_state.get_ffmpeg_count()
    metrics['ffmpeg']['max_processes'] = MAX_CONCURRENT_FFMPEG
    metrics['ffmpeg']['process_details'] = server_state.get_ffmpeg_processes()

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
        logger.error(f"Error scanning cache: {e}", exc_info=True)
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
                logger.error(f"Error removing expired file {filepath}: {e}", exc_info=True)

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

# FFmpeg configuration
MAX_CONCURRENT_FFMPEG = 3  # Maximum concurrent FFmpeg processes

# Rate limiting (prevent abuse)
request_counter = defaultdict(list)
rate_limit_lock = threading.Lock()
MAX_REQUESTS_PER_MINUTE_OFFROAD = 120  # 120 requests per minute per IP when offroad
MAX_REQUESTS_PER_MINUTE_ONROAD = 6     # 6 requests per minute total when onroad (1 per 10s)
RATE_LIMIT_WINDOW = 60  # 1 minute window

# Global onroad request tracking
onroad_request_timestamps = []


def check_rate_limit(client_ip):
    """Check if client has exceeded rate limit

    Args:
        client_ip: Client IP address

    Returns:
        tuple: (is_allowed: bool, retry_after: int)
    """
    current_time = time.monotonic()
    onroad = is_onroad()

    with rate_limit_lock:
        if onroad:
            # Onroad: Global rate limit (all clients combined)
            global onroad_request_timestamps
            onroad_request_timestamps[:] = [t for t in onroad_request_timestamps
                                           if current_time - t < RATE_LIMIT_WINDOW]

            if len(onroad_request_timestamps) >= MAX_REQUESTS_PER_MINUTE_ONROAD:
                retry_after = int(RATE_LIMIT_WINDOW - (current_time - onroad_request_timestamps[0])) + 1
                return False, retry_after

            onroad_request_timestamps.append(current_time)
            return True, 0
        else:
            # Offroad: Per-IP rate limit
            timestamps = request_counter[client_ip]
            timestamps[:] = [t for t in timestamps if current_time - t < RATE_LIMIT_WINDOW]

            if len(timestamps) >= MAX_REQUESTS_PER_MINUTE_OFFROAD:
                retry_after = int(RATE_LIMIT_WINDOW - (current_time - timestamps[0])) + 1
                return False, retry_after

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
    """Check if idle and restore power save mode (only when offroad)"""
    global last_activity_time
    import time

    if last_activity_time is None:
        return

    # CRITICAL: Never disable cores when onroad or about to go onroad
    # Check onroad status BEFORE checking idle time
    if is_onroad():
        # Device is onroad, keep cores enabled
        last_activity_time = None  # Reset so we don't keep trying
        return

    idle_time = time.time() - last_activity_time
    if idle_time > IDLE_TIMEOUT_SECONDS:
        restore_power_save()
        last_activity_time = None  # Reset so we don't keep trying


def cleanup_on_shutdown():
    """Critical cleanup on server shutdown - thread-safe"""
    logger.info("Server shutting down - performing cleanup...")

    # IMPORTANT: Do NOT disable CPU cores on shutdown!
    # The web server shuts down when:
    # 1. Going onroad (cores MUST stay enabled for openpilot processes)
    # 2. User disabled the server (cores should stay as-is)
    # 3. Device shutdown (doesn't matter, system is shutting down anyway)

    logger.info("Leaving CPU cores in current state (not disabling on shutdown)")

    # Kill any remaining FFmpeg processes tracked by server state
    ffmpeg_count = server_state.get_ffmpeg_count()
    if ffmpeg_count > 0:
        logger.warning(f"Killing {ffmpeg_count} remaining FFmpeg processes")
        ffmpeg_processes = server_state.get_ffmpeg_processes()

        # Try to kill specific tracked processes first
        for pid, info in ffmpeg_processes.items():
            try:
                import psutil
                proc = psutil.Process(pid)
                proc.terminate()
                try:
                    proc.wait(timeout=1)
                except psutil.TimeoutExpired:
                    proc.kill()
                logger.info(f"Killed FFmpeg process {pid} ({info['route']})")
            except (psutil.NoSuchProcess, ImportError):
                pass
            except Exception as e:
                logger.debug(f"Error killing process {pid}: {e}")

        # Fallback: pkill any remaining ffmpeg processes
        try:
            subprocess.run(['pkill', '-9', 'ffmpeg'], timeout=2, capture_output=True)
        except Exception as e:
            logger.debug(f"Error running pkill: {e}")

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
    """Check if server should be running (always runs when enabled, rate-limited onroad)"""
    try:
        return params.get_bool("BPWebServerEnabled")
    except:
        return True  # Default to running if we can't check

def get_wifi_ip():
    """Get WiFi interface IP address (wlan0 on Comma devices)"""
    try:
        import netifaces
        for iface in netifaces.interfaces():
            if iface.startswith('wlan'):
                addrs = netifaces.ifaddresses(iface)
                if netifaces.AF_INET in addrs:
                    for addr in addrs[netifaces.AF_INET]:
                        ip = addr.get('addr')
                        if ip and not ip.startswith('127.'):
                            return ip
    except ImportError:
        # Fallback without netifaces
        import subprocess
        try:
            result = subprocess.run(['ip', 'addr', 'show', 'wlan0'],
                                    capture_output=True, text=True, timeout=2)
            for line in result.stdout.split('\n'):
                if 'inet ' in line:
                    ip = line.strip().split()[1].split('/')[0]
                    return ip
        except:
            pass
    return None

def get_connection_type():
    """Determine current network connection type"""
    try:
        import subprocess
        # Check which interface is being used for default route
        result = subprocess.run(['ip', 'route', 'get', '8.8.8.8'],
                                capture_output=True, text=True, timeout=2)
        output = result.stdout.lower()

        if 'wlan' in output:
            return 'wifi'
        elif 'rmnet' in output or 'ccmni' in output:
            return 'cellular'
        elif 'eth' in output:
            return 'ethernet'
        else:
            return 'unknown'
    except:
        return 'unknown'

def is_cellular_access_allowed():
    """Check if cellular access is currently allowed (within timeout window) - thread-safe"""
    try:
        # Check if cellular access is enabled
        cellular_enabled = params.get_bool("BPWebServerAllowCellular")

        if not cellular_enabled:
            server_state.set_cellular_enabled_time(None)
            return False

        # Get timeout duration from params (in minutes)
        timeout_str = params.get("BPWebServerCellularTimeoutMinutes")
        if timeout_str:
            try:
                timeout_minutes = int(timeout_str)
                if timeout_minutes <= 0:
                    timeout_minutes = CELLULAR_ACCESS_TIMEOUT_DEFAULT
            except (ValueError, TypeError):
                timeout_minutes = CELLULAR_ACCESS_TIMEOUT_DEFAULT
        else:
            timeout_minutes = CELLULAR_ACCESS_TIMEOUT_DEFAULT

        # Thread-safe: Get current enabled time
        cellular_access_enabled_time = server_state.get_cellular_enabled_time()

        # If this is first time enabled, record timestamp
        if cellular_access_enabled_time is None:
            server_state.set_cellular_enabled_time(time.time())
            logger.info(f"Cellular access enabled with {timeout_minutes} minute timeout")
            return True

        # Check if timeout has expired
        elapsed_minutes = (time.time() - cellular_access_enabled_time) / 60
        if elapsed_minutes >= timeout_minutes:
            # Timeout expired, auto-disable
            logger.warning(f"Cellular access timeout expired after {timeout_minutes} minutes - disabling")
            params.put_bool("BPWebServerAllowCellular", False)
            server_state.set_cellular_enabled_time(None)

            # Broadcast status change
            broadcast_websocket_event(WebSocketEvent.STATUS_CHANGED, {
                'cellular_access': False,
                'reason': 'timeout_expired',
                'message': f'Cellular access auto-disabled after {timeout_minutes} minutes'
            })
            return False

        # Still within timeout window
        remaining_minutes = int(timeout_minutes - elapsed_minutes)
        logger.debug(f"Cellular access active, {remaining_minutes} minutes remaining")
        return True

    except Exception as e:
        logger.error(f"Error checking cellular access: {e}", exc_info=True)
        return False

def get_cellular_access_status():
    """Get detailed cellular access status for API responses - thread-safe"""
    enabled = params.get_bool("BPWebServerAllowCellular")
    cellular_access_enabled_time = server_state.get_cellular_enabled_time()

    if not enabled or cellular_access_enabled_time is None:
        return {
            'enabled': False,
            'active': False,
            'time_remaining_minutes': 0,
            'timeout_minutes': CELLULAR_ACCESS_TIMEOUT_DEFAULT
        }

    # Get timeout
    timeout_str = params.get("BPWebServerCellularTimeoutMinutes")
    if timeout_str:
        try:
            timeout_minutes = int(timeout_str)
            if timeout_minutes <= 0:
                timeout_minutes = CELLULAR_ACCESS_TIMEOUT_DEFAULT
        except (ValueError, TypeError):
            timeout_minutes = CELLULAR_ACCESS_TIMEOUT_DEFAULT
    else:
        timeout_minutes = CELLULAR_ACCESS_TIMEOUT_DEFAULT

    # Calculate remaining time
    elapsed_minutes = (time.time() - cellular_access_enabled_time) / 60
    remaining_minutes = max(0, int(timeout_minutes - elapsed_minutes))

    return {
        'enabled': enabled,
        'active': is_cellular_access_allowed(),
        'time_remaining_minutes': remaining_minutes,
        'timeout_minutes': timeout_minutes,
        'enabled_at': cellular_access_enabled_time
    }


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


def get_disk_space_info():
    """Get current disk space status with safety thresholds

    Returns dict with:
    - available_bytes: Free space in bytes
    - available_percent: Free space as percentage
    - total_bytes: Total disk space
    - is_low: True if below safety thresholds
    - can_record: True if enough space to record next route
    - warning_level: 'critical', 'low', 'medium', or 'ok'
    """
    try:
        if DISK_SPACE_UTILS_AVAILABLE:
            available_bytes = get_available_bytes(default=MIN_BYTES + 1)
            available_percent = get_available_percent(default=MIN_PERCENT + 1)
        else:
            # Fallback using standard library
            stat = os.statvfs(ROUTES_DIR)
            available_bytes = stat.f_bavail * stat.f_frsize
            total_bytes = stat.f_blocks * stat.f_frsize
            available_percent = (available_bytes / total_bytes * 100) if total_bytes > 0 else 0

        # Calculate total bytes for display
        stat = os.statvfs(ROUTES_DIR)
        total_bytes = stat.f_blocks * stat.f_frsize

        # Check if below safety thresholds
        is_low = available_bytes < MIN_BYTES or available_percent < MIN_PERCENT

        # Estimate space needed for next route
        # Typical route: ~60 segments * ~100MB/segment = ~6GB for 1 hour
        # Let's require 2x the minimum (10GB) to safely record next route
        SAFE_RECORDING_BYTES = MIN_BYTES * 2  # 10 GB
        can_record = available_bytes >= SAFE_RECORDING_BYTES

        # Determine warning level
        if available_bytes < MIN_BYTES or available_percent < MIN_PERCENT:
            warning_level = 'critical'  # At cleanup threshold
        elif available_bytes < SAFE_RECORDING_BYTES:
            warning_level = 'low'  # Below safe recording threshold
        elif available_bytes < (MIN_BYTES * 3):  # 15 GB
            warning_level = 'medium'  # Getting low
        else:
            warning_level = 'ok'

        return {
            'available_bytes': available_bytes,
            'available_percent': round(available_percent, 1),
            'total_bytes': total_bytes,
            'used_bytes': total_bytes - available_bytes,
            'is_low': is_low,
            'can_record': can_record,
            'warning_level': warning_level,
            'formatted': {
                'available': format_size(available_bytes),
                'total': format_size(total_bytes),
                'used': format_size(total_bytes - available_bytes)
            }
        }
    except Exception as e:
        logger.error(f"Error getting disk space info: {e}")
        # Return safe defaults on error
        return {
            'available_bytes': MIN_BYTES + 1,
            'available_percent': MIN_PERCENT + 1,
            'total_bytes': 0,
            'used_bytes': 0,
            'is_low': False,
            'can_record': True,
            'warning_level': 'unknown',
            'formatted': {
                'available': 'Unknown',
                'total': 'Unknown',
                'used': 'Unknown'
            }
        }


def check_route_preserve_status(route_base):
    """Check if a route has the preserve xattr set

    Returns True if ANY segment of the route has user.preserve xattr
    """
    if not XATTR_AVAILABLE:
        # Fallback to old .star file method
        star_file = os.path.join(ROUTES_DIR, route_base, '.star')
        return os.path.exists(star_file)

    try:
        segments = get_route_segments(route_base)
        for seg in segments:
            try:
                attr_value = getxattr(seg['path'], PRESERVE_ATTR_NAME)
                if attr_value == PRESERVE_ATTR_VALUE:
                    return True
            except Exception:
                continue
        return False
    except Exception as e:
        logger.debug(f"Error checking preserve status for {route_base}: {e}")
        return False


def set_route_preserve(route_base, preserve=True):
    """Set or remove preserve xattr on all segments of a route

    Args:
        route_base: Route base name (e.g., "2025-10-22--14-30-15")
        preserve: True to preserve (star), False to unpreserve (unstar)

    Returns:
        dict with success status and message
    """
    if not XATTR_AVAILABLE:
        return {
            'success': False,
            'error': 'xattr support not available',
            'fallback': 'star_file'
        }

    try:
        segments = get_route_segments(route_base)
        if not segments:
            return {
                'success': False,
                'error': 'Route not found'
            }

        # Before preserving, check disk space
        if preserve:
            disk_info = get_disk_space_info()

            # Don't allow starring if we can't safely record the next route
            if not disk_info['can_record']:
                return {
                    'success': False,
                    'error': 'Insufficient disk space to preserve route',
                    'details': f"Need {format_size(MIN_BYTES * 2)} free to safely record next drive. Currently: {disk_info['formatted']['available']}",
                    'disk_space': disk_info,
                    'hint': 'Delete some routes first to free up space'
                }

            # Warn if disk space is getting low
            if disk_info['warning_level'] in ('low', 'medium'):
                logger.warning(f"Preserving route {route_base} with {disk_info['warning_level']} disk space: {disk_info['formatted']['available']} free")

        # Set or remove preserve xattr on all segments
        affected_segments = 0
        for seg in segments:
            try:
                if preserve:
                    setxattr(seg['path'], PRESERVE_ATTR_NAME, PRESERVE_ATTR_VALUE)
                    affected_segments += 1
                else:
                    # Remove xattr
                    try:
                        xattr_module.removexattr(seg['path'], PRESERVE_ATTR_NAME)
                        affected_segments += 1
                    except OSError:
                        # Attribute doesn't exist - that's fine
                        pass
            except Exception as e:
                logger.warning(f"Error setting preserve on {seg['name']}: {e}")

        # Clear cache so next scan picks up the change
        get_route_segments.cache_clear()

        action = 'preserved' if preserve else 'unpreserved'
        return {
            'success': True,
            'message': f'Route {action} successfully',
            'affected_segments': affected_segments,
            'total_segments': len(segments)
        }

    except Exception as e:
        logger.error(f"Error setting preserve on route {route_base}: {e}", exc_info=True)
        return {
            'success': False,
            'error': str(e)
        }


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


def get_log_files(segment_path):
    """Get available log files in segment"""
    logs = {}
    log_types = {
        'qlog.zst': 'qlog',
        'rlog.zst': 'rlog'
    }

    for filename, log_type in log_types.items():
        filepath = os.path.join(segment_path, filename)
        if os.path.exists(filepath):
            logs[log_type] = {
                'filename': filename,
                'path': filepath,
                'size': os.path.getsize(filepath)
            }

    return logs


def extract_log_messages(log_path, search_query=None, level_filter=None, max_messages=500):
    """Extract cloudlog messages from qlog/rlog file

    Args:
        log_path: Path to qlog.zst or rlog.zst file
        search_query: Optional search string to filter messages (case-insensitive)
        level_filter: Optional level filter ('info', 'warning', 'error', 'all')
        max_messages: Maximum number of messages to return

    Returns:
        dict with:
            - messages: List of log messages with timestamps
            - total_count: Total number of log messages found
            - start_time: First message timestamp (seconds)
            - end_time: Last message timestamp (seconds)
    """
    try:
        import zstandard as zstd

        # Add cereal to path if needed
        sys.path.append(os.path.join(os.path.dirname(__file__), "../.."))
        from cereal import log as capnp_log

        # Read and decompress log file
        with open(log_path, 'rb') as f:
            compressed_data = f.read()

        dctx = zstd.ZstdDecompressor()
        with dctx.stream_reader(compressed_data) as reader:
            decompressed_data = reader.read()

        # Parse capnp events
        events_reader = capnp_log.Event.read_multiple_bytes(decompressed_data)

        log_messages = []
        first_time = None
        last_time = None
        total_log_count = 0

        for event in events_reader:
            try:
                event_type = event.which()

                # Only process log messages
                if event_type not in ('logMessage', 'errorLogMessage'):
                    continue

                total_log_count += 1
                event_time = event.logMonoTime

                # Track first and last timestamps
                if first_time is None:
                    first_time = event_time
                last_time = event_time

                # Determine log level and message
                if event_type == 'errorLogMessage':
                    level = 'error'
                    message = event.errorLogMessage
                else:
                    message = event.logMessage

                    # Try to parse as JSON to get structured log data
                    level = 'info'  # Default to info
                    try:
                        import json
                        log_data = json.loads(message)

                        # Extract level from JSON structure
                        if isinstance(log_data, dict) and 'level' in log_data:
                            json_level = log_data['level'].upper()
                            if json_level in ('ERROR', 'CRITICAL', 'FATAL'):
                                level = 'error'
                            elif json_level in ('WARNING', 'WARN'):
                                level = 'warning'
                            elif json_level in ('INFO', 'DEBUG'):
                                level = 'info'
                            else:
                                # Unknown level, default to info
                                level = 'info'
                        else:
                            # JSON but no level field, default to info
                            level = 'info'

                    except (json.JSONDecodeError, ValueError, AttributeError, TypeError):
                        # Not JSON or JSON parsing failed
                        # Only use keyword detection for non-JSON messages
                        # Don't search in JSON strings to avoid false positives
                        if not message.strip().startswith('{'):
                            message_upper = message.upper()
                            if any(keyword in message_upper for keyword in ['ERROR', 'FATAL', 'CRITICAL', 'EXCEPTION', 'FAILED']):
                                level = 'error'
                            elif any(keyword in message_upper for keyword in ['WARN', 'WARNING', 'CAUTION']):
                                level = 'warning'
                        # Otherwise leave as default 'info'

                # Apply level filter
                if level_filter and level_filter != 'all':
                    if level != level_filter:
                        continue

                # Apply search filter
                if search_query:
                    if search_query.lower() not in message.lower():
                        continue

                # Stop if we've reached max messages
                if len(log_messages) >= max_messages:
                    break

                log_messages.append({
                    'timestamp': event_time / 1e9,  # Convert to seconds
                    'level': level,
                    'message': message
                })

            except Exception as e:
                logger.debug(f"Error parsing log event: {e}")
                continue

        return {
            'success': True,
            'messages': log_messages,
            'total_count': total_log_count,
            'returned_count': len(log_messages),
            'start_time': first_time / 1e9 if first_time else None,
            'end_time': last_time / 1e9 if last_time else None,
            'truncated': len(log_messages) >= max_messages
        }

    except ImportError as e:
        logger.exception("Missing required module for log parsing")
        return {
            'success': False,
            'error': f'Missing required module: {str(e)}. Try: pip install zstandard pycapnp',
            'messages': [],
            'total_count': 0,
            'returned_count': 0
        }
    except Exception as e:
        logger.exception(f"Error parsing log file {log_path}")
        import traceback
        return {
            'success': False,
            'error': str(e),
            'traceback': traceback.format_exc(),
            'messages': [],
            'total_count': 0,
            'returned_count': 0
        }


def extract_cereal_messages(log_path, message_type, max_messages=1000):
    """Extract specific cereal message type from qlog/rlog file

    Args:
        log_path: Path to qlog.zst or rlog.zst file
        message_type: Cereal message type to extract (e.g., 'carState', 'controlsState')
        max_messages: Maximum number of messages to return

    Returns:
        dict with:
            - messages: List of cereal messages with timestamps and data
            - message_type: The requested message type
            - total_count: Total number of messages found
            - start_time: First message timestamp (seconds)
            - end_time: Last message timestamp (seconds)
    """
    try:
        import zstandard as zstd

        # Add cereal to path if needed
        sys.path.append(os.path.join(os.path.dirname(__file__), "../.."))
        from cereal import log as capnp_log

        # Read and decompress log file
        with open(log_path, 'rb') as f:
            compressed_data = f.read()

        dctx = zstd.ZstdDecompressor()
        with dctx.stream_reader(compressed_data) as reader:
            decompressed_data = reader.read()

        # Parse capnp events
        events_reader = capnp_log.Event.read_multiple_bytes(decompressed_data)

        cereal_messages = []
        first_time = None
        last_time = None
        total_count = 0

        for event in events_reader:
            try:
                event_type = event.which()

                # Only process the requested message type
                if event_type != message_type:
                    continue

                total_count += 1
                event_time = event.logMonoTime

                # Track first and last timestamps
                if first_time is None:
                    first_time = event_time
                last_time = event_time

                # Stop if we've reached max messages
                if len(cereal_messages) >= max_messages:
                    break

                # Extract message data
                try:
                    event_obj = getattr(event, event_type)
                    message_data = serialize_cereal_object(event_obj)

                    cereal_messages.append({
                        'timestamp': event_time / 1e9,  # Convert to seconds
                        'data': message_data
                    })

                except Exception as e:
                    logger.debug(f"Could not serialize message {event_type}: {e}")
                    continue

            except Exception as e:
                logger.debug(f"Error parsing cereal event: {e}")
                continue

        return {
            'success': True,
            'messages': cereal_messages,
            'message_type': message_type,
            'total_count': total_count,
            'returned_count': len(cereal_messages),
            'start_time': first_time / 1e9 if first_time else None,
            'end_time': last_time / 1e9 if last_time else None,
            'truncated': len(cereal_messages) >= max_messages
        }

    except ImportError as e:
        logger.exception("Missing required module for cereal parsing")
        return {
            'success': False,
            'error': f'Missing required module: {str(e)}. Try: pip install zstandard pycapnp',
            'messages': [],
            'message_type': message_type,
            'total_count': 0,
            'returned_count': 0
        }
    except Exception as e:
        logger.exception(f"Error parsing cereal file {log_path}")
        import traceback
        return {
            'success': False,
            'error': str(e),
            'traceback': traceback.format_exc(),
            'messages': [],
            'message_type': message_type,
            'total_count': 0,
            'returned_count': 0
        }


def serialize_cereal_object(obj, depth=0):
    """Recursively serialize a cereal object to a dict"""
    # Prevent infinite recursion
    if depth > 10:
        return str(obj)

    if obj is None:
        return None

    # Handle primitives
    if isinstance(obj, (bool, int, float, str, bytes)):
        return obj

    # Handle lists/tuples
    if isinstance(obj, (list, tuple)):
        return [serialize_cereal_object(item, depth + 1) for item in obj]

    # Try to serialize capnp struct by extracting fields from to_dict() if available
    try:
        # Many capnp structs have a to_dict() method
        if hasattr(obj, 'to_dict') and callable(obj.to_dict):
            return obj.to_dict()
    except Exception:
        pass

    # Handle capnp structs manually
    result = {}
    try:
        # Check if it has a schema (capnp struct)
        if hasattr(obj, 'schema'):
            schema = obj.schema

            # Get all non-union fields
            try:
                for field in schema.non_union_fields:
                    try:
                        value = getattr(obj, field.name)
                        result[field.name] = serialize_cereal_object(value, depth + 1)
                    except Exception as e:
                        logger.debug(f"Could not serialize field {field.name}: {e}")
                        result[field.name] = None
            except Exception as e:
                logger.debug(f"Error iterating non_union_fields: {e}")

            # Handle union fields
            try:
                which_field = obj.which()
                if which_field:
                    value = getattr(obj, which_field)
                    result[which_field] = serialize_cereal_object(value, depth + 1)
            except Exception:
                pass

            # If we got fields, return them
            if result:
                return result

        # Try alternative approach: use dir() to find all attributes
        # This is a fallback for capnp objects that don't have schema.non_union_fields
        if hasattr(obj, '__dir__'):
            for attr_name in dir(obj):
                # Skip private/magic methods and common capnp internals
                if attr_name.startswith('_') or attr_name in ('schema', 'which', 'to_bytes', 'from_bytes', 'as_builder', 'total_size'):
                    continue

                try:
                    attr_value = getattr(obj, attr_name)
                    # Skip methods
                    if callable(attr_value):
                        continue
                    result[attr_name] = serialize_cereal_object(attr_value, depth + 1)
                except Exception:
                    continue

            if result:
                return result

    except Exception as e:
        logger.debug(f"Error serializing capnp object: {e}")

    # Last resort fallback
    try:
        if hasattr(obj, '__dict__'):
            return {k: serialize_cereal_object(v, depth + 1) for k, v in obj.__dict__.items()}
    except Exception:
        pass

    # Absolute last resort: return string representation
    return str(obj)


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

        # Check if route is preserved (starred) via xattr
        is_starred = check_route_preserve_status(base_name)

        # Calculate duration (1 minute per segment)
        duration_seconds = len(segments) * 60
        hours = duration_seconds // 3600
        minutes = (duration_seconds % 3600) // 60
        if hours > 0:
            duration_str = f"{hours}h {minutes}m"
        else:
            duration_str = f"{minutes}m"

        # Calculate end time from last segment's timestamp
        # Parse the last segment name to get its timestamp
        last_segment = segments[-1]
        last_segment_dt = parse_route_datetime(last_segment['name'].rsplit('--', 1)[0])
        if last_segment_dt:
            # Add 1 minute to account for the segment's duration
            end_dt = last_segment_dt + timedelta(minutes=1)
        else:
            # Fallback: use start time + duration if parsing fails
            end_dt = route_dt + timedelta(seconds=duration_seconds)

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
            'displayEndTime': format_time_12hr(end_dt),
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
        """Send JSON response with consistent error format"""
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_cors_headers()
        self.end_headers()

        # Add timestamp to all responses for debugging
        if 'timestamp' not in data:
            data['timestamp'] = datetime.now().isoformat()

        # Ensure error responses have consistent format
        if status >= 400 and 'error' in data:
            if 'success' not in data:
                data['success'] = False
        elif status < 400:
            if 'success' not in data and 'error' not in data:
                data['success'] = True

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

                # Trigger prefetch of next segments (non-blocking)
                prefetch_next_segments(route_base, segment_num, camera, count=2)
                return

        # Check cache size and cleanup if needed
        try:
            cleanup_old_cache()
        except Exception as e:
            logger.warning(f"Cache cleanup failed: {e}")

        # Check disk space before remuxing (estimate 2x source file size)
        source_size = os.path.getsize(hevc_path)
        estimated_output_size = source_size * 2  # Conservative estimate
        cache_dir = "/data" if os.path.exists("/data") else os.path.expanduser("~")

        if not has_sufficient_disk_space(estimated_output_size, cache_dir, min_free_gb=0.5):
            free_space = get_free_disk_space(cache_dir)
            free_gb = free_space / (1024**3) if free_space else 0
            logger.error(f"Insufficient disk space to remux {cache_filename}: {free_gb:.1f}GB free")
            self.send_json_response({
                'error': 'Insufficient disk space for video processing',
                'details': f'Only {free_gb:.1f}GB free, need ~{estimated_output_size/(1024**3):.1f}GB',
                'hint': 'Clear cache via settings or delete old routes',
                'free_space_gb': round(free_gb, 1),
                'required_gb': round(estimated_output_size/(1024**3), 1)
            }, 507)  # HTTP 507 Insufficient Storage
            return

        # Enable performance mode for fast remuxing
        enable_performance_mode()

        # Remux using FFmpeg with progressive streaming - use context manager for guaranteed cleanup
        logger.info(f"Remuxing HEVC to MP4 (streaming): {cache_filename}")

        # Use FFmpegProcess context manager for guaranteed cleanup
        route_info = f"{route_base}:{segment_num}:{camera}"
        try:
            with FFmpegProcess(route_info, max_concurrent=MAX_CONCURRENT_FFMPEG) as ffmpeg_mgr:
                # Use FFmpeg to remux raw HEVC to MP4 container
                # Stream to stdout while also writing to cache file
                cmd = [
                    'ffmpeg',
                    '-f', 'hevc',
                    '-r', '20',  # Comma camera framerate (20 fps)
                    '-i', hevc_path,
                    '-c', 'copy',
                    '-movflags', 'frag_keyframe+empty_moov+faststart+default_base_moof',
                    '-fflags', '+genpts',  # Generate missing PTS if needed
                    '-avoid_negative_ts', 'make_zero',  # Handle negative timestamps
                    '-bsf:v', 'hevc_mp4toannexb',  # Convert HEVC bitstream for better compatibility
                    '-f', 'mp4',
                    '-'  # Output to stdout
                ]

                # Start FFmpeg process using context manager
                process = ffmpeg_mgr.start(cmd)

                # Send HTTP headers
                self.send_response(200)
                self.send_header('Content-Type', 'video/mp4')
                self.send_header('Accept-Ranges', 'bytes')
                self.send_cors_headers()
                self.end_headers()

                # Trigger prefetch of next segments (non-blocking, before streaming)
                # This ensures prefetch starts while current segment is being sent
                prefetch_next_segments(route_base, segment_num, camera, count=2)

                # Stream output while also saving to cache
                with open(cache_path, 'wb') as cache_file:
                    while True:
                        chunk = process.stdout.read(8192)
                        if not chunk:
                            break

                        # Send to client
                        try:
                            self.wfile.write(chunk)
                        except (BrokenPipeError, ConnectionResetError) as e:
                            logger.warning(f"Client disconnected during streaming: {e}")
                            process.kill()
                            try:
                                process.wait(timeout=2)
                            except subprocess.TimeoutExpired:
                                pass  # Context manager will handle cleanup
                            break

                        # Save to cache
                        cache_file.write(chunk)

                # Wait for process to complete
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    logger.warning("FFmpeg process timeout, context manager will handle cleanup")

                # Check result
                if process.returncode != 0:
                    stderr = process.stderr.read().decode('utf-8', errors='ignore')
                    logger.error(f"FFmpeg remux failed (exit {process.returncode}): {stderr[:500]}")
                    # Clean up incomplete cache file
                    if os.path.exists(cache_path):
                        try:
                            os.remove(cache_path)
                        except OSError as e:
                            logger.debug(f"Error removing incomplete cache file: {e}")
                else:
                    logger.info(f"Remux successful: {cache_filename}")

        except RuntimeError as e:
            # Too many concurrent processes
            current_count = server_state.get_ffmpeg_count()
            logger.warning(f"Cannot start FFmpeg: {e}")
            self.send_json_response({
                'error': 'Server busy processing other videos',
                'details': f'{current_count} of {MAX_CONCURRENT_FFMPEG} video streams in use',
                'hint': 'Please wait a moment and try again',
                'active_processes': current_count,
                'max_processes': MAX_CONCURRENT_FFMPEG,
                'retry_after': 5  # Suggest 5 second retry
            }, 503)
            return

        except FileNotFoundError:
            logger.error("FFmpeg not found - install with: apt-get install ffmpeg")
            self.send_json_response({
                'error': 'FFmpeg not installed on server',
                'details': 'Video conversion tool is not available',
                'hint': 'Contact system administrator or install FFmpeg',
                'technical_hint': 'apt-get install ffmpeg'
            }, 500)
            return

        except Exception as e:
            logger.error(f"Unexpected error remuxing {hevc_path}: {e}", exc_info=True)
            # Clean up incomplete cache file
            if os.path.exists(cache_path):
                try:
                    os.remove(cache_path)
                except OSError as cleanup_err:
                    logger.debug(f"Error cleaning up cache file: {cleanup_err}")

            # Try to serve raw HEVC as fallback
            logger.warning(f"Remuxing failed, attempting to serve raw HEVC as fallback")
            try:
                self.send_file_response(hevc_path, 'video/mp4; codecs="hev1"')
                return
            except Exception as fallback_error:
                logger.error(f"Fallback to raw HEVC also failed: {fallback_error}")
                self.send_json_response({
                    'error': 'Video conversion failed',
                    'details': str(e),
                    'hint': 'Try refreshing the page or selecting a different segment',
                    'fallback_attempted': True,
                    'fallback_error': str(fallback_error)
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
                onroad = is_onroad()
                limit = MAX_REQUESTS_PER_MINUTE_ONROAD if onroad else MAX_REQUESTS_PER_MINUTE_OFFROAD

                self.send_response(429)  # Too Many Requests
                self.send_header('Retry-After', str(retry_after))
                self.send_cors_headers()
                self.end_headers()

                error_msg = json.dumps({
                    'success': False,
                    'error': 'Rate limit exceeded',
                    'details': f'Too many requests from {client_ip}',
                    'retry_after_seconds': retry_after,
                    'limit': f'{limit} requests per minute',
                    'hint': f'Please wait {retry_after} seconds before trying again',
                    'reason': 'onroad_protection' if onroad else 'rate_limit',
                    'timestamp': datetime.now().isoformat()
                }).encode()
                self.wfile.write(error_msg)
                return

            # Check if server is enabled (always run when enabled, just rate-limited onroad)
            if not should_server_run():
                self.send_json_response({
                    'error': 'Server disabled',
                    'details': 'Web routes server is currently disabled',
                    'hint': 'Enable server in settings to access routes'
                }, 503)
                return

            # Onroad: Block ALL interactions except status endpoints
            # The frontend shows a full-page overlay to prevent any interactions
            onroad = is_onroad()
            if onroad and path.startswith('/api/'):
                # Allow only status endpoints to check onroad state and show overlay
                status_only_endpoints = ['/api/status', '/api/health']
                is_status_check = any(path.startswith(ep) for ep in status_only_endpoints)

                if not is_status_check:
                    # Block ALL operations when onroad (including routes list and video playback)
                    self.send_json_response({
                        'error': 'All operations disabled while driving for safety',
                        'onroad': True,
                        'readonly_mode': False,
                        'hint': 'Park the vehicle to access routes and videos'
                    }, 403)
                    return

            # Route handlers
            if path == '/' or path == '/index.html':
                self.send_file_response(str(WEBAPP_DIR / 'index.html'), 'text/html')

            elif path == '/api/health':
                # Health check endpoint for monitoring
                try:
                    onroad = is_onroad()
                    ffmpeg_count = server_state.get_ffmpeg_count()
                    ws_clients = len(server_state.get_websocket_clients())
                    error_summary = server_state.get_error_summary()
                    uptime = server_state.get_server_uptime()

                    # Server is unhealthy if there are recent CRITICAL errors
                    recent_critical = [e for e in server_state.get_recent_errors(limit=10)
                                      if e['level'] == 'CRITICAL']
                    is_healthy = len(recent_critical) == 0

                    self.send_json_response({
                        'healthy': is_healthy,
                        'status': 'onroad' if onroad else 'online',
                        'onroad': onroad,
                        'server_enabled': should_server_run(),
                        'uptime_seconds': int(uptime),
                        'ffmpeg_available_slots': MAX_CONCURRENT_FFMPEG - ffmpeg_count,
                        'websocket_clients': ws_clients,
                        'errors': {
                            'total': error_summary['total'],
                            'critical': error_summary.get('CRITICAL', 0),
                            'errors': error_summary.get('ERROR', 0),
                            'warnings': error_summary.get('WARNING', 0),
                            'has_recent_critical': len(recent_critical) > 0
                        }
                    })
                except Exception as e:
                    logger.error(f"Health check failed: {e}", exc_info=True)
                    self.send_json_response({
                        'healthy': False,
                        'error': str(e)
                    }, 500)

            elif path == '/api/status':
                # Basic status endpoint (lightweight, always available)
                onroad = is_onroad()
                self.send_json_response({
                    'status': 'onroad' if onroad else 'online',
                    'onroad': onroad,
                    'routes_dir': ROUTES_DIR,
                    'routes_dir_exists': os.path.exists(ROUTES_DIR),
                    'isMetric': params.get_bool("IsMetric")
                })

            elif path == '/api/status/detailed':
                # Detailed status endpoint with connection info
                try:
                    onroad = is_onroad()
                    connection_type = get_connection_type()
                    wifi_ip = get_wifi_ip()
                    cellular_status = get_cellular_access_status()

                    # Server uptime
                    try:
                        import psutil
                        process = psutil.Process(os.getpid())
                        uptime_seconds = time.time() - process.create_time()
                    except Exception as e:
                        logger.debug(f"Could not get uptime: {e}")
                        uptime_seconds = int(server_state.get_server_uptime())

                    # WebSocket client count (thread-safe)
                    ws_clients = len(server_state.get_websocket_clients())

                    # FFmpeg info (thread-safe)
                    ffmpeg_count = server_state.get_ffmpeg_count()

                    # Rate limit info
                    current_limit = MAX_REQUESTS_PER_MINUTE_ONROAD if onroad else MAX_REQUESTS_PER_MINUTE_OFFROAD

                    self.send_json_response({
                    'status': 'onroad' if onroad else 'online',
                    'onroad': onroad,
                    'server': {
                        'uptime_seconds': int(uptime_seconds),
                        'version': '1.0.0',
                        'python_version': sys.version.split()[0]
                    },
                    'network': {
                        'connection_type': connection_type,
                        'wifi_ip': wifi_ip,
                        'wifi_available': wifi_ip is not None,
                        'port': int(params.get("BPWebServerPort") or "8088")
                    },
                    'cellular_access': cellular_status,
                    'clients': {
                        'websocket_connected': ws_clients,
                        'websocket_available': WEBSOCKETS_AVAILABLE
                    },
                    'rate_limit': {
                        'requests_per_minute': current_limit,
                        'window_seconds': RATE_LIMIT_WINDOW,
                        'mode': 'global' if onroad else 'per_ip'
                    },
                    'ffmpeg': {
                        'active_processes': ffmpeg_count,
                        'max_processes': MAX_CONCURRENT_FFMPEG,
                        'available_slots': MAX_CONCURRENT_FFMPEG - ffmpeg_count,
                        'utilization_percent': int((ffmpeg_count / MAX_CONCURRENT_FFMPEG) * 100) if MAX_CONCURRENT_FFMPEG > 0 else 0
                    },
                    'features': {
                        'read_only': onroad,  # Onroad = read-only mode
                        'write_operations_enabled': not onroad,
                        'websocket_enabled': WEBSOCKETS_AVAILABLE
                    }
                    })
                except Exception as e:
                    logger.error(f"Error in /api/status/detailed: {e}", exc_info=True)
                    self.send_json_response({
                        'success': False,
                        'error': 'Failed to get detailed status',
                        'details': str(e)
                    }, 500)

            elif path == '/api/logs':
                # Error logs endpoint for debugging
                try:
                    # Parse query parameters for filtering
                    query = urlparse(self.path).query
                    params_dict = dict(param.split('=') for param in query.split('&') if '=' in param) if query else {}

                    limit = int(params_dict.get('limit', 20))
                    level = params_dict.get('level', None)  # 'ERROR', 'WARNING', 'CRITICAL'

                    errors = server_state.get_recent_errors(limit=limit, level=level)
                    error_summary = server_state.get_error_summary()

                    self.send_json_response({
                        'success': True,
                        'logs': errors,
                        'summary': error_summary,
                        'server_uptime': int(server_state.get_server_uptime())
                    })
                except Exception as e:
                    logger.error(f"Error retrieving logs: {e}", exc_info=True)
                    self.send_json_response({
                        'success': False,
                        'error': 'Failed to retrieve logs',
                        'details': str(e)
                    }, 500)

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

                # Calculate end time from last segment's timestamp
                # Parse the last segment name to get its timestamp
                last_segment = segments[-1]
                last_segment_dt = parse_route_datetime(last_segment['name'].rsplit('--', 1)[0])
                if last_segment_dt:
                    # Add 1 minute to account for the segment's duration
                    end_dt = last_segment_dt + timedelta(minutes=1)
                else:
                    # Fallback: use start time + duration if parsing fails
                    end_dt = route_dt + timedelta(seconds=duration_seconds)

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
                    'displayEndTime': format_time_12hr(end_dt),
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
                disk_info = get_disk_space_info()
                self.send_json_response({
                    'success': True,
                    'routes': routes,
                    'total': len(routes),
                    'disk_space': disk_info
                })

            elif path == '/api/disk-space':
                # Get current disk space status
                disk_info = get_disk_space_info()
                self.send_json_response({
                    'success': True,
                    'disk_space': disk_info
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

            elif path.startswith('/api/logs/'):
                # Log messages endpoint: /api/logs/{route_base}/{segment}/{log_type}
                # Query params: search, level (info/warning/error/all), max
                parts = path.split('/')[3:]  # Skip '', 'api', 'logs'
                if len(parts) < 3:
                    self.send_json_response({'error': 'Invalid log path. Format: /api/logs/{route}/{segment}/{qlog|rlog}'}, 400)
                    return

                route_base = parts[0]
                try:
                    segment_num = int(parts[1])
                except ValueError:
                    self.send_json_response({'error': 'Invalid segment number'}, 400)
                    return

                log_type = parts[2].lower()
                if log_type not in ('qlog', 'rlog'):
                    self.send_json_response({'error': 'Invalid log type. Use qlog or rlog'}, 400)
                    return

                # Get segment data
                segments = get_route_segments(route_base)
                segment_data = next((s for s in segments if s['segment'] == segment_num), None)

                if not segment_data:
                    self.send_json_response({'error': 'Segment not found'}, 404)
                    return

                # Map log type to filename
                log_files = {
                    'qlog': 'qlog.zst',
                    'rlog': 'rlog.zst'
                }

                log_path = os.path.join(segment_data['path'], log_files[log_type])

                if not os.path.exists(log_path):
                    self.send_json_response({
                        'success': False,
                        'error': f'{log_type} file not found for this segment'
                    }, 404)
                    return

                # Parse query parameters
                from urllib.parse import parse_qs
                query_params = parse_qs(parsed.query)
                search_query = query_params.get('search', [None])[0]
                level_filter = query_params.get('level', ['all'])[0]
                try:
                    max_messages = int(query_params.get('max', [500])[0])
                    max_messages = min(max_messages, 5000)  # Cap at 5000 for safety
                except ValueError:
                    max_messages = 500

                # Extract log messages
                result = extract_log_messages(log_path, search_query, level_filter, max_messages)
                self.send_json_response(result)

            elif path.startswith('/api/cereal/'):
                # Cereal data endpoint: /api/cereal/{route_base}/{segment}/{log_type}/{message_type}
                parts = path.split('/')[3:]  # Skip '', 'api', 'cereal'
                if len(parts) < 4:
                    self.send_json_response({'error': 'Invalid cereal path. Format: /api/cereal/{route}/{segment}/{qlog|rlog}/{message_type}'}, 400)
                    return

                route_base = parts[0]
                try:
                    segment_num = int(parts[1])
                except ValueError:
                    self.send_json_response({'error': 'Invalid segment number'}, 400)
                    return

                log_type = parts[2].lower()
                if log_type not in ('qlog', 'rlog'):
                    self.send_json_response({'error': 'Invalid log type. Use qlog or rlog'}, 400)
                    return

                message_type = parts[3]

                # Get segment data
                segments = get_route_segments(route_base)
                segment_data = next((s for s in segments if s['segment'] == segment_num), None)

                if not segment_data:
                    self.send_json_response({'error': 'Segment not found'}, 404)
                    return

                # Map log type to filename
                log_files = {
                    'qlog': 'qlog.zst',
                    'rlog': 'rlog.zst'
                }

                log_path = os.path.join(segment_data['path'], log_files[log_type])

                if not os.path.exists(log_path):
                    self.send_json_response({
                        'success': False,
                        'error': f'{log_type} file not found for this segment'
                    }, 404)
                    return

                # Extract cereal messages
                result = extract_cereal_messages(log_path, message_type)
                self.send_json_response(result)

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

        except (BrokenPipeError, ConnectionResetError) as e:
            # Client disconnected - don't log as error
            logger.debug(f"Client disconnected during GET {self.path}: {e}")
        except Exception as e:
            logger.error(f"Error handling GET request to {self.path}: {e}", exc_info=True)
            try:
                self.send_json_response({'error': 'Internal server error', 'details': str(e)}, 500)
            except Exception:
                # Connection broken, can't send response
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
                        broadcaster_instance = server_state.get_broadcaster()
                        if broadcaster_instance:
                            broadcaster_instance.broadcast(event_data.get('type'), event_data.get('data'))

                    self.send_json_response({'success': True})
                except Exception as e:
                    logger.exception("Error handling internal broadcast")
                    self.send_json_response({
                        'error': 'Broadcast failed',
                        'details': str(e)
                    }, 500)
                return

            # Check if server should be running
            if not should_server_run():
                self.send_json_response({
                    'error': 'Server disabled',
                    'details': 'Web routes server is currently disabled',
                    'hint': 'Enable server in settings'
                }, 503)
                return

            if is_onroad():
                self.send_json_response({
                    'error': 'Operation not allowed while driving',
                    'details': 'Write operations are disabled for safety while vehicle is in motion',
                    'hint': 'Park the vehicle to modify routes',
                    'reason': 'safety'
                }, 503)
                return

            if path.startswith('/api/star/'):
                # Star/unstar a route using preserve xattr with disk space checking
                route_base = path.split('/api/star/')[1].strip('/')

                # Check current preserve status
                currently_starred = check_route_preserve_status(route_base)

                # Toggle star status
                preserve = not currently_starred

                # Set or remove preserve xattr (includes disk space checks)
                result = set_route_preserve(route_base, preserve)

                if result['success']:
                    is_starred = preserve

                    # Include disk space info in response
                    disk_info = get_disk_space_info()

                    self.send_json_response({
                        'success': True,
                        'isStarred': is_starred,
                        'affected_segments': result.get('affected_segments', 0),
                        'message': result.get('message'),
                        'disk_space': disk_info
                    })

                    # Broadcast WebSocket event
                    event_type = WebSocketEvent.ROUTE_STARRED if is_starred else WebSocketEvent.ROUTE_UNSTARRED
                    broadcast_websocket_event(event_type, {
                        'route_base': route_base,
                        'is_starred': is_starred,
                        'disk_space': disk_info
                    })
                else:
                    # Return error from set_route_preserve
                    self.send_json_response(result, 400 if 'disk space' in result.get('error', '').lower() else 500)
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

        except (BrokenPipeError, ConnectionResetError) as e:
            logger.debug(f"Client disconnected during POST {self.path}: {e}")
        except Exception as e:
            logger.error(f"Error handling POST request to {self.path}: {e}", exc_info=True)
            try:
                self.send_json_response({'error': 'Internal server error', 'details': str(e)}, 500)
            except Exception:
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
                self.send_json_response({
                    'error': 'Server disabled',
                    'details': 'Web routes server is currently disabled',
                    'hint': 'Enable server in settings'
                }, 503)
                return

            if is_onroad():
                self.send_json_response({
                    'error': 'Operation not allowed while driving',
                    'details': 'Delete operations are disabled for safety while vehicle is in motion',
                    'hint': 'Park the vehicle to delete routes',
                    'reason': 'safety'
                }, 503)
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

        except (BrokenPipeError, ConnectionResetError) as e:
            logger.debug(f"Client disconnected during DELETE {self.path}: {e}")
        except Exception as e:
            logger.error(f"Error handling DELETE request to {self.path}: {e}", exc_info=True)
            try:
                self.send_json_response({'error': 'Internal server error', 'details': str(e)}, 500)
            except Exception:
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
                    try:
                        import websockets
                        WEBSOCKETS_AVAILABLE = True
                        logger.info("WebSocket support enabled after uv installation")
                    except ImportError:
                        logger.warning("Installation completed but websockets still not available")
                        logger.info("You may need to restart the web server to use WebSocket features")
                else:
                    logger.info("uv not available, trying pip...")
                    subprocess.check_call([sys.executable, "-m", "pip", "install", "websockets"])
                    logger.info("websockets installed successfully via pip")

                    # Check if it's now available
                    try:
                        import websockets
                        WEBSOCKETS_AVAILABLE = True
                        logger.info("WebSocket support enabled after pip installation")
                    except ImportError:
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

        # Wait for WebSocket to be ready (max 2 seconds)
        if server_state.wait_for_websocket(timeout=2.0):
            logger.info("WebSocket event loop ready")
            # Get WebSocket clients and loop from server state
            ws_clients = server_state.get_websocket_clients()
            ws_loop = server_state.get_websocket_loop()

            # Initialize broadcaster with in-process WebSocket support
            broadcaster_instance = WebSocketBroadcaster(websocket_clients=set(ws_clients), loop=ws_loop)
            server_state.set_broadcaster(broadcaster_instance)
            logger.info("WebSocket broadcaster initialized (in-process mode)")
        else:
            logger.warning("WebSocket not ready after 2 seconds, using HTTP fallback")
            broadcaster_instance = WebSocketBroadcaster(http_fallback_port=port)
            server_state.set_broadcaster(broadcaster_instance)
            logger.info("WebSocket broadcaster initialized (HTTP fallback mode)")

    except ImportError:
        logger.info("WebSocket server not available - HTTP polling will be used")
        broadcaster_instance = WebSocketBroadcaster(http_fallback_port=port)
        server_state.set_broadcaster(broadcaster_instance)
        logger.info("WebSocket broadcaster initialized (HTTP fallback mode)")

    except Exception as e:
        logger.error(f"Failed to start WebSocket server: {e}", exc_info=True)
        logger.warning("Continuing without WebSocket support (HTTP polling will still work)")
        broadcaster_instance = WebSocketBroadcaster(http_fallback_port=port)
        server_state.set_broadcaster(broadcaster_instance)
        logger.info("WebSocket broadcaster initialized (HTTP fallback mode)")

    # Determine bind address based on WiFi availability and cellular access settings
    wifi_ip = get_wifi_ip()
    cellular_allowed = is_cellular_access_allowed()

    if cellular_allowed:
        # Cellular access explicitly enabled - bind to all interfaces
        bind_address = '0.0.0.0'
        cellular_status = get_cellular_access_status()
        logger.warning("=" * 60)
        logger.warning("CELLULAR ACCESS ENABLED")
        logger.warning(f"Server will be accessible over cellular networks!")
        logger.warning(f"Time remaining: {cellular_status['time_remaining_minutes']} minutes")
        logger.warning(f"Timeout: {cellular_status['timeout_minutes']} minutes")
        logger.warning("This may use significant cellular data!")
        logger.warning("=" * 60)
        if wifi_ip:
            logger.info(f"WiFi also available at: {wifi_ip}")
    elif wifi_ip:
        # WiFi available, bind only to WiFi interface (secure default)
        bind_address = wifi_ip
        logger.info(f"Binding to WiFi interface: {wifi_ip} (cellular access disabled)")
    else:
        # No WiFi and cellular not allowed - bind to all with warning
        bind_address = '0.0.0.0'
        logger.warning("WiFi interface not found - binding to all interfaces (0.0.0.0)")
        logger.warning("To enable cellular access, set BPWebServerAllowCellular param")

    # Create HTTP server with socket reuse to prevent "Address already in use" errors
    try:
        server = ReuseAddressHTTPServer((bind_address, port), WebRoutesHandler)
        server.timeout = 30  # Set timeout to prevent hanging connections
    except OSError as e:
        if e.errno == 98:  # Address already in use
            logger.error(f"Port {port} is already in use on {bind_address}. Another instance may be running.")
            logger.error("Try: pkill -f web_routes_server or reboot the device")
            return
        raise

    # Track previous onroad status for change detection
    last_onroad_status = [None]  # Use list for closure modification

    # Periodic status monitoring (no longer stops server)
    def monitor_status():
        try:
            # Check if we should restore power save mode
            check_and_restore_power_save()

            # Check cellular access timeout
            is_cellular_access_allowed()  # This checks and auto-disables if expired

            # Check for onroad status changes and broadcast via WebSocket
            current_onroad = is_onroad()
            if last_onroad_status[0] is not None and current_onroad != last_onroad_status[0]:
                status_str = 'onroad' if current_onroad else 'online'
                broadcast_websocket_event(WebSocketEvent.STATUS_CHANGED, {
                    'status': status_str,
                    'onroad': current_onroad
                })
                logger.info(f"Device status changed to: {status_str}")

            last_onroad_status[0] = current_onroad
        except Exception as e:
            logger.error(f"Error in status monitor: {e}")

    # Start HTTP server - runs continuously until disabled or terminated
    try:
        logger.info(f"Web server starting on {bind_address}:{port}")
        logger.info("Server will run continuously (rate-limited when onroad)")

        # Override handle_timeout to monitor status and power save periodically
        original_handle_timeout = server.handle_timeout

        def custom_handle_timeout():
            monitor_status()  # Check onroad status and broadcast changes
            if original_handle_timeout:
                original_handle_timeout()

        server.handle_timeout = custom_handle_timeout

        server.serve_forever()  # Run indefinitely until process killed
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
