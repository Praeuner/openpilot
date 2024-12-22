import math
from opendbc.can.packer import CANPacker
from opendbc.car import ACCELERATION_DUE_TO_GRAVITY, Bus, DT_CTRL, apply_std_steer_angle_limits, structs
from opendbc.car.ford import fordcan
from opendbc.car.ford.values import CarControllerParams, FordFlags
from opendbc.car.common.numpy_fast import clip, interp
from opendbc.car.interfaces import CarControllerBase, V_CRUISE_MAX
from openpilot.common.params import Params
from opendbc.car.ford.helpers import (
  initialize_param_defaults,
  update_settings_params,
  load_initial_cc_pref_params,
  get_ford_vehicle_tuning_carcontroller,
  compute_dm_msg_values,
  logDebug,
)

LongCtrlState = structs.CarControl.Actuators.LongControlState
VisualAlert = structs.CarControl.HUDControl.VisualAlert


def apply_ford_curvature_limits(apply_curvature, apply_curvature_last, current_curvature, v_ego_raw):
  # No blending at low speed due to lack of torque wind-up and inaccurate current curvature
  if v_ego_raw > 9:
    apply_curvature = clip(apply_curvature, current_curvature - CarControllerParams.CURVATURE_ERROR,
                           current_curvature + CarControllerParams.CURVATURE_ERROR)

  # Curvature rate limit after driver torque limit
  apply_curvature = apply_std_steer_angle_limits(apply_curvature, apply_curvature_last, v_ego_raw, CarControllerParams)

  return clip(apply_curvature, -CarControllerParams.CURVATURE_MAX, CarControllerParams.CURVATURE_MAX)


def apply_creep_compensation(accel: float, v_ego: float) -> float:
  creep_accel = interp(v_ego, [1., 3.], [0.6, 0.])
  creep_accel = interp(accel, [0., 0.2], [creep_accel, 0.])
  accel -= creep_accel
  return accel


class CarController(CarControllerBase):
  def __init__(self, dbc_names, CP):
    super().__init__(dbc_names, CP)

    self.params = Params()
    # Allow the menu to be shown
    self.params.put('FordMenuAllowed', '1')

    self.packer = CANPacker(dbc_names[Bus.pt])
    self.CAN = fordcan.CanBus(CP)

    # Load the initial parameters
    load_initial_cc_pref_params(self)

    self.apply_curvature_last = 0
    self.accel = 0.0
    self.gas = 0.0
    self.brake_request = False

    # Initialize the last values
    self.main_on_last = False  # previous main cruise control state
    self.lkas_enabled_last = False  # previous lkas state
    self.steer_alert_last = False  # previous status of steering alert
    self.fcw_alert_last = False  # previous status of collision alert
    self.send_ui_last = False  # previous state of ui elements
    self.send_bars_ts_last = 0  # prevous state of ui elements
    self.send_bars_last = False  # previous state of ACC Gap elements
    self.lead_distance_bars_last = None
    self.distance_bar_frame = 0

    # Human turn detection
    self.human_turn = False

    # print(f'Car Fingerprint (CarController): {CP.carFingerprint}')
    logDebug(f'Car Fingerprint (CarController): {CP.carFingerprint}')
    # Ford Model Specific Tuning
    if CP.flags & FordFlags.CANFD:
      # Check FORD_VEHICLE_TUNINGS has a key for the carFingerprint
      ford_tuning = get_ford_vehicle_tuning_carcontroller(CP.carFingerprint)
      if ford_tuning:
        # loop throught each key in ford_tuning and set the value to the corresponding key in the CarController object
        for key in ford_tuning:
          logDebug(f'Ford Tuning (carcontroller.py) Key: {key} | Value: {ford_tuning[key]}')
          if ford_tuning[key] is not None:
            setattr(self, key, ford_tuning[key])

    # check each param in helpers.SETTINGS_PARAMS to make sure they are set and if not sets them to the default values based on the car being driven
    initialize_param_defaults(self)

  def update(self, CC, CS, now_nanos):
    can_sends = []

    # Trigger the update of the settings params defined in helpers.SETTINGS_PARAMS
    update_settings_params(self)
    actuators = CC.actuators
    hud_control = CC.hudControl

    main_on = CS.out.cruiseState.available  # main cruise control button status
    steer_alert = 0  # alerts on screen and dashboard
    fcw_alert = hud_control.visualAlert == VisualAlert.fcw  # alert on screen and dashboard

    # Compute the DM message values
    tja_msg = 0
    tja_warn = 0
    if self.send_driver_monitor_can_msg:
      if self.send_hands_free_cluster_msg:
        # print(f'HudControl: {hud_control}')
        # print(f'tja_msg: {tja_msg} | tja_warn: {tja_warn}')
        tja_msg, tja_warn = compute_dm_msg_values(hud_control, self.send_hands_free_cluster_msg)
    else:
      steer_alert = hud_control.visualAlert in (VisualAlert.steerRequired, VisualAlert.ldw)

    ### acc buttons ###
    if CC.cruiseControl.cancel:
      can_sends.append(fordcan.create_button_msg(self.packer, self.CAN.camera, CS.buttons_stock_values, cancel=True))
      can_sends.append(fordcan.create_button_msg(self.packer, self.CAN.main, CS.buttons_stock_values, cancel=True))
    elif CC.cruiseControl.resume and (self.frame % CarControllerParams.BUTTONS_STEP) == 0:
      can_sends.append(fordcan.create_button_msg(self.packer, self.CAN.camera, CS.buttons_stock_values, resume=True))
      can_sends.append(fordcan.create_button_msg(self.packer, self.CAN.main, CS.buttons_stock_values, resume=True))
    # if stock lane centering isn't off, send a button press to toggle it off
    # the stock system checks for steering pressed, and eventually disengages cruise control
    elif CS.acc_tja_status_stock_values["Tja_D_Stat"] != 0 and (self.frame % CarControllerParams.ACC_UI_STEP) == 0:
      can_sends.append(fordcan.create_button_msg(self.packer, self.CAN.camera, CS.buttons_stock_values, tja_toggle=True))

    ### lateral control ###
    # send steer msg at 20Hz
    if (self.frame % CarControllerParams.STEER_STEP) == 0:
      if CC.latActive:
        # Get steering state for human turn detection
        steeringPressed = CS.out.steeringPressed
        steeringAngleDeg = CS.out.steeringAngleDeg

        # Human turn detection
        if steeringPressed and abs(steeringAngleDeg) > 45 and self.enable_human_turn_detection:
          self.human_turn = True
        else:
          self.human_turn = False

        # apply rate limits, curvature error limit, and clip to signal range
        current_curvature = -CS.out.yawRate / max(CS.out.vEgoRaw, 0.1)
        apply_curvature = apply_ford_curvature_limits(actuators.curvature, self.apply_curvature_last, current_curvature, CS.out.vEgoRaw)

        # Reset steering if human turn detected
        if self.human_turn:
          apply_curvature = 0
      else:
        apply_curvature = 0.

      self.apply_curvature_last = apply_curvature
      lat_active = CC.latActive and not self.human_turn

      if self.CP.flags & FordFlags.CANFD:
        # TODO: extended mode
        # Ford uses four individual signals to dictate how to drive to the car. Curvature alone (limited to 0.02m/s^2)
        # can actuate the steering for a large portion of any lateral movements. However, in order to get further control on
        # steer actuation, the other three signals are necessary. Ford controls vehicles differently than most other makes.
        # A detailed explanation on ford control can be found here:
        # https://www.f150gen14.com/forum/threads/introducing-bluepilot-a-ford-specific-fork-for-comma3x-openpilot.24241/#post-457706
        mode = 1 if (lat_active) else 0  # Disable control during human turns
        ramp_type = 0
        precision_type = 1
        counter = (self.frame // CarControllerParams.STEER_STEP) % 0x10
        # can_sends.append(fordcan.create_lat_ctl2_msg(self.packer, self.CAN, mode, 0., 0., -apply_curvature, 0., counter))
        can_sends.append(fordcan.create_lat_ctl2_msg(self.packer, self.CAN, mode, 0., 0., -apply_curvature, 0., counter, ramp_type, precision_type))
      else:
        ramp_type = 0
        precision_type = 1
        can_sends.append(fordcan.create_lat_ctl_msg(self.packer, self.CAN, lat_active, 0., 0., -apply_curvature, 0., ramp_type, precision_type))

    # send lka msg at 33Hz
    if (self.frame % CarControllerParams.LKA_STEP) == 0:
      can_sends.append(fordcan.create_lka_msg(self.packer, self.CAN, CC.latActive, hud_control if self.send_lane_depart_can_msg else None))

    ### longitudinal control ###
    # send acc msg at 50Hz
    if self.CP.openpilotLongitudinalControl and (self.frame % CarControllerParams.ACC_CONTROL_STEP) == 0:
      accel = actuators.accel
      gas = accel

      if CC.longActive:
        # Compensate for engine creep at low speed.
        # Either the ABS does not account for engine creep, or the correction is very slow
        # TODO: verify this applies to EV/hybrid
        accel = apply_creep_compensation(accel, CS.out.vEgo)

        # The stock system has been seen rate limiting the brake accel to 5 m/s^3,
        # however even 3.5 m/s^3 causes some overshoot with a step response.
        accel = max(accel, self.accel - (3.5 * CarControllerParams.ACC_CONTROL_STEP * DT_CTRL))

      accel = clip(accel, CarControllerParams.ACCEL_MIN, CarControllerParams.ACCEL_MAX)

      # Both gas and accel are in m/s^2, accel is used solely for braking
      if not CC.longActive or gas < CarControllerParams.MIN_GAS:
        gas = CarControllerParams.INACTIVE_GAS

      # PCM applies pitch compensation to gas/accel, but we need to compensate for the brake/pre-charge bits
      accel_due_to_pitch = 0.0
      if len(CC.orientationNED) == 3:
        accel_due_to_pitch = math.sin(CC.orientationNED[1]) * ACCELERATION_DUE_TO_GRAVITY

      accel_pitch_compensated = accel + accel_due_to_pitch
      if accel_pitch_compensated > self.brake_actuator_activate + self.precharge_actuator_target_delta or not CC.longActive:
        self.brake_request = False
      elif accel_pitch_compensated < self.brake_actuator_activate:
        self.brake_request = True

      stopping = CC.actuators.longControlState == LongCtrlState.stopping
      # TODO: look into using the actuators packet to send the desired speed
      can_sends.append(fordcan.create_acc_msg(self.packer, self.CAN, CC.longActive, gas, accel, stopping, self.brake_request, v_ego_kph=V_CRUISE_MAX))

      self.accel = accel
      self.gas = gas

    ### ui ###
    send_ui = (self.main_on_last != main_on) or (self.lkas_enabled_last != CC.latActive) or (self.steer_alert_last != steer_alert)
    # send lkas ui msg at 1Hz or if ui state changes
    if (self.frame % CarControllerParams.LKAS_UI_STEP) == 0 or send_ui:
      can_sends.append(fordcan.create_lkas_ui_msg(self.packer, self.CAN, main_on, CC.latActive, steer_alert, hud_control, CS.lkas_status_stock_values))

    send_bars = False
    # send acc ui msg at 5Hz or if ui state changes
    if hud_control.leadDistanceBars != self.lead_distance_bars_last:
      send_ui = True
      send_bars = True

    # Logic to keep sending the bars for 4 seconds
    if not self.send_bars_last and send_bars:
      # Save the frame # for the last flip from False to True
      self.send_bars_ts_last = self.frame

    # keep sending the bars for 4 seconds (400 at 100Hz)
    if ( self.send_bars_ts_last > 0 and (self.frame - self.send_bars_ts_last) <= (400)):
      send_ui = True
      send_bars = True

    if (self.frame % CarControllerParams.ACC_UI_STEP) == 0 or send_ui:
      can_sends.append(
        fordcan.create_acc_ui_msg(
          self.packer,
          self.CAN,
          self.CP,
          main_on,
          CC.latActive,
          fcw_alert,
          CS.out.cruiseState.standstill,
          hud_control,
          CS.acc_tja_status_stock_values,
          self.send_hands_free_cluster_msg,
          send_ui,
          send_bars,
          tja_warn,
          tja_msg
        )
      )

    self.main_on_last = main_on
    self.send_ui_last = send_ui
    self.send_bars_last = send_bars
    self.lkas_enabled_last = CC.latActive
    self.steer_alert_last = steer_alert
    self.fcw_alert_last = fcw_alert
    self.lead_distance_bars_last = hud_control.leadDistanceBars

    new_actuators = actuators.as_builder()
    new_actuators.curvature = self.apply_curvature_last
    new_actuators.accel = self.accel
    new_actuators.gas = self.gas
    new_actuators.brake = float(self.brake_request)

    self.frame += 1
    return new_actuators, can_sends
