import math
import cereal.messaging as messaging
import numpy as np
from collections import deque  # used for moving averages
from opendbc.can.packer import CANPacker
from opendbc.car import ACCELERATION_DUE_TO_GRAVITY, Bus, DT_CTRL, apply_std_steer_angle_limits, structs
from opendbc.car.ford import fordcan
from opendbc.car.ford.values import CarControllerParams, FordFlags
from opendbc.car.interfaces import CarControllerBase, V_CRUISE_MAX
from openpilot.common.params import Params
from openpilot.common.filter_simple import FirstOrderFilter
from opendbc.car.ford.helpers import (
  initialize_param_defaults,
  update_settings_params,
  load_initial_cc_pref_params,
  get_ford_vehicle_tuning_carcontroller,
  actuators_calc,
  compute_dm_msg_values,
  logDebug,
)
from openpilot.selfdrive.controls.lib.drive_helpers import CONTROL_N
from openpilot.selfdrive.modeld.constants import ModelConstants

LongCtrlState = structs.CarControl.Actuators.LongControlState
VisualAlert = structs.CarControl.HUDControl.VisualAlert

def index_function(idx, max_val=192, max_idx=32):
  return (max_val) * ((idx/max_idx)**2)

# ISO 11270
ISO_LATERAL_ACCEL = 3.0  # m/s^2  # TODO: import from test lateral limits file?

# Limit to average banked road since safety doesn't have the roll
EARTH_G = 9.81
AVERAGE_ROAD_ROLL = 0.06  # ~3.4 degrees, 6% superelevation
MAX_LATERAL_ACCEL = ISO_LATERAL_ACCEL - (EARTH_G * AVERAGE_ROAD_ROLL)  # ~2.4 m/s^2
IDX_N = 33
T_IDXS = [index_function(idx, max_val=10.0) for idx in range(IDX_N)]

def apply_ford_curvature_limits(apply_curvature, apply_curvature_last, current_curvature, v_ego_raw, steering_angle, lat_active, CP):
  # No blending at low speed due to lack of torque wind-up and inaccurate current curvature
  if v_ego_raw > 9:
    apply_curvature = np.clip(apply_curvature, current_curvature - CarControllerParams.CURVATURE_ERROR,
                              current_curvature + CarControllerParams.CURVATURE_ERROR)

  # Curvature rate limit after driver torque limit
  apply_curvature = apply_std_steer_angle_limits(apply_curvature, apply_curvature_last, v_ego_raw, steering_angle, lat_active, CarControllerParams.ANGLE_LIMITS)

  # Ford Q4/CAN FD has more torque available compared to Q3/CAN so we limit it based on lateral acceleration.
  # Safety is not aware of the road roll so we subtract a conservative amount at all times
  if CP.flags & FordFlags.CANFD:
    # Limit curvature to conservative max lateral acceleration
    curvature_accel_limit = MAX_LATERAL_ACCEL / (max(v_ego_raw, 1) ** 2)
    apply_curvature = float(np.clip(apply_curvature, -curvature_accel_limit, curvature_accel_limit))

  return apply_curvature


def apply_creep_compensation(accel: float, v_ego: float) -> float:
  creep_accel = np.interp(v_ego, [1., 3.], [0.6, 0.])
  creep_accel = np.interp(accel, [0., 0.2], [creep_accel, 0.])
  accel -= creep_accel
  return float(accel)


class CarController(CarControllerBase):
  def __init__(self, dbc_names, CP, CP_SP):
    super().__init__(dbc_names, CP, CP_SP)

    self.params = Params()

    self.packer = CANPacker(dbc_names[Bus.pt])
    self.CAN = fordcan.CanBus(CP)

    # Load the initial preference settings parameters like send_hands_free_cluster_msg, enable_human_turn_detection, and anti_ping_pong_value, curvature_max, and curvature_error from the params
    load_initial_cc_pref_params(self)

    self.apply_curvature_last = 0  # previous value of apply_curvature
    self.accel = 0.0
    self.gas = 0.0
    self.brake_request = False
    self.main_on_last = False  # previous main cruise control state
    self.lkas_enabled_last = False  # previous lkas state
    self.steer_alert_last = False  # previous status of steering alert
    self.fcw_alert_last = False  # previous status of collision alert
    self.send_ui_last = False  # previous state of ui elements
    self.send_bars_ts_last = 0  # prevous state of ui elements
    self.send_bars_last = False  # previous state of ACC Gap elements
    self.lead_distance_bars_last = None
    self.distance_bar_frame = 0

     # Variables to initialize (these get updated every scan as part of the control code)
    self.accel_pitch_compensated = 0.0
    self.path_lookup_time = 0.5  # how far into the future to we need to look for our path_angle signals.
    self.precision_type = 1  # precise or comfort
    self.human_turn_frames = 0  # how many scans has a human been turning the wheel
    self.human_turn = False  # have we detected a human override in a turn
    self.steer_warning = False # warning for steering limits exceeded
    self.steer_warning_count = 0 # count how many cycles the warning has existed

    # Curvature rate variables
    self.curvature_rate_delta_t = 0.3  # [s] used in denominator for curvature rate calculation
    self.curvature_rate_deque = deque(maxlen=int(round(self.curvature_rate_delta_t / 0.05)))  # 0.3 seconds at 20Hz

    ############################ path offset variables
    self.path_offset_lat_accel_adjust_scale = 0.00 # this variable was calculated in plot_juggler.  It's used to help determine path_offset in curves.
    self.custom_path_offset = 0.0 # updated from UI: applies a custom offset to help with in-lane positioning
    self.offset_lookup_time = 0.20 # time into the future to use for calculating path_offset
    self.lane_width_tolerance_factor = 0.75
    self.min_laneline_confidence_bp = [0.6, 0.8]

    ########################### path angle variables
    self.path_angle_filter_samples = 6 # number of samples to use for the moving average filter
    self.path_angle_deque = deque(maxlen=self.path_angle_filter_samples) # deque to hold the samples
    self.path_angle_wheel_angle_conversion = 0.0017 # degrees to milliradians
    self.path_angle_speed_bp = [4.4, 40.23]  # what speeds to adjust path_angle_speed_factor over.
    self.path_angle_low_speed_factor = 0.15 # path_angle_speed_factor at 4.45 m/s
    self.path_angle_high_speed_factor = 3.0 # path_angle_speed_factor at 40.23 m/s

    # max absolute values for all four signals
    self.path_angle_max = 0.25  # too much path_angle can cause jerky steering
    self.path_offset_max = 1.50  # too much path_offset can cause jerky steering
    self.curvature_max = 0.02  # too much curvature can cause steering windup
    self.curvature_rate_max = 0.001023  # from dbc files

    # values from previous frame
    self.curvature_rate_last = 0.0
    self.path_offset_last = 0.0
    self.path_angle_last = 0.0
    self.curvature_rate = 0  # initialize curvature_rate

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
    self.sm = messaging.SubMaster(['modelV2'])
    self.model = None

  def update(self, CC, CC_SP, CS, now_nanos):
    can_sends = []
    self.sm.update(0)

    if self.sm.updated['modelV2']:
      self.model = self.sm["modelV2"]

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
    apply_curvature = 0.0 # initialize apply_curvature
    desired_curvature_rate = 0.0 # initialize desired_curvature_rate
    path_offset = 0.0 # initialize path_offset
    path_angle = 0.0 # initialize path_angle
    reset_steering = 0 # initialize reset_steering
    ramp_type = 2 # initialize ramp_type
    precision_type = 1 # initialize precision_type

    # send steer msg at 20Hz
    if (self.frame % CarControllerParams.STEER_STEP) == 0:
      if CC.latActive:
        # Get steering state for human turn detection
        steeringPressed = CS.out.steeringPressed
        steeringAngleDeg = CS.out.steeringAngleDeg
        steeringAngleDeg_PV = CS.out.steeringAngleDeg
        steeringAngleDeg_SP = actuators.steeringAngleDeg
        ramp_type = 2
        precision_type = 1

        # Human turn detection
        if steeringPressed and abs(steeringAngleDeg) > 45 and self.enable_human_turn_detection:
          self.human_turn = True
        else:
          self.human_turn = False

        # apply rate limits, curvature error limit, and clip to signal range
        current_curvature = -CS.out.yawRate / max(CS.out.vEgoRaw, 0.1)
        requested_curvature = actuators.curvature
        apply_curvature = apply_ford_curvature_limits(requested_curvature, self.apply_curvature_last, current_curvature, CS.out.vEgoRaw)

        # detect if steering was limited
        if (requested_curvature != apply_curvature) and (not steeringPressed):
          self.steering_limited = True
        else:
          self.steering_limited =False

        # if steering was limited turn on steer_warning if above 15mph
        if self.steering_limited and CS.out.vEgoRaw > 7:
            self.steer_warning = True

        # latch steer_warning and count cycles before clearing
        if self.steer_warning and not self.steering_limited:
            self.steer_warning_count = self.steer_warning_count + 1

        # clear steer_warning after 10 counts of no steering limited
        if self.steer_warning_count > 10:
          self.steer_warning = False
          self.steer_warning_count = 0

        # compute curvature rate
        self.curvature_rate_deque.append(apply_curvature)
        if len(self.curvature_rate_deque) > 1:
          delta_t = (
            self.curvature_rate_delta_t if len(self.curvature_rate_deque) == self.curvature_rate_deque.maxlen else (len(self.curvature_rate_deque) - 1) * 0.05
          )
          desired_curvature_rate = (self.curvature_rate_deque[-1] - self.curvature_rate_deque[0]) / delta_t / max(0.01, CS.out.vEgoRaw)
        else:
          desired_curvature_rate = 0.0

        if self.model is not None and len(self.model.orientation.x) >= CONTROL_N:
          # get path offset from model.position.y
          path_offset_model = np.interp(self.offset_lookup_time, T_IDXS, self.model.position.y)

          # now get path offset from lanelines
          path_offset_lanelines = (self.model.laneLines[1].y[0] + self.model.laneLines[2].y[0]) / 2

          # determinie laneline width tolerance scaling factor
          laneline_width = self.model.laneLines[2].y[0] + (-self.model.laneLines[1].y[0]) # laneLines[1] is a negative value because it is left of the vehicle.

          laneline_width_tolerance = np.interp(laneline_width, [3.75,4.25], [0.81, 0.59]) # 3.7 is the width of standard US lane in meters

          # determine laneline confidence
          laneline_confidence = min(self.model.laneLineProbs[1], self.model.laneLineProbs[2], laneline_width_tolerance)

          # determine laneline path offset scale
          laneline_path_offset_scale = np.interp(laneline_confidence, self.min_laneline_confidence_bp, [0.0, 1.0])

          # determine if a lane change is active
          if self.model.meta.laneChangeState == 1 or self.model.meta.laneChangeState == 2:
            self.lane_change = True
          else:
            self.lane_change = False

          # no path_offset during lane changes (it will fight you until it swaps to new lane if you don't set to zero)
          if self.lane_change:
            if apply_curvature > 0 and self.custom_path_offset > 0:
              path_offset_total = self.custom_path_offset
            else:
              path_offset_total = 0
            if apply_curvature < 0 and self.custom_path_offset < 0:
              path_offset_total = self.custom_path_offset
            else:
              path_offset_total = 0
          else:
            path_offset_total = (path_offset_model * (1-laneline_path_offset_scale) + (path_offset_lanelines * laneline_path_offset_scale)) + self.custom_path_offset

          # Use path_angle to help with centering vehicle in lane, derive path_angle from the models desired steering wheel position
          # path_angle is a corrective variable, so subtract out current wheel position (associated with curvature)

          # calcualte the path_angle_speed_factor
          path_angle_speed_v = [self.path_angle_low_speed_factor, self.path_angle_high_speed_factor]  # what should the range on path_angle_speed_factor be at low and high speed
          path_angle_speed_factor = np.interp(abs(CS.out.vEgoRaw), self.path_angle_speed_bp, path_angle_speed_v)

          # calculate steering angle associated with the base path (predicted_curvature)
          steering_wheel_delta = steeringAngleDeg_PV - steeringAngleDeg_SP

          # calculate wheel angle from path_offset
          steerAnglePathOffset = steering_wheel_delta * self.path_angle_wheel_angle_conversion * path_angle_speed_factor

          # filter path_angle for smoothing
          self.path_angle_deque.append(steerAnglePathOffset)
          path_angle_model = sum(self.path_angle_deque) / len(self.path_angle_deque) if len(self.path_angle_deque) > 0 else 0.0
          if self.lane_change:
            path_angle_model = 0.0

          # set path_offset and path_angle
          path_offset = path_offset_total
          path_angle = path_angle_model

          # rate limit path_angle
          path_angle_roc = np.interp(abs(CS.out.vEgoRaw), [5, 25],  [0.003, 0.002])
          path_angle = np.clip(path_angle, self.path_angle_last - path_angle_roc, self.path_angle_last + path_angle_roc)

          # clip all values
          apply_curvature = np.clip(apply_curvature, -self.curvature_max, self.curvature_max)
          desired_curvature_rate = np.clip(desired_curvature_rate, -self.curvature_rate_max, self.curvature_rate_max)
          path_offset = np.clip(path_offset, -self.path_offset_max, self.path_offset_max)
          path_angle = np.clip(path_angle, -self.path_angle_max, self.path_angle_max)

          # Determine if a human is making a turn and trap the value
          # if a human turn is active, reset steering to prevent windup
          if steeringPressed and abs(steeringAngleDeg_PV) > 45:
            self.human_turn = True
          else:
            self.human_turn = False

          # Determine when to reset steering
          if (self.human_turn) and self.enable_human_turn_detection:
            reset_steering = 1
          else:
            reset_steering = 0

          # reset steering by setting all values to 0 and ramp_type to immediate
          if reset_steering == 1:
            apply_curvature = 0
            path_offset = 0
            path_angle = 0
            desired_curvature_rate = 0
            ramp_type = 3
            self.path_angle_deque.clear()
          else:
            ramp_type = 2

          # remove path_angle and path_offset if not enabled
          if self.enable_path_angle == 0:
            path_angle = 0
            path_offset = 0
            desired_curvature_rate = 0

      self.apply_curvature_last = apply_curvature
      self.curvature_rate_last = desired_curvature_rate
      self.path_offset_last = path_offset
      self.path_angle_last = path_angle

      lat_active = CC.latActive and not self.human_turn

      # send steer msg at 20Hz

      if self.CP.flags & FordFlags.CANFD:
        # TODO: extended mode
        # Ford uses four individual signals to dictate how to drive to the car. Curvature alone (limited to 0.02m/s^2)
        # can actuate the steering for a large portion of any lateral movements. However, in order to get further control on
        # steer actuation, the other three signals are necessary. Ford controls vehicles differently than most other makes.
        # A detailed explanation on ford control can be found here:
        # https://www.f150gen14.com/forum/threads/introducing-bluepilot-a-ford-specific-fork-for-comma3x-openpilot.24241/#post-457706

        mode = 1 if (lat_active) else 0  # Disable control during human turns
        counter = (self.frame // CarControllerParams.STEER_STEP) % 0x10
        # can_sends.append(fordcan.create_lat_ctl2_msg(self.packer, self.CAN, mode, 0., 0., -apply_curvature, 0., counter))
        can_sends.append(fordcan.create_lat_ctl2_msg(self.packer, self.CAN, mode, -path_offset, -path_angle, -apply_curvature, -desired_curvature_rate, counter, ramp_type, precision_type))
      else:
        can_sends.append(fordcan.create_lat_ctl_msg(self.packer, self.CAN, lat_active, ramp_type, self.precision_type, -path_offset, -path_angle, -apply_curvature, -desired_curvature_rate))

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

      accel = float(np.clip(accel, CarControllerParams.ACCEL_MIN, CarControllerParams.ACCEL_MAX))
      gas = float(np.clip(gas, CarControllerParams.ACCEL_MIN, CarControllerParams.ACCEL_MAX))

      # Both gas and accel are in m/s^2, accel is used solely for braking
      if not CC.longActive or gas < CarControllerParams.MIN_GAS:
        gas = CarControllerParams.INACTIVE_GAS

      # PCM applies pitch compensation to gas/accel, but we need to compensate for the brake/pre-charge bits
      accel_due_to_pitch = 0.0
      if len(CC.orientationNED) == 3:
        accel_due_to_pitch = math.sin(CC.orientationNED[1]) * ACCELERATION_DUE_TO_GRAVITY

      accel_pitch_compensated = accel + accel_due_to_pitch
      if accel_pitch_compensated > 0.3 or not CC.longActive:
        self.brake_request = False
      elif accel_pitch_compensated < 0.0:
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
    new_actuators.curvature = float(self.apply_curvature_last)
    new_actuators.accel = self.accel
    new_actuators.gas = self.gas
    new_actuators.speed = float(self.accel_pitch_compensated)

    self.frame += 1
    return new_actuators, can_sends
