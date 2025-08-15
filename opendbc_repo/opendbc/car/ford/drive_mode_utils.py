"""
Ford Drive Mode Utilities

This module provides utilities for handling Ford vehicle drive modes,
including drive mode definitions, validation, and utility functions.
"""

from typing import Dict, List, Optional, Tuple
from enum import IntEnum


class FordDriveMode(IntEnum):
    """Ford drive mode enumeration based on DBC values."""

    # Powertrain Drive Modes (SelDrvMdePt_D_Rq)
    NORMAL = 0
    SPORT = 1
    ECONOMY = 2
    TOW_HAUL = 3
    GRASS_GRAVEL_SNOW = 5
    SAND = 7
    MUD_RUTS = 8
    ROCK_CRAWL = 9
    SPORT_ADAPTIVE = 14
    HIGH_SPEED_DESERT_BAJA = 15
    DRAG_MODE = 16
    EV_NOW_MODE = 17
    EV_LATER_CHARGER_MODE = 18

    # Chassis Drive Modes (SelDrvMdeChassis_D_Rq)
    NORMAL_ADAPTIVE = 1
    COMFORT = 5
    COMFORT_ADAPTIVE = 6
    LOW_MU_MODE = 7
    MUD_AND_RUTS = 8
    TRACK_MODE = 13
    ROUGH_ROAD_MODE = 14
    HIGH_SPEED_DESERT = 12

    # AWD Drive Modes (SelDrvMdeAwd_D_Rq)
    FOUR_BY_TWO = 0
    FOUR_BY_FOUR_AUTO = 1
    FOUR_BY_FOUR_HIGH = 2
    FOUR_BY_FOUR_LOW = 3
    NEUTRAL = 4


class DriveModeCategory(IntEnum):
    """Drive mode categories for organization."""
    POWERTRAIN = 0
    CHASSIS = 1
    AWD = 2


# Drive mode definitions with metadata
DRIVE_MODE_DEFINITIONS = {
    # Powertrain modes
    FordDriveMode.NORMAL: {
        'name': 'Normal',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Standard driving mode with balanced performance and efficiency',
        'available_on': ['all'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.SPORT: {
        'name': 'Sport',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Enhanced performance with sportier throttle response and shift points',
        'available_on': ['all'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.ECONOMY: {
        'name': 'Economy',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Optimized for fuel efficiency with relaxed throttle response',
        'available_on': ['all'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.TOW_HAUL: {
        'name': 'Tow Haul',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Optimized for towing with enhanced engine braking and shift patterns',
        'available_on': ['truck', 'suv'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.GRASS_GRAVEL_SNOW: {
        'name': 'Grass/Gravel/Snow',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Enhanced traction control for loose surfaces',
        'available_on': ['suv', 'truck'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.SAND: {
        'name': 'Sand',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Optimized for sand driving with reduced traction control',
        'available_on': ['suv', 'truck'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.MUD_RUTS: {
        'name': 'Mud/Ruts',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Enhanced off-road performance for muddy conditions',
        'available_on': ['suv', 'truck'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.ROCK_CRAWL: {
        'name': 'Rock Crawl',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Low-speed off-road mode for technical terrain',
        'available_on': ['suv', 'truck'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.SPORT_ADAPTIVE: {
        'name': 'Sport Adaptive',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Adaptive sport mode that adjusts to driving style',
        'available_on': ['performance'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.HIGH_SPEED_DESERT_BAJA: {
        'name': 'High Speed Desert/Baja',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'High-speed off-road mode for desert racing',
        'available_on': ['performance'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.DRAG_MODE: {
        'name': 'Drag Mode',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Optimized for drag racing with launch control',
        'available_on': ['performance'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.EV_NOW_MODE: {
        'name': 'EV Now Mode',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Electric vehicle mode prioritizing battery usage',
        'available_on': ['ev', 'phev'],
        'can_combine_with': ['chassis', 'awd']
    },
    FordDriveMode.EV_LATER_CHARGER_MODE: {
        'name': 'EV Later Charger Mode',
        'category': DriveModeCategory.POWERTRAIN,
        'description': 'Electric vehicle mode preserving battery for later charging',
        'available_on': ['ev', 'phev'],
        'can_combine_with': ['chassis', 'awd']
    },

    # Chassis modes
    FordDriveMode.NORMAL_ADAPTIVE: {
        'name': 'Normal Adaptive',
        'category': DriveModeCategory.CHASSIS,
        'description': 'Adaptive suspension that adjusts to road conditions',
        'available_on': ['all'],
        'can_combine_with': ['powertrain', 'awd']
    },
    FordDriveMode.COMFORT: {
        'name': 'Comfort',
        'category': DriveModeCategory.CHASSIS,
        'description': 'Smooth ride with softer suspension settings',
        'available_on': ['all'],
        'can_combine_with': ['powertrain', 'awd']
    },
    FordDriveMode.COMFORT_ADAPTIVE: {
        'name': 'Comfort Adaptive',
        'category': DriveModeCategory.CHASSIS,
        'description': 'Adaptive comfort mode that adjusts to road conditions',
        'available_on': ['luxury'],
        'can_combine_with': ['powertrain', 'awd']
    },
    FordDriveMode.LOW_MU_MODE: {
        'name': 'Low Mu Mode',
        'category': DriveModeCategory.CHASSIS,
        'description': 'Enhanced stability control for low friction surfaces',
        'available_on': ['all'],
        'can_combine_with': ['powertrain', 'awd']
    },
    FordDriveMode.MUD_AND_RUTS: {
        'name': 'Mud and Ruts',
        'category': DriveModeCategory.CHASSIS,
        'description': 'Off-road suspension settings for challenging terrain',
        'available_on': ['suv', 'truck'],
        'can_combine_with': ['powertrain', 'awd']
    },
    FordDriveMode.TRACK_MODE: {
        'name': 'Track Mode',
        'category': DriveModeCategory.CHASSIS,
        'description': 'Firm suspension and enhanced handling for track use',
        'available_on': ['performance'],
        'can_combine_with': ['powertrain', 'awd']
    },
    FordDriveMode.ROUGH_ROAD_MODE: {
        'name': 'Rough Road Mode',
        'description': 'Enhanced suspension for rough road conditions',
        'category': DriveModeCategory.CHASSIS,
        'available_on': ['suv', 'truck'],
        'can_combine_with': ['powertrain', 'awd']
    },
    FordDriveMode.HIGH_SPEED_DESERT: {
        'name': 'High Speed Desert',
        'description': 'High-speed off-road suspension settings',
        'category': DriveModeCategory.CHASSIS,
        'available_on': ['performance'],
        'can_combine_with': ['powertrain', 'awd']
    },

    # AWD modes
    FordDriveMode.FOUR_BY_TWO: {
        'name': '2WD',
        'category': DriveModeCategory.AWD,
        'description': 'Two-wheel drive mode for improved fuel efficiency',
        'available_on': ['all'],
        'can_combine_with': ['powertrain', 'chassis']
    },
    FordDriveMode.FOUR_BY_FOUR_AUTO: {
        'name': '4WD Auto',
        'category': DriveModeCategory.AWD,
        'description': 'Automatic four-wheel drive that engages when needed',
        'available_on': ['suv', 'truck'],
        'can_combine_with': ['powertrain', 'chassis']
    },
    FordDriveMode.FOUR_BY_FOUR_HIGH: {
        'name': '4WD High',
        'category': DriveModeCategory.AWD,
        'description': 'Four-wheel drive high range for off-road use',
        'available_on': ['suv', 'truck'],
        'can_combine_with': ['powertrain', 'chassis']
    },
    FordDriveMode.FOUR_BY_FOUR_LOW: {
        'name': '4WD Low',
        'category': DriveModeCategory.AWD,
        'description': 'Four-wheel drive low range for extreme off-road',
        'available_on': ['suv', 'truck'],
        'can_combine_with': ['powertrain', 'chassis']
    },
    FordDriveMode.NEUTRAL: {
        'name': 'Neutral',
        'category': DriveModeCategory.AWD,
        'description': 'Neutral position for towing or maintenance',
        'available_on': ['suv', 'truck'],
        'can_combine_with': ['powertrain', 'chassis']
    }
}


def get_drive_mode_name(mode: int) -> str:
    """Get the human-readable name for a drive mode.

    Args:
        mode: Drive mode integer value

    Returns:
        str: Human-readable name or 'Unknown' if not found
    """
    if mode in DRIVE_MODE_DEFINITIONS:
        return DRIVE_MODE_DEFINITIONS[mode]['name']
    return 'Unknown'


def get_drive_mode_description(mode: int) -> str:
    """Get the description for a drive mode.

    Args:
        mode: Drive mode integer value

    Returns:
        str: Description or 'No description available' if not found
    """
    if mode in DRIVE_MODE_DEFINITIONS:
        return DRIVE_MODE_DEFINITIONS[mode]['description']
    return 'No description available'


def get_drive_mode_category(mode: int) -> Optional[DriveModeCategory]:
    """Get the category for a drive mode.

    Args:
        mode: Drive mode integer value

    Returns:
        DriveModeCategory or None if not found
    """
    if mode in DRIVE_MODE_DEFINITIONS:
        return DRIVE_MODE_DEFINITIONS[mode]['category']
    return None


def is_drive_mode_available(mode: int, vehicle_type: str = 'all') -> bool:
    """Check if a drive mode is available for a specific vehicle type.

    Args:
        mode: Drive mode integer value
        vehicle_type: Vehicle type ('all', 'car', 'suv', 'truck', 'ev', 'phev', 'performance', 'luxury')

    Returns:
        bool: True if mode is available for the vehicle type
    """
    if mode not in DRIVE_MODE_DEFINITIONS:
        return False

    available_on = DRIVE_MODE_DEFINITIONS[mode]['available_on']
    return 'all' in available_on or vehicle_type in available_on


def get_available_drive_modes(vehicle_type: str = 'all', category: Optional[DriveModeCategory] = None) -> List[int]:
    """Get list of available drive modes for a vehicle type and category.

    Args:
        vehicle_type: Vehicle type to filter by
        category: Optional category to filter by

    Returns:
        List[int]: List of available drive mode values
    """
    available_modes = []

    for mode, definition in DRIVE_MODE_DEFINITIONS.items():
        if not is_drive_mode_available(mode, vehicle_type):
            continue

        if category is not None and definition['category'] != category:
            continue

        available_modes.append(mode)

    return sorted(available_modes)


def validate_drive_mode_combination(powertrain_mode: int, chassis_mode: int, awd_mode: int) -> Tuple[bool, str]:
    """Validate if a combination of drive modes is valid.

    Args:
        powertrain_mode: Powertrain drive mode
        chassis_mode: Chassis drive mode
        awd_mode: AWD drive mode

    Returns:
        Tuple[bool, str]: (is_valid, error_message)
    """
    # Check if all modes exist
    if powertrain_mode not in DRIVE_MODE_DEFINITIONS:
        return False, f"Invalid powertrain mode: {powertrain_mode}"
    if chassis_mode not in DRIVE_MODE_DEFINITIONS:
        return False, f"Invalid chassis mode: {chassis_mode}"
    if awd_mode not in DRIVE_MODE_DEFINITIONS:
        return False, f"Invalid AWD mode: {awd_mode}"

    # Check if modes can be combined
    powertrain_def = DRIVE_MODE_DEFINITIONS[powertrain_mode]
    chassis_def = DRIVE_MODE_DEFINITIONS[chassis_mode]
    awd_def = DRIVE_MODE_DEFINITIONS[awd_mode]

    if 'chassis' not in powertrain_def['can_combine_with']:
        return False, f"Powertrain mode {powertrain_def['name']} cannot be combined with chassis modes"
    if 'awd' not in powertrain_def['can_combine_with']:
        return False, f"Powertrain mode {powertrain_def['name']} cannot be combined with AWD modes"
    if 'powertrain' not in chassis_def['can_combine_with']:
        return False, f"Chassis mode {chassis_def['name']} cannot be combined with powertrain modes"
    if 'awd' not in chassis_def['can_combine_with']:
        return False, f"Chassis mode {chassis_def['name']} cannot be combined with AWD modes"
    if 'powertrain' not in awd_def['can_combine_with']:
        return False, f"AWD mode {awd_def['name']} cannot be combined with powertrain modes"
    if 'chassis' not in awd_def['can_combine_with']:
        return False, f"AWD mode {awd_def['name']} cannot be combined with chassis modes"

    return True, "Valid combination"


def get_recommended_drive_modes(vehicle_type: str, driving_conditions: str) -> Dict[str, int]:
    """Get recommended drive mode combinations for different driving conditions.

    Args:
        vehicle_type: Type of vehicle
        driving_conditions: Driving conditions ('city', 'highway', 'offroad', 'towing', 'performance')

    Returns:
        Dict[str, int]: Dictionary with recommended modes for powertrain, chassis, and AWD
    """
    recommendations = {
        'city': {
            'powertrain': FordDriveMode.NORMAL,
            'chassis': FordDriveMode.NORMAL_ADAPTIVE,
            'awd': FordDriveMode.FOUR_BY_TWO
        },
        'highway': {
            'powertrain': FordDriveMode.ECONOMY,
            'chassis': FordDriveMode.COMFORT,
            'awd': FordDriveMode.FOUR_BY_TWO
        },
        'offroad': {
            'powertrain': FordDriveMode.GRASS_GRAVEL_SNOW,
            'chassis': FordDriveMode.MUD_AND_RUTS,
            'awd': FordDriveMode.FOUR_BY_FOUR_AUTO
        },
        'towing': {
            'powertrain': FordDriveMode.TOW_HAUL,
            'chassis': FordDriveMode.NORMAL_ADAPTIVE,
            'awd': FordDriveMode.FOUR_BY_FOUR_AUTO
        },
        'performance': {
            'powertrain': FordDriveMode.SPORT,
            'chassis': FordDriveMode.TRACK_MODE,
            'awd': FordDriveMode.FOUR_BY_TWO
        }
    }

    if driving_conditions in recommendations:
        return recommendations[driving_conditions]

    # Default to city driving
    return recommendations['city']


def format_drive_mode_display(mode: int) -> str:
    """Format a drive mode for display in the UI.

    Args:
        mode: Drive mode integer value

    Returns:
        str: Formatted display string
    """
    if mode in DRIVE_MODE_DEFINITIONS:
        definition = DRIVE_MODE_DEFINITIONS[mode]
        category_name = definition['category'].name.replace('_', ' ').title()
        return f"{definition['name']} ({category_name})"
    return f"Mode {mode} (Unknown)"
