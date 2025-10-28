#!/usr/bin/env python3
"""
Comprehensive alert testing script for BluePilot UI development.

This script allows testing all alert types with flexible filtering options.

Usage:
    # Test all alerts
    ./selfdrive/debug/test_all_alerts.py

    # Test specific alert size
    ./selfdrive/debug/test_all_alerts.py --size small
    ./selfdrive/debug/test_all_alerts.py --size mid
    ./selfdrive/debug/test_all_alerts.py --size full

    # Test specific alert status
    ./selfdrive/debug/test_all_alerts.py --status normal
    ./selfdrive/debug/test_all_alerts.py --status userPrompt
    ./selfdrive/debug/test_all_alerts.py --status critical

    # Test specific alert types
    ./selfdrive/debug/test_all_alerts.py --type laneChange
    ./selfdrive/debug/test_all_alerts.py --type blindspot
    ./selfdrive/debug/test_all_alerts.py --type driver

    # Combine filters
    ./selfdrive/debug/test_all_alerts.py --size small --status normal
    ./selfdrive/debug/test_all_alerts.py --status userPrompt --duration 3.0

    # Run once through filtered alerts
    ./selfdrive/debug/test_all_alerts.py --size mid --once

    # Simulate calibration progress (0% to 100%)
    ./selfdrive/debug/test_all_alerts.py --calibration
    ./selfdrive/debug/test_all_alerts.py --calibration --duration 60
"""
import time
from cereal import log
import cereal.messaging as messaging
from openpilot.common.realtime import DT_CTRL

AlertSize = log.SelfdriveState.AlertSize
AlertStatus = log.SelfdriveState.AlertStatus


def create_comprehensive_alert_catalog():
    """
    Create comprehensive catalog of all possible alerts in the system.

    Returns a list of (category, name, alert_data) tuples organized by:
    - Alert size (SMALL, MID, FULL)
    - Alert status (NORMAL, USER_PROMPT, CRITICAL)
    - Alert type/purpose

    Note: Pill rendering is used for all non-critical/non-full alerts.
    Critical alerts and FULL size always use fullscreen rendering.
    """
    alerts = []

    # ========================================================================
    # SMALL SIZE ALERTS - Compact single-line notifications
    # ========================================================================

    # SMALL + NORMAL - Informational pills
    alerts.extend([
        ("SMALL/NORMAL", "Cruise Control Ready", {
            'text1': 'Cruise Control Ready',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.normal,
            'type': 'cruiseReady',
        }),
        ("SMALL/NORMAL", "Openpilot Active", {
            'text1': 'openpilot Active',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.normal,
            'type': 'openpilotActive',
        }),
        ("SMALL/NORMAL", "Lane Change - Left", {
            'text1': 'Changing Lanes',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.normal,
            'type': 'preLaneChangeLeft',
        }),
        ("SMALL/NORMAL", "Lane Change - Right", {
            'text1': 'Changing Lanes',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.normal,
            'type': 'preLaneChangeRight',
        }),
        ("SMALL/NORMAL", "Lane Change Active", {
            'text1': 'Lane Change in Progress',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.normal,
            'type': 'laneChangeActive',
        }),
        ("SMALL/NORMAL", "Pay Attention", {
            'text1': 'Pay Attention',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.normal,
            'type': 'preDriverDistracted',
        }),
        ("SMALL/NORMAL", "Dashcam Mode", {
            'text1': 'Dashcam Mode',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.normal,
            'type': 'dashcamMode',
        }),
    ])

    # SMALL + USER_PROMPT - Warning pills (orange)
    alerts.extend([
        ("SMALL/USER_PROMPT", "Press Resume to Engage", {
            'text1': 'Press Resume to Engage',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.userPrompt,
            'type': 'pressResume',
        }),
        ("SMALL/USER_PROMPT", "Pay Attention - Warning", {
            'text1': 'Pay Attention',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.userPrompt,
            'type': 'promptDriverDistracted',
        }),
        ("SMALL/USER_PROMPT", "Lane Departure Detected", {
            'text1': 'Lane Departure Detected',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.userPrompt,
            'type': 'ldw',
        }),
        ("SMALL/USER_PROMPT", "Steering Temporarily Unavailable", {
            'text1': 'Steering Temporarily Unavailable',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.userPrompt,
            'type': 'steerTempUnavailable',
        }),
        ("SMALL/USER_PROMPT", "Blindspot - Left", {
            'text1': 'Vehicle in Blindspot',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.userPrompt,
            'type': 'blindspotLeft',
        }),
        ("SMALL/USER_PROMPT", "Blindspot - Right", {
            'text1': 'Vehicle in Blindspot',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.userPrompt,
            'type': 'blindspotRight',
        }),
        ("SMALL/USER_PROMPT", "Lane Change Blocked", {
            'text1': 'Lane Change Blocked',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.userPrompt,
            'type': 'laneChangeBlocked',
        }),
    ])

    # SMALL + CRITICAL - Critical alerts (forces fullscreen)
    alerts.extend([
        ("SMALL/CRITICAL", "Take Control Immediately", {
            'text1': 'Take Control Immediately',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.critical,
            'type': 'takeControlCritical',
        }),
    ])

    # ========================================================================
    # SMALL SIZE - TWO LINE EXAMPLES (text2 present)
    # ========================================================================

    alerts.extend([
        ("SMALL/NORMAL/2-line", "System Ready", {
            'text1': 'System Ready',
            'text2': 'Cruise can be activated',
            'size': AlertSize.small,
            'status': AlertStatus.normal,
            'type': 'systemReady2line',
        }),
        ("SMALL/USER_PROMPT/2-line", "Blindspot Warning", {
            'text1': 'Car Detected in Blindspot',
            'text2': 'Lane Change Blocked',
            'size': AlertSize.small,
            'status': AlertStatus.userPrompt,
            'type': 'blindspotDetected',
        }),
        ("SMALL/USER_PROMPT/2-line", "Keep Hands on Wheel", {
            'text1': 'Keep Hands on Wheel',
            'text2': 'Driver Monitoring Active',
            'size': AlertSize.small,
            'status': AlertStatus.userPrompt,
            'type': 'handsOnWheel',
        }),
    ])

    # ========================================================================
    # MID SIZE ALERTS - Two-line notifications with more detail
    # ========================================================================

    # MID + NORMAL - Informational two-line pills
    alerts.extend([
        ("MID/NORMAL", "Openpilot Unavailable", {
            'text1': 'Openpilot Unavailable',
            'text2': 'Waiting for controls to start',
            'size': AlertSize.mid,
            'status': AlertStatus.normal,
            'type': 'openpilotUnavailable',
        }),
        ("MID/NORMAL", "Speed Too Low", {
            'text1': 'openpilot Canceled',
            'text2': 'Speed too low',
            'size': AlertSize.mid,
            'status': AlertStatus.normal,
            'type': 'speedTooLow',
        }),
        ("MID/NORMAL", "Seatbelt Reminder", {
            'text1': 'Seatbelt Unlatched',
            'text2': 'Buckle Up',
            'size': AlertSize.mid,
            'status': AlertStatus.normal,
            'type': 'seatbeltUnbuckled',
        }),
        ("MID/NORMAL", "Door Open", {
            'text1': 'Door Open',
            'text2': 'Close to Continue',
            'size': AlertSize.mid,
            'status': AlertStatus.normal,
            'type': 'doorOpen',
        }),
        ("MID/NORMAL", "Parking Brake Engaged", {
            'text1': 'Parking Brake Engaged',
            'text2': 'Release to Drive',
            'size': AlertSize.mid,
            'status': AlertStatus.normal,
            'type': 'parkingBrake',
        }),
    ])

    # MID + USER_PROMPT - Warning two-line pills (orange)
    alerts.extend([
        ("MID/USER_PROMPT", "Driver Distracted", {
            'text1': 'DISENGAGE IMMEDIATELY',
            'text2': 'Driver Distracted',
            'size': AlertSize.mid,
            'status': AlertStatus.userPrompt,
            'type': 'driverDistractedPrompt',
        }),
        ("MID/USER_PROMPT", "Speed Too High", {
            'text1': 'Speed Too High',
            'text2': 'Model uncertain at this speed',
            'size': AlertSize.mid,
            'status': AlertStatus.userPrompt,
            'type': 'speedTooHigh',
        }),
        ("MID/USER_PROMPT", "Calibration Required", {
            'text1': 'Calibration Required',
            'text2': 'Drive to Calibrate',
            'size': AlertSize.mid,
            'status': AlertStatus.userPrompt,
            'type': 'calibrationRequired',
        }),
        ("MID/USER_PROMPT", "Steer Unavailable", {
            'text1': 'Steering Unavailable',
            'text2': 'Keep Hands on Wheel',
            'size': AlertSize.mid,
            'status': AlertStatus.userPrompt,
            'type': 'steerUnavailable',
        }),
        ("MID/USER_PROMPT", "Poor Visibility", {
            'text1': 'Limited Visibility',
            'text2': 'Be Ready to Take Over',
            'size': AlertSize.mid,
            'status': AlertStatus.userPrompt,
            'type': 'poorVisibility',
        }),
        ("MID/USER_PROMPT", "Approaching End of Route", {
            'text1': 'End of Route Approaching',
            'text2': 'Prepare to Take Over',
            'size': AlertSize.mid,
            'status': AlertStatus.userPrompt,
            'type': 'endOfRoute',
        }),
    ])

    # MID + USER_PROMPT - Long text wrapping tests
    alerts.extend([
        ("MID/USER_PROMPT", "Calibration Long Text", {
            'text1': 'Calibration Required: Drive Above 45 mph',
            'text2': 'Keep steady speed with good lane lines visible',
            'size': AlertSize.mid,
            'status': AlertStatus.userPrompt,
            'type': 'calibrationLongText',
        }),
        ("MID/USER_PROMPT", "System Warning Long", {
            'text1': 'System Temporary Unavailable',
            'text2': 'Environmental conditions may affect performance',
            'size': AlertSize.mid,
            'status': AlertStatus.userPrompt,
            'type': 'systemWarningLong',
        }),
    ])

    # MID + CRITICAL - Critical two-line (forces fullscreen)
    alerts.extend([
        ("MID/CRITICAL", "Take Control - System Issue", {
            'text1': 'TAKE CONTROL',
            'text2': 'System Malfunction',
            'size': AlertSize.mid,
            'status': AlertStatus.critical,
            'type': 'takeControlMalfunction',
        }),
        ("MID/CRITICAL", "Disengage - Driver Issue", {
            'text1': 'DISENGAGE IMMEDIATELY',
            'text2': 'Driver Unresponsive',
            'size': AlertSize.mid,
            'status': AlertStatus.critical,
            'type': 'disengageDriverIssue',
        }),
    ])

    # ========================================================================
    # FULL SIZE ALERTS - Fullscreen takeover (always fullscreen regardless)
    # ========================================================================

    # FULL + NORMAL
    alerts.extend([
        ("FULL/NORMAL", "Openpilot Unavailable - Restart", {
            'text1': 'openpilot Unavailable',
            'text2': 'Restart the Device',
            'size': AlertSize.full,
            'status': AlertStatus.normal,
            'type': 'fullOpenpilotUnavailable',
        }),
        ("FULL/NORMAL", "Car Unrecognized", {
            'text1': 'Car Unrecognized',
            'text2': 'Dashcam Mode Only',
            'size': AlertSize.full,
            'status': AlertStatus.normal,
            'type': 'carUnrecognized',
        }),
    ])

    # FULL + USER_PROMPT
    alerts.extend([
        ("FULL/USER_PROMPT", "Be Ready to Take Over", {
            'text1': 'Be Ready to Take Over',
            'text2': 'Reduced Visibility',
            'size': AlertSize.full,
            'status': AlertStatus.userPrompt,
            'type': 'readyToTakeOver',
        }),
        ("FULL/USER_PROMPT", "Camera Malfunction", {
            'text1': 'Camera Malfunction',
            'text2': 'Contact Support',
            'size': AlertSize.full,
            'status': AlertStatus.userPrompt,
            'type': 'cameraMalfunction',
        }),
    ])

    # FULL + CRITICAL - Safety critical fullscreen alerts
    alerts.extend([
        ("FULL/CRITICAL", "AEB - Emergency Braking", {
            'text1': 'BRAKE!',
            'text2': 'Emergency Braking: Risk of Collision',
            'size': AlertSize.full,
            'status': AlertStatus.critical,
            'type': 'aeb',
        }),
        ("FULL/CRITICAL", "Stock AEB", {
            'text1': 'BRAKE!',
            'text2': 'Stock AEB: Risk of Collision',
            'size': AlertSize.full,
            'status': AlertStatus.critical,
            'type': 'stockAeb',
        }),
        ("FULL/CRITICAL", "Forward Collision Warning", {
            'text1': 'BRAKE!',
            'text2': 'Risk of Collision',
            'size': AlertSize.full,
            'status': AlertStatus.critical,
            'type': 'fcw',
        }),
        ("FULL/CRITICAL", "Driver Distracted - Critical", {
            'text1': 'DISENGAGE IMMEDIATELY',
            'text2': 'Driver Distracted',
            'size': AlertSize.full,
            'status': AlertStatus.critical,
            'type': 'driverDistracted',
        }),
        ("FULL/CRITICAL", "Driver Unresponsive", {
            'text1': 'DISENGAGE IMMEDIATELY',
            'text2': 'Driver Unresponsive',
            'size': AlertSize.full,
            'status': AlertStatus.critical,
            'type': 'driverUnresponsive',
        }),
        ("FULL/CRITICAL", "Calibration Invalid", {
            'text1': 'TAKE CONTROL IMMEDIATELY',
            'text2': 'Calibration Invalid',
            'size': AlertSize.full,
            'status': AlertStatus.critical,
            'type': 'calibrationInvalid',
        }),
        ("FULL/CRITICAL", "Out of Storage", {
            'text1': 'TAKE CONTROL IMMEDIATELY',
            'text2': 'Out of Storage',
            'size': AlertSize.full,
            'status': AlertStatus.critical,
            'type': 'outOfSpace',
        }),
        ("FULL/CRITICAL", "Controls Unresponsive", {
            'text1': 'TAKE CONTROL IMMEDIATELY',
            'text2': 'Controls Unresponsive',
            'size': AlertSize.full,
            'status': AlertStatus.critical,
            'type': 'controlsUnresponsive',
        }),
    ])

    # FULL + CRITICAL - Long text variations
    alerts.extend([
        ("FULL/CRITICAL", "Take Control - Long Text", {
            'text1': 'TAKE CONTROL IMMEDIATELY',
            'text2': 'Lane Keeping Temporarily Unavailable',
            'size': AlertSize.full,
            'status': AlertStatus.critical,
            'type': 'takeControlLongText',
        }),
    ])

    return alerts


def filter_alerts(all_alerts, size_filter=None, status_filter=None, type_filter=None):
    """
    Filter alerts based on criteria.

    Args:
        all_alerts: List of (category, name, alert_data) tuples
        size_filter: 'small', 'mid', or 'full'
        status_filter: 'normal', 'userPrompt', or 'critical'
        type_filter: String to match in alert type (e.g., 'laneChange', 'blindspot')

    Returns:
        Filtered list of alerts
    """
    filtered = all_alerts

    if size_filter:
        size_map = {
            'small': AlertSize.small,
            'mid': AlertSize.mid,
            'full': AlertSize.full,
        }
        size_value = size_map.get(size_filter.lower())
        if size_value is not None:
            filtered = [(cat, name, data) for cat, name, data in filtered
                       if data['size'] == size_value]

    if status_filter:
        status_map = {
            'normal': AlertStatus.normal,
            'userprompt': AlertStatus.userPrompt,
            'critical': AlertStatus.critical,
        }
        status_value = status_map.get(status_filter.lower())
        if status_value is not None:
            filtered = [(cat, name, data) for cat, name, data in filtered
                       if data['status'] == status_value]

    if type_filter:
        type_lower = type_filter.lower()
        filtered = [(cat, name, data) for cat, name, data in filtered
                   if type_lower in data['type'].lower() or
                      type_lower in name.lower()]

    return filtered


def simulate_calibration_progress(duration=30.0):
    """
    Simulate calibration progress from 0% to 100%.

    Args:
        duration: Total duration for calibration from 0% to 100% (seconds)
    """
    pm = messaging.PubMaster(['selfdriveState', 'deviceState', 'pandaStates'])

    print("=" * 80)
    print("CALIBRATION PROGRESS SIMULATION")
    print("=" * 80)
    print(f"Simulating calibration from 0% to 100% over {duration} seconds")
    print("Press Ctrl+C to stop")
    print()

    try:
        start_time = time.time()
        while True:
            elapsed = time.time() - start_time

            # Calculate percentage (0-100)
            percentage = min(100, int((elapsed / duration) * 100))

            # Create calibration alert
            dat = messaging.new_message('selfdriveState')
            dat.selfdriveState.enabled = False
            dat.selfdriveState.alertText1 = f'Calibration: {percentage}%'
            dat.selfdriveState.alertText2 = 'Drive Above 45 mph'
            dat.selfdriveState.alertSize = AlertSize.mid
            dat.selfdriveState.alertStatus = AlertStatus.userPrompt
            dat.selfdriveState.alertType = 'calibrationProgress'
            pm.send('selfdriveState', dat)

            # Send deviceState to keep system "alive"
            dat = messaging.new_message('deviceState')
            dat.deviceState.started = True
            pm.send('deviceState', dat)

            # Send pandaStates
            dat = messaging.new_message('pandaStates', 1)
            dat.pandaStates[0].ignitionLine = True
            dat.pandaStates[0].pandaType = log.PandaState.PandaType.uno
            pm.send('pandaStates', dat)

            print(f"\rCalibration Progress: {percentage}%", end='', flush=True)

            # Reset after reaching 100%
            if percentage >= 100:
                print("\n\nCalibration complete! Restarting...")
                time.sleep(2)
                start_time = time.time()

            time.sleep(DT_CTRL)

    except KeyboardInterrupt:
        print("\n\nStopping calibration test...")

        # Clear alerts
        dat = messaging.new_message('selfdriveState')
        dat.selfdriveState.alertSize = AlertSize.none
        pm.send('selfdriveState', dat)

        print("Test complete!")


def cycle_test_alerts(duration_per_alert=5.0, continuous=True,
                     size_filter=None, status_filter=None, type_filter=None):
    """
    Cycle through test alerts for UI development.

    Args:
        duration_per_alert: How long to display each alert (seconds)
        continuous: If True, loop forever. If False, run once.
        size_filter: Filter by alert size ('small', 'mid', 'full')
        status_filter: Filter by alert status ('normal', 'userPrompt', 'critical')
        type_filter: Filter by alert type string (e.g., 'laneChange')
    """
    all_alerts = create_comprehensive_alert_catalog()
    alerts = filter_alerts(all_alerts, size_filter, status_filter, type_filter)

    pm = messaging.PubMaster(['selfdriveState', 'deviceState', 'pandaStates'])

    print("=" * 80)
    print("BLUEPILOT ALERT UI TEST SCRIPT - COMPREHENSIVE EDITION")
    print("=" * 80)

    # Print filter information
    filters_applied = []
    if size_filter:
        filters_applied.append(f"Size: {size_filter.upper()}")
    if status_filter:
        filters_applied.append(f"Status: {status_filter.upper()}")
    if type_filter:
        filters_applied.append(f"Type: {type_filter}")

    if filters_applied:
        print(f"Filters: {', '.join(filters_applied)}")
    else:
        print("Filters: None (showing all alerts)")

    print(f"Total alerts in catalog: {len(all_alerts)}")
    print(f"Alerts to display: {len(alerts)}")
    print(f"Duration per alert: {duration_per_alert} seconds")
    print(f"Mode: {'Continuous loop' if continuous else 'Single pass'}")
    print("=" * 80)
    print()
    print("RENDERING LOGIC:")
    print("  • CRITICAL status or FULL size -> FULLSCREEN")
    print("  • All other alerts (SMALL/MID + NORMAL/USER_PROMPT) -> PILL")
    print("=" * 80)
    print("Press Ctrl+C to stop")
    print()

    if not alerts:
        print("ERROR: No alerts match the specified filters!")
        return

    loop_count = 0
    try:
        while True:
            loop_count += 1
            if loop_count > 1:
                print(f"\n{'=' * 80}")
                print(f"Starting loop #{loop_count}")
                print(f"{'=' * 80}\n")

            for category, alert_name, alert_data in alerts:
                # Determine rendering mode for display
                is_critical = (alert_data['status'] == AlertStatus.critical or
                             alert_data['size'] == AlertSize.full)
                render_mode = "FULLSCREEN" if is_critical else "PILL"

                print(f"\n>>> [{category}] {alert_name}")
                print(f"    Render Mode: {render_mode}")
                print(f"    Size: {alert_data['size']} | Status: {alert_data['status']}")
                print(f"    Type: {alert_data['type']}")
                print(f"    Text1: {alert_data['text1']}")
                if alert_data['text2']:
                    print(f"    Text2: {alert_data['text2']}")

                # Calculate frames for duration
                frames = int(duration_per_alert / DT_CTRL)

                for _ in range(frames):
                    # Send selfdriveState with alert
                    dat = messaging.new_message('selfdriveState')
                    dat.selfdriveState.enabled = False
                    dat.selfdriveState.alertText1 = alert_data['text1']
                    dat.selfdriveState.alertText2 = alert_data['text2']
                    dat.selfdriveState.alertSize = alert_data['size']
                    dat.selfdriveState.alertStatus = alert_data['status']
                    dat.selfdriveState.alertType = alert_data['type']
                    pm.send('selfdriveState', dat)

                    # Send deviceState to keep system "alive"
                    dat = messaging.new_message('deviceState')
                    dat.deviceState.started = True
                    pm.send('deviceState', dat)

                    # Send pandaStates
                    dat = messaging.new_message('pandaStates', 1)
                    dat.pandaStates[0].ignitionLine = True
                    dat.pandaStates[0].pandaType = log.PandaState.PandaType.uno
                    pm.send('pandaStates', dat)

                    time.sleep(DT_CTRL)

            if not continuous:
                break

    except KeyboardInterrupt:
        print("\n\nStopping alert test...")

        # Clear alerts
        dat = messaging.new_message('selfdriveState')
        dat.selfdriveState.alertSize = AlertSize.none
        pm.send('selfdriveState', dat)

        print("Test complete!")


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(
        description='Comprehensive alert UI testing with flexible filtering',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Test all alerts
  %(prog)s

  # Test only SMALL alerts
  %(prog)s --size small

  # Test only USER_PROMPT alerts
  %(prog)s --status userPrompt

  # Test only lane change alerts
  %(prog)s --type laneChange

  # Combine filters
  %(prog)s --size mid --status normal
  %(prog)s --status critical --duration 3.0

  # Run once with custom duration
  %(prog)s --size small --duration 2.0 --once

  # Simulate calibration progress (0%% to 100%% over 30 seconds)
  %(prog)s --calibration

  # Calibration with custom duration (60 seconds)
  %(prog)s --calibration --duration 60
        """)

    parser.add_argument('--duration', type=float, default=5.0,
                       help='Duration to show each alert in seconds (default: 5.0)')
    parser.add_argument('--once', action='store_true',
                       help='Run through alerts once instead of looping')
    parser.add_argument('--size', type=str, choices=['small', 'mid', 'full'],
                       help='Filter by alert size (small, mid, or full)')
    parser.add_argument('--status', type=str,
                       choices=['normal', 'userPrompt', 'critical'],
                       help='Filter by alert status (normal, userPrompt, or critical)')
    parser.add_argument('--type', type=str,
                       help='Filter by alert type keyword (e.g., "laneChange", "blindspot", "driver")')
    parser.add_argument('--calibration', action='store_true',
                       help='Simulate calibration progress from 0%% to 100%% (overrides all other options)')

    args = parser.parse_args()

    # Special mode: calibration simulation
    if args.calibration:
        simulate_calibration_progress(duration=args.duration if args.duration != 5.0 else 30.0)
    else:
        cycle_test_alerts(
            duration_per_alert=args.duration,
            continuous=not args.once,
            size_filter=args.size,
            status_filter=args.status,
            type_filter=args.type
        )
