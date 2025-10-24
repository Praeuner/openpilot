#!/usr/bin/env python3
"""
Test script to cycle through all alert sizes and statuses for UI development.
This helps visualize how different alerts appear in the UI.

Usage:
    ./selfdrive/debug/test_all_alerts.py
"""
import time
from cereal import log
import cereal.messaging as messaging
from openpilot.common.realtime import DT_CTRL

AlertSize = log.SelfdriveState.AlertSize
AlertStatus = log.SelfdriveState.AlertStatus

def create_test_alerts():
    """
    Create comprehensive test alerts covering all sizes and statuses.
    Returns a list of (name, alert_data) tuples.
    """
    alerts = []

    # SMALL SIZE ALERTS - Single line, appears at bottom
    alerts.extend([
        ("Small - Normal Status", {
            'text1': 'Cruise Control Ready',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.normal,
            'type': 'smallNormal',
        }),
        ("Small - User Prompt", {
            'text1': 'Press Resume to Engage',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.userPrompt,
            'type': 'smallPrompt',
        }),
        ("Small - Critical", {
            'text1': 'Take Control Immediately',
            'text2': '',
            'size': AlertSize.small,
            'status': AlertStatus.critical,
            'type': 'smallCritical',
        }),
    ])

    # MID SIZE ALERTS - Two lines with main and secondary text
    alerts.extend([
        ("Mid - Normal Status", {
            'text1': 'Openpilot Unavailable',
            'text2': 'Waiting for controls to start',
            'size': AlertSize.mid,
            'status': AlertStatus.normal,
            'type': 'midNormal',
        }),
        ("Mid - User Prompt", {
            'text1': 'DISENGAGE IMMEDIATELY',
            'text2': 'Driver Distracted',
            'size': AlertSize.mid,
            'status': AlertStatus.userPrompt,
            'type': 'midPrompt',
        }),
        ("Mid - Critical", {
            'text1': 'TAKE CONTROL',
            'text2': 'System Malfunction',
            'size': AlertSize.mid,
            'status': AlertStatus.critical,
            'type': 'midCritical',
        }),
    ])

    # MID SIZE - Long text to test wrapping
    alerts.extend([
        ("Mid - Long Text Wrapping", {
            'text1': 'Calibration Required: Drive Above 45 mph',
            'text2': 'Keep steady speed with good lane lines visible',
            'size': AlertSize.mid,
            'status': AlertStatus.userPrompt,
            'type': 'midLongText',
        }),
    ])

    # FULL SIZE ALERTS - Takes over entire screen
    alerts.extend([
        ("Full - Normal Status", {
            'text1': 'openpilot Unavailable',
            'text2': 'Restart the Device',
            'size': AlertSize.full,
            'status': AlertStatus.normal,
            'type': 'fullNormal',
        }),
        ("Full - User Prompt", {
            'text1': 'Be Ready to Take Over',
            'text2': 'Reduced Visibility',
            'size': AlertSize.full,
            'status': AlertStatus.userPrompt,
            'type': 'fullPrompt',
        }),
        ("Full - Critical", {
            'text1': 'DISENGAGE IMMEDIATELY',
            'text2': 'Controls Unresponsive',
            'size': AlertSize.full,
            'status': AlertStatus.critical,
            'type': 'fullCritical',
        }),
    ])

    # FULL SIZE - Long text
    alerts.extend([
        ("Full - Long Text", {
            'text1': 'TAKE CONTROL IMMEDIATELY',
            'text2': 'Lane Keeping Temporarily Unavailable',
            'size': AlertSize.full,
            'status': AlertStatus.critical,
            'type': 'fullLongText',
        }),
    ])

    return alerts


def cycle_test_alerts(duration_per_alert=5.0, continuous=True):
    """
    Cycle through all test alerts for UI development.

    Args:
        duration_per_alert: How long to display each alert (seconds)
        continuous: If True, loop forever. If False, run once.
    """
    alerts = create_test_alerts()

    pm = messaging.PubMaster(['selfdriveState', 'deviceState', 'pandaStates'])

    print("=" * 70)
    print("ALERT UI TEST SCRIPT")
    print("=" * 70)
    print(f"Testing {len(alerts)} different alert configurations")
    print(f"Each alert will display for {duration_per_alert} seconds")
    print("Press Ctrl+C to stop")
    print("=" * 70)
    print()

    loop_count = 0
    try:
        while True:
            loop_count += 1
            if loop_count > 1:
                print(f"\n{'=' * 70}")
                print(f"Starting loop #{loop_count}")
                print(f"{'=' * 70}\n")

            for alert_name, alert_data in alerts:
                print(f"\n>>> Displaying: {alert_name}")
                print(f"    Size: {alert_data['size']}")
                print(f"    Status: {alert_data['status']}")
                print(f"    Text1: {alert_data['text1']}")
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

    parser = argparse.ArgumentParser(description='Test alert UI with different alert types')
    parser.add_argument('--duration', type=float, default=5.0,
                        help='Duration to show each alert (seconds)')
    parser.add_argument('--once', action='store_true',
                        help='Run through alerts once instead of looping')

    args = parser.parse_args()

    cycle_test_alerts(duration_per_alert=args.duration, continuous=not args.once)

