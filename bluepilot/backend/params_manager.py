#!/usr/bin/env python3
"""
BluePilot Backend Params Manager
Safe operations for reading and writing openpilot parameters
"""

import os
import logging
from typing import Dict, List, Optional, Any, Union

logger = logging.getLogger(__name__)

# Import Params with fallback for testing
try:
    from openpilot.common.params import Params
except ImportError:
    # Mock for testing
    class Params:
        def __init__(self):
            self._params = {
                "IsOnRoad": False,
                "BPWebServerPort": "8088",
                "BPWebServerEnabled": True,
            }

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
            result[key] = {
                "key": key,
                "value": value,
                "category": categorize_param(key),
                "readonly": key in READONLY_PARAMS,
                "critical": key in CRITICAL_PARAMS,
                "type": determine_param_type(value)
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
                "error": str(e)
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

        # Try getting as string
        value = params.get(key, encoding='utf-8')

        if value is None or value == b'':
            return None

        if isinstance(value, bytes):
            value = value.decode('utf-8', errors='replace')

        # Try to parse as number
        if value.isdigit():
            return int(value)

        try:
            return float(value)
        except ValueError:
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
