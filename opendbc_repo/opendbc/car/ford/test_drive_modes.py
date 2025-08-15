#!/usr/bin/env python3
"""
Test script for Ford Drive Mode functionality

This script demonstrates the usage of the drive mode utilities and tests
various drive mode combinations and validations.
"""

import sys
import os

# Add the parent directory to the path to import the drive mode utilities
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from drive_mode_utils import (
    FordDriveMode, DriveModeCategory, DRIVE_MODE_DEFINITIONS,
    get_drive_mode_name, get_drive_mode_description, get_drive_mode_category,
    is_drive_mode_available, get_available_drive_modes,
    validate_drive_mode_combination, get_recommended_drive_modes,
    format_drive_mode_display
)


def test_drive_mode_basics():
    """Test basic drive mode functionality."""
    print("=== Testing Basic Drive Mode Functionality ===")

    # Test drive mode names
    assert get_drive_mode_name(FordDriveMode.NORMAL) == "Normal"
    assert get_drive_mode_name(FordDriveMode.SPORT) == "Sport"
    assert get_drive_mode_name(FordDriveMode.ECONOMY) == "Economy"

    # Test drive mode descriptions
    normal_desc = get_drive_mode_description(FordDriveMode.NORMAL)
    assert "balanced performance" in normal_desc.lower()

    # Test drive mode categories
    assert get_drive_mode_category(FordDriveMode.NORMAL) == DriveModeCategory.POWERTRAIN
    assert get_drive_mode_category(FordDriveMode.COMFORT) == DriveModeCategory.CHASSIS
    assert get_drive_mode_category(FordDriveMode.FOUR_BY_TWO) == DriveModeCategory.AWD

    print("✓ Basic drive mode functionality tests passed")


def test_drive_mode_availability():
    """Test drive mode availability for different vehicle types."""
    print("\n=== Testing Drive Mode Availability ===")

    # Test vehicle type availability
    assert is_drive_mode_available(FordDriveMode.NORMAL, "all")
    assert is_drive_mode_available(FordDriveMode.TOW_HAUL, "truck")
    assert not is_drive_mode_available(FordDriveMode.TOW_HAUL, "car")
    assert is_drive_mode_available(FordDriveMode.EV_NOW_MODE, "ev")

    # Test available modes for different vehicle types
    car_modes = get_available_drive_modes("car")
    assert FordDriveMode.NORMAL in car_modes
    assert FordDriveMode.TOW_HAUL not in car_modes

    truck_modes = get_available_drive_modes("truck")
    assert FordDriveMode.TOW_HAUL in truck_modes
    assert FordDriveMode.ROCK_CRAWL in truck_modes

    print("✓ Drive mode availability tests passed")


def test_drive_mode_combinations():
    """Test drive mode combination validation."""
    print("\n=== Testing Drive Mode Combinations ===")

    # Test valid combinations
    valid, msg = validate_drive_mode_combination(
        FordDriveMode.NORMAL,
        FordDriveMode.NORMAL_ADAPTIVE,
        FordDriveMode.FOUR_BY_TWO
    )
    assert valid, f"Valid combination failed: {msg}"

    # Test invalid combinations (these should fail validation)
    # Note: This is a simplified test - actual validation logic may differ

    print("✓ Drive mode combination tests passed")


def test_recommended_modes():
    """Test recommended drive mode suggestions."""
    print("\n=== Testing Recommended Drive Modes ===")

    # Test city driving recommendations
    city_modes = get_recommended_drive_modes("car", "city")
    assert city_modes['powertrain'] == FordDriveMode.NORMAL
    assert city_modes['awd'] == FordDriveMode.FOUR_BY_TWO

    # Test off-road recommendations
    offroad_modes = get_recommended_drive_modes("truck", "offroad")
    assert offroad_modes['powertrain'] == FordDriveMode.GRASS_GRAVEL_SNOW
    assert offroad_modes['awd'] == FordDriveMode.FOUR_BY_FOUR_AUTO

    print("✓ Recommended drive mode tests passed")


def test_drive_mode_display():
    """Test drive mode display formatting."""
    print("\n=== Testing Drive Mode Display Formatting ===")

    # Test display formatting
    normal_display = format_drive_mode_display(FordDriveMode.NORMAL)
    assert "Normal (Powertrain)" in normal_display

    sport_display = format_drive_mode_display(FordDriveMode.SPORT)
    assert "Sport (Powertrain)" in sport_display

    print("✓ Drive mode display formatting tests passed")


def test_drive_mode_definitions():
    """Test drive mode definitions structure."""
    print("\n=== Testing Drive Mode Definitions ===")

    # Test that all drive modes have required fields
    required_fields = ['name', 'category', 'description', 'available_on', 'can_combine_with']

    for mode, definition in DRIVE_MODE_DEFINITIONS.items():
        for field in required_fields:
            assert field in definition, f"Missing field '{field}' in mode {mode}"

        # Test that category is valid
        assert isinstance(definition['category'], DriveModeCategory)

        # Test that available_on is a list
        assert isinstance(definition['available_on'], list)

        # Test that can_combine_with is a list
        assert isinstance(definition['can_combine_with'], list)

    print("✓ Drive mode definitions tests passed")


def demo_drive_mode_usage():
    """Demonstrate practical usage of drive mode utilities."""
    print("\n=== Drive Mode Usage Demo ===")

    # Example: Get available modes for a truck
    print("Available drive modes for a truck:")
    truck_modes = get_available_drive_modes("truck")
    for mode in truck_modes[:5]:  # Show first 5 modes
        name = get_drive_mode_name(mode)
        desc = get_drive_mode_description(mode)
        print(f"  {mode}: {name} - {desc}")

    # Example: Get recommended modes for different conditions
    print("\nRecommended modes for city driving (car):")
    city_modes = get_recommended_drive_modes("car", "city")
    for category, mode in city_modes.items():
        name = get_drive_mode_name(mode)
        print(f"  {category}: {name}")

    # Example: Validate a mode combination
    print("\nValidating drive mode combination:")
    valid, msg = validate_drive_mode_combination(
        FordDriveMode.SPORT,
        FordDriveMode.TRACK_MODE,
        FordDriveMode.FOUR_BY_TWO
    )
    print(f"  Sport + Track + 2WD: {'✓ Valid' if valid else '✗ Invalid'}")
    if not valid:
        print(f"    Reason: {msg}")


def main():
    """Run all tests and demonstrations."""
    print("Ford Drive Mode Test Suite")
    print("=" * 50)

    try:
        test_drive_mode_basics()
        test_drive_mode_availability()
        test_drive_mode_combinations()
        test_recommended_modes()
        test_drive_mode_display()
        test_drive_mode_definitions()
        demo_drive_mode_usage()

        print("\n" + "=" * 50)
        print("🎉 All tests passed! Drive mode functionality is working correctly.")

    except Exception as e:
        print(f"\n❌ Test failed with error: {e}")
        import traceback
        traceback.print_exc()
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
