#!/usr/bin/env python3
import sys
import os
import argparse
import subprocess

def check_power_save_state():
    """Check if power save mode is active by counting online CPU cores"""
    try:
        # Count online CPU cores
        with open('/sys/devices/system/cpu/online', 'r') as f:
            online_cpus = f.read().strip()

        # Parse the online CPU range (e.g., "0-3" means 4 cores)
        if '-' in online_cpus:
            start, end = map(int, online_cpus.split('-'))
            core_count = end - start + 1
        else:
            core_count = len(online_cpus.split(','))

        return core_count <= 4
    except:
        # Fallback to multiprocessing if sysfs is not available
        import multiprocessing
        return multiprocessing.cpu_count() <= 4

def set_power_save(enable):
    """Set power save mode by controlling CPU cores"""
    try:
        if enable:
            # Enable power save: turn off big cluster (cores 4-7)
            for i in range(4, 8):
                result = subprocess.run(['sudo', 'sh', '-c', f'echo 0 > /sys/devices/system/cpu/cpu{i}/online'],
                                      capture_output=True, text=True, check=False)
                if result.returncode != 0:
                    print(f"Warning: Could not disable CPU {i}: {result.stderr}")
            print("Power save mode enabled")
        else:
            # Disable power save: turn on big cluster (cores 4-7)
            for i in range(4, 8):
                result = subprocess.run(['sudo', 'sh', '-c', f'echo 1 > /sys/devices/system/cpu/cpu{i}/online'],
                                      capture_output=True, text=True, check=False)
                if result.returncode != 0:
                    print(f"Warning: Could not enable CPU {i}: {result.stderr}")
            print("Power save mode disabled")
    except Exception as e:
        print(f"Error setting power save mode: {e}")
        # Don't exit with error, just log the issue
        return False
    return True

def main():
    parser = argparse.ArgumentParser(description='BP Power Management')
    parser.add_argument('--disable', action='store_true', help='Disable power save mode')
    parser.add_argument('--restore', action='store_true', help='Restore power save mode to previous state')
    parser.add_argument('--get-state', action='store_true', help='Get current power save state')
    args = parser.parse_args()

    if args.get_state:
        power_save_active = check_power_save_state()
        print(f"power_save_active={power_save_active}")
        sys.exit(0 if power_save_active else 1)

    elif args.disable:
        success = set_power_save(False)
        sys.exit(0 if success else 1)

    elif args.restore:
        success = set_power_save(True)
        sys.exit(0 if success else 1)

    else:
        parser.print_help()
        sys.exit(1)

if __name__ == "__main__":
    main()
