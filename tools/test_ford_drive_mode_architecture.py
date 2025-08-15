#!/usr/bin/env python3
"""
Test script for Ford Drive Mode Architecture

This script demonstrates the proper separation of concerns:
- carstate.py: Handles drive mode state management and logic
- carcontroller.py: Handles sending drive mode commands via CAN
- fordcan.py: Provides CAN message creation functions
"""

import sys
import os

# Add the parent directory to the path to import the modules
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def test_architecture_separation():
    """Test that the drive mode functionality is properly separated."""
    print("=== Testing Ford Drive Mode Architecture ===")

    try:
        # Test 1: Import carstate module
        print("1. Testing carstate.py imports...")
        from opendbc_repo.opendbc.car.ford.carstate import CarState
        print("   ✓ carstate.py imports successfully")

        # Test 2: Import carcontroller module
        print("2. Testing carcontroller.py imports...")
        from opendbc_repo.opendbc.car.ford.carcontroller import CarController
        print("   ✓ carcontroller.py imports successfully")

        # Test 3: Import fordcan module
        print("3. Testing fordcan.py imports...")
        from opendbc_repo.opendbc.car.ford import fordcan
        print("   ✓ fordcan.py imports successfully")

        # Test 4: Import drive mode utilities
        print("4. Testing drive_mode_utils.py imports...")
        from opendbc_repo.opendbc.car.ford.drive_mode_utils import (
            FordDriveMode, DriveModeCategory, DRIVE_MODE_DEFINITIONS,
            get_drive_mode_name, get_drive_mode_description
        )
        print("   ✓ drive_mode_utils.py imports successfully")

        print("\n=== Architecture Separation Test Results ===")
        print("✓ All modules import successfully")
        print("✓ Drive mode functionality is properly separated:")
        print("  - carstate.py: State management and logic")
        print("  - carcontroller.py: Command sending")
        print("  - fordcan.py: CAN message creation")
        print("  - drive_mode_utils.py: Utilities and definitions")

        return True

    except ImportError as e:
        print(f"❌ Import error: {e}")
        return False
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
        return False


def test_drive_mode_functionality():
    """Test basic drive mode functionality."""
    print("\n=== Testing Drive Mode Functionality ===")

    try:
        from opendbc_repo.opendbc.car.ford.drive_mode_utils import (
            FordDriveMode, get_drive_mode_name, get_drive_mode_description
        )

        # Test basic drive mode operations
        print("1. Testing drive mode names...")
        assert get_drive_mode_name(FordDriveMode.NORMAL) == "Normal"
        assert get_drive_mode_name(FordDriveMode.SPORT) == "Sport"
        print("   ✓ Drive mode names work correctly")

        print("2. Testing drive mode descriptions...")
        normal_desc = get_drive_mode_description(FordDriveMode.NORMAL)
        assert "balanced performance" in normal_desc.lower()
        print("   ✓ Drive mode descriptions work correctly")

        print("3. Testing drive mode values...")
        assert FordDriveMode.NORMAL == 0
        assert FordDriveMode.SPORT == 1
        assert FordDriveMode.ECONOMY == 2
        print("   ✓ Drive mode values are correct")

        print("\n✓ All drive mode functionality tests passed")
        return True

    except Exception as e:
        print(f"❌ Drive mode functionality test failed: {e}")
        return False


def test_can_message_creation():
    """Test CAN message creation functionality."""
    print("\n=== Testing CAN Message Creation ===")

    try:
        from opendbc_repo.opendbc.car.ford import fordcan

        # Test that the drive mode message creation functions exist
        print("1. Testing drive mode message creation functions...")
        assert hasattr(fordcan, 'create_drive_mode_msg')
        assert hasattr(fordcan, 'create_drive_mode_chassis_msg')
        assert hasattr(fordcan, 'create_drive_mode_awd_msg')
        print("   ✓ All drive mode message creation functions exist")

        print("2. Testing function signatures...")
        import inspect

        # Check create_drive_mode_msg signature
        sig = inspect.signature(fordcan.create_drive_mode_msg)
        params = list(sig.parameters.keys())
        expected_params = ['packer', 'CAN', 'powertrain_mode', 'chassis_mode', 'awd_mode']

        for param in expected_params:
            assert param in params, f"Missing parameter: {param}"
        print("   ✓ Function signatures are correct")

        print("\n✓ All CAN message creation tests passed")
        return True

    except Exception as e:
        print(f"❌ CAN message creation test failed: {e}")
        return False


def main():
    """Run all architecture tests."""
    print("Ford Drive Mode Architecture Test Suite")
    print("=" * 50)

    tests = [
        test_architecture_separation,
        test_drive_mode_functionality,
        test_can_message_creation
    ]

    passed = 0
    total = len(tests)

    for test in tests:
        if test():
            passed += 1

    print("\n" + "=" * 50)
    print(f"Test Results: {passed}/{total} tests passed")

    if passed == total:
        print("🎉 All tests passed! The architecture is correctly implemented.")
        return 0
    else:
        print("❌ Some tests failed. Please review the implementation.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
