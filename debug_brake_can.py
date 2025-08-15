#!/usr/bin/env python3
"""Debug script to monitor Ford brake-related CAN signals

NOTE: This script must be run on the comma device with openpilot running!
It will not work on your local development machine.

Usage:
  1. SSH into your comma device: ssh comma@[device-ip]
  2. Navigate to openpilot: cd /data/openpilot
  3. Run this script: python debug_brake_can.py
"""

import sys
import time
from opendbc.can import CANParser
from opendbc.car import Bus
from opendbc.car.ford.values import DBC
from opendbc.car.ford.fordcan import CanBus
from openpilot.common.params import Params
from cereal import messaging


def main():
    """Monitor raw CAN signals related to braking"""
    
    params = Params()
    
    # Check if we're running on a comma device
    try:
        car_params_bytes = params.get("CarParams")
        if car_params_bytes is None:
            print("ERROR: CarParams not found!")
            print("\nThis script must be run on a comma device with openpilot running.")
            print("Please SSH into your comma device and run this script there.")
            print("\nUsage:")
            print("  1. SSH into comma device: ssh comma@[device-ip]")
            print("  2. Navigate to openpilot: cd /data/openpilot")
            print("  3. Run this script: python debug_brake_can.py")
            sys.exit(1)
        
        CP = messaging.log_from_bytes(car_params_bytes)
    except Exception as e:
        print(f"ERROR: Failed to load CarParams: {e}")
        print("\nMake sure openpilot has been started at least once to generate CarParams.")
        sys.exit(1)
    
    # Create CAN parsers for the signals we're interested in
    pt_signals = [
        ("BrakeSysFeatures_2", 50),  # Contains BrkLamp_B_Rq
        ("EngBrakeData", 10),         # Contains BpedDrvAppl_D_Actl
        ("BrakeSnData_4", 50),        # Contains BrkTot_Tq_Actl
        ("DesiredTorqBrk", 50),       # Contains VehStop_D_Stat
    ]
    
    cam_signals = [
        ("ACCDATA", 50),              # Contains ACC brake signals
    ]
    
    cp_pt = CANParser(DBC[CP.carFingerprint][Bus.pt], pt_signals, CanBus(CP).main)
    cp_cam = CANParser(DBC[CP.carFingerprint][Bus.pt], cam_signals, CanBus(CP).camera)
    
    print("=" * 80)
    print("Ford Brake CAN Signal Monitor")
    print("=" * 80)
    print("\nMonitoring brake-related CAN signals...")
    print("Press Ctrl+C to exit\n")
    
    # Track last values to detect changes
    last_values = {}
    
    try:
        while True:
            cp_pt.update_strings([])
            cp_cam.update_strings([])
            
            # Monitor BrakeSysFeatures_2
            try:
                brake_lamp = cp_pt.vl["BrakeSysFeatures_2"]["BrkLamp_B_Rq"]
                key = "BrkLamp_B_Rq"
                if key not in last_values or last_values[key] != brake_lamp:
                    print(f"[BrakeSysFeatures_2] BrkLamp_B_Rq: {brake_lamp} {'(BRAKE LIGHTS ON)' if brake_lamp == 1 else '(BRAKE LIGHTS OFF)'}")
                    last_values[key] = brake_lamp
            except KeyError:
                pass
            
            # Monitor EngBrakeData
            try:
                brake_applied = cp_pt.vl["EngBrakeData"]["BpedDrvAppl_D_Actl"]
                key = "BpedDrvAppl_D_Actl"
                if key not in last_values or last_values[key] != brake_applied:
                    print(f"[EngBrakeData] BpedDrvAppl_D_Actl: {brake_applied} {'(BRAKE PEDAL PRESSED)' if brake_applied == 2 else '(BRAKE PEDAL RELEASED)'}")
                    last_values[key] = brake_applied
            except KeyError:
                pass
            
            # Monitor BrakeSnData_4
            try:
                brake_torque = cp_pt.vl["BrakeSnData_4"]["BrkTot_Tq_Actl"]
                key = "BrkTot_Tq_Actl"
                if key not in last_values or abs(last_values.get(key, 0) - brake_torque) > 100:
                    print(f"[BrakeSnData_4] BrkTot_Tq_Actl: {brake_torque} (Torque: {brake_torque/32756:.3f} Nm)")
                    last_values[key] = brake_torque
            except KeyError:
                pass
            
            # Monitor DesiredTorqBrk
            try:
                veh_stop = cp_pt.vl["DesiredTorqBrk"]["VehStop_D_Stat"]
                key = "VehStop_D_Stat"
                if key not in last_values or last_values[key] != veh_stop:
                    print(f"[DesiredTorqBrk] VehStop_D_Stat: {veh_stop} {'(VEHICLE STOPPED)' if veh_stop == 1 else '(VEHICLE MOVING)'}")
                    last_values[key] = veh_stop
            except KeyError:
                pass
            
            # Monitor ACCDATA
            try:
                acc_data = cp_cam.vl["ACCDATA"]
                
                # AccBrkPrchg_B_Rq
                key = "AccBrkPrchg_B_Rq"
                value = acc_data.get("AccBrkPrchg_B_Rq", 0)
                if key not in last_values or last_values[key] != value:
                    print(f"[ACCDATA] AccBrkPrchg_B_Rq: {value} {'(ACC PRE-CHARGE ACTIVE)' if value == 1 else '(ACC PRE-CHARGE INACTIVE)'}")
                    last_values[key] = value
                
                # AccBrkDecel_B_Rq
                key = "AccBrkDecel_B_Rq"
                value = acc_data.get("AccBrkDecel_B_Rq", 0)
                if key not in last_values or last_values[key] != value:
                    print(f"[ACCDATA] AccBrkDecel_B_Rq: {value} {'(ACC DECEL ACTIVE)' if value == 1 else '(ACC DECEL INACTIVE)'}")
                    last_values[key] = value
                
                # AccBrkTot_A_Rq
                key = "AccBrkTot_A_Rq"
                value = acc_data.get("AccBrkTot_A_Rq", 0)
                if key not in last_values or abs(last_values.get(key, 0) - value) > 0.1:
                    print(f"[ACCDATA] AccBrkTot_A_Rq: {value:.2f} m/s²")
                    last_values[key] = value
                
                # Cmbb_B_Enbl
                key = "Cmbb_B_Enbl"
                value = acc_data.get("Cmbb_B_Enbl", 0)
                if key not in last_values or last_values[key] != value:
                    print(f"[ACCDATA] Cmbb_B_Enbl: {value} {'(ACC ENABLED)' if value == 1 else '(ACC DISABLED)'}")
                    last_values[key] = value
                    
            except KeyError as e:
                print(f"Error reading ACCDATA: {e}")
            
            time.sleep(0.02)  # 50Hz update rate
            
    except KeyboardInterrupt:
        print("\n\nStopping CAN signal monitor...")
    except Exception as e:
        print(f"Error: {e}")
        raise


if __name__ == "__main__":
    main()