#!/usr/bin/env python3
"""Test script to monitor Ford brake light status signals

NOTE: This script must be run on the comma device with openpilot running!
It will not work on your local development machine.

Usage:
  1. SSH into your comma device: ssh comma@[device-ip]
  2. Navigate to openpilot: cd /data/openpilot
  3. Run this script: python test_brake_lights.py
"""

import sys
import time
from cereal import messaging


def main():
    """Monitor brake light status from CarStateBP messages"""

    try:
        sm = messaging.SubMaster(['carStateBP', 'carState', 'carControl'])
    except Exception as e:
        print(f"ERROR: Failed to connect to openpilot messaging: {e}")
        print("\nThis script must be run on a comma device with openpilot running.")
        print("Please SSH into your comma device and run this script there.")
        print("\nUsage:")
        print("  1. SSH into comma device: ssh comma@[device-ip]")
        print("  2. Navigate to openpilot: cd /data/openpilot")
        print("  3. Run this script: python test_brake_lights.py")
        sys.exit(1)

    print("=" * 60)
    print("Ford Brake Light Status Monitor")
    print("=" * 60)
    print("\nMonitoring brake light signals...")
    print("Press Ctrl+C to exit\n")

    # Track state changes
    last_brake_lights = None
    last_brake_pressed = None
    last_acc_brake = None

    try:
        while True:
            sm.update()

            # Get carState for basic brake info
            if sm.updated['carState']:
                cs = sm['carState']
                brake_pressed = cs.brakePressed
                brake_torque = cs.brake

                if brake_pressed != last_brake_pressed:
                    print(f"[CarState] Brake Pedal: {'PRESSED' if brake_pressed else 'RELEASED'} (Torque: {brake_torque:.3f} Nm)")
                    last_brake_pressed = brake_pressed

            # Get carStateBP for brake light status
            if sm.updated['carStateBP']:
                cs_bp = sm['carStateBP']

                if cs_bp.brakeLightStatus.dataAvailable:
                    brake_lights = cs_bp.brakeLightStatus.brakeLightsOn

                    if brake_lights != last_brake_lights:
                        print(f"[CarStateBP] Brake Lights: {'ON' if brake_lights else 'OFF'}")
                        last_brake_lights = brake_lights

                        # Show additional context
                        if sm.valid['carState']:
                            cs = sm['carState']
                            print(f"  - Brake Pedal: {'Pressed' if cs.brakePressed else 'Released'}")
                            print(f"  - Brake Torque: {cs.brake:.3f} Nm")
                            print(f"  - Cruise Enabled: {cs.cruiseState.enabled}")
                            print(f"  - Speed: {cs.vEgo * 3.6:.1f} km/h")
                else:
                    if last_brake_lights is not None:
                        print("[CarStateBP] Brake light data not available")
                        last_brake_lights = None

            # Get carControl to see if OP is requesting braking
            if sm.updated['carControl']:
                cc = sm['carControl']
                if cc.enabled and cc.actuators.accel < -0.1:
                    acc_brake_active = True
                else:
                    acc_brake_active = False

                if acc_brake_active != last_acc_brake:
                    if acc_brake_active:
                        print(f"[CarControl] ACC Braking: ACTIVE (accel: {cc.actuators.accel:.2f} m/s²)")
                    last_acc_brake = acc_brake_active

            time.sleep(0.05)  # 20Hz update rate

    except KeyboardInterrupt:
        print("\n\nStopping brake light monitor...")
    except Exception as e:
        print(f"Error in brake light monitor: {e}")
        raise


if __name__ == "__main__":
    main()
