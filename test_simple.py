#!/usr/bin/env python3
"""
Simple test for Onroad Display Behavior test mode
"""

def test():
    try:
        from selfdrive.ui.ui_state import device

        print("Simple Test Mode")
        print("=" * 30)

        # Check current state
        print(f"Current brightness: {device._last_brightness}")
        print(f"Test mode enabled: {device.params.get('OffroadDisplayTest', '0')}")

        # Enable test mode
        print("\nEnabling test mode...")
        device.params.put("OffroadDisplayTest", "1")

        # Set behavior to dim 70%
        print("Setting behavior to Dim 70%...")
        device.params.put("OnroadDisplayBehavior", "1")
        device.params.put("OnroadDisplayTimeout", "0")

        # Simulate onroad
        import ui_state
        original = ui_state.started
        ui_state.started = True

        try:
            # Force update
            device._update_onroad_display_behavior()
            print("Test mode active - behavior should apply immediately")
            print(f"Current brightness: {device._last_brightness}")
        finally:
            # Restore
            ui_state.started = original
            device.params.put("OffroadDisplayTest", "0")
            print("Test mode disabled")

    except Exception as e:
        print(f"Test failed: {e}")

if __name__ == "__main__":
    test()
