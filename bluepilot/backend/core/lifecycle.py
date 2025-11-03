#!/usr/bin/env python3
"""
BluePilot Backend Lifecycle Management
Server lifecycle functions including crash tracking and dependency management
"""

import time
import logging
import subprocess
import shutil

logger = logging.getLogger(__name__)


def record_crash(params):
    """
    Record a server crash for monitoring (but don't disable server)

    Args:
        params: Params instance for storing crash tracking data
    """
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


def check_and_handle_crashes(params):
    """
    Check server status but never disable it automatically

    Args:
        params: Params instance for checking crash tracking data

    Returns:
        bool: Always returns True (server always allowed to start)
    """
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
    """
    Check if all required dependencies are available, install if missing, and restart server

    Returns:
        bool: True if restart is needed (dependencies were installed), False otherwise
    """
    try:
        # Check if websockets is available (direct import)
        try:
            import websockets
            logger.info("websockets library is available")
            return False  # No restart needed
        except ImportError:
            pass

        logger.warning("websockets library not available - attempting installation")
        logger.info("HTTP API will still work during installation")

        # Try to install websockets package
        try:
            # Use uv pip install to avoid modifying pyproject.toml
            if shutil.which("uv"):
                logger.info("Installing websockets using uv pip (user site-packages)...")
                result = subprocess.run(
                    ["uv", "pip", "install", "websockets", "websocket-client"],
                    capture_output=True,
                    text=True,
                    timeout=60
                )

                if result.returncode == 0:
                    logger.info("websockets installed successfully")
                    logger.info("Server will restart in 3 seconds to enable WebSocket features")
                    return True  # Restart needed
                else:
                    logger.error(f"Failed to install websockets: {result.stderr}")
                    logger.info("WebSocket features will remain disabled")
                    return False
            else:
                logger.warning("uv not available - cannot install websockets automatically")
                logger.info("To enable WebSocket support, run: uv pip install websockets websocket-client")
                return False

        except subprocess.TimeoutExpired:
            logger.error("Package installation timed out (60s)")
            logger.info("Network may not be available yet. WebSocket features will remain disabled")
            return False
        except Exception as e:
            logger.error(f"Error during package installation: {e}")
            return False

    except Exception as e:
        logger.warning(f"Error during dependency check: {e}")
        return False
