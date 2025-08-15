from opendbc.can import CANDefine, CANParser
from opendbc.car import Bus, create_button_events, structs
from opendbc.car.common.conversions import Conversions as CV
from openpilot.common.params import Params
from opendbc.car.ford.fordcan import CanBus
from opendbc.car.ford.values import DBC, CarControllerParams, FordConfig, FordFlags
from opendbc.car.interfaces import CarStateBase
from cereal import messaging
from bluepilot.logger.bp_logger import debug, info, warning, error, critical
from opendbc.sunnypilot.car.ford.mads import MadsCarState

# from opendbc.car.ford.fordcanparser import FordCanParser
from opendbc.car.ford.helpers import get_hev_power_flow_text, get_hev_engine_on_reason_text
import time

ButtonType = structs.CarState.ButtonEvent.Type
GearShifter = structs.CarState.GearShifter
TransmissionType = structs.CarParams.TransmissionType


class CarState(CarStateBase, MadsCarState):
  def __init__(self, CP, CP_SP):
    CarStateBase.__init__(self, CP, CP_SP)
    MadsCarState.__init__(self, CP, CP_SP)
    can_define = CANDefine(DBC[CP.carFingerprint][Bus.pt])
    self.params = Params()
    # self.ford_can_parser = FordCanParser(CP)

    self.bluecruise_cluster_present = FordConfig.BLUECRUISE_CLUSTER_PRESENT # Sets the value of whether the car has the blue cruise cluster
    if CP.transmissionType == TransmissionType.automatic:
      if CP.flags & FordFlags.CANFD:
        self.shifter_values = can_define.dv["Gear_Shift_by_Wire_FD1"]["TrnRng_D_RqGsm"]
      elif CP.flags & FordFlags.ALT_STEER_ANGLE:
        self.shifter_values = can_define.dv["TransGearData"]["GearLvrPos_D_Actl"]
      else:
        self.shifter_values = can_define.dv["PowertrainData_10"]["TrnRng_D_Rq"]

    self.cluster_min_speed = CV.KPH_TO_MS * 1.5
    self.cluster_speed_hyst_gap = CV.KPH_TO_MS / 2.
    self.distance_button = 0
    self.lc_button = 0

    # Save the HEV data available flag to a param
    self.params.put_bool("FordPrefHevDataAvailable", True if CP.flags & FordFlags.HEV_CLUSTER_DATA else False)
    self.params.put_bool("FordPrefHevBattDataAvailable", True if CP.flags & FordFlags.HEV_BATTERY_DATA else False)
    self.hev_data_available = CP.flags & FordFlags.HEV_CLUSTER_DATA

    # Drive mode control variables
    self.default_drive_mode = 0  # Normal mode by default
    self.target_drive_mode = 0   # Target drive mode to set
    self.drive_mode_change_requested = False
    self.drive_mode_change_counter = 0
    self.last_drive_mode_change_time = 0

    # Advanced drive mode control variables
    self.target_powertrain_mode = 0  # Default to Normal mode
    self.target_chassis_mode = 0     # Default to Normal mode
    self.target_awd_mode = 0         # Default to 2WD mode
    self.advanced_drive_mode_change_requested = False

    # Load default drive mode from params
    try:
      default_mode = self.params.get("FordDefaultDriveMode")
      if default_mode:
        self.default_drive_mode = int(default_mode)
        self.target_drive_mode = self.default_drive_mode
    except (ValueError, TypeError):
      pass  # Use default value if param is invalid


  def update(self, can_parsers) -> tuple[structs.CarState, structs.CarStateSP]:
    cp = can_parsers[Bus.pt]
    cp_cam = can_parsers[Bus.cam]

	# Publish CAN data first so any parsing errors don't affect critical CarState updates
    # if(self.params.get_bool("FordPrefStreamCanData")):
    #   try:
    #     self.ford_can_parser.publish_can_data(cp, cp_cam, self.CP.carFingerprint)
    #   except Exception as e:
    #     print(f"Error publishing Ford CAN data: {e}")

    ret = structs.CarState()
    ret_sp = structs.CarStateSP()

    if self.CP.flags & FordFlags.ALT_STEER_ANGLE:
      self.vehicle_sensors_valid = (
        int((cp.vl["ParkAid_Data"]["ExtSteeringAngleReq2"] + 1000) * 10) not in (32766, 32767)
        and cp.vl["ParkAid_Data"]["EPASExtAngleStatReq"] == 0
        and cp.vl["ParkAid_Data"]["ApaSys_D_Stat"] in (0, 1)
      )
    else:
   	  # Occasionally on startup, the ABS module recalibrates the steering pinion offset, so we need to block engagement
      # The vehicle usually recovers out of this state within a minute of normal driving
      ret.vehicleSensorsInvalid = cp.vl["SteeringPinion_Data"]["StePinCompAnEst_D_Qf"] != 3

    # car speed
    ret.vEgoRaw = cp.vl["BrakeSysFeatures"]["Veh_V_ActlBrk"] * CV.KPH_TO_MS
    ret.vEgo, ret.aEgo = self.update_speed_kf(ret.vEgoRaw)
    if self.CP.flags & FordFlags.CANFD:
      ret.vEgoCluster = ((cp.vl["Cluster_Info_3_FD1"]["DISPLAY_SPEED_SCALING"]/100) * cp.vl["EngVehicleSpThrottle2"]["Veh_V_ActlEng"] +
                         cp.vl["Cluster_Info_3_FD1"]["DISPLAY_SPEED_OFFSET"]) * CV.KPH_TO_MS

    ret.yawRate = cp.vl["Yaw_Data_FD1"]["VehYaw_W_Actl"]
    ret.standstill = cp.vl["DesiredTorqBrk"]["VehStop_D_Stat"] == 1

    # gas pedal
    ret.gasPressed = cp.vl["EngVehicleSpThrottle"]["ApedPos_Pc_ActlArb"] / 100. > 1e-6

    # brake pedal
    ret.brake = cp.vl["BrakeSnData_4"]["BrkTot_Tq_Actl"] / 32756.  # torque in Nm
    ret.brakePressed = cp.vl["EngBrakeData"]["BpedDrvAppl_D_Actl"] == 2
    ret.parkingBrake = cp.vl["DesiredTorqBrk"]["PrkBrkStatus"] in (1, 2)

    # steering wheel
    if self.CP.flags & FordFlags.ALT_STEER_ANGLE:
      steering_angle_init = cp.vl["SteeringPinion_Data_Alt"]["StePinRelInit_An_Sns"]
      if self.vehicle_sensors_valid:
        steering_angle_est = cp.vl["ParkAid_Data"]["ExtSteeringAngleReq2"]
        self.steering_angle_offset_deg = steering_angle_est - steering_angle_init
      ret.steeringAngleDeg = steering_angle_init + self.steering_angle_offset_deg
    else:
      ret.steeringAngleDeg = cp.vl["SteeringPinion_Data"]["StePinComp_An_Est"]
    ret.steeringTorque = cp.vl["EPAS_INFO"]["SteeringColumnTorque"]
    ret.steeringPressed = self.update_steering_pressed(abs(ret.steeringTorque) > CarControllerParams.STEER_DRIVER_ALLOWANCE, 5)
    ret.steerFaultTemporary = cp.vl["EPAS_INFO"]["EPAS_Failure"] == 1
    ret.steerFaultPermanent = cp.vl["EPAS_INFO"]["EPAS_Failure"] in (2, 3)
    ret.espDisabled = cp.vl["Cluster_Info1_FD1"]["DrvSlipCtlMde_D_Rq"] != 0  # 0 is default mode

    if self.CP.flags & FordFlags.CANFD:
      # this signal is always 0 on non-CAN FD cars
      ret.steerFaultTemporary |= cp.vl["Lane_Assist_Data3_FD1"]["LatCtlSte_D_Stat"] not in (1, 2, 3)

    # cruise state
    is_metric = cp.vl["INSTRUMENT_PANEL"]["METRIC_UNITS"] == 1 if not self.CP.flags & FordFlags.CANFD else cp_cam.vl["IPMA_Data2"]["IsaVLimUnit_D_Rq"] == 1
    ret.cruiseState.speed = cp.vl["EngBrakeData"]["Veh_V_DsplyCcSet"] * (CV.KPH_TO_MS if is_metric else CV.MPH_TO_MS)
    ret.cruiseState.enabled = cp.vl["EngBrakeData"]["CcStat_D_Actl"] in (4, 5)
    ret.cruiseState.available = cp.vl["EngBrakeData"]["CcStat_D_Actl"] in (3, 4, 5)
    ret.cruiseState.nonAdaptive = cp.vl["Cluster_Info1_FD1"]["AccEnbl_B_RqDrv"] == 0
    ret.cruiseState.standstill = cp.vl["EngBrakeData"]["AccStopMde_D_Rq"] == 3
    ret.accFaulted = cp.vl["EngBrakeData"]["CcStat_D_Actl"] in (1, 2)

    if self.CP.flags & FordFlags.CANFD:
      ret.cruiseState.speedLimit = self.update_traffic_signals(cp_cam)

    if not self.CP.openpilotLongitudinalControl:
      ret.accFaulted = ret.accFaulted or cp_cam.vl["ACCDATA"]["CmbbDeny_B_Actl"] == 1

    # gear
    if self.CP.transmissionType == TransmissionType.automatic:
      if self.CP.flags & FordFlags.CANFD:
        gear = self.shifter_values.get(cp.vl["Gear_Shift_by_Wire_FD1"]["TrnRng_D_RqGsm"])
      elif self.CP.flags & FordFlags.ALT_STEER_ANGLE:
           gear = self.shifter_values.get(cp.vl["TransGearData"]["GearLvrPos_D_Actl"])
      else:
        gear = self.shifter_values.get(cp.vl["PowertrainData_10"]["TrnRng_D_Rq"])

      ret.gearShifter = self.parse_gear_shifter(gear)
    elif self.CP.transmissionType == TransmissionType.manual:
      if bool(cp.vl["BCM_Lamp_Stat_FD1"]["RvrseLghtOn_B_Stat"]):
        ret.gearShifter = GearShifter.reverse
      else:
        ret.gearShifter = GearShifter.drive

    # safety
    ret.stockFcw = bool(cp_cam.vl["ACCDATA_3"]["FcwVisblWarn_B_Rq"])
    ret.stockAeb = bool(cp_cam.vl["ACCDATA_2"]["CmbbBrkDecel_B_Rq"])

    # drive mode parsing - store data for use in update_car_state_bp
    try:
      # Parse drive mode signals if available
      if "SelectDriveModeData" in cp.vl:
        self.current_drive_mode_data = cp.vl["SelectDriveModeData"]
      else:
        self.current_drive_mode_data = None
    except Exception as e:
      # Handle any parsing errors gracefully
      print(f"Error parsing drive mode data: {e}")
      self.current_drive_mode_data = None

    # button presses
    ret.leftBlinker = cp.vl["Steering_Data_FD1"]["TurnLghtSwtch_D_Stat"] == 1
    ret.rightBlinker = cp.vl["Steering_Data_FD1"]["TurnLghtSwtch_D_Stat"] == 2
    # TODO: block this going to the camera otherwise it will enable stock TJA
    ret.genericToggle = bool(cp.vl["Steering_Data_FD1"]["TjaButtnOnOffPress"])
    prev_distance_button = self.distance_button
    prev_lc_button = self.lc_button
    self.distance_button = cp.vl["Steering_Data_FD1"]["AccButtnGapTogglePress"]
    self.lc_button = bool(cp.vl["Steering_Data_FD1"]["TjaButtnOnOffPress"])

    # lock info
    ret.doorOpen = any([cp.vl["BodyInfo_3_FD1"]["DrStatDrv_B_Actl"], cp.vl["BodyInfo_3_FD1"]["DrStatPsngr_B_Actl"],
                        cp.vl["BodyInfo_3_FD1"]["DrStatRl_B_Actl"], cp.vl["BodyInfo_3_FD1"]["DrStatRr_B_Actl"]])
    ret.seatbeltUnlatched = cp.vl["RCMStatusMessage2_FD1"]["FirstRowBuckleDriver"] == 2

    # blindspot sensors
    if self.CP.enableBsm:
      cp_bsm = cp_cam if self.CP.flags & FordFlags.CANFD else cp
      ret.leftBlindspot = cp_bsm.vl["Side_Detect_L_Stat"]["SodDetctLeft_D_Stat"] != 0
      ret.rightBlindspot = cp_bsm.vl["Side_Detect_R_Stat"]["SodDetctRight_D_Stat"] != 0

    # Stock steering buttons so that we can passthru blinkers etc.
    self.buttons_stock_values = cp.vl["Steering_Data_FD1"]
    # Stock values from IPMA so that we can retain some stock functionality
    self.acc_tja_status_stock_values = cp_cam.vl["ACCDATA_3"]
    self.lkas_status_stock_values = cp_cam.vl["IPMA_Data"]

    MadsCarState.update_mads(self, ret, can_parsers)

    ret.buttonEvents = [
      *create_button_events(self.distance_button, prev_distance_button, {1: ButtonType.gapAdjustCruise}),
      *create_button_events(self.lc_button, prev_lc_button, {1: ButtonType.lkas}),
    ]

    self.car_state_bp_msg = self.update_car_state_bp(cp, cp_cam)
    return ret, ret_sp

  def set_drive_mode(self, mode: int) -> bool:
    """Set the target drive mode for the vehicle.

    Args:
        mode: Drive mode to set (0=Normal, 1=Sport, 2=Economy, 3=TowHaul, etc.)

    Returns:
        bool: True if mode change was requested, False otherwise
    """
    if mode != self.target_drive_mode:
      self.target_drive_mode = mode
      self.drive_mode_change_requested = True
      self.drive_mode_change_counter = 0
      self.last_drive_mode_change_time = time.time()
      debug(f'Drive mode change requested: {mode}', True)
      return True
    return False

  def set_advanced_drive_mode(self, powertrain_mode: int, chassis_mode: int = None, awd_mode: int = None) -> bool:
    """Set advanced drive mode with separate control over powertrain, chassis, and AWD.

    Args:
        powertrain_mode: Powertrain drive mode (0=Normal, 1=Sport, 2=Economy, etc.)
        chassis_mode: Chassis drive mode (None=keep current, 0=Normal, 1=Normal Adaptive, etc.)
        awd_mode: AWD drive mode (None=keep current, 0=2WD, 1=4WD Auto, etc.)

    Returns:
        bool: True if mode change was requested, False otherwise
    """
    try:
      # Store the advanced mode settings
      self.target_powertrain_mode = powertrain_mode
      if chassis_mode is not None:
        self.target_chassis_mode = chassis_mode
      if awd_mode is not None:
        self.target_awd_mode = awd_mode

      # Mark that we want to change to advanced drive mode
      self.advanced_drive_mode_change_requested = True
      self.drive_mode_change_counter = 0
      self.last_drive_mode_change_time = time.time()

      debug(f'Advanced drive mode change requested: PT={powertrain_mode}, Chassis={chassis_mode}, AWD={awd_mode}', True)
      return True

    except Exception as e:
      error(f'Error setting advanced drive mode: {e}', True)
      return False

  def get_available_drive_modes(self) -> list:
    """Get list of available drive modes for the current vehicle.

    Returns:
        list: List of available drive mode indices
    """
    return getattr(self, 'available_drive_modes', [0])

  def get_current_drive_mode(self) -> int:
    """Get the current drive mode of the vehicle.

    Returns:
        int: Current drive mode index
    """
    return getattr(self, 'current_drive_mode', 0)

  def get_drive_mode_status(self) -> dict:
    """Get the current drive mode status and information.

    Returns:
        dict: Dictionary containing current drive mode information
    """
    try:
      from opendbc.car.ford.drive_mode_utils import (
        get_drive_mode_name, get_drive_mode_description,
        get_drive_mode_category, is_drive_mode_available
      )

      status = {
        'current_powertrain_mode': getattr(self, 'current_drive_mode', 0),
        'current_chassis_mode': getattr(self, 'current_chassis_mode', 0),
        'current_awd_mode': getattr(self, 'current_awd_mode', 0),
        'target_powertrain_mode': self.target_drive_mode,
        'target_chassis_mode': getattr(self, 'target_chassis_mode', 0),
        'target_awd_mode': getattr(self, 'target_awd_mode', 0),
        'change_requested': self.drive_mode_change_requested,
        'advanced_change_requested': getattr(self, 'advanced_drive_mode_change_requested', False),
        'available_modes': getattr(self, 'available_drive_modes', [0]),
        'default_mode': self.default_drive_mode,
      }

      # Add human-readable names and descriptions
      if status['current_powertrain_mode'] is not None:
        status['current_powertrain_name'] = get_drive_mode_name(status['current_powertrain_mode'])
        status['current_powertrain_description'] = get_drive_mode_description(status['current_powertrain_mode'])
        status['current_powertrain_category'] = get_drive_mode_category(status['current_powertrain_mode'])

      if status['target_powertrain_mode'] is not None:
        status['target_powertrain_name'] = get_drive_mode_name(status['target_powertrain_mode'])
        status['target_powertrain_description'] = get_drive_mode_description(status['target_powertrain_mode'])

      return status

    except Exception as e:
      error(f'Error getting drive mode status: {e}', True)
      return {}

  def validate_drive_mode_change(self, mode: int) -> tuple[bool, str]:
    """Validate if a drive mode change is allowed.

    Args:
        mode: Drive mode to validate

    Returns:
        tuple: (is_valid, error_message)
    """
    try:
      from opendbc.car.ford.drive_mode_utils import (
        is_drive_mode_available, validate_drive_mode_combination
      )

      # Check if the mode is available for the current vehicle
      # For now, assume 'truck' as default vehicle type
      # This could be enhanced to detect actual vehicle type
      vehicle_type = 'truck'  # Default assumption
      if not is_drive_mode_available(mode, vehicle_type):
        return False, f"Drive mode {mode} is not available for this vehicle type"

      # Validate mode combination with current chassis and AWD modes
      current_chassis = getattr(self, 'current_chassis_mode', 0)
      current_awd = getattr(self, 'current_awd_mode', 0)

      is_valid, error_msg = validate_drive_mode_combination(mode, current_chassis, current_awd)
      if not is_valid:
        return False, f"Invalid mode combination: {error_msg}"

      return True, "Mode change is valid"

    except Exception as e:
      error(f'Error validating drive mode change: {e}', True)
      return False, f"Validation error: {e}"

  def update_car_state_bp(self, cp, cp_cam):
    """Update the CarStateBP message for HEV/PHEV data

    Args:
        cp: Powertrain bus CAN parser
        cp_cam: Camera bus CAN parser
    """
    # Create a new message
    dat = messaging.new_message("carStateBP")
    dat.valid = True

    # Get handles to the message structures
    hybrid_drive = dat.carStateBP.hybridDrive
    hybrid_battery = dat.carStateBP.hybridBattery
    brake_light_status = dat.carStateBP.brakeLightStatus

    # Initialize with default values
    hybrid_drive.dataAvailable = False
    hybrid_drive.throttleDemandPercent = 0.0
    hybrid_drive.throttleThresholdPercent = 0.0
    hybrid_drive.powerFlowMode = ""
    hybrid_drive.engineOnReason = ""

    hybrid_battery.dataAvailable = False
    hybrid_battery.voltHighLimit = 0.0
    hybrid_battery.voltLowLimit = 0.0
    hybrid_battery.voltActual = 0.0
    hybrid_battery.ampsActual = 0.0
    hybrid_battery.socMinPerc = 0.0
    hybrid_battery.socMaxPerc = 0.0
    hybrid_battery.socActual = 0.0

    # Initialize brake light status
    brake_light_status.dataAvailable = False
    brake_light_status.brakeLightsOn = False

    # Initialize drive mode status
    drive_mode_status = dat.carStateBP.driveModeStatus
    drive_mode_status.dataAvailable = False
    drive_mode_status.currentPowertrainMode = 0
    drive_mode_status.currentChassisMode = 0
    drive_mode_status.currentAwdMode = 0
    drive_mode_status.modeChangeStatus = 0
    drive_mode_status.availableModes = []

    # Initialize individual drive mode fields
    dat.carStateBP.driveMode = 0
    dat.carStateBP.chassisDriveMode = 0
    dat.carStateBP.awdDriveMode = 0
    dat.carStateBP.driveModeStatusValue = 0
    dat.carStateBP.availableDriveModes = []

    # Brake light status from BrakeSysFeatures_2 message
    try:
      brake_data = cp.vl["BrakeSysFeatures_2"]
      if brake_data is not None:
        brake_light_status.dataAvailable = True
        # BrkLamp_B_Rq indicates when brake lights should be on
        brake_light_status.brakeLightsOn = brake_data["BrkLamp_B_Rq"] == 1

        # When openpilot long control is active, ABS may not set brake lights
        # for ACC braking, so also check if we're commanding decel via ACC
        if self.CP.openpilotLongitudinalControl:
          try:
            acc_data = cp_cam.vl["ACCDATA"]  # ACCDATA is on camera bus
            # Check if openpilot is actively requesting braking via ACC
            acc_brake_active = (acc_data["AccBrkPrchg_B_Rq"] == 1 or
                               acc_data["AccBrkDecel_B_Rq"] == 1)
            brake_light_status.brakeLightsOn = (brake_light_status.brakeLightsOn or
                                               acc_brake_active)
          except (KeyError, AttributeError):
            pass  # ACCDATA not available, use original brake light status

        # Add drive mode data from BrakeSysFeatures_2 message
        if "SelDrvMdeChassis2_D_Rq" in brake_data:
          drive_mode_status.dataAvailable = True
          dat.carStateBP.chassisDriveMode = brake_data["SelDrvMdeChassis2_D_Rq"]
          drive_mode_status.currentChassisMode = brake_data["SelDrvMdeChassis2_D_Rq"]

        if "SelDrvMdeAwd_D_Rq" in brake_data:
          drive_mode_status.dataAvailable = True
          dat.carStateBP.awdDriveMode = brake_data["SelDrvMdeAwd_D_Rq"]
          drive_mode_status.currentAwdMode = brake_data["SelDrvMdeAwd_D_Rq"]

    except (KeyError, AttributeError):
      pass  # BrakeSysFeatures_2 not available

    # Add drive mode data from SelectDriveModeData message
    if hasattr(self, 'current_drive_mode_data') and self.current_drive_mode_data is not None:
      drive_mode_status.dataAvailable = True

      # Set individual drive mode fields
      dat.carStateBP.driveMode = self.current_drive_mode_data.get("SelDrvMdePt_D_Rq", 0)
      dat.carStateBP.chassisDriveMode = self.current_drive_mode_data.get("SelDrvMdeChassis_D_Rq", 0)
      dat.carStateBP.awdDriveMode = self.current_drive_mode_data.get("SelDrvMdeAwd_D_Rq", 0)
      dat.carStateBP.driveModeStatusValue = self.current_drive_mode_data.get("SelDrvMde_D_Stat", 0)

      # Also populate the drive mode status struct for compatibility
      drive_mode_status.currentPowertrainMode = dat.carStateBP.driveMode
      drive_mode_status.currentChassisMode = dat.carStateBP.chassisDriveMode
      drive_mode_status.currentAwdMode = dat.carStateBP.awdDriveMode
      drive_mode_status.modeChangeStatus = dat.carStateBP.driveModeStatusValue

      # Convert available modes list to array
      available_modes = []
      for i in range(1, 13):
        avail_key = f"SelDrvMdePos{i:02d}_B_Avail"
        if avail_key in self.current_drive_mode_data and self.current_drive_mode_data[avail_key] == 1:
          available_modes.append(i)
      dat.carStateBP.availableDriveModes = available_modes
      drive_mode_status.availableModes = available_modes

    # HEV cluster data
    try:
        if self.CP.flags & FordFlags.HEV_CLUSTER_DATA:
          hev_data = cp.vl["Cluster_HEV_Data2"]
          if hev_data is not None:
            hybrid_drive.dataAvailable = True
            hybrid_drive.throttleDemandPercent = hev_data["EffWhlLvl2_Pc_Dsply"]
            hybrid_drive.throttleThresholdPercent = hev_data[
                "EffWhlThres_Pc_Dsply"
            ]
            hybrid_drive.powerFlowMode = get_hev_power_flow_text(
                hev_data["PwrFlowTxt_D_Dsply"]
            )
            hybrid_drive.engineOnReason = get_hev_engine_on_reason_text(
                hev_data["EngOnMsg1_D_Dsply"]
            )
    except (KeyError, AttributeError):
      pass

    # HEV battery data
    try:
      if self.CP.flags & FordFlags.HEV_BATTERY_DATA:
        batt_data1 = cp.vl["Battery_Traction_1_FD1"]
        batt_data3 = cp.vl["Battery_Traction_3_FD1"]
        batt_data4 = cp.vl["Battery_Traction_4_FD1"]

        if all(x is not None for x in [batt_data1, batt_data3, batt_data4]):
          hybrid_battery.dataAvailable = True
          hybrid_battery.voltHighLimit = batt_data1["BattTrac_U_LimHi"]
          hybrid_battery.voltLowLimit = batt_data1["BattTrac_U_LimLo"]
          hybrid_battery.voltActual = batt_data1["BattTrac_U_Actl"]
          hybrid_battery.ampsActual = batt_data1["BattTrac_I_Actl"]
          hybrid_battery.socMinPerc = batt_data3["BattTracSoc_Pc_MnPrtct"]
          hybrid_battery.socMaxPerc = batt_data3["BattTracSoc_Pc_MxPrtct"]
          hybrid_battery.socActual = batt_data4["BattTracSoc2_Pc_Actl"]
    except (KeyError, AttributeError):
        pass

    return dat

  def update_traffic_signals(self, cp_cam):
    # TODO: Check if CAN platforms have the same signals
    if self.CP.flags & FordFlags.CANFD:
      self.v_limit = cp_cam.vl["Traffic_RecognitnData"]["TsrVLim1MsgTxt_D_Rq"]
      v_limit_unit = cp_cam.vl["Traffic_RecognitnData"]["TsrVlUnitMsgTxt_D_Rq"]

      speed_factor = CV.MPH_TO_MS if v_limit_unit == 2 else CV.KPH_TO_MS if v_limit_unit == 1 else 0

      return self.v_limit * speed_factor if self.v_limit not in (0, 255) else 0

  @staticmethod
  def get_can_parsers(CP, CP_SP):
    pt_messages = [
      # sig_address, frequency
      ("VehicleOperatingModes", 100),
      ("BrakeSysFeatures", 50),
      ("BrakeSysFeatures_2", 50),
      ("Yaw_Data_FD1", 100),
      ("DesiredTorqBrk", 50),
      ("EngVehicleSpThrottle", 100),
      ("EngVehicleSpThrottle2", 50),
      ("BrakeSnData_4", 50),
      ("EngBrakeData", 10),
      ("Cluster_Info1_FD1", 10),
      ("EPAS_INFO", 50),
      ("Steering_Data_FD1", 10),
      ("BodyInfo_3_FD1", 2),
      ("RCMStatusMessage2_FD1", 10),
      # Add drive mode messages
      ("SelectDriveModeData", 10),  # Message 1056 - Drive mode selection
    ]

    # Try to add HEV message to parser config
    if CP.flags & FordFlags.HEV_CLUSTER_DATA:
      print("Cluster_HEV_Data2 signal exists (get_can_parser)")
      pt_messages.append(("Cluster_HEV_Data2", 10))

    if CP.flags & FordFlags.HEV_BATTERY_DATA:
      print("Battery_Traction_1_FD1 signal exists (get_can_parser)")
      pt_messages.append(("Battery_Traction_1_FD1", 10))
      print("Battery_Traction_3_FD1 signal exists (get_can_parser)")
      pt_messages.append(("Battery_Traction_3_FD1", 10))
      print("Battery_Traction_4_FD1 signal exists (get_can_parser)")
      pt_messages.append(("Battery_Traction_4_FD1", 10))

    if CP.flags & FordFlags.ALT_STEER_ANGLE:
      pt_messages += [
        ("SteeringPinion_Data_Alt", 100),
        ("ParkAid_Data", 50),
        ("TransGearData",10),
      ]
    else:
      pt_messages += [
        ("SteeringPinion_Data", 100),
      ]
      if CP.transmissionType == TransmissionType.automatic:
        pt_messages += [
          ("PowertrainData_10",10)
        ]

    if CP.flags & FordFlags.CANFD:
      pt_messages += [
        ("Lane_Assist_Data3_FD1", 33),
        ("Cluster_Info_3_FD1", 10),
      ]
    else:
      pt_messages += [
        ("INSTRUMENT_PANEL", 1),
      ]

    if CP.transmissionType == TransmissionType.automatic:
      pt_messages += [
        ("Gear_Shift_by_Wire_FD1", 10),
      ]
    elif CP.transmissionType == TransmissionType.manual:
      pt_messages += [
        ("Engine_Clutch_Data", 33),
        ("BCM_Lamp_Stat_FD1", 1),
      ]

    if CP.enableBsm and not (CP.flags & FordFlags.CANFD):
      pt_messages += [
        ("Side_Detect_L_Stat", 5),
        ("Side_Detect_R_Stat", 5),
      ]

    cam_messages = [
      # sig_address, frequency
      ("ACCDATA", 50),
      ("ACCDATA_2", 50),
      ("ACCDATA_3", 5),
      ("IPMA_Data", 1),
    ]

    if CP.flags & FordFlags.CANFD:
      cam_messages += [
        ("Traffic_RecognitnData", 1),
        ("IPMA_Data2", 1),
      ]

    if CP.enableBsm and CP.flags & FordFlags.CANFD:
      cam_messages += [
        ("Side_Detect_L_Stat", 5),
        ("Side_Detect_R_Stat", 5),
      ]

    return {
      Bus.pt: CANParser(DBC[CP.carFingerprint][Bus.pt], pt_messages, CanBus(CP).main),
      Bus.cam: CANParser(DBC[CP.carFingerprint][Bus.pt], cam_messages, CanBus(CP).camera),
    }
