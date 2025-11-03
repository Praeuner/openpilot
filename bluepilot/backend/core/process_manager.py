#!/usr/bin/env python3
"""
BluePilot Backend Process Manager
Process management and capacity control for FFmpeg and other background tasks
"""

import os
import time
import logging
import subprocess
import threading
from collections import defaultdict

logger = logging.getLogger(__name__)


def get_ffmpeg_background_capacity(max_concurrent, reserved_for_playback):
    """
    Maximum number of background FFmpeg jobs allowed while keeping playback responsive.

    Args:
        max_concurrent: Maximum concurrent FFmpeg processes
        reserved_for_playback: Slots to reserve for interactive playback

    Returns:
        int: Number of background FFmpeg slots available
    """
    return max(1, max_concurrent - reserved_for_playback)


def wait_for_ffmpeg_capacity(server_state, timeout=30.0, max_concurrent=3, reserved_for_playback=1):
    """
    Wait until enough FFmpeg capacity is available for a background task.

    Args:
        server_state: ServerState instance for tracking FFmpeg processes
        timeout: Maximum seconds to wait before giving up (None to wait indefinitely)
        max_concurrent: Maximum concurrent FFmpeg processes allowed
        reserved_for_playback: Slots to keep free for interactive playback

    Returns:
        bool: True if capacity became available, False if timed out
    """
    deadline = None if timeout is None else time.time() + timeout
    min_capacity = max(1, max_concurrent - reserved_for_playback)

    while True:
        current = server_state.get_ffmpeg_count()
        if current < min_capacity:
            return True

        if deadline and time.time() >= deadline:
            return False

        time.sleep(0.5)


# Rate limiting state
request_counter = defaultdict(list)
rate_limit_lock = threading.Lock()
onroad_request_timestamps = []

# Rate limiting constants (can be overridden)
MAX_REQUESTS_PER_MINUTE_OFFROAD = 120  # 120 requests per minute per IP when offroad
MAX_REQUESTS_PER_MINUTE_ONROAD = 6     # 6 requests per minute total when onroad (1 per 10s)
RATE_LIMIT_WINDOW = 60  # 1 minute window


def check_rate_limit(client_ip, is_onroad_func,
                     max_offroad=MAX_REQUESTS_PER_MINUTE_OFFROAD,
                     max_onroad=MAX_REQUESTS_PER_MINUTE_ONROAD,
                     window_seconds=RATE_LIMIT_WINDOW):
    """
    Check if client has exceeded rate limit

    Args:
        client_ip: Client IP address
        is_onroad_func: Function that returns True if vehicle is onroad
        max_offroad: Maximum requests per minute per IP when offroad
        max_onroad: Maximum requests per minute total when onroad
        window_seconds: Time window for rate limiting in seconds

    Returns:
        tuple: (is_allowed: bool, retry_after: int)
    """
    current_time = time.monotonic()
    onroad = is_onroad_func()

    with rate_limit_lock:
        if onroad:
            # Onroad: Global rate limit (all clients combined)
            global onroad_request_timestamps
            onroad_request_timestamps[:] = [t for t in onroad_request_timestamps
                                           if current_time - t < window_seconds]

            if len(onroad_request_timestamps) >= max_onroad:
                retry_after = int(window_seconds - (current_time - onroad_request_timestamps[0])) + 1
                return False, retry_after

            onroad_request_timestamps.append(current_time)
            return True, 0
        else:
            # Offroad: Per-IP rate limit
            timestamps = request_counter[client_ip]
            timestamps[:] = [t for t in timestamps if current_time - t < window_seconds]

            if len(timestamps) >= max_offroad:
                retry_after = int(window_seconds - (current_time - timestamps[0])) + 1
                return False, retry_after

            timestamps.append(current_time)
            return True, 0


def kill_port_process(port):
    """
    Kill any process using the specified port

    Args:
        port: Port number to check and clear

    Returns:
        bool: True if a process was killed, False otherwise
    """
    try:
        import psutil

        killed = False
        for proc in psutil.process_iter(['pid', 'name']):
            try:
                # Get inet connections (TCP/UDP), handle systems where this might fail
                try:
                    connections = proc.connections(kind='inet')
                except (AttributeError, NotImplementedError):
                    # Fallback to all connections on systems that don't support kind filter
                    connections = proc.connections()

                for conn in connections:
                    if hasattr(conn, 'laddr') and hasattr(conn.laddr, 'port') and conn.laddr.port == port:
                        logger.warning(f"Found process {proc.pid} ({proc.name()}) using port {port}")
                        logger.info(f"Killing process {proc.pid} to free port {port}")
                        proc.terminate()
                        try:
                            proc.wait(timeout=3)
                        except psutil.TimeoutExpired:
                            logger.warning(f"Process {proc.pid} didn't terminate, force killing")
                            proc.kill()
                        killed = True
                        logger.info(f"Successfully killed process {proc.pid}")
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                continue

        return killed

    except ImportError:
        # Fallback without psutil - try lsof and kill
        logger.warning("psutil not available, trying lsof fallback")
        try:
            result = subprocess.run(
                ['lsof', '-t', '-i', f':{port}'],
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0 and result.stdout.strip():
                pids = result.stdout.strip().split('\n')
                for pid in pids:
                    try:
                        logger.info(f"Killing process {pid} using port {port}")
                        subprocess.run(['kill', '-9', pid], timeout=2)
                        logger.info(f"Successfully killed process {pid}")
                    except Exception as e:
                        logger.warning(f"Failed to kill process {pid}: {e}")
                return True
        except (subprocess.TimeoutExpired, FileNotFoundError, Exception) as e:
            logger.warning(f"Could not kill port process using lsof: {e}")

    return False
