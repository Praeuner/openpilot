#!/usr/bin/env python3
"""
BluePilot Backend Params Manager
Safe operations for reading and writing openpilot parameters
"""

import os
import logging
from typing import Dict, List, Optional, Any, Union

logger = logging.getLogger(__name__)

# Import Params with fallback for direct file reading
PARAMS_DIR = "/data/params/d"
USE_DIRECT_FILE_READING = False

try:
    from openpilot.common.params import Params
    logger.info("Successfully imported openpilot.common.params.Params")
except ImportError as e:
    logger.warning(f"Failed to import openpilot Params: {e}. Using direct file reading fallback.")
    USE_DIRECT_FILE_READING = True

    # Fallback Params class that reads directly from filesystem
    class Params:
        def __init__(self, params_dir=PARAMS_DIR):
            self.params_dir = params_dir

        def _read_file(self, key):
            """Read param file directly"""
            try:
                param_path = os.path.join(self.params_dir, key)
                if not os.path.exists(param_path):
                    return None
                with open(param_path, 'rb') as f:
                    return f.read()
            except Exception as e:
                logger.debug(f"Error reading param file {key}: {e}")
                return None

        def get_bool(self, key):
            """Get boolean param"""
            value = self._read_file(key)
            if value is None:
                return False
            # openpilot stores bools as "1" or "0"
            return value == b"1"

        def get(self, key):
            """Get param as bytes"""
            value = self._read_file(key)
            if value is None:
                return b""
            return value

        def put(self, key, value):
            """Write param to file"""
            try:
                param_path = os.path.join(self.params_dir, key)
                if isinstance(value, str):
                    value = value.encode('utf-8')
                with open(param_path, 'wb') as f:
                    f.write(value)
                return 0
            except Exception as e:
                logger.error(f"Error writing param {key}: {e}")
                return -1

        def put_bool(self, key, value):
            """Write boolean param"""
            return self.put(key, "1" if value else "0")


# Critical params that should not be modified through the web interface
READONLY_PARAMS = {
    "DongleId",
    "GitCommit",
    "Version",
    "GitBranch",
    "GitRemote",
    "Updated",
    "Passive",
}

# Params that require extra warnings before modification
CRITICAL_PARAMS = {
    "DisableLogging",
    "DisableUpdates",
    "OpenpilotEnabledToggle",
    "LongitudinalPersonality",
}

# Categorized params for better organization
PARAM_CATEGORIES = {
    "system": {
        "name": "System",
        "description": "Core system parameters",
        "params": [
            "DongleId", "Version", "GitCommit", "GitBranch", "Updated",
            "IsOnRoad", "IsOffroad", "Passive"
        ]
    },
    "bluepilot": {
        "name": "BluePilot",
        "description": "BluePilot-specific settings",
        "params": [
            "BPWebServerEnabled", "BPWebServerPort", "BPWebServerCellularAccess",
            "BPRoutePreprocessorEnabled"
        ]
    },
    "ui": {
        "name": "User Interface",
        "description": "Display and UI settings",
        "params": [
            "UIBrightness", "UIVolume", "EndToEndLong",
            "ExperimentalMode", "ExperimentalLongitudinalEnabled"
        ]
    },
    "controls": {
        "name": "Controls",
        "description": "Vehicle control parameters",
        "params": [
            "OpenpilotEnabledToggle", "LongitudinalPersonality",
            "Disengage OnAccelerator"
        ]
    },
    "logging": {
        "name": "Logging",
        "description": "Data logging settings",
        "params": [
            "DisableLogging", "AthenadUploadQueue"
        ]
    },
    "other": {
        "name": "Other",
        "description": "Miscellaneous parameters",
        "params": []
    }
}


def categorize_param(key: str) -> str:
    """Determine which category a param belongs to

    Args:
        key: Parameter key

    Returns:
        Category name
    """
    for category, info in PARAM_CATEGORIES.items():
        if key in info["params"]:
            return category
    return "other"


def get_all_params(params: Optional[Params] = None) -> Dict[str, Any]:
    """Get all readable parameters

    Args:
        params: Params instance (creates new one if None)

    Returns:
        Dictionary of all params with metadata
    """
    if params is None:
        params = Params()

    result = {}

    # Try to get all params by listing the params directory
    params_dir = "/data/params/d" if os.path.exists("/data/params/d") else None

    if params_dir and os.path.exists(params_dir):
        # List all param files
        try:
            param_keys = os.listdir(params_dir)
        except Exception as e:
            logger.error(f"Error listing params directory: {e}")
            param_keys = []
    else:
        # Fallback to known params
        param_keys = []
        for category_info in PARAM_CATEGORIES.values():
            param_keys.extend(category_info["params"])

    for key in param_keys:
        try:
            value = get_param_value(key, params)

            # Get file modification time (last change time)
            last_modified = None
            try:
                param_path = os.path.join(params_dir if params_dir else PARAMS_DIR, key)
                if os.path.exists(param_path):
                    mtime = os.path.getmtime(param_path)
                    last_modified = mtime
            except Exception as e:
                logger.debug(f"Error getting mtime for param {key}: {e}")

            result[key] = {
                "key": key,
                "value": value,
                "category": categorize_param(key),
                "readonly": key in READONLY_PARAMS,
                "critical": key in CRITICAL_PARAMS,
                "type": determine_param_type(value),
                "last_modified": last_modified
            }
        except Exception as e:
            logger.debug(f"Error reading param {key}: {e}")
            # Include params that failed to read with error info
            result[key] = {
                "key": key,
                "value": None,
                "category": categorize_param(key),
                "readonly": True,
                "critical": False,
                "type": "unknown",
                "error": str(e),
                "last_modified": None
            }

    return result


def get_param_value(key: str, params: Optional[Params] = None) -> Union[str, bool, int, float, None]:
    """Get a single parameter value with type detection

    Args:
        key: Parameter key
        params: Params instance (creates new one if None)

    Returns:
        Parameter value with appropriate type
    """
    if params is None:
        params = Params()

    try:
        # Try boolean first
        if key.endswith("Enabled") or key.endswith("Toggle") or key in ["IsOnRoad", "IsOffroad", "Passive"]:
            return params.get_bool(key)

        # Try getting as bytes/string
        value = params.get(key)

        # Handle None and empty values
        if value is None or value == b'' or value == '':
            return None

        # Decode bytes to string if needed
        if isinstance(value, bytes):
            try:
                value = value.decode('utf-8', errors='replace').strip()
            except Exception as e:
                logger.debug(f"Failed to decode param {key}: {e}")
                return None
        elif isinstance(value, str):
            value = value.strip()

        # Handle empty strings
        if not value or value == '':
            return None

        # Try to parse as number
        try:
            if '.' in str(value):
                return float(value)
            elif str(value).lstrip('-').isdigit():
                return int(value)
        except (ValueError, AttributeError):
            pass

        return value

    except Exception as e:
        logger.debug(f"Error getting param {key}: {e}")
        return None


def determine_param_type(value: Any) -> str:
    """Determine the type of a parameter value

    Args:
        value: Parameter value

    Returns:
        Type string (bool, int, float, string, null)
    """
    if value is None:
        return "null"
    elif isinstance(value, bool):
        return "bool"
    elif isinstance(value, int):
        return "int"
    elif isinstance(value, float):
        return "float"
    else:
        return "string"


def set_param_value(key: str, value: Any, params: Optional[Params] = None) -> Dict[str, Any]:
    """Set a parameter value with validation

    Args:
        key: Parameter key
        value: New value
        params: Params instance (creates new one if None)

    Returns:
        Result dictionary with success status and message
    """
    if params is None:
        params = Params()

    # Check if param is readonly
    if key in READONLY_PARAMS:
        return {
            "success": False,
            "error": f"Parameter '{key}' is read-only and cannot be modified"
        }

    try:
        # Handle different types
        if isinstance(value, bool) or (isinstance(value, str) and value.lower() in ["true", "false"]):
            bool_value = value if isinstance(value, bool) else value.lower() == "true"
            params.put_bool(key, bool_value)
        else:
            # Convert to string for storage
            params.put(key, str(value))

        return {
            "success": True,
            "message": f"Successfully updated parameter '{key}'",
            "key": key,
            "value": value
        }

    except Exception as e:
        logger.error(f"Error setting param {key}: {e}")
        return {
            "success": False,
            "error": f"Failed to update parameter: {str(e)}"
        }


def get_params_by_category(params: Optional[Params] = None) -> Dict[str, Any]:
    """Get all parameters organized by category

    Args:
        params: Params instance (creates new one if None)

    Returns:
        Dictionary with params organized by category
    """
    all_params = get_all_params(params)

    result = {}
    for category, info in PARAM_CATEGORIES.items():
        result[category] = {
            "name": info["name"],
            "description": info["description"],
            "params": []
        }

    # Organize params into categories
    for param_data in all_params.values():
        category = param_data["category"]
        result[category]["params"].append(param_data)

    # Sort params within each category
    for category_data in result.values():
        category_data["params"].sort(key=lambda p: p["key"])

    return result


def search_params(query: str, params: Optional[Params] = None) -> List[Dict[str, Any]]:
    """Search parameters by key or value

    Args:
        query: Search query
        params: Params instance (creates new one if None)

    Returns:
        List of matching params
    """
    all_params = get_all_params(params)
    query_lower = query.lower()

    results = []
    for param_data in all_params.values():
        # Search in key
        if query_lower in param_data["key"].lower():
            results.append(param_data)
            continue

        # Search in value
        value_str = str(param_data.get("value", "")).lower()
        if query_lower in value_str:
            results.append(param_data)

    return results
