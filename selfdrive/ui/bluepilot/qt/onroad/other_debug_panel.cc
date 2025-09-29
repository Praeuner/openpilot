// selfdrive/ui/bluepilot/qt/onroad/other_debug_panel.cc

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include "selfdrive/ui/bluepilot/qt/onroad/other_debug_panel.h"
#include <QLinearGradient>
#include <QFont>
#include <QScrollBar>
#include <QHeaderView>
#include <QRegularExpression>
#include <QGraphicsDropShadowEffect>
#include <QDateTime>
#include <QScroller>
#include <algorithm>

OtherDataWorker::OtherDataWorker(QObject *parent) : QObject(parent), m_abort(false), m_canSubscriptionActive(false), m_canUpdatesPaused(false) {
  m_lastCache = new OtherDataCache();
  m_lastCANUpdate = 0;

  // Create timer for slower CAN updates (2Hz instead of 20Hz)
  m_canUpdateTimer = new QTimer(this);
  m_canUpdateTimer->setInterval(500); // 500ms = 2Hz
  m_canUpdateTimer->setSingleShot(false);
}

void OtherDataWorker::setCANSubscriptionActive(bool active) {
  if (m_canSubscriptionActive.load() == active) {
    return; // No change needed
  }

  m_canSubscriptionActive.store(active);

  if (active) {
    // Create CAN SubMaster when activating
    try {
      m_canSubMaster = std::make_unique<SubMaster>(std::vector<const char*>{"can"});
      // Note: We don't use the timer anymore, processCANData is called from processData
      // m_canUpdateTimer->start();
      qDebug() << "CAN subscription activated, SubMaster created successfully";
      qDebug() << "Note: CAN data requires pandad running with a connected panda device";
    } catch (const std::exception &e) {
      qWarning() << "Failed to create CAN SubMaster:" << e.what();
      qWarning() << "This usually means pandad is not running or no panda is connected";
      m_canSubMaster.reset();
      m_canSubscriptionActive.store(false);
    }
  } else {
    // Destroy CAN SubMaster when deactivating
    m_canUpdateTimer->stop();
    m_canSubMaster.reset();
    qDebug() << "CAN subscription deactivated";
  }
}

void OtherDataWorker::setCANUpdatesPaused(bool paused) {
  m_canUpdatesPaused.store(paused);
  qDebug() << "CAN updates" << (paused ? "paused" : "resumed");
}

OtherDataWorker::~OtherDataWorker() {
  m_abort = true;
  m_condition.wakeAll();
  delete m_lastCache;
}

void OtherDataWorker::processData(const UIState *s) {
  QMutexLocker locker(&m_mutex);
  if (m_abort)
    return;

  if (!s->scene.started || !s->sm)
    return;

  OtherDataCache cache = *m_lastCache; // Start with the last cache
  cache.valid = true;
  cache.lastUpdateTime = QDateTime::currentMSecsSinceEpoch();

  // Reset update flags
  cache.updated = OtherDataCache::UpdateFlags();

  // Process each data section separately
  processCarState(s, &cache);
  processRadarState(s, &cache);
  processCarOutput(s, &cache);
  processCarParams(s, &cache);
  processDeviceState(s, &cache);
  // processCANData(s, &cache); // DISABLED: CAN debug section disabled

  // Update our internal cache
  *m_lastCache = cache;

  // Send the updated cache to the UI thread
  emit dataReady(cache);
}

void OtherDataWorker::processCarState(const UIState *s, OtherDataCache *cache) {
  try {
    auto &sm = *(s->sm);
    bool valid = sm.valid("carState");
    // BPLog::bpInfo() << "[bp.other.debug.panel] processCarState: carState valid: " << valid << std::endl;
    if (valid) {
      auto car = sm["carState"].getCarState();
      cache->carValues.vEgo = car.getVEgo();
      cache->carValues.aEgo = car.getAEgo();
      cache->carValues.vEgoRaw = car.getVEgoRaw();
      cache->carValues.yawRate = car.getYawRate();
      cache->carValues.standstill = car.getStandstill();
      // engineRpm is deprecated, set to 0
      cache->carValues.engineRpm = 0.0f;

      cache->carValues.steeringAngleDeg = car.getSteeringAngleDeg();
      cache->carValues.steeringRateDeg = car.getSteeringRateDeg();
      cache->carValues.steeringTorque = car.getSteeringTorque();
      cache->carValues.steeringTorqueEps = car.getSteeringTorqueEps();
      cache->carValues.steeringPressed = car.getSteeringPressed();
      cache->carValues.steerFaultTemporary = car.getSteerFaultTemporary();
      cache->carValues.steerFaultPermanent = car.getSteerFaultPermanent();

      cache->carValues.brake = car.getBrake();
      // gas is deprecated, set to 0
      cache->carValues.gas = 0.0f;
      cache->carValues.gasPressed = car.getGasPressed();
      cache->carValues.brakePressed = car.getBrakePressed();
      cache->carValues.regenBraking = car.getRegenBraking();
      // clutchPressed is deprecated, set to false
      cache->carValues.clutchPressed = false;
      cache->carValues.parkingBrake = car.getParkingBrake();
      cache->carValues.brakeHoldActive = car.getBrakeHoldActive();

      cache->carValues.espDisabled = car.getEspDisabled();
      cache->carValues.espActive = car.getEspActive();
      cache->carValues.leftBlinker = car.getLeftBlinker();
      cache->carValues.rightBlinker = car.getRightBlinker();

      // Handle gearShifter as an int
      cache->carValues.gearShifter = static_cast<int>(car.getGearShifter());

      cache->carValues.fuelGauge = car.getFuelGauge();
      cache->carValues.charging = car.getCharging();

      cache->carValues.stockAeb = car.getStockAeb();
      cache->carValues.stockFcw = car.getStockFcw();
      cache->carValues.invalidLkasSetting = car.getInvalidLkasSetting();
      cache->carValues.doorOpen = car.getDoorOpen();
      cache->carValues.seatbeltUnlatched = car.getSeatbeltUnlatched();
      cache->carValues.vehicleSensorsInvalid = car.getVehicleSensorsInvalid();

      // Get cruise state
      if (car.hasCruiseState()) {
        auto cruise = car.getCruiseState();
        cache->carValues.cruiseEnabled = cruise.getEnabled();
        cache->carValues.cruiseSpeed = cruise.getSpeed();
        cache->carValues.cruiseAvailable = cruise.getAvailable();
        cache->carValues.cruiseStandstill = cruise.getStandstill();
        cache->carValues.cruiseNonAdaptive = cruise.getNonAdaptive();
        cache->carValues.cruiseSpeedLimit = cruise.getSpeedLimit();
      }

      // BPLog::bpInfo() << "[bp.other.debug.panel] processCarState: update cache with vEgo: " << cache->carValues.vEgo << std::endl;

      cache->updated.carState = true;
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] processCarState: Error updating CarState:" << e.what() << std::endl;
  }
}

void OtherDataWorker::processRadarState(const UIState *s, OtherDataCache *cache) {
  try {
    auto &sm = *(s->sm);
    if (sm.valid("radarState")) {
      auto radar = sm["radarState"].getRadarState();

      cache->radarValues.errors.canError = radar.getRadarErrors().getCanError();
      cache->radarValues.errors.radarFault = radar.getRadarErrors().getRadarFault();
      cache->radarValues.errors.wrongConfig = radar.getRadarErrors().getWrongConfig();
      cache->radarValues.errors.radarUnavailableTemporary = radar.getRadarErrors().getRadarUnavailableTemporary();

      if (radar.hasLeadOne()) {
        auto lead = radar.getLeadOne();
        cache->radarValues.leadOne.dRel = lead.getDRel();
        cache->radarValues.leadOne.yRel = lead.getYRel();
        cache->radarValues.leadOne.vRel = lead.getVRel();
        cache->radarValues.leadOne.aRel = lead.getARel();
        cache->radarValues.leadOne.vLead = lead.getVLead();
        cache->radarValues.leadOne.dPath = lead.getDPath();
        cache->radarValues.leadOne.vLat = lead.getVLat();
        cache->radarValues.leadOne.vLeadK = lead.getVLeadK();
        cache->radarValues.leadOne.aLeadK = lead.getALeadK();
        cache->radarValues.leadOne.status = lead.getStatus();
        cache->radarValues.leadOne.fcw = lead.getFcw();
        cache->radarValues.leadOne.radar = lead.getRadar();
        cache->radarValues.leadOne.radarTrackId = lead.getRadarTrackId();
      }

      if (radar.hasLeadTwo()) {
        auto lead = radar.getLeadTwo();
        cache->radarValues.leadTwo.dRel = lead.getDRel();
        cache->radarValues.leadTwo.yRel = lead.getYRel();
        cache->radarValues.leadTwo.vRel = lead.getVRel();
        cache->radarValues.leadTwo.aRel = lead.getARel();
        cache->radarValues.leadTwo.vLead = lead.getVLead();
        cache->radarValues.leadTwo.dPath = lead.getDPath();
        cache->radarValues.leadTwo.vLat = lead.getVLat();
        cache->radarValues.leadTwo.vLeadK = lead.getVLeadK();
        cache->radarValues.leadTwo.aLeadK = lead.getALeadK();
        cache->radarValues.leadTwo.status = lead.getStatus();
        cache->radarValues.leadTwo.fcw = lead.getFcw();
        cache->radarValues.leadTwo.radar = lead.getRadar();
        cache->radarValues.leadTwo.radarTrackId = lead.getRadarTrackId();
      }

      cache->updated.radarState = true;
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] processRadarState: Error updating RadarState:" << e.what() << std::endl;
  }
}

void OtherDataWorker::processCarOutput(const UIState *s, OtherDataCache *cache) {
  try {
    auto &sm = *(s->sm);

    // Set default values in case of error
    cache->outputValues.accel = 0.0f;
    cache->outputValues.gas = 0.0f;
    cache->outputValues.brake = 0.0f;
    cache->outputValues.speed = 0.0f;
    cache->outputValues.steeringAngleDeg = 0.0f;
    cache->outputValues.torque = 0.0f;
    cache->outputValues.curvature = 0.0f;
    cache->outputValues.torqueOutputCan = 0.0f;
    cache->outputValues.longControlState = 0;

    if (sm.valid("carOutput")) {
      try {
        auto output = sm["carOutput"].getCarOutput();

        // Check if actuatorsOutput exists
        if (output.hasActuatorsOutput()) {
          try {
            auto actuators = output.getActuatorsOutput();
            try {
              cache->outputValues.accel = actuators.getAccel();
            } catch (...) {
            }
            try {
              cache->outputValues.gas = actuators.getGas();
            } catch (...) {
            }
            try {
              cache->outputValues.brake = actuators.getBrake();
            } catch (...) {
            }
            try {
              cache->outputValues.speed = actuators.getSpeed();
            } catch (...) {
            }
            try {
              cache->outputValues.steeringAngleDeg = actuators.getSteeringAngleDeg();
            } catch (...) {
            }
            try {
              cache->outputValues.torque = actuators.getTorque();
            } catch (...) {
            }
            try {
              cache->outputValues.curvature = actuators.getCurvature();
            } catch (...) {
            }
            try {
              cache->outputValues.torqueOutputCan = actuators.getTorqueOutputCan();
            } catch (...) {
            }
            try {
              auto longState = actuators.getLongControlState();
              cache->outputValues.longControlState = static_cast<int>(longState);
            } catch (...) {
            }
          } catch (const std::exception &e) {
            BPLog::bpError() << "[bp.other.debug.panel] processCarOutput: Error accessing actuatorsOutput:" << e.what() << std::endl;
          }
        }
      } catch (const std::exception &e) {
        BPLog::bpError() << "[bp.other.debug.panel] processCarOutput: Error accessing carOutput message:" << e.what() << std::endl;
      }

      cache->updated.carOutput = true;
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] processCarOutput: Error processing carOutput:" << e.what() << std::endl;
  }
}

void OtherDataWorker::processCarParams(const UIState *s, OtherDataCache *cache) {
  try {
    auto &sm = *(s->sm);
    if (sm.valid("carParams")) {
      auto params = sm["carParams"].getCarParams();
      cache->paramValues.mass = params.getMass();
      cache->paramValues.wheelbase = params.getWheelbase();
      cache->paramValues.steerRatio = params.getSteerRatio();
      cache->paramValues.steerActuatorDelay = params.getSteerActuatorDelay();
      cache->paramValues.longitudinalActuatorDelay = params.getLongitudinalActuatorDelay();
      cache->paramValues.vEgoStopping = params.getVEgoStopping();
      cache->paramValues.vEgoStarting = params.getVEgoStarting();
      cache->paramValues.tireStiffnessFactor = params.getTireStiffnessFactor();
      cache->paramValues.radarUnavailable = params.getRadarUnavailable();
      cache->paramValues.carFingerprint = QString::fromStdString(params.getCarFingerprint());
      cache->paramValues.carVin = QString::fromStdString(params.getCarVin());
      cache->paramValues.brand = QString::fromStdString(params.getBrand());
      cache->paramValues.fuzzyFingerprint = params.getFuzzyFingerprint();
      cache->paramValues.fingerprintSource = static_cast<int>(params.getFingerprintSource());
      cache->paramValues.networkLocation = static_cast<int>(params.getNetworkLocation());
      cache->paramValues.transmissionType = static_cast<int>(params.getTransmissionType());
      cache->paramValues.steerLimitTimer = params.getSteerLimitTimer();

      // Get lateral tuning parameters
      // Get lateral tuning parameters
      if (params.getLateralTuning().which() == cereal::CarParams::LateralTuning::PID) {
        cache->paramValues.lateralTuningType = OtherDataCache::CarParameterValues::LateralTuningType::PID;
        auto pid = params.getLateralTuning().getPid();

        // Get the values from the arrays at appropriate indices
        if (pid.getKpBP().size() > 0 && pid.getKpV().size() > 0) {
          cache->paramValues.pidKp = pid.getKpV()[0];
        }

        if (pid.getKiBP().size() > 0 && pid.getKiV().size() > 0) {
          cache->paramValues.pidKi = pid.getKiV()[0];
        }

        cache->paramValues.pidKf = pid.getKf();
      } else if (params.getLateralTuning().which() == cereal::CarParams::LateralTuning::TORQUE) {
        cache->paramValues.lateralTuningType = OtherDataCache::CarParameterValues::LateralTuningType::TORQUE;
        auto torque = params.getLateralTuning().getTorque();
        // useSteeringAngle is deprecated, set to false
        cache->paramValues.torqueUseSteeringAngle = false;
        cache->paramValues.torqueKp = torque.getKp();
        cache->paramValues.torqueKi = torque.getKi();
        cache->paramValues.torqueKf = torque.getKf();
        cache->paramValues.torqueFriction = torque.getFriction();
        cache->paramValues.torqueLatAccelFactor = torque.getLatAccelFactor();
        cache->paramValues.torqueLatAccelOffset = torque.getLatAccelOffset();
      }

      // Get longitudinal tuning parameters
      auto longTuning = params.getLongitudinalTuning();

      // Convert from capnp::List to QList for BP and V values
      cache->paramValues.longKpBP.clear();
      cache->paramValues.longKpV.clear();
      cache->paramValues.longKiBP.clear();
      cache->paramValues.longKiV.clear();

      for (auto v : longTuning.getKpBP()) {
        cache->paramValues.longKpBP.append(v);
      }

      for (auto v : longTuning.getKpV()) {
        cache->paramValues.longKpV.append(v);
      }

      for (auto v : longTuning.getKiBP()) {
        cache->paramValues.longKiBP.append(v);
      }

      for (auto v : longTuning.getKiV()) {
        cache->paramValues.longKiV.append(v);
      }

      cache->paramValues.longKf = longTuning.getKf();

      // Get safety configs
      if (params.getSafetyConfigs().size() > 0) {
        auto safety = params.getSafetyConfigs()[0];
        cache->paramValues.safetyModel = static_cast<int>(safety.getSafetyModel());
        cache->paramValues.safetyParam = safety.getSafetyParam();
      }

      cache->paramValues.alternativeExperience = params.getAlternativeExperience();

      // Get car firmware information
      auto carFwList = params.getCarFw();
      cache->paramValues.carFw.clear();
      for (auto fw : carFwList) {
        OtherDataCache::CarParameterValues::CarFirmware carFw;
        carFw.ecu = static_cast<int>(fw.getEcu());

        // Handle capnp::Data conversion to string
        auto fwVersionData = fw.getFwVersion();
        std::string fwVersionStr(reinterpret_cast<const char *>(fwVersionData.begin()), fwVersionData.size());
        carFw.fwVersion = QString::fromStdString(fwVersionStr);

        carFw.address = fw.getAddress();
        carFw.subAddress = fw.getSubAddress();
        carFw.bus = fw.getBus();

        // BPLog::bpInfo() << "[bp.other.debug.panel] processCarParams: Found firmware - "
        //          << "ECU: " << carFw.ecu
        //          << " | Version: " << carFw.fwVersion.toStdString()
        //          << " | Address: 0x" << std::hex << carFw.address << std::dec
        //          << " | Bus: " << carFw.bus << std::endl;

        cache->paramValues.carFw.append(carFw);
      }

      // BPLog::bpInfo() << "[bp.other.debug.panel] processCarParams: Total firmware entries processed: " << cache->paramValues.carFw.size() << std::endl;
      cache->updated.carParams = true;
    } else {
      // BPLog::bpInfo() << "[bp.other.debug.panel] processCarParams: carParams not valid in state manager" << std::endl;
    }
  } catch (const std::exception &e) {
    // BPLog::bpError() << "[bp.other.debug.panel] processCarParams: Error updating CarParams: " << e.what() << std::endl;
  }
}

void OtherDataWorker::processDeviceState(const UIState *s, OtherDataCache *cache) {
  try {
    auto &sm = *(s->sm);
    if (sm.valid("deviceState")) {
      auto device = sm["deviceState"].getDeviceState();

      cache->deviceValues.deviceType = static_cast<int>(device.getDeviceType());
      cache->deviceValues.freeSpacePercent = device.getFreeSpacePercent();
      cache->deviceValues.memoryUsagePercent = device.getMemoryUsagePercent();
      cache->deviceValues.gpuUsagePercent = device.getGpuUsagePercent();

      // CPU usage
      cache->deviceValues.cpuUsagePercent.clear();
      for (auto usage : device.getCpuUsagePercent()) {
        cache->deviceValues.cpuUsagePercent.append(usage);
      }

      // Power
      cache->deviceValues.offroadPowerUsageUwh = device.getOffroadPowerUsageUwh();
      cache->deviceValues.carBatteryCapacityUwh = device.getCarBatteryCapacityUwh();
      cache->deviceValues.powerDrawW = device.getPowerDrawW();
      cache->deviceValues.somPowerDrawW = device.getSomPowerDrawW();

      // Temperatures
      cache->deviceValues.cpuTempC.clear();
      for (auto temp : device.getCpuTempC()) {
        cache->deviceValues.cpuTempC.append(temp);
      }

      cache->deviceValues.gpuTempC.clear();
      for (auto temp : device.getGpuTempC()) {
        cache->deviceValues.gpuTempC.append(temp);
      }

      cache->deviceValues.memoryTempC = device.getMemoryTempC();

      // nvmeTempC is deprecated, set to empty list
      cache->deviceValues.nvmeTempC.clear();

      cache->deviceValues.modemTempC.clear();
      for (auto temp : device.getModemTempC()) {
        cache->deviceValues.modemTempC.append(temp);
      }

      cache->deviceValues.pmicTempC.clear();
      for (auto temp : device.getPmicTempC()) {
        cache->deviceValues.pmicTempC.append(temp);
      }

      cache->deviceValues.intakeTempC = device.getIntakeTempC();
      cache->deviceValues.exhaustTempC = device.getExhaustTempC();
      cache->deviceValues.caseTempC = device.getCaseTempC();
      cache->deviceValues.maxTempC = device.getMaxTempC();

      // Status
      cache->deviceValues.thermalStatus = static_cast<int>(device.getThermalStatus());
      cache->deviceValues.fanSpeedPercentDesired = device.getFanSpeedPercentDesired();
      cache->deviceValues.screenBrightnessPercent = device.getScreenBrightnessPercent();
      cache->deviceValues.started = device.getStarted();
      cache->deviceValues.startedMonoTime = device.getStartedMonoTime();

      // Network
      cache->deviceValues.networkType = static_cast<int>(device.getNetworkType());
      cache->deviceValues.networkStrength = static_cast<int>(device.getNetworkStrength());
      cache->deviceValues.networkMetered = device.getNetworkMetered();
      cache->deviceValues.lastAthenaPingTime = device.getLastAthenaPingTime();

      if (device.hasNetworkInfo()) {
        auto netInfo = device.getNetworkInfo();
        cache->deviceValues.networkInfo.technology = QString::fromStdString(netInfo.getTechnology());
        cache->deviceValues.networkInfo.operator_ = QString::fromStdString(netInfo.getOperator());
        cache->deviceValues.networkInfo.band = QString::fromStdString(netInfo.getBand());
        cache->deviceValues.networkInfo.channel = netInfo.getChannel();
        cache->deviceValues.networkInfo.state = QString::fromStdString(netInfo.getState());
      }

      if (device.hasNetworkStats()) {
        auto netStats = device.getNetworkStats();
        cache->deviceValues.networkStats.wwanTx = netStats.getWwanTx();
        cache->deviceValues.networkStats.wwanRx = netStats.getWwanRx();
      }

      cache->updated.deviceState = true;
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] processDeviceState Error updating DeviceState:" << e.what() << std::endl;
  }
}


// Helper function to get CAN message name from ID
// TODO: Integrate with vehicle's DBC file for accurate names
// Future implementation should:
// 1. Load the DBC file based on carFingerprint (e.g., ford_lincoln_base_pt.dbc)
// 2. Use opendbc's CANDefine or a C++ DBC parser to get message names
// 3. Cache the parsed DBC data for performance
static QString getMessageName(uint32_t can_id, const QString &carFingerprint = "") {
  Q_UNUSED(can_id);
  Q_UNUSED(carFingerprint);
  // Static message names removed - only show DBC-matched names
  return "";  // Return empty - will be populated from DBC data only
}

// Helper function to parse CAN signals - generic version with optional DBC parsing
static void parseCANSignals(uint32_t can_id, const kj::ArrayPtr<const uint8_t> &data,
                           OtherDataCache::CANMessage *msg, OtherDataCache *cache,
                           const QString &carFingerprint = "") {
  // Store previous values for change detection
  QMap<QString, double> previousValues;
  for (const auto &signal : msg->signalList) {
    previousValues[signal.name] = signal.value;
  }

  msg->signalList.clear();

  if (data.size() == 0) return; // No data to parse

  const uint8_t *bytes = data.begin();

  // Static signal parsing removed - only show DBC-matched signals
  Q_UNUSED(carFingerprint);
  bool hasKnownSignals = false;

  // Skip all hardcoded signal parsing - only use DBC data
  // All the switch statement cases have been removed

  // Static signal names removed - signals will only be populated from DBC data
  Q_UNUSED(bytes);
  Q_UNUSED(hasKnownSignals);
  // Signal list remains empty - will only be populated from actual DBC parsing in the future
}

void OtherDataWorker::processCANData(const UIState *s, OtherDataCache *cache) {
  try {
    // Check if CAN subscription is active and available
    if (!m_canSubscriptionActive.load() || !m_canSubMaster) {
      qDebug() << "CAN subscription not active or SubMaster not available";
      return;
    }

    // Get car fingerprint for DBC lookups
    QString carFingerprint = cache->paramValues.carFingerprint;

    // Check if updates are paused
    if (m_canUpdatesPaused.load()) {
      // Keep existing data when paused, just mark as available
      cache->canDataAvailable = !cache->canMessages.isEmpty();
      cache->updated.canData = false; // Don't trigger UI updates when paused
      // Clear the hasNewData flags when paused
      for (auto &msg : cache->canMessages) {
        msg.hasNewData = false;
      }
      return;
    }

    // Rate limit CAN updates to 10Hz for better responsiveness
    uint64_t current_time = QDateTime::currentMSecsSinceEpoch();
    if (current_time - m_lastCANUpdate < 100) { // 100ms = 10Hz
      return;
    }
    m_lastCANUpdate = current_time;

    // Update the CAN SubMaster with a small timeout to ensure we get data
    m_canSubMaster->update(10); // 10ms timeout for faster response

    // Check if CAN data is available (but don't return if not, just log)
    bool canValid = m_canSubMaster->valid("can");
    bool canRecent = m_canSubMaster->rcv_frame("can");

    if (!canValid || !canRecent) {
      static int warnCounter = 0;
      if (warnCounter++ % 50 == 0) {  // Log every 50th miss
        qDebug() << "CAN status - valid:" << canValid << "recent:" << canRecent;
      }
      // Don't return - try to process whatever data we have
    }

    // Get CAN messages from the dedicated CAN SubMaster
    const auto &can_list = (*m_canSubMaster)["can"].getCan();

    static int debugCounter = 0;
    if (debugCounter++ % 10 == 0) {  // Log every 10th update
      qDebug() << "CAN list size:" << can_list.size() << "CAN frame:" << m_canSubMaster->rcv_frame("can");
    }

    // Track message frequency calculation (only for updated messages)
    static std::map<uint32_t, std::vector<uint64_t>> message_timestamps;
    static std::map<uint32_t, uint64_t> last_message_count;

    // Process each CAN message in the list
    for (const auto &can_msg : can_list) {
      uint32_t can_id = can_msg.getAddress();
      auto can_data = can_msg.getDat();
      uint8_t bus = can_msg.getSrc();

      // Process all buses to see all CAN traffic
      // if (bus != 0) continue;  // Removed to see all buses

      // Initialize message entry if it doesn't exist (Cabana-style persistence)
      if (!cache->canMessages.contains(can_id)) {
        OtherDataCache::CANMessage new_msg;
        new_msg.id = can_id;
        new_msg.name = getMessageName(can_id, carFingerprint);  // Will be empty for unknown messages
        new_msg.bus = bus;  // Store bus number separately
        new_msg.frequency = 0;
        new_msg.firstSeen = current_time;
        new_msg.lastSeen = current_time;
        new_msg.updateCount = 0;
        new_msg.hasNewData = true;
        cache->canMessages[can_id] = new_msg;
        // Track discovery order
        cache->discoveryOrder.append(can_id);
      }

      // Check if data has changed
      QByteArray currentData(reinterpret_cast<const char*>(can_data.begin()), can_data.size());
      bool dataChanged = (cache->canMessages[can_id].lastData != currentData);

      // Update message timing and data
      cache->canMessages[can_id].lastSeen = current_time;
      cache->canMessages[can_id].hasNewData = dataChanged;
      if (dataChanged) {
        cache->canMessages[can_id].lastData = currentData;
        cache->canMessages[can_id].updateCount++;
      }

      // Calculate frequency using sliding window (like Cabana)
      message_timestamps[can_id].push_back(current_time);

      // Keep only timestamps from last 2 seconds for frequency calculation
      auto &timestamps = message_timestamps[can_id];
      timestamps.erase(
        std::remove_if(timestamps.begin(), timestamps.end(),
                      [current_time](uint64_t ts) { return current_time - ts > 2000; }),
        timestamps.end());

      // Calculate frequency based on timestamps in last 2 seconds
      if (timestamps.size() > 1) {
        cache->canMessages[can_id].frequency = timestamps.size() / 2.0; // Messages per second
      }

      // Parse signals based on known CAN message IDs (Ford-specific if applicable)
      parseCANSignals(can_id, can_data, &cache->canMessages[can_id], cache, carFingerprint);
    }

    // Age out old messages that haven't been seen (like Cabana timeout)
    auto it = cache->canMessages.begin();
    while (it != cache->canMessages.end()) {
      uint32_t msg_id = it.key();
      auto &msg = it.value();

      // Remove messages not seen for more than 10 seconds
      if (current_time - msg.lastSeen > 10000) {
        message_timestamps.erase(msg_id);
        it = cache->canMessages.erase(it);
      } else {
        ++it;
      }
    }

    // Mark CAN data as available if we have any messages
    cache->canDataAvailable = !cache->canMessages.isEmpty();
    cache->updated.canData = true;

  } catch (const std::exception &e) {
    qWarning() << "Error updating CAN data:" << e.what();
    cache->canDataAvailable = false;
  }
}

OtherDebugPanel::OtherDebugPanel(QWidget *parent) : QWidget(parent), m_dataProcessing(false) {
  // Register types for cross-thread signal/slot usage
  qRegisterMetaType<const UIState *>("const UIState*");
  qRegisterMetaType<OtherDataCache>("OtherDataCache");

  // Create data cache
  m_cache = new OtherDataCache();

  // Initialize update timer
  m_updateTimer.setSingleShot(true);
  m_updateTimer.setInterval(UpdateRates::MIN_UPDATE_INTERVAL_MS);
  connect(&m_updateTimer, &QTimer::timeout, this, &OtherDebugPanel::updateUI);

  // Initialize tab change timer - delay updates when switching tabs
  m_tabChangeTimer.setSingleShot(true);
  m_tabChangeTimer.setInterval(50); // 50ms delay for tab changes
  connect(&m_tabChangeTimer, &QTimer::timeout, this, &OtherDebugPanel::updateVisibleTab);

  // Setup worker thread
  m_worker = new OtherDataWorker();
  m_worker->moveToThread(&m_workerThread);
  connect(this, &OtherDebugPanel::processStateUpdate, m_worker, &OtherDataWorker::processData);

  // Initialize CAN subscription as inactive
  m_worker->setCANSubscriptionActive(false);
  connect(m_worker, &OtherDataWorker::dataReady, this, &OtherDebugPanel::updateFromWorker);
  m_workerThread.start();

  // Initialize gradient
  m_backgroundGradient = QLinearGradient(0, 0, 0, height());
  m_backgroundGradient.setColorAt(0, QColor(30, 30, 30, 230));
  m_backgroundGradient.setColorAt(1, QColor(20, 20, 20, 230));
  m_backgroundGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
  m_gradientInitialized = true;

  setAttribute(Qt::WA_AcceptTouchEvents);

  // Set up material styling first
  setupMaterialStyle();
  setupLabelStyles();

  // Set up the main layout
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(10);

  // Title
  QLabel *title = new QLabel("Vehicle Debug Monitor", this);
  title->setStyleSheet(R"(
    font-size: 34px;
    font-weight: bold;
    color: white;
    margin: 0px;
    background: transparent;
  )");
  title->setFont(QFont("Arial", 34, QFont::Bold));
  title->setFixedHeight(60);
  title->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(title);

  // Create Tab Widget
  m_tabWidget = new QTabWidget(this);

  // Set tab position to bottom
  m_tabWidget->setTabPosition(QTabWidget::South);

  // Connect tab change signals
  connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
    // Schedule a tab change update
    m_tabChangeTimer.start();
  });

  mainLayout->addWidget(m_tabWidget);

  // Setup all tabs
  setupTabs();
}

void OtherDebugPanel::setCANTabActive(bool active) {
  if (m_worker) {
    m_worker->setCANSubscriptionActive(active);
  }
}

void OtherDebugPanel::setCANUpdatesPaused(bool paused) {
  if (m_worker) {
    m_worker->setCANUpdatesPaused(paused);
  }
}

OtherDebugPanel::~OtherDebugPanel() {
  // Clean up worker thread
  m_workerThread.quit();
  m_workerThread.wait();
  delete m_worker;

  // Free data cache
  delete m_cache;
}

void OtherDebugPanel::updateState(const UIState &s) {
  // Skip if not visible
  if (!isVisible())
    return;

  // Skip if no valid data
  if (!s.scene.started || !s.sm)
    return;

  // If we're already processing data and the timer isn't active, start it
  if (m_dataProcessing.load() && !m_updateTimer.isActive()) {
    m_updateTimer.start();
    return;
  }

  // Otherwise, process this update
  m_dataProcessing.store(true);
  emit processStateUpdate(&s);
}

void OtherDebugPanel::updateFromWorker(const OtherDataCache &cache) {
  // Skip if not visible
  if (!isVisible()) {
    m_dataProcessing.store(false);
    return;
  }

  // Update our cache with the new data
  *m_cache = cache;

  uint64_t currentTime = QDateTime::currentMSecsSinceEpoch();
  if (currentTime - m_lastUpdates.lastPanelUpdate >= UpdateRates::MIN_UPDATE_INTERVAL_MS) {
    updateUI();
    m_lastUpdates.lastPanelUpdate = currentTime;
  } else {
    m_updateTimer.start(); // Schedule an update
  }
}

void OtherDebugPanel::updateUI() {
  if (!isVisible()) {
    m_dataProcessing.store(false);
    return;
  }

  // Only update the currently visible tab
  updateVisibleTab();

  if (m_cache) {
    m_cache->updated = OtherDataCache::UpdateFlags();
  }

  m_dataProcessing.store(false);
}

void OtherDebugPanel::updateVisibleTab() {
  if (!isVisible())
    return;

  int currentTabIndex = m_tabWidget->currentIndex();

  uint64_t currentTime = QDateTime::currentMSecsSinceEpoch();

  switch (currentTabIndex) {
  case 0: // Main tab
    if (m_cache->updated.carState || currentTime - m_lastUpdates.dynamicsLastUpdate >= UpdateRates::DYNAMICS_UPDATE_RATE_MS) {
      updateMainLabels();
      m_lastUpdates.dynamicsLastUpdate = currentTime;
    }
    break;

  case 1: // Radar tab
    if (m_cache->updated.radarState || currentTime - m_lastUpdates.steeringLastUpdate >= UpdateRates::STEERING_UPDATE_RATE_MS) {
      updateRadarLabels();
      m_lastUpdates.steeringLastUpdate = currentTime;
    }
    break;

  case 2: // Tuning tab
    if (m_cache->updated.carParams || currentTime - m_lastUpdates.systemsLastUpdate >= UpdateRates::SYSTEMS_UPDATE_RATE_MS) {
      updateTuningLabels();
      m_lastUpdates.systemsLastUpdate = currentTime;
    }
    break;

  case 3: // Firmware tab
    if (m_cache->updated.carParams && (m_cache->updated.carParams || currentTime - m_lastUpdates.firmwareLastUpdate >= UpdateRates::FIRMWARE_UPDATE_RATE_MS)) {
      updateFirmwareLabels();
      updateFirmwareTable();
      m_lastUpdates.firmwareLastUpdate = currentTime;
    }
    break;

  case 4: // Device tab
    if (m_cache->updated.deviceState || currentTime - m_lastUpdates.deviceLastUpdate >= UpdateRates::DEVICE_UPDATE_RATE_MS) {
      updateDeviceLabels();
      m_lastUpdates.deviceLastUpdate = currentTime;
    }
    break;

  // case 5: // CAN tab DISABLED: CAN debug section disabled
  //   if (m_cache->updated.canData || currentTime - m_lastUpdates.deviceLastUpdate >= UpdateRates::DYNAMICS_UPDATE_RATE_MS) {
  //     // Update CAN message list if needed
  //     populateCANMessageTable();
  //     // Update signal table for selected message
  //     updateCANSignals();
  //   }
  //   break;
  }

  update(); // Request a repaint
}

void OtherDebugPanel::setupMaterialStyle() {
  // Set up the main layout with automotive design spacing
  setStyleSheet(R"(
    QWidget {
      background-color: transparent;
      color: #ecf0f1;
      font-family: Inter, Arial, sans-serif;
    }

    QLabel {
      color: white;
    }

    QTabWidget::pane {
      border: none;
      background: #242424;
      border-radius: 12px;
    }

    QTabBar {
      alignment: center;
    }

    QTabBar::tab {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #2c3e50, stop: 1 #1a252f);
      color: #bdc3c7;
      padding: 15px 30px;
      margin: 5px 8px 0px 8px;
      border-top-left-radius: 12px;
      border-top-right-radius: 12px;
      font-size: 32px;
      min-width: 150px;
      min-height: 50px;
      border: 1px solid rgba(100, 149, 237, 80);
      border-bottom: 3px solid transparent;
    }

    QTabWidget::tab-bar {
      alignment: center;
    }


    QTabBar::tab:selected {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #34495e, stop: 1 #2c3e50);
      color: #18b4ff;
      border: 2px solid #18b4ff;
      border-bottom: 3px solid #18b4ff;
      font-weight: bold;
    }

    QTabBar::tab:hover:!selected {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #34495e, stop: 1 #2c3e50);
      color: #ecf0f1;
      border-bottom: 3px solid #7f8c8d;
    }

    QTabBar::tab:disabled {
      background: #242424;
      color: #757575;
    }

    QScrollBar:vertical {
      width: 12px;
      background: transparent;
      margin: 0px;
    }

    QScrollBar::handle:vertical {
      background: #666666;
      min-height: 20px;
      border-radius: 6px;
      margin: 0 2px;
    }

    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
      height: 0px;
    }

    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
      background: none;
    }
  )");
}

void OtherDebugPanel::setupLabelStyles() {
  nameStyle = R"(
    font-size: 28px;
    color: #BBBBBB;
    padding-right: 10px;
    font-weight: normal;
  )";

  valueStyle = R"(
    font-size: 32px;
    color: #2196F3;
    font-weight: 500;
    padding-right: 20px;
  )";
}

QFrame *OtherDebugPanel::createLabelFrame(QGridLayout *layout, QString title) {
  QFrame *frame = new QFrame();
  frame->setLayout(layout);

  // Add a heading to the frame with modern styling
  QLabel *heading = new QLabel(title, frame);
  heading->setStyleSheet(R"(
    font-size: 32px;
    font-weight: 500;
    color: #2196F3;
    padding: 5px 0px;
    border-bottom: 2px solid #555555;
  )");
  heading->setAlignment(Qt::AlignLeft);

  layout->setContentsMargins(20, 20, 20, 20);
  layout->addWidget(heading, 0, 0, 1, 4, Qt::AlignLeft);

  // Material Design card-like styling with elevation
  frame->setStyleSheet(R"(
    QFrame {
      background-color: #242424;
      border-radius: 12px;
      margin: 5px;
    }
  )");

  // Add subtle shadow effect for elevation
  QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(frame);
  shadow->setColor(QColor(0, 0, 0, 80));
  shadow->setBlurRadius(15);
  shadow->setOffset(0, 3);
  frame->setGraphicsEffect(shadow);

  return frame;
}

void OtherDebugPanel::setupTableStyle() {
  m_firmwareTable->setStyleSheet(R"(
    QTableWidget {
      background-color: #242424;
      color: white;
      border: none;
      border-radius: 10px;
      font-size: 32px;
      gridline-color: #444444;
    }

    QTableWidget::item {
      padding: 12px;
      border-bottom: 1px solid #393939;
    }

    QTableWidget::item:selected {
      background-color: rgba(33, 150, 243, 120);
      color: white;
    }

    QHeaderView::section {
      background-color: #303030;
      color: white;
      font-weight: 500;
      padding: 15px;
      border: none;
      border-bottom: 2px solid #2196F3;
      font-size: 28px;
      height: 60px;
    }

    QScrollBar:vertical {
      width: 24px;  /* Wider scrollbar for touch */
      background: transparent;
      margin: 0px;
    }

    QScrollBar::handle:vertical {
      background: #666666;
      min-height: 60px;  /* Larger handle for touch */
      border-radius: 12px;
      margin: 0 2px;
    }
  )");

  // Improve touch scrolling on the table
  m_firmwareTable->setShowGrid(false);
  m_firmwareTable->setAlternatingRowColors(true);
  m_firmwareTable->setStyleSheet(m_firmwareTable->styleSheet() + "QTableWidget { alternate-background-color: #2A2A2A; }");
  m_firmwareTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_firmwareTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_firmwareTable->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
  m_firmwareTable->setProperty("kinetic_scrolling", true);
  m_firmwareTable->setTextElideMode(Qt::ElideRight);

  // Prevent text selection
  m_firmwareTable->setSelectionMode(QAbstractItemView::NoSelection);
  m_firmwareTable->setDragEnabled(false);
}

void OtherDebugPanel::setupTabs() {
  // Setup each tab
  setupMainTab();
  setupRadarTab();
  setupTuningTab();
  setupFirmwareTab();
  setupDeviceTab();
  // setupCANTab(); // DISABLED: CAN debug section disabled

  // Add tabs to tab widget
  m_tabWidget->tabBar()->setShape(QTabBar::RoundedSouth);
  m_tabWidget->tabBar()->setExpanding(true); // This helps with centering
  m_tabWidget->tabBar()->setDocumentMode(true);
  m_tabWidget->tabBar()->setDrawBase(false);
  m_tabWidget->addTab(m_mainTab, "Main");
  m_tabWidget->addTab(m_radarTab, "Radar");
  m_tabWidget->addTab(m_tuningTab, "Tuning");
  m_tabWidget->addTab(m_firmwareTab, "Firmware");
  m_tabWidget->addTab(m_deviceTab, "Device");
  // m_tabWidget->addTab(m_canTab, "CAN"); // DISABLED: CAN debug section disabled
}

void OtherDebugPanel::setupMainTab() {
  // Create Main tab
  m_mainTab = new QWidget(m_tabWidget);
  m_mainTab->setStyleSheet("background: transparent;");

  // Create scroll area for main tab
  m_mainScrollArea = new QScrollArea(m_mainTab);
  m_mainScrollArea->setWidgetResizable(true);
  m_mainScrollArea->setFrameShape(QFrame::NoFrame);
  m_mainScrollArea->setStyleSheet("background: transparent;");
  m_mainScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_mainScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_mainScrollArea->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);

  // Enable proper kinetic scrolling
  QScroller::grabGesture(m_mainScrollArea->viewport(), QScroller::TouchGesture);
  QScrollerProperties scrollerProperties = QScroller::scroller(m_mainScrollArea->viewport())->scrollerProperties();

  // Adjust the physics of the scrolling
  scrollerProperties.setScrollMetric(QScrollerProperties::DragVelocitySmoothingFactor, 0.6);
  scrollerProperties.setScrollMetric(QScrollerProperties::MinimumVelocity, 0.0);
  scrollerProperties.setScrollMetric(QScrollerProperties::MaximumVelocity, 0.6);
  scrollerProperties.setScrollMetric(QScrollerProperties::AcceleratingFlickMaximumTime, 0.4);
  scrollerProperties.setScrollMetric(QScrollerProperties::AcceleratingFlickSpeedupFactor, 1.2);
  scrollerProperties.setScrollMetric(QScrollerProperties::SnapPositionRatio, 0.5);
  scrollerProperties.setScrollMetric(QScrollerProperties::MaximumClickThroughVelocity, 0);
  scrollerProperties.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.5);
  scrollerProperties.setScrollMetric(QScrollerProperties::OvershootDragDistanceFactor, 0.3);

  QScroller::scroller(m_mainScrollArea->viewport())->setScrollerProperties(scrollerProperties);

  // Create content widget for scroll area
  m_mainScrollContent = new QWidget(m_mainScrollArea);
  m_mainScrollContent->setStyleSheet("background: transparent;");
  m_mainScrollArea->setWidget(m_mainScrollContent);

  // Create layout for scrollable content
  m_mainLayout = new QGridLayout(m_mainScrollContent);
  m_mainLayout->setContentsMargins(0, 10, 10, 10);
  m_mainLayout->setSpacing(15);

  // Main tab layout
  QVBoxLayout *mainTabLayout = new QVBoxLayout(m_mainTab);
  mainTabLayout->setContentsMargins(0, 0, 0, 0);
  mainTabLayout->addWidget(m_mainScrollArea);

  // Create layouts for each group first
  QGridLayout *dynamicsLayout = new QGridLayout();
  dynamicsLayout->setSpacing(5);

  QGridLayout *steeringLayout = new QGridLayout();
  steeringLayout->setSpacing(5);

  QGridLayout *pedalsLayout = new QGridLayout();
  pedalsLayout->setSpacing(5);

  QGridLayout *systemsLayout = new QGridLayout();
  systemsLayout->setSpacing(5);

  QGridLayout *safetyLayout = new QGridLayout();
  safetyLayout->setSpacing(5);

  QGridLayout *paramsLayout = new QGridLayout();
  paramsLayout->setSpacing(5);

  QGridLayout *cruiseLayout = new QGridLayout();
  cruiseLayout->setSpacing(5);

  QGridLayout *outputsLayout = new QGridLayout();
  outputsLayout->setSpacing(5);

  // Create frames for each layout
  QFrame *dynamicsFrame = createLabelFrame(dynamicsLayout, "Vehicle Dynamics");
  QFrame *steeringFrame = createLabelFrame(steeringLayout, "Steering");
  QFrame *pedalsFrame = createLabelFrame(pedalsLayout, "Pedals & Controls");
  QFrame *systemsFrame = createLabelFrame(systemsLayout, "Vehicle Systems");
  QFrame *safetyFrame = createLabelFrame(safetyLayout, "Safety Systems");
  QFrame *paramsFrame = createLabelFrame(paramsLayout, "Vehicle Parameters");
  QFrame *cruiseFrame = createLabelFrame(cruiseLayout, "Cruise Control");
  QFrame *outputsFrame = createLabelFrame(outputsLayout, "Actuator Outputs");

  // Add frames to layout in a 2-column grid
  // Row 0: Vehicle Dynamics | Steering
  m_mainLayout->addWidget(dynamicsFrame, 0, 0);
  m_mainLayout->addWidget(steeringFrame, 0, 1);

  // Row 1: Pedals & Controls | Cruise Control
  m_mainLayout->addWidget(pedalsFrame, 1, 0);
  m_mainLayout->addWidget(cruiseFrame, 1, 1);

  // Row 2: Vehicle Systems | Safety Systems
  m_mainLayout->addWidget(systemsFrame, 2, 0);
  m_mainLayout->addWidget(safetyFrame, 2, 1);

  // Row 3: Actuator Outputs | Vehicle Parameters (spans 2 columns)
  m_mainLayout->addWidget(outputsFrame, 3, 0);
  m_mainLayout->addWidget(paramsFrame, 3, 1);

  // Initialize the rows for each group
  int dynamicsRow = 1; // Row 0 is for the heading
  int dynamicsCol = 0;

  int steeringRow = 1;
  int steeringCol = 0;

  int pedalsRow = 1;
  int pedalsCol = 0;

  int systemsRow = 1;
  int systemsCol = 0;

  int safetyRow = 1;
  int safetyCol = 0;

  int paramsRow = 1;
  int paramsCol = 0;

  int cruiseRow = 1;
  int cruiseCol = 0;

  int outputsRow = 1;
  int outputsCol = 0;

  // Helper to add a label pair to the appropriate layout
  auto addLabel = [&](const QString &group, const QString &name, const QString &initialValue) {
    QLabel *nameLabel = new QLabel(name, m_mainScrollContent);
    nameLabel->setStyleSheet(nameStyle);
    nameLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    QLabel *valueLabel = new QLabel(initialValue, m_mainScrollContent);
    valueLabel->setStyleSheet(valueStyle);
    valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QGridLayout *targetLayout;
    int *targetRow = nullptr;
    int *targetCol = nullptr;

    if (group == "Vehicle Dynamics") {
      targetLayout = dynamicsLayout;
      targetRow = &dynamicsRow;
      targetCol = &dynamicsCol;
    } else if (group == "Steering") {
      targetLayout = steeringLayout;
      targetRow = &steeringRow;
      targetCol = &steeringCol;
    } else if (group == "Pedals") {
      targetLayout = pedalsLayout;
      targetRow = &pedalsRow;
      targetCol = &pedalsCol;
    } else if (group == "Vehicle Systems") {
      targetLayout = systemsLayout;
      targetRow = &systemsRow;
      targetCol = &systemsCol;
    } else if (group == "Safety") {
      targetLayout = safetyLayout;
      targetRow = &safetyRow;
      targetCol = &safetyCol;
    } else if (group == "Vehicle Parameters") {
      targetLayout = paramsLayout;
      targetRow = &paramsRow;
      targetCol = &paramsCol;
    } else if (group == "Cruise Control") {
      targetLayout = cruiseLayout;
      targetRow = &cruiseRow;
      targetCol = &cruiseCol;
    } else if (group == "Actuator Outputs") {
      targetLayout = outputsLayout;
      targetRow = &outputsRow;
      targetCol = &outputsCol;
    } else {
      return; // Unknown group, skip
    }

    targetLayout->addWidget(nameLabel, *targetRow, *targetCol * 2);
    targetLayout->addWidget(valueLabel, *targetRow, *targetCol * 2 + 1);

    // Store in groups map
    LabelPair pair;
    pair.nameLabel = nameLabel;
    pair.valueLabel = valueLabel;
    m_groups[group].append(pair);

    // Move to next position
    (*targetCol)++;
    if (*targetCol >= 2) { // 2 columns of pairs
      *targetCol = 0;
      (*targetRow)++;
    }
  };

  // Vehicle Dynamics labels
  addLabel("Vehicle Dynamics", "Speed:", "0.0 m/s");
  addLabel("Vehicle Dynamics", "Raw Speed:", "0.0 m/s");
  addLabel("Vehicle Dynamics", "Accel:", "0.0 m/s²");
  addLabel("Vehicle Dynamics", "Yaw Rate:", "0.0 rad/s");
  addLabel("Vehicle Dynamics", "Standstill:", "No");
  addLabel("Vehicle Dynamics", "Engine RPM:", "0");

  // Steering labels
  addLabel("Steering", "Angle:", "0.0°");
  addLabel("Steering", "Rate:", "0.0°/s");
  addLabel("Steering", "Torque:", "0.0");
  addLabel("Steering", "EPS Torque:", "0.0");
  addLabel("Steering", "Pressed:", "No");
  addLabel("Steering", "Temp Fault:", "No");
  addLabel("Steering", "Perm Fault:", "No");

  // Pedals labels
  addLabel("Pedals", "Gas:", "0.0");
  addLabel("Pedals", "Gas Pressed:", "No");
  addLabel("Pedals", "Brake:", "0.0");
  addLabel("Pedals", "Brake Pressed:", "No");
  addLabel("Pedals", "Regen Braking:", "No");
  addLabel("Pedals", "Clutch Pressed:", "No");
  addLabel("Pedals", "Parking Brake:", "No");
  addLabel("Pedals", "Brake Hold:", "No");

  // Systems labels
  addLabel("Vehicle Systems", "ESP Disabled:", "No");
  addLabel("Vehicle Systems", "ESP Active:", "No");
  addLabel("Vehicle Systems", "Left Blinker:", "No");
  addLabel("Vehicle Systems", "Right Blinker:", "No");
  addLabel("Vehicle Systems", "Gear:", "Unknown");
  addLabel("Vehicle Systems", "Fuel Level:", "0.0%");
  addLabel("Vehicle Systems", "Charging:", "No");

  // Safety labels
  addLabel("Safety", "Stock AEB:", "No");
  addLabel("Safety", "Stock FCW:", "No");
  addLabel("Safety", "LKAS Invalid:", "No");
  addLabel("Safety", "Door Open:", "No");
  addLabel("Safety", "Seatbelt:", "Latched");
  addLabel("Safety", "Bad Sensors:", "No");

  // Parameters labels
  addLabel("Vehicle Parameters", "Mass:", "0.0 kg");
  addLabel("Vehicle Parameters", "Wheelbase:", "0.0 m");
  addLabel("Vehicle Parameters", "Steer Ratio:", "0.0");
  addLabel("Vehicle Parameters", "Steer Delay:", "0.0s");
  addLabel("Vehicle Parameters", "Long Delay:", "0.0s");
  addLabel("Vehicle Parameters", "vEgo Stop:", "0.0 m/s");
  addLabel("Vehicle Parameters", "vEgo Start:", "0.0 m/s");
  addLabel("Vehicle Parameters", "Tire Stiffness:", "0.0");

  // Cruise labels
  addLabel("Cruise Control", "Enabled:", "No");
  addLabel("Cruise Control", "Speed:", "0.0 m/s");
  addLabel("Cruise Control", "Available:", "No");
  addLabel("Cruise Control", "Standstill:", "No");
  addLabel("Cruise Control", "Non-Adaptive:", "No");
  addLabel("Cruise Control", "Speed Limit:", "0.0 m/s");

  // Actuator Output labels
  addLabel("Actuator Outputs", "Steer Angle:", "0.0°");
  addLabel("Actuator Outputs", "Torque:", "0.0");
  addLabel("Actuator Outputs", "Curvature:", "0.0");
  addLabel("Actuator Outputs", "Accel:", "0.0 m/s²");
  addLabel("Actuator Outputs", "Gas Output:", "0.0");
  addLabel("Actuator Outputs", "Brake Output:", "0.0");
  addLabel("Actuator Outputs", "CAN Torque:", "0.0");
  addLabel("Actuator Outputs", "LongState:", "Off");

  // Set column stretch factors to ensure even sizing
  m_mainLayout->setColumnStretch(0, 1);
  m_mainLayout->setColumnStretch(1, 1);
}

void OtherDebugPanel::setupRadarTab() {
  m_radarTab = new QWidget(m_tabWidget);
  m_radarTab->setStyleSheet("background: transparent;");

  // Create scroll area for radar tab
  m_radarScrollArea = new QScrollArea(m_radarTab);
  m_radarScrollArea->setWidgetResizable(true);
  m_radarScrollArea->setFrameShape(QFrame::NoFrame);
  m_radarScrollArea->setStyleSheet("background: transparent;");
  m_radarScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_radarScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_radarScrollArea->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);

  // Enable proper kinetic scrolling
  QScroller::grabGesture(m_radarScrollArea->viewport(), QScroller::TouchGesture);
  QScrollerProperties scrollerProperties = QScroller::scroller(m_radarScrollArea->viewport())->scrollerProperties();

  // Adjust the physics of the scrolling
  scrollerProperties.setScrollMetric(QScrollerProperties::DragVelocitySmoothingFactor, 0.6);
  scrollerProperties.setScrollMetric(QScrollerProperties::MinimumVelocity, 0.0);
  scrollerProperties.setScrollMetric(QScrollerProperties::MaximumVelocity, 0.6);
  scrollerProperties.setScrollMetric(QScrollerProperties::AcceleratingFlickMaximumTime, 0.4);
  scrollerProperties.setScrollMetric(QScrollerProperties::AcceleratingFlickSpeedupFactor, 1.2);
  scrollerProperties.setScrollMetric(QScrollerProperties::SnapPositionRatio, 0.5);
  scrollerProperties.setScrollMetric(QScrollerProperties::MaximumClickThroughVelocity, 0);
  scrollerProperties.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.5);
  scrollerProperties.setScrollMetric(QScrollerProperties::OvershootDragDistanceFactor, 0.3);

  QScroller::scroller(m_radarScrollArea->viewport())->setScrollerProperties(scrollerProperties);

  // Create content widget for scroll area
  m_radarScrollContent = new QWidget(m_radarScrollArea);
  m_radarScrollContent->setStyleSheet("background: transparent;");
  m_radarScrollArea->setWidget(m_radarScrollContent);

  // Create layout for scrollable content
  m_radarLayout = new QGridLayout(m_radarScrollContent);
  m_radarLayout->setContentsMargins(0, 10, 10, 10);
  m_radarLayout->setSpacing(15);

  // Radar tab layout
  QVBoxLayout *radarTabLayout = new QVBoxLayout(m_radarTab);
  radarTabLayout->setContentsMargins(0, 0, 0, 0);
  radarTabLayout->addWidget(m_radarScrollArea);

  // Create layouts for each group
  QGridLayout *radarStatusLayout = new QGridLayout();
  radarStatusLayout->setSpacing(5);

  QGridLayout *lead1Layout = new QGridLayout();
  lead1Layout->setSpacing(5);

  QGridLayout *lead2Layout = new QGridLayout();
  lead2Layout->setSpacing(5);

  // Create frames for each layout
  QFrame *radarStatusFrame = createLabelFrame(radarStatusLayout, "Radar Status");
  QFrame *lead1Frame = createLabelFrame(lead1Layout, "Primary Lead Vehicle");
  QFrame *lead2Frame = createLabelFrame(lead2Layout, "Secondary Lead Vehicle");

  // Add frames to layout
  m_radarLayout->addWidget(radarStatusFrame, 0, 0, 1, 2);
  m_radarLayout->addWidget(lead1Frame, 1, 0);
  m_radarLayout->addWidget(lead2Frame, 1, 1);

  // Initialize the rows for each group
  int statusRow = 1; // Row 0 is for the heading
  int statusCol = 0;

  int lead1Row = 1;
  int lead1Col = 0;

  int lead2Row = 1;
  int lead2Col = 0;

  // Helper to add a label pair to the appropriate layout
  auto addRadarLabel = [&](const QString &group, const QString &name, const QString &initialValue) {
    QLabel *nameLabel = new QLabel(name, m_radarScrollContent);
    nameLabel->setStyleSheet(nameStyle);
    nameLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    QLabel *valueLabel = new QLabel(initialValue, m_radarScrollContent);
    valueLabel->setStyleSheet(valueStyle);
    valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QGridLayout *targetLayout;
    int *targetRow = nullptr;
    int *targetCol = nullptr;

    if (group == "Radar Status") {
      targetLayout = radarStatusLayout;
      targetRow = &statusRow;
      targetCol = &statusCol;
    } else if (group == "Lead1") {
      targetLayout = lead1Layout;
      targetRow = &lead1Row;
      targetCol = &lead1Col;
    } else if (group == "Lead2") {
      targetLayout = lead2Layout;
      targetRow = &lead2Row;
      targetCol = &lead2Col;
    } else {
      return; // Unknown group, skip
    }

    targetLayout->addWidget(nameLabel, *targetRow, *targetCol * 2);
    targetLayout->addWidget(valueLabel, *targetRow, *targetCol * 2 + 1);

    // Store in groups map
    LabelPair pair;
    pair.nameLabel = nameLabel;
    pair.valueLabel = valueLabel;
    m_radarGroups[group].append(pair);

    // Move to next position
    (*targetCol)++;
    if (*targetCol >= 2) { // 2 columns of pairs
      *targetCol = 0;
      (*targetRow)++;
    }
  };

  // Radar Status labels
  addRadarLabel("Radar Status", "CAN Error:", "No");
  addRadarLabel("Radar Status", "Radar Fault:", "No");
  addRadarLabel("Radar Status", "Wrong Config:", "No");
  addRadarLabel("Radar Status", "Temp Unavailable:", "No");
  addRadarLabel("Radar Status", "Params Unavailable:", "No");

  // Lead1 Vehicle labels
  addRadarLabel("Lead1", "Distance:", "0.0 m");
  addRadarLabel("Lead1", "Lateral Pos:", "0.0 m");
  addRadarLabel("Lead1", "Rel Velocity:", "0.0 m/s");
  addRadarLabel("Lead1", "Rel Accel:", "0.0 m/s²");
  addRadarLabel("Lead1", "Lead Velocity:", "0.0 m/s");
  addRadarLabel("Lead1", "Path Distance:", "0.0 m");
  addRadarLabel("Lead1", "Lat Velocity:", "0.0 m/s");
  addRadarLabel("Lead1", "Lead Velocity K:", "0.0 m/s");
  addRadarLabel("Lead1", "Lead Accel K:", "0.0 m/s²");
  addRadarLabel("Lead1", "FCW:", "No");
  addRadarLabel("Lead1", "Status:", "Not Valid");
  addRadarLabel("Lead1", "Accel Tau:", "0.0 s");
  addRadarLabel("Lead1", "Model Prob:", "0.0");
  addRadarLabel("Lead1", "Radar Detection:", "No");
  addRadarLabel("Lead1", "Track ID:", "-1");

  // Lead2 Vehicle labels
  addRadarLabel("Lead2", "Distance:", "0.0 m");
  addRadarLabel("Lead2", "Lateral Pos:", "0.0 m");
  addRadarLabel("Lead2", "Rel Velocity:", "0.0 m/s");
  addRadarLabel("Lead2", "Rel Accel:", "0.0 m/s²");
  addRadarLabel("Lead2", "Lead Velocity:", "0.0 m/s");
  addRadarLabel("Lead2", "Path Distance:", "0.0 m");
  addRadarLabel("Lead2", "Lat Velocity:", "0.0 m/s");
  addRadarLabel("Lead2", "Lead Velocity K:", "0.0 m/s");
  addRadarLabel("Lead2", "Lead Accel K:", "0.0 m/s²");
  addRadarLabel("Lead2", "FCW:", "No");
  addRadarLabel("Lead2", "Status:", "Not Valid");
  addRadarLabel("Lead2", "Accel Tau:", "0.0 s");
  addRadarLabel("Lead2", "Model Prob:", "0.0");
  addRadarLabel("Lead2", "Radar Detection:", "No");
  addRadarLabel("Lead2", "Track ID:", "-1");

  // Set column stretch factors
  m_radarLayout->setColumnStretch(0, 1);
  m_radarLayout->setColumnStretch(1, 1);
}

void OtherDebugPanel::setupTuningTab() {
  m_tuningTab = new QWidget(m_tabWidget);
  m_tuningTab->setStyleSheet("background: transparent;");

  // Create scroll area for tuning tab
  m_tuningScrollArea = new QScrollArea(m_tuningTab);
  m_tuningScrollArea->setWidgetResizable(true);
  m_tuningScrollArea->setFrameShape(QFrame::NoFrame);
  m_tuningScrollArea->setStyleSheet("background: transparent;");
  m_tuningScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_tuningScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_tuningScrollArea->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);

  // Enable proper kinetic scrolling
  QScroller::grabGesture(m_tuningScrollArea->viewport(), QScroller::TouchGesture);
  QScrollerProperties scrollerProperties = QScroller::scroller(m_tuningScrollArea->viewport())->scrollerProperties();

  // Adjust the physics of the scrolling
  scrollerProperties.setScrollMetric(QScrollerProperties::DragVelocitySmoothingFactor, 0.6);
  scrollerProperties.setScrollMetric(QScrollerProperties::MinimumVelocity, 0.0);
  scrollerProperties.setScrollMetric(QScrollerProperties::MaximumVelocity, 0.6);
  scrollerProperties.setScrollMetric(QScrollerProperties::AcceleratingFlickMaximumTime, 0.4);
  scrollerProperties.setScrollMetric(QScrollerProperties::AcceleratingFlickSpeedupFactor, 1.2);
  scrollerProperties.setScrollMetric(QScrollerProperties::SnapPositionRatio, 0.5);
  scrollerProperties.setScrollMetric(QScrollerProperties::MaximumClickThroughVelocity, 0);
  scrollerProperties.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.5);
  scrollerProperties.setScrollMetric(QScrollerProperties::OvershootDragDistanceFactor, 0.3);

  QScroller::scroller(m_tuningScrollArea->viewport())->setScrollerProperties(scrollerProperties);

  // Create content widget for scroll area
  m_tuningScrollContent = new QWidget(m_tuningScrollArea);
  m_tuningScrollContent->setStyleSheet("background: transparent;");
  m_tuningScrollArea->setWidget(m_tuningScrollContent);

  // Create layout for scrollable content
  m_tuningLayout = new QGridLayout(m_tuningScrollContent);
  m_tuningLayout->setContentsMargins(0, 10, 10, 10);
  m_tuningLayout->setSpacing(15);

  // Tuning tab layout
  QVBoxLayout *tuningTabLayout = new QVBoxLayout(m_tuningTab);
  tuningTabLayout->setContentsMargins(0, 0, 0, 0);
  tuningTabLayout->addWidget(m_tuningScrollArea);

  // Create layouts for each group
  QGridLayout *longitudinalLayout = new QGridLayout();
  longitudinalLayout->setSpacing(5);

  QGridLayout *lateralLayout = new QGridLayout();
  lateralLayout->setSpacing(5);

  QGridLayout *safetyLayout = new QGridLayout();
  safetyLayout->setSpacing(5);

  QGridLayout *paramsLayout = new QGridLayout();
  paramsLayout->setSpacing(5);

  // Create frames for each layout
  QFrame *longitudinalFrame = createLabelFrame(longitudinalLayout, "Longitudinal Tuning");
  QFrame *lateralFrame = createLabelFrame(lateralLayout, "Lateral Tuning");
  QFrame *safetyFrame = createLabelFrame(safetyLayout, "Safety Model");
  QFrame *paramsFrame = createLabelFrame(paramsLayout, "Car Parameters");

  // Add frames to layout
  m_tuningLayout->addWidget(longitudinalFrame, 0, 0);
  m_tuningLayout->addWidget(lateralFrame, 0, 1);
  m_tuningLayout->addWidget(safetyFrame, 1, 0);
  m_tuningLayout->addWidget(paramsFrame, 1, 1);

  // Initialize the rows for each group
  int longRow = 1; // Row 0 is for the heading
  int longCol = 0;

  int latRow = 1;
  int latCol = 0;

  int safetyRow = 1;
  int safetyCol = 0;

  int paramsRow = 1;
  int paramsCol = 0;

  // Helper to add a label pair to the appropriate layout
  auto addTuningLabel = [&](const QString &group, const QString &name, const QString &initialValue) {
    QLabel *nameLabel = new QLabel(name, m_tuningScrollContent);
    nameLabel->setStyleSheet(nameStyle);
    nameLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    QLabel *valueLabel = new QLabel(initialValue, m_tuningScrollContent);
    valueLabel->setStyleSheet(valueStyle);
    valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QGridLayout *targetLayout;
    int *targetRow = nullptr;
    int *targetCol = nullptr;

    if (group == "Longitudinal Tuning") {
      targetLayout = longitudinalLayout;
      targetRow = &longRow;
      targetCol = &longCol;
    } else if (group == "Lateral Tuning") {
      targetLayout = lateralLayout;
      targetRow = &latRow;
      targetCol = &latCol;
    } else if (group == "Safety Model") {
      targetLayout = safetyLayout;
      targetRow = &safetyRow;
      targetCol = &safetyCol;
    } else if (group == "Car Parameters") {
      targetLayout = paramsLayout;
      targetRow = &paramsRow;
      targetCol = &paramsCol;
    } else {
      return; // Unknown group, skip
    }

    targetLayout->addWidget(nameLabel, *targetRow, *targetCol * 2);
    targetLayout->addWidget(valueLabel, *targetRow, *targetCol * 2 + 1);

    // Store in groups map
    LabelPair pair;
    pair.nameLabel = nameLabel;
    pair.valueLabel = valueLabel;
    m_tuningGroups[group].append(pair);

    // Move to next position
    (*targetCol)++;
    if (*targetCol >= 2) { // 2 columns of pairs
      *targetCol = 0;
      (*targetRow)++;
    }
  };

  // Longitudinal Tuning labels
  addTuningLabel("Longitudinal Tuning", "KpBP:", "[ ]");
  addTuningLabel("Longitudinal Tuning", "KpV:", "[ ]");
  addTuningLabel("Longitudinal Tuning", "KiBP:", "[ ]");
  addTuningLabel("Longitudinal Tuning", "KiV:", "[ ]");
  addTuningLabel("Longitudinal Tuning", "Kf:", "0.0");

  // Lateral Tuning labels
  addTuningLabel("Lateral Tuning", "Type:", "Unknown");
  addTuningLabel("Lateral Tuning", "Kp:", "0.0");
  addTuningLabel("Lateral Tuning", "Ki:", "0.0");
  addTuningLabel("Lateral Tuning", "Kf:", "0.0");
  addTuningLabel("Lateral Tuning", "Friction:", "0.0");
  addTuningLabel("Lateral Tuning", "LatAccelFactor:", "0.0");
  addTuningLabel("Lateral Tuning", "LatAccelOffset:", "0.0");

  // Safety Model labels
  addTuningLabel("Safety Model", "Model:", "Unknown");
  addTuningLabel("Safety Model", "Param:", "0x0000");
  addTuningLabel("Safety Model", "Alt Experience:", "0");

  // Car Parameters labels
  addTuningLabel("Car Parameters", "Radar Unavailable:", "No");
  addTuningLabel("Car Parameters", "Rate Cost:", "0.0");
  addTuningLabel("Car Parameters", "Limit Timer:", "0.0 s");
  addTuningLabel("Car Parameters", "Steer Delay:", "0.0 s");
  addTuningLabel("Car Parameters", "Long Delay:", "0.0 s");

  // Set column stretch factors
  m_tuningLayout->setColumnStretch(0, 1);
  m_tuningLayout->setColumnStretch(1, 1);
}

void OtherDebugPanel::setupFirmwareTab() {
  m_firmwareTab = new QWidget(m_tabWidget);
  m_firmwareTab->setStyleSheet("background: transparent;");

  // Create scroll area for firmware tab
  m_firmwareScrollArea = new QScrollArea(m_firmwareTab);
  m_firmwareScrollArea->setWidgetResizable(true);
  m_firmwareScrollArea->setFrameShape(QFrame::NoFrame);
  m_firmwareScrollArea->setStyleSheet("background: transparent;");
  m_firmwareScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_firmwareScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_firmwareScrollArea->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);

  // Enable proper kinetic scrolling
  QScroller::grabGesture(m_firmwareScrollArea->viewport(), QScroller::TouchGesture);
  QScrollerProperties scrollerProperties = QScroller::scroller(m_firmwareScrollArea->viewport())->scrollerProperties();

  // Adjust the physics of the scrolling
  scrollerProperties.setScrollMetric(QScrollerProperties::DragVelocitySmoothingFactor, 0.6);
  scrollerProperties.setScrollMetric(QScrollerProperties::MinimumVelocity, 0.0);
  scrollerProperties.setScrollMetric(QScrollerProperties::MaximumVelocity, 0.6);
  scrollerProperties.setScrollMetric(QScrollerProperties::AcceleratingFlickMaximumTime, 0.4);
  scrollerProperties.setScrollMetric(QScrollerProperties::AcceleratingFlickSpeedupFactor, 1.2);
  scrollerProperties.setScrollMetric(QScrollerProperties::SnapPositionRatio, 0.5);
  scrollerProperties.setScrollMetric(QScrollerProperties::MaximumClickThroughVelocity, 0);
  scrollerProperties.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.5);
  scrollerProperties.setScrollMetric(QScrollerProperties::OvershootDragDistanceFactor, 0.3);

  QScroller::scroller(m_firmwareScrollArea->viewport())->setScrollerProperties(scrollerProperties);

  // Create content widget for scroll area
  m_firmwareScrollContent = new QWidget(m_firmwareScrollArea);
  m_firmwareScrollContent->setStyleSheet("background: transparent;");
  m_firmwareScrollArea->setWidget(m_firmwareScrollContent);

  // Create layout for scrollable content
  m_firmwareLayout = new QVBoxLayout(m_firmwareScrollContent);
  m_firmwareLayout->setContentsMargins(10, 10, 10, 10);
  m_firmwareLayout->setSpacing(15);

  // Firmware tab layout
  QVBoxLayout *firmwareTabLayout = new QVBoxLayout(m_firmwareTab);
  firmwareTabLayout->setContentsMargins(0, 0, 0, 0);
  firmwareTabLayout->addWidget(m_firmwareScrollArea);

  // Car info section
  QGridLayout *carInfoLayout = new QGridLayout();
  carInfoLayout->setSpacing(10); // Increased spacing
  QFrame *carInfoFrame = createLabelFrame(carInfoLayout, "Car Information");
  m_firmwareLayout->addWidget(carInfoFrame);

  // Initialize the rows for car info
  int carInfoRow = 1; // Row 0 is for the heading
  int carInfoCol = 0;

  // Helper to add a car info label pair
  auto addCarInfoLabel = [&](const QString &name, const QString &initialValue) {
    QLabel *nameLabel = new QLabel(name, m_firmwareScrollContent);
    nameLabel->setStyleSheet(nameStyle);

    QLabel *valueLabel = new QLabel(initialValue, m_firmwareScrollContent);
    valueLabel->setStyleSheet(valueStyle);
    valueLabel->setFixedWidth(400); // Wider for VIN and fingerprint

    carInfoLayout->addWidget(nameLabel, carInfoRow, carInfoCol * 2);
    carInfoLayout->addWidget(valueLabel, carInfoRow, carInfoCol * 2 + 1);

    // Store in groups map
    LabelPair pair;
    pair.nameLabel = nameLabel;
    pair.valueLabel = valueLabel;
    m_firmwareGroups["Car Info"].append(pair);

    // Move to next position
    carInfoCol++;
    if (carInfoCol >= 2) { // 2 columns of pairs
      carInfoCol = 0;
      carInfoRow++;
    }
  };

  // Add car info labels
  addCarInfoLabel("Fingerprint:", "Not Available");
  addCarInfoLabel("VIN:", "Not Available");
  addCarInfoLabel("Brand:", "Unknown");
  addCarInfoLabel("Transmission:", "Unknown");
  addCarInfoLabel("Fuzzy Fingerprint:", "No");
  addCarInfoLabel("Fingerprint Source:", "Unknown");
  addCarInfoLabel("Network Location:", "Unknown");

  // Create table for firmware info with larger title
  QLabel *firmwareTitle = new QLabel("ECU Firmware Information", m_firmwareScrollContent);
  firmwareTitle->setStyleSheet("font-size: 32px; font-weight: bold; color: #00AAFF; margin-top: 20px;");
  firmwareTitle->setAlignment(Qt::AlignLeft);
  m_firmwareLayout->addWidget(firmwareTitle);

  m_firmwareTable = new QTableWidget(0, 4, m_firmwareScrollContent);
  m_firmwareTable->setHorizontalHeaderLabels({"ECU", "FW Version", "Address", "Bus"});

  // Increased header font size and padding
  m_firmwareTable->horizontalHeader()->setStyleSheet(
      "QHeaderView::section { background-color: rgba(60, 60, 60, 200); color: white; font-weight: bold; padding: 15px; border: 1px solid #555; font-size: 28px; height: 60px; }");

  // Configure table
  m_firmwareTable->setColumnWidth(0, 400); // ECU name - wider
  m_firmwareTable->setColumnWidth(1, 650); // FW Version - much wider
  m_firmwareTable->setColumnWidth(2, 200); // Address - wider
  m_firmwareTable->setColumnWidth(3, 120); // Bus - wider

  // Make rows taller
  m_firmwareTable->verticalHeader()->setDefaultSectionSize(70);

  m_firmwareTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_firmwareTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_firmwareTable->verticalHeader()->setVisible(false);

  // Set size policy to expand both horizontally and vertically
  m_firmwareTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  // Enable touch scrolling for the firmware table
  QScroller::grabGesture(m_firmwareTable->viewport(), QScroller::TouchGesture);
  QScrollerProperties tableScrollerProps = QScroller::scroller(m_firmwareTable->viewport())->scrollerProperties();

  // Adjust the scrolling physics for the table
  tableScrollerProps.setScrollMetric(QScrollerProperties::DragVelocitySmoothingFactor, 0.6);
  tableScrollerProps.setScrollMetric(QScrollerProperties::MinimumVelocity, 0.0);
  tableScrollerProps.setScrollMetric(QScrollerProperties::MaximumVelocity, 0.6);
  tableScrollerProps.setScrollMetric(QScrollerProperties::AcceleratingFlickMaximumTime, 0.4);
  tableScrollerProps.setScrollMetric(QScrollerProperties::AcceleratingFlickSpeedupFactor, 1.2);
  tableScrollerProps.setScrollMetric(QScrollerProperties::SnapPositionRatio, 0.5);
  tableScrollerProps.setScrollMetric(QScrollerProperties::MaximumClickThroughVelocity, 0);
  tableScrollerProps.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.5);
  tableScrollerProps.setScrollMetric(QScrollerProperties::OvershootDragDistanceFactor, 0.3);

  QScroller::scroller(m_firmwareTable->viewport())->setScrollerProperties(tableScrollerProps);

  // Make table take the full width
  m_firmwareLayout->addWidget(m_firmwareTable, 1); // The 1 argument gives it a stretch factor

  // Add empty space at bottom
  m_firmwareLayout->addStretch();

  setupTableStyle();
}

void OtherDebugPanel::setupDeviceTab() {
  m_deviceTab = new QWidget(m_tabWidget);
  m_deviceTab->setStyleSheet("background: transparent;");

  // Create scroll area for device tab
  m_deviceScrollArea = new QScrollArea(m_deviceTab);
  m_deviceScrollArea->setWidgetResizable(true);
  m_deviceScrollArea->setFrameShape(QFrame::NoFrame);
  m_deviceScrollArea->setStyleSheet("background: transparent;");
  m_deviceScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_deviceScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_deviceScrollArea->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);

  // Enable proper kinetic scrolling
  QScroller::grabGesture(m_deviceScrollArea->viewport(), QScroller::TouchGesture);
  QScrollerProperties scrollerProperties = QScroller::scroller(m_deviceScrollArea->viewport())->scrollerProperties();

  // Adjust the physics of the scrolling
  scrollerProperties.setScrollMetric(QScrollerProperties::DragVelocitySmoothingFactor, 0.6);
  scrollerProperties.setScrollMetric(QScrollerProperties::MinimumVelocity, 0.0);
  scrollerProperties.setScrollMetric(QScrollerProperties::MaximumVelocity, 0.6);
  scrollerProperties.setScrollMetric(QScrollerProperties::AcceleratingFlickMaximumTime, 0.4);
  scrollerProperties.setScrollMetric(QScrollerProperties::AcceleratingFlickSpeedupFactor, 1.2);
  scrollerProperties.setScrollMetric(QScrollerProperties::SnapPositionRatio, 0.5);
  scrollerProperties.setScrollMetric(QScrollerProperties::MaximumClickThroughVelocity, 0);
  scrollerProperties.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.5);
  scrollerProperties.setScrollMetric(QScrollerProperties::OvershootDragDistanceFactor, 0.3);

  QScroller::scroller(m_deviceScrollArea->viewport())->setScrollerProperties(scrollerProperties);

  // Create content widget for scroll area
  m_deviceScrollContent = new QWidget(m_deviceScrollArea);
  m_deviceScrollContent->setStyleSheet("background: transparent;");
  m_deviceScrollArea->setWidget(m_deviceScrollContent);

  // Create layout for scrollable content
  m_deviceLayout = new QGridLayout(m_deviceScrollContent);
  m_deviceLayout->setContentsMargins(0, 10, 10, 10);
  m_deviceLayout->setSpacing(10);

  // Device tab layout
  QVBoxLayout *deviceTabLayout = new QVBoxLayout(m_deviceTab);
  deviceTabLayout->setContentsMargins(0, 0, 0, 0);
  deviceTabLayout->addWidget(m_deviceScrollArea);

  // Create layouts for each group
  QGridLayout *deviceStatusLayout = new QGridLayout();
  deviceStatusLayout->setSpacing(2);

  QGridLayout *powerLayout = new QGridLayout();
  powerLayout->setSpacing(2);

  QGridLayout *networkLayout = new QGridLayout();
  networkLayout->setSpacing(2);

  QGridLayout *systemAndTempLayout = new QGridLayout();
  systemAndTempLayout->setSpacing(2);

  // Create frames for each layout
  QFrame *deviceStatusFrame = createLabelFrame(deviceStatusLayout, "Device Status");
  deviceStatusFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  QFrame *powerFrame = createLabelFrame(powerLayout, "Power");
  powerFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  QFrame *networkFrame = createLabelFrame(networkLayout, "Network");
  networkFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  QFrame *systemAndTempFrame = createLabelFrame(systemAndTempLayout, "System & Temperatures");
  systemAndTempFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  // Add frames to layout
  // Row 0: Device Status | Power
  m_deviceLayout->addWidget(deviceStatusFrame, 0, 0);
  m_deviceLayout->addWidget(powerFrame, 0, 1);

  // Row 1: Network (spans 2 columns)
  m_deviceLayout->addWidget(networkFrame, 1, 0, 1, 2);

  // Row 2: System & Temperatures (spans 2 columns)
  m_deviceLayout->addWidget(systemAndTempFrame, 2, 0, 1, 2);

  // Set column stretch factors to ensure even distribution
  m_deviceLayout->setColumnStretch(0, 1);
  m_deviceLayout->setColumnStretch(1, 1);

  // Initialize the rows for each group
  int deviceStatusRow = 1; // Row 0 is for the heading
  int deviceStatusCol = 0;

  int powerRow = 1;
  int powerCol = 0;

  int networkRow = 1;
  int networkCol = 0;

  int systemAndTempRow = 1;
  int systemAndTempCol = 0;

  // Helper to add a label pair to the appropriate layout
  auto addDeviceLabel = [&](const QString &group, const QString &name, const QString &initialValue) {
    QLabel *nameLabel = new QLabel(name, m_deviceScrollContent);
    nameLabel->setStyleSheet(nameStyle);
    nameLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    QLabel *valueLabel = new QLabel(initialValue, m_deviceScrollContent);
    valueLabel->setStyleSheet(valueStyle);
    valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QGridLayout *targetLayout;
    int *targetRow = nullptr;
    int *targetCol = nullptr;

    if (group == "Device Status") {
      targetLayout = deviceStatusLayout;
      targetRow = &deviceStatusRow;
      targetCol = &deviceStatusCol;
    } else if (group == "Power") {
      targetLayout = powerLayout;
      targetRow = &powerRow;
      targetCol = &powerCol;
    } else if (group == "Network") {
      targetLayout = networkLayout;
      targetRow = &networkRow;
      targetCol = &networkCol;
    } else if (group == "System & Temperatures") {
      targetLayout = systemAndTempLayout;
      targetRow = &systemAndTempRow;
      targetCol = &systemAndTempCol;
    } else {
      return; // Unknown group, skip
    }

    targetLayout->addWidget(nameLabel, *targetRow, *targetCol * 2);
    targetLayout->addWidget(valueLabel, *targetRow, *targetCol * 2 + 1);

    // Store in groups map
    LabelPair pair;
    pair.nameLabel = nameLabel;
    pair.valueLabel = valueLabel;
    m_deviceGroups[group].append(pair);

    // Move to next position
    (*targetCol)++;
    if (*targetCol >= 2) { // 2 columns of pairs
      *targetCol = 0;
      (*targetRow)++;
    }
  };

  // Device Status labels
  addDeviceLabel("Device Status", "Device Type:", "Unknown");
  addDeviceLabel("Device Status", "Started:", "No");
  addDeviceLabel("Device Status", "Thermal Status:", "Unknown");
  addDeviceLabel("Device Status", "Fan Speed:", "0%");
  addDeviceLabel("Device Status", "Brightness:", "0%");
  // addDeviceLabel("Device Status", "Last Ping:", "N/A");

  // Power labels
  addDeviceLabel("Power", "Power Draw:", "0.0 W");
  addDeviceLabel("Power", "SOM Power Draw:", "0.0 W");
  addDeviceLabel("Power", "Offroad Power:", "0 µWh");
  addDeviceLabel("Power", "Car Battery:", "0 µWh");

  // Network labels
  addDeviceLabel("Network", "Type:", "None");
  addDeviceLabel("Network", "Strength:", "Unknown");
  addDeviceLabel("Network", "Metered:", "No");
  addDeviceLabel("Network", "Technology:", "N/A");
  addDeviceLabel("Network", "Operator:", "N/A");
  addDeviceLabel("Network", "Band:", "N/A");
  addDeviceLabel("Network", "Channel:", "0");
  addDeviceLabel("Network", "State:", "N/A");
  addDeviceLabel("Network", "TX Data:", "0 B");
  addDeviceLabel("Network", "RX Data:", "0 B");

  // System & Temperatures labels
  addDeviceLabel("System & Temperatures", "Free Space:", "0%");
  addDeviceLabel("System & Temperatures", "Memory Usage:", "0%");
  addDeviceLabel("System & Temperatures", "GPU Usage:", "0%");
  addDeviceLabel("System & Temperatures", "CPU Usage:", "N/A");

  // Temperature labels
  addDeviceLabel("System & Temperatures", "CPU Temp:", "N/A");
  addDeviceLabel("System & Temperatures", "GPU Temp:", "N/A");
  addDeviceLabel("System & Temperatures", "Memory Temp:", "0.0°C");
  addDeviceLabel("System & Temperatures", "NVME Temp:", "N/A");
  addDeviceLabel("System & Temperatures", "Modem Temp:", "N/A");
  addDeviceLabel("System & Temperatures", "PMIC Temp:", "N/A");
  addDeviceLabel("System & Temperatures", "Intake Temp:", "0.0°C");
  addDeviceLabel("System & Temperatures", "Exhaust Temp:", "0.0°C");
  addDeviceLabel("System & Temperatures", "Case Temp:", "0.0°C");
  addDeviceLabel("System & Temperatures", "Max Temp:", "0.0°C");
}

void OtherDebugPanel::setupCANTab() {
  m_canTab = new QWidget(m_tabWidget);
  m_canTab->setStyleSheet("background: transparent;");

  // Create main layout for CAN tab
  QVBoxLayout *canMainLayout = new QVBoxLayout(m_canTab);
  canMainLayout->setContentsMargins(5, 5, 5, 5);  // Smaller margins for 6" display
  canMainLayout->setSpacing(5);  // Tighter spacing

  // Create compact title label
  QLabel *titleLabel = new QLabel("CAN Monitor (Cabana Lite)", m_canTab);
  titleLabel->setStyleSheet(R"(
    font-size: 36px;
    font-weight: bold;
    color: #18b4ff;
    padding: 3px;
    text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.6);
  )");
  titleLabel->setAlignment(Qt::AlignCenter);
  canMainLayout->addWidget(titleLabel);

  // Create compact control panel
  QWidget *controlPanel = new QWidget(m_canTab);
  QHBoxLayout *controlLayout = new QHBoxLayout(controlPanel);
  controlLayout->setContentsMargins(3, 1, 3, 1);

  // Compact Pause/Resume button
  m_canPauseButton = new QPushButton("⏸", controlPanel);
  m_canPauseButton->setToolTip("Pause/Resume updates");
  m_canPauseButton->setStyleSheet(R"(
    QPushButton {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #27ae60, stop: 1 #2ecc71);
      color: white;
      border: none;
      border-radius: 6px;
      font-size: 28px;
      font-weight: bold;
      padding: 5px 10px;
      min-width: 45px;
      max-width: 45px;
    }
    QPushButton:pressed {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #2ecc71, stop: 1 #27ae60);
    }
  )");

  // Remove filter checkbox since we're showing all DBC-matched messages

  // Message count label
  m_canUpdateRateLabel = new QLabel("Messages: 0", controlPanel);
  m_canUpdateRateLabel->setStyleSheet(R"(
    color: #95a5a6;
    font-size: 24px;
    font-weight: bold;
  )");

  controlLayout->addWidget(m_canPauseButton);
  controlLayout->addStretch();
  controlLayout->addWidget(m_canUpdateRateLabel);

  canMainLayout->addWidget(controlPanel);

  // Create horizontal splitter to divide messages and signals
  QSplitter *splitter = new QSplitter(Qt::Horizontal, m_canTab);
  splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  canMainLayout->addWidget(splitter, 1);  // Give it stretch factor 1 to use available space

  // Left side: Message table (Cabana-style)
  QWidget *messageWidget = new QWidget();
  QVBoxLayout *messageLayout = new QVBoxLayout(messageWidget);
  messageLayout->setContentsMargins(3, 3, 3, 3);

  m_canMessageLabel = new QLabel("Messages", messageWidget);
  m_canMessageLabel->setStyleSheet(R"(
    font-size: 28px;
    font-weight: bold;
    color: #18b4ff;
    padding: 2px;
    background: #1a1a1a;
    border-bottom: 2px solid #18b4ff;
  )");
  messageLayout->addWidget(m_canMessageLabel);

  // Create Cabana-like message table with optimized columns for 5"+ display
  m_canMessageTable = new QTableWidget(0, 3, messageWidget);
  m_canMessageTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  QStringList messageHeaders = {"Bus", "Freq", "Name"};
  m_canMessageTable->setHorizontalHeaderLabels(messageHeaders);

  // Configure table to look like Cabana
  m_canMessageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_canMessageTable->setAlternatingRowColors(true);
  m_canMessageTable->horizontalHeader()->setStretchLastSection(false);  // Don't stretch last column
  m_canMessageTable->verticalHeader()->setVisible(false);
  m_canMessageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

  // Optimize column widths for better space utilization
  m_canMessageTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch); // Name stretches
  m_canMessageTable->setColumnWidth(0, 100);  // Bus - reduced width since font is smaller
  m_canMessageTable->setColumnWidth(1, 80);   // Freq - increased width for better readability
  // Column 2 (Name) stretches
  m_canMessageTable->setStyleSheet(R"(
    QTableWidget {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #1a1a1a, stop: 1 #0d0d0d);
      color: #ecf0f1;
      border: 1px solid #333;
      border-radius: 4px;
      font-family: 'Consolas', 'Monaco', monospace;
      font-size: 22px;
      gridline-color: #333;
      alternate-background-color: rgba(255, 255, 255, 5);
    }
    QTableWidget::item {
      padding: 2px;
      border: none;
    }
    QTableWidget::item:selected {
      background: rgba(24, 180, 255, 100);
      color: white;
    }
    QHeaderView::section {
      background: #2a2a2a;
      color: #18b4ff;
      font-weight: bold;
      padding: 4px;
      border: 1px solid #333;
      font-size: 24px;
      text-transform: uppercase;
    }
  )");
  messageLayout->addWidget(m_canMessageTable, 1);  // Give stretch factor to the table
  splitter->addWidget(messageWidget);

  // Right side: Signal details
  QWidget *signalWidget = new QWidget();
  QVBoxLayout *signalLayout = new QVBoxLayout(signalWidget);
  signalLayout->setContentsMargins(3, 3, 3, 3);

  QLabel *signalLabel = new QLabel("Signals", signalWidget);
  signalLabel->setObjectName("signalLabel"); // Set object name for easy access
  signalLabel->setStyleSheet(R"(
    font-size: 28px;
    font-weight: bold;
    color: #18b4ff;
    padding: 2px;
    background: #1a1a1a;
    border-bottom: 2px solid #18b4ff;
    )");
  signalLayout->addWidget(signalLabel);

  m_canSignalTable = new QTableWidget(0, 4, signalWidget);
  m_canSignalTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  QStringList headers = {"Signal Name", "Value", "Unit", "Range"};
  m_canSignalTable->setHorizontalHeaderLabels(headers);
  m_canSignalTable->setStyleSheet(R"(
    QTableWidget {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #1a1a1a, stop: 1 #0d0d0d);
      color: #ecf0f1;
      border: 1px solid #333;
      border-radius: 4px;
      gridline-color: #333;
      font-family: 'Consolas', 'Monospace', monospace;
      font-size: 22px;
      alternate-background-color: rgba(255, 255, 255, 5);
    }
    QTableWidget::item {
      padding: 2px;
      border: none;
    }
    QTableWidget::item:selected {
      background: rgba(24, 180, 255, 100);
      color: white;
    }
    QHeaderView::section {
      background: #2a2a2a;
      color: #18b4ff;
      font-weight: bold;
      padding: 4px;
      border: 1px solid #333;
      text-transform: uppercase;
      font-size: 24px;
    }
  )");

  // Configure table properties
  m_canSignalTable->horizontalHeader()->setStretchLastSection(true);
  m_canSignalTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  m_canSignalTable->verticalHeader()->setVisible(false);
  m_canSignalTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_canSignalTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_canSignalTable->setAlternatingRowColors(true);
  m_canSignalTable->setShowGrid(false);

  // Set initial column widths - optimized for smaller fonts
  m_canSignalTable->setColumnWidth(0, 200); // Signal Name - reduced width
  m_canSignalTable->setColumnWidth(1, 120); // Value - reduced width
  m_canSignalTable->setColumnWidth(2, 80);  // Unit - reduced width
  m_canSignalTable->setColumnWidth(3, 150); // Range - reduced width

  signalLayout->addWidget(m_canSignalTable, 1);  // Give stretch factor to the table
  splitter->addWidget(signalWidget);

  // Set splitter proportions (70% for messages, 30% for signals)
  splitter->setSizes({700, 300});

  // Connect message selection to signal display
  QObject::connect(m_canMessageTable, &QTableWidget::currentCellChanged,
          this, [this](int currentRow, int currentColumn, int previousRow, int previousColumn) {
    Q_UNUSED(currentColumn);
    Q_UNUSED(previousRow);
    Q_UNUSED(previousColumn);
    if (currentRow >= 0 && currentRow < m_canMessageTable->rowCount()) {
      QTableWidgetItem *item = m_canMessageTable->item(currentRow, 0); // ID column (now column 0)
      if (item) {
        m_selectedCANMessage = item->data(Qt::UserRole).toInt();
        updateCANSignals();
      }
    }
  });

  // Connect pause button
  QObject::connect(m_canPauseButton, &QPushButton::clicked, this, [this]() {
    static bool isPaused = false;
    isPaused = !isPaused;
    m_canPauseButton->setText(isPaused ? "▶" : "⏸");
    m_canPauseButton->setStyleSheet(isPaused ? R"(
      QPushButton {
        background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                    stop: 0 #e74c3c, stop: 1 #c0392b);
        color: white; border: none; border-radius: 8px;
        font-size: 18px; font-weight: bold; padding: 8px 16px; min-width: 120px;
      }
    )" : R"(
      QPushButton {
        background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                    stop: 0 #27ae60, stop: 1 #2ecc71);
        color: white; border: none; border-radius: 8px;
        font-size: 18px; font-weight: bold; padding: 8px 16px; min-width: 120px;
      }
    )");
    if (m_worker) {
      m_worker->setCANUpdatesPaused(isPaused);
    }
  });

  // Filter checkbox removed - showing all DBC-matched messages

  // Connect tab changes to CAN subscription management
  // DISABLED: CAN debug section disabled
  /*
  QObject::connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
    bool isCANTab = (m_tabWidget->tabText(index) == "CAN");
    qDebug() << "Tab changed to index:" << index << "tab text:" << m_tabWidget->tabText(index) << "isCANTab:" << isCANTab;
    setCANTabActive(isCANTab);
  });
  */
}

void OtherDebugPanel::updateMainLabels() {
  // Helper to format boolean values
  auto formatBool = [](bool value, const QString &trueText = "Yes", const QString &falseText = "No") { return value ? trueText : falseText; };

  // Helper to format gear shifter enum
  auto formatGear = [](int gear) {
    switch (gear) {
    case 1:
      return "Park";
    case 2:
      return "Drive";
    case 3:
      return "Neutral";
    case 4:
      return "Reverse";
    case 5:
      return "Sport";
    case 6:
      return "Low";
    case 7:
      return "Brake";
    case 8:
      return "Eco";
    case 9:
      return "Manumatic";
    default:
      return "Unknown";
    }
  };

  // Update Vehicle Dynamics group
  try {
    if (m_groups.contains("Vehicle Dynamics")) {
      int idx = 0;
      auto &group = m_groups["Vehicle Dynamics"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->carValues.vEgo, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->carValues.vEgoRaw, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_cache->carValues.aEgo, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 rad/s").arg(m_cache->carValues.yawRate, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.standstill));
      if (idx < group.size())
        group[idx++].valueLabel->setText(m_cache->carValues.engineRpm > 0 ? QString("%1").arg(m_cache->carValues.engineRpm, 0, 'f', 0) : "N/A");
    }
  } catch (const std::exception &e) {
    qWarning() << "Error updating Vehicle Dynamics group:" << e.what();
  }

  // Update Steering group
  try {
    if (m_groups.contains("Steering")) {
      int idx = 0;
      auto &group = m_groups["Steering"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1°").arg(m_cache->carValues.steeringAngleDeg, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1°/s").arg(m_cache->carValues.steeringRateDeg, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->carValues.steeringTorque, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->carValues.steeringTorqueEps, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.steeringPressed));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.steerFaultTemporary));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.steerFaultPermanent));
    }
  } catch (const std::exception &e) {
    qWarning() << "Error updating Steering group:" << e.what();
  }

  // Update Pedals group
  try {
    if (m_groups.contains("Pedals")) {
      int idx = 0;
      auto &group = m_groups["Pedals"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->carValues.gas, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.gasPressed));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->carValues.brake, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.brakePressed));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.regenBraking));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.clutchPressed));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.parkingBrake));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.brakeHoldActive));
    }
  } catch (const std::exception &e) {
    qWarning() << "Error updating Pedals group:" << e.what();
  }

  // Update Vehicle Systems group
  try {
    if (m_groups.contains("Vehicle Systems")) {
      int idx = 0;
      auto &group = m_groups["Vehicle Systems"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.espDisabled));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.espActive));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.leftBlinker));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.rightBlinker));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatGear(m_cache->carValues.gearShifter));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1%").arg(m_cache->carValues.fuelGauge * 100.0, 0, 'f', 1));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.charging));
    }
  } catch (const std::exception &e) {
    qWarning() << "Error updating Vehicle Systems group:" << e.what();
  }

  // Update Safety group
  try {
    if (m_groups.contains("Safety")) {
      int idx = 0;
      auto &group = m_groups["Safety"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.stockAeb));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.stockFcw));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.invalidLkasSetting));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.doorOpen));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.seatbeltUnlatched, "Unlatched", "Latched"));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.vehicleSensorsInvalid));
    }
  } catch (const std::exception &e) {
    qWarning() << "Error updating Safety group:" << e.what();
  }

  // Update Vehicle Parameters group
  try {
    if (m_groups.contains("Vehicle Parameters")) {
      int idx = 0;
      auto &group = m_groups["Vehicle Parameters"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 kg").arg(m_cache->paramValues.mass, 0, 'f', 0));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m").arg(m_cache->paramValues.wheelbase, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.steerRatio, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1s").arg(m_cache->paramValues.steerActuatorDelay, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1s").arg(m_cache->paramValues.longitudinalActuatorDelay, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->paramValues.vEgoStopping, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->paramValues.vEgoStarting, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.tireStiffnessFactor, 0, 'f', 2));
    }
  } catch (const std::exception &e) {
    qWarning() << "Error updating Vehicle Parameters group:" << e.what();
  }

  // Update Cruise Control group
  try {
    if (m_groups.contains("Cruise Control")) {
      int idx = 0;
      auto &group = m_groups["Cruise Control"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.cruiseEnabled));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->carValues.cruiseSpeed, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.cruiseAvailable));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.cruiseStandstill));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->carValues.cruiseNonAdaptive));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->carValues.cruiseSpeedLimit, 0, 'f', 2));
    }
  } catch (const std::exception &e) {
    qWarning() << "Error updating Cruise Control group:" << e.what();
  }

  // Update Actuator Outputs group
  try {
    if (m_groups.contains("Actuator Outputs")) {
      int idx = 0;
      auto &group = m_groups["Actuator Outputs"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1°").arg(m_cache->outputValues.steeringAngleDeg, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->outputValues.torque, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->outputValues.curvature, 0, 'f', 6));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_cache->outputValues.accel, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->outputValues.gas, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->outputValues.brake, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->outputValues.torqueOutputCan, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatLongControlState(m_cache->outputValues.longControlState));
    }
  } catch (const std::exception &e) {
    qWarning() << "Error updating Actuator Outputs group:" << e.what();
  }
}

void OtherDebugPanel::updateRadarLabels() {
  // Helper to format boolean values
  auto formatBool = [](bool value, const QString &trueText = "Yes", const QString &falseText = "No") { return value ? trueText : falseText; };

  // Update Radar Status
  try {
    if (m_radarGroups.contains("Radar Status")) {
      int idx = 0;
      auto &group = m_radarGroups["Radar Status"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->radarValues.errors.canError));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->radarValues.errors.radarFault));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->radarValues.errors.wrongConfig));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->radarValues.errors.radarUnavailableTemporary));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->paramValues.radarUnavailable));
    }
  } catch (const std::exception &e) {
    qWarning() << "Error updating Radar Status group:" << e.what();
  }

  // Update Lead1 group
  try {
    if (m_radarGroups.contains("Lead1")) {
      int idx = 0;
      auto &group = m_radarGroups["Lead1"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m").arg(m_cache->radarValues.leadOne.dRel, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m").arg(m_cache->radarValues.leadOne.yRel, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->radarValues.leadOne.vRel, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_cache->radarValues.leadOne.aRel, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->radarValues.leadOne.vLead, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m").arg(m_cache->radarValues.leadOne.dPath, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->radarValues.leadOne.vLat, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->radarValues.leadOne.vLeadK, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_cache->radarValues.leadOne.aLeadK, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->radarValues.leadOne.fcw));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->radarValues.leadOne.status, "Valid", "Not Valid"));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 s").arg(m_cache->radarValues.leadOne.aLeadTau, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->radarValues.leadOne.modelProb, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->radarValues.leadOne.radar));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->radarValues.leadOne.radarTrackId));
    }
  } catch (const std::exception &e) {
    qWarning() << "Error updating Lead1 group:" << e.what();
  }

  // Update Lead2 group
  try {
    if (m_radarGroups.contains("Lead2")) {
      int idx = 0;
      auto &group = m_radarGroups["Lead2"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m").arg(m_cache->radarValues.leadTwo.dRel, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m").arg(m_cache->radarValues.leadTwo.yRel, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->radarValues.leadTwo.vRel, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_cache->radarValues.leadTwo.aRel, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->radarValues.leadTwo.vLead, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m").arg(m_cache->radarValues.leadTwo.dPath, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->radarValues.leadTwo.vLat, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s").arg(m_cache->radarValues.leadTwo.vLeadK, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 m/s²").arg(m_cache->radarValues.leadTwo.aLeadK, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->radarValues.leadTwo.fcw));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->radarValues.leadTwo.status, "Valid", "Not Valid"));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 s").arg(m_cache->radarValues.leadTwo.aLeadTau, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->radarValues.leadTwo.modelProb, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->radarValues.leadTwo.radar));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->radarValues.leadTwo.radarTrackId));
    }
  } catch (const std::exception &e) {
    qWarning() << "Error updating Lead2 group:" << e.what();
  }
}

void OtherDebugPanel::updateTuningLabels() {
  // Update Lateral Tuning
  try {
    if (m_tuningGroups.contains("Lateral Tuning")) {
      int idx = 0;
      auto &group = m_tuningGroups["Lateral Tuning"];

      if (m_cache->paramValues.lateralTuningType == OtherDataCache::CarParameterValues::LateralTuningType::PID) {
        if (idx < group.size())
          group[idx++].valueLabel->setText("PID");
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.pidKp, 0, 'f', 4));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.pidKi, 0, 'f', 4));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.pidKf, 0, 'f', 4));
        if (idx < group.size())
          group[idx++].valueLabel->setText("N/A"); // Friction
        if (idx < group.size())
          group[idx++].valueLabel->setText("N/A"); // LatAccelFactor
        if (idx < group.size())
          group[idx++].valueLabel->setText("N/A"); // LatAccelOffset
      } else if (m_cache->paramValues.lateralTuningType == OtherDataCache::CarParameterValues::LateralTuningType::TORQUE) {
        if (idx < group.size())
          group[idx++].valueLabel->setText("Torque");
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.torqueKp, 0, 'f', 4));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.torqueKi, 0, 'f', 4));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.torqueKf, 0, 'f', 4));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.torqueFriction, 0, 'f', 4));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.torqueLatAccelFactor, 0, 'f', 4));
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.torqueLatAccelOffset, 0, 'f', 4));
      } else {
        // Handle any other lateral tuning type
        if (idx < group.size())
          group[idx++].valueLabel->setText("Unknown");
      }
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] updateTuningLabels: Error updating Lateral Tuning group:" << e.what() << std::endl;
  }

  // Update Longitudinal Tuning
  try {
    if (m_tuningGroups.contains("Longitudinal Tuning")) {
      int idx = 0;
      auto &group = m_tuningGroups["Longitudinal Tuning"];

      QString kpBPStr = "[";
      for (int i = 0; i < m_cache->paramValues.longKpBP.size(); i++) {
        kpBPStr += QString("%1").arg(m_cache->paramValues.longKpBP[i], 0, 'f', 1);
        if (i < m_cache->paramValues.longKpBP.size() - 1)
          kpBPStr += ", ";
      }
      kpBPStr += "]";
      if (idx < group.size())
        group[idx++].valueLabel->setText(kpBPStr);

      QString kpVStr = "[";
      for (int i = 0; i < m_cache->paramValues.longKpV.size(); i++) {
        kpVStr += QString("%1").arg(m_cache->paramValues.longKpV[i], 0, 'f', 3);
        if (i < m_cache->paramValues.longKpV.size() - 1)
          kpVStr += ", ";
      }
      kpVStr += "]";
      if (idx < group.size())
        group[idx++].valueLabel->setText(kpVStr);

      QString kiBPStr = "[";
      for (int i = 0; i < m_cache->paramValues.longKiBP.size(); i++) {
        kiBPStr += QString("%1").arg(m_cache->paramValues.longKiBP[i], 0, 'f', 1);
        if (i < m_cache->paramValues.longKiBP.size() - 1)
          kiBPStr += ", ";
      }
      kiBPStr += "]";
      if (idx < group.size())
        group[idx++].valueLabel->setText(kiBPStr);

      QString kiVStr = "[";
      for (int i = 0; i < m_cache->paramValues.longKiV.size(); i++) {
        kiVStr += QString("%1").arg(m_cache->paramValues.longKiV[i], 0, 'f', 3);
        if (i < m_cache->paramValues.longKiV.size() - 1)
          kiVStr += ", ";
      }
      kiVStr += "]";
      if (idx < group.size())
        group[idx++].valueLabel->setText(kiVStr);

      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.longKf, 0, 'f', 4));
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] updateTuningLabels: Error updating Longitudinal Tuning group:" << e.what() << std::endl;
  }

  // Update Safety Model section
  try {
    if (m_tuningGroups.contains("Safety Model")) {
      int idx = 0;
      auto &group = m_tuningGroups["Safety Model"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatSafetyModel(m_cache->paramValues.safetyModel));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("0x%1").arg(m_cache->paramValues.safetyParam, 4, 16, QChar('0')));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.alternativeExperience));
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] updateTuningLabels: Error updating Safety Model group:" << e.what() << std::endl;
  }

  // Update Car Parameters section
  try {
    if (m_tuningGroups.contains("Car Parameters")) {
      int idx = 0;
      auto &group = m_tuningGroups["Car Parameters"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->paramValues.radarUnavailable));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1").arg(m_cache->paramValues.steerRateCost, 0, 'f', 4));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 s").arg(m_cache->paramValues.steerLimitTimer, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 s").arg(m_cache->paramValues.steerActuatorDelay, 0, 'f', 3));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 s").arg(m_cache->paramValues.longitudinalActuatorDelay, 0, 'f', 3));
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] updateTuningLabels: Error updating Car Parameters group:" << e.what() << std::endl;
  }
}

void OtherDebugPanel::updateFirmwareTable() {
  // Skip if tab isn't visible
  if (!m_firmwareTab->isVisible()) {
    BPLog::bpWarn() << "[bp.other.debug.panel] updateFirmwareTable: Firmware tab not visible, skipping update" << std::endl;
    return;
  }

  // Update firmware table with batch updates
  try {
    if (!m_firmwareTable) {
      BPLog::bpError() << "[bp.other.debug.panel] updateFirmwareTable: Firmware table widget is null" << std::endl;
      return;
    }

    if (!m_cache) {
      BPLog::bpError() << "[bp.other.debug.panel] updateFirmwareTable: Cache is null" << std::endl;
      return;
    }

    // Disable updates for better performance
    m_firmwareTable->setUpdatesEnabled(false);
    m_firmwareTable->setRowCount(0); // Clear existing rows

    // Debug: Print total number of firmware entries
    // BPLog::bpInfo() << "[bp.other.debug.panel] updateFirmwareTable: === Firmware Table Update ===" << std::endl;
    // BPLog::bpInfo() << "[bp.other.debug.panel] updateFirmwareTable: Cache valid: " << (m_cache->valid ? "true" : "false") << std::endl;
    // BPLog::bpInfo() << "[bp.other.debug.panel] updateFirmwareTable: CarParams updated: " << (m_cache->updated.carParams ? "true" : "false") << std::endl;
    // BPLog::bpInfo() << "[bp.other.debug.panel] updateFirmwareTable: Total firmware entries in cache: " << m_cache->paramValues.carFw.size() << std::endl;
    // BPLog::bpInfo() << "[bp.other.debug.panel] updateFirmwareTable: Safety model: " << m_cache->paramValues.safetyModel << std::endl;

    // Define Ford firmware pattern
    static const QRegularExpression fordPattern("^[A-Za-z0-9]{4}-[A-Za-z0-9]{5,6}-[A-Za-z0-9]{2,4}$");

    for (const auto &fw : m_cache->paramValues.carFw) {
      // Clean and validate the version string
      QString cleanVersion;
      for (QChar c : fw.fwVersion) {
        if (c != QChar(0) && c.isPrint()) {
          cleanVersion.append(c);
        }
      }
      cleanVersion = cleanVersion.trimmed();

      // Skip if not matching Ford firmware pattern
      if (!fordPattern.match(cleanVersion).hasMatch()) {
        continue;
      }

      // Skip empty firmware versions
      if (fw.fwVersion.isEmpty()) {
        // BPLog::bpInfo() << "[bp.other.debug.panel] updateFirmwareTable: Skipping empty firmware version" << std::endl;
        continue;
      }

      try {
        // Create and add each item
        QTableWidgetItem *ecuItem = new QTableWidgetItem(formatEcu(fw.ecu));
        QTableWidgetItem *versionItem = new QTableWidgetItem(fw.fwVersion);
        QTableWidgetItem *addressItem = new QTableWidgetItem(QString("0x%1").arg(fw.address, 0, 16));
        QTableWidgetItem *busItem = new QTableWidgetItem(QString("%1").arg(fw.bus));

        // Set large font for all items
        QFont largeFont("Arial", 28, QFont::Normal);
        ecuItem->setFont(largeFont);
        versionItem->setFont(largeFont);
        addressItem->setFont(largeFont);
        busItem->setFont(largeFont);

        // Special highlight for Ford ECUs
        if (m_cache->paramValues.safetyModel == 6) { // 6 is the Ford safety model
          ecuItem->setForeground(QColor(0, 255, 0)); // Bright green
          ecuItem->setFont(QFont("Arial", 28, QFont::Bold));
        }

        // Add a new row
        int row = m_firmwareTable->rowCount();
        m_firmwareTable->insertRow(row);
        m_firmwareTable->setItem(row, 0, ecuItem);
        m_firmwareTable->setItem(row, 1, versionItem);
        m_firmwareTable->setItem(row, 2, addressItem);
        m_firmwareTable->setItem(row, 3, busItem);

        // BPLog::bpInfo() << "[bp.other.debug.panel] updateFirmwareTable: Added row " << row << " to table" << std::endl;
      } catch (const std::exception &e) {
        BPLog::bpError() << "[bp.other.debug.panel] updateFirmwareTable: Error adding firmware row: " << e.what() << std::endl;
      }
    }

    // Re-enable updates now that we're done
    m_firmwareTable->setUpdatesEnabled(true);
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] updateFirmwareTable: Error updating firmware table: " << e.what() << std::endl;
  }
}

void OtherDebugPanel::updateDeviceLabels() {
  // Helper to format boolean values
  auto formatBool = [](bool value, const QString &trueText = "Yes", const QString &falseText = "No") { return value ? trueText : falseText; };

  // Update Device Status section
  try {
    if (m_deviceGroups.contains("Device Status")) {
      int idx = 0;
      auto &group = m_deviceGroups["Device Status"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatDeviceType(m_cache->deviceValues.deviceType));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->deviceValues.started));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatThermalStatus(m_cache->deviceValues.thermalStatus));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1%").arg(m_cache->deviceValues.fanSpeedPercentDesired));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1%").arg(m_cache->deviceValues.screenBrightnessPercent));

      // Format last ping time
      // QString pingTimeStr = "N/A";
      // if (m_deviceValues.lastAthenaPingTime > 0) {
      //   // Convert from nanoseconds to milliseconds for QDateTime
      //   QDateTime pingTime = QDateTime::fromMSecsSinceEpoch(m_deviceValues.lastAthenaPingTime / 1000000);
      //   pingTimeStr = pingTime.toString("yyyy-MM-dd hh:mm:ss");
      // }
      // if (idx < group.size())
      //   group[idx++].valueLabel->setText(pingTimeStr);
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] updateDeviceLabels: Error updating Device Status section:" << e.what() << std::endl;
  }

  // Update Power section
  try {
    if (m_deviceGroups.contains("Power")) {
      int idx = 0;
      auto &group = m_deviceGroups["Power"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 W").arg(m_cache->deviceValues.powerDrawW, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 W").arg(m_cache->deviceValues.somPowerDrawW, 0, 'f', 2));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 µWh").arg(m_cache->deviceValues.offroadPowerUsageUwh));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1 µWh").arg(m_cache->deviceValues.carBatteryCapacityUwh));
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] updateDeviceLabels: Error updating Power section:" << e.what() << std::endl;
  }

  // Update Network section
  try {
    if (m_deviceGroups.contains("Network")) {
      int idx = 0;
      auto &group = m_deviceGroups["Network"];
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatNetworkType(m_cache->deviceValues.networkType));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatNetworkStrength(m_cache->deviceValues.networkStrength));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->deviceValues.networkMetered));

      // If we have network, show the details
      if (m_cache->deviceValues.networkType != 0) { // 0 = None
        if (idx < group.size())
          group[idx++].valueLabel->setText(m_cache->deviceValues.networkInfo.technology);
        if (idx < group.size())
          group[idx++].valueLabel->setText(m_cache->deviceValues.networkInfo.operator_);
        if (idx < group.size())
          group[idx++].valueLabel->setText(m_cache->deviceValues.networkInfo.band);
        if (idx < group.size())
          group[idx++].valueLabel->setText(QString("%1").arg(m_cache->deviceValues.networkInfo.channel));
        if (idx < group.size())
          group[idx++].valueLabel->setText(m_cache->deviceValues.networkInfo.state);
      } else {
        // Set N/A for all network details when there's no network
        for (int i = 0; i < 6; i++) {
          if (idx < group.size())
            group[idx++].valueLabel->setText("N/A");
        }
      }

      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBytes(m_cache->deviceValues.networkStats.wwanTx));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBytes(m_cache->deviceValues.networkStats.wwanRx));
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] updateDeviceLabels: Error updating Network section:" << e.what() << std::endl;
  }

  // Update System & Temperatures section
  try {
    if (m_deviceGroups.contains("System & Temperatures")) {
      int idx = 0;
      auto &group = m_deviceGroups["System & Temperatures"];

      // System usage information
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1%").arg(m_cache->deviceValues.freeSpacePercent, 0, 'f', 1));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1%").arg(m_cache->deviceValues.memoryUsagePercent));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1%").arg(m_cache->deviceValues.gpuUsagePercent));

      // Create CPU usage string showing all cores with line breaks every 4 cores
      QString cpuUsageStr = "";
      for (int i = 0; i < m_cache->deviceValues.cpuUsagePercent.size(); i++) {
        cpuUsageStr += QString("C%1:%2% ").arg(i).arg(m_cache->deviceValues.cpuUsagePercent[i]);
        // Add a line break after every 4 cores (except at the end)
        if ((i + 1) % 4 == 0 && i < m_cache->deviceValues.cpuUsagePercent.size() - 1) {
          cpuUsageStr += "<br>";
        }
      }
      if (idx < group.size()) {
        QLabel *cpuUsageLabel = group[idx++].valueLabel;
        cpuUsageLabel->setText(cpuUsageStr);
        cpuUsageLabel->setTextFormat(Qt::RichText); // Enable rich text formatting to support HTML tags
      }

      // CPU temps - add line breaks for better display
      QString cpuTempStr = "";
      for (int i = 0; i < m_cache->deviceValues.cpuTempC.size(); i++) {
        if (i > 0 && i % 4 == 0) { // Add a line break after every 4 values
          cpuTempStr += "<br>";
        } else if (i > 0) {
          cpuTempStr += " ";
        }
        cpuTempStr += QString("%1°C").arg(m_cache->deviceValues.cpuTempC[i], 0, 'f', 1);
      }

      if (idx < group.size()) {
        QLabel *cpuTempLabel = group[idx++].valueLabel;
        cpuTempLabel->setText(cpuTempStr.isEmpty() ? "N/A" : cpuTempStr);
        cpuTempLabel->setTextFormat(Qt::RichText); // Enable rich text formatting
      }

      // GPU temps
      QString gpuTempStr = "";
      for (int i = 0; i < m_cache->deviceValues.gpuTempC.size(); i++) {
        if (i > 0)
          gpuTempStr += " ";
        gpuTempStr += QString("%1°C").arg(m_cache->deviceValues.gpuTempC[i], 0, 'f', 1);
      }
      if (idx < group.size())
        group[idx++].valueLabel->setText(gpuTempStr.isEmpty() ? "N/A" : gpuTempStr);

      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1°C").arg(m_cache->deviceValues.memoryTempC, 0, 'f', 1));

      // NVME temps
      QString nvmeTempStr = "";
      for (int i = 0; i < m_cache->deviceValues.nvmeTempC.size(); i++) {
        if (i > 0)
          nvmeTempStr += " ";
        nvmeTempStr += QString("%1°C").arg(m_cache->deviceValues.nvmeTempC[i], 0, 'f', 1);
      }
      if (idx < group.size())
        group[idx++].valueLabel->setText(nvmeTempStr.isEmpty() ? "N/A" : nvmeTempStr);

      // Modem temps
      QString modemTempStr = "";
      for (int i = 0; i < m_cache->deviceValues.modemTempC.size(); i++) {
        if (i > 0)
          modemTempStr += " ";
        modemTempStr += QString("%1°C").arg(m_cache->deviceValues.modemTempC[i], 0, 'f', 1);
      }
      if (idx < group.size())
        group[idx++].valueLabel->setText(modemTempStr.isEmpty() ? "N/A" : modemTempStr);

      // PMIC temps
      QString pmicTempStr = "";
      for (int i = 0; i < m_cache->deviceValues.pmicTempC.size(); i++) {
        if (i > 0)
          pmicTempStr += " ";
        pmicTempStr += QString("%1°C").arg(m_cache->deviceValues.pmicTempC[i], 0, 'f', 1);
      }
      if (idx < group.size())
        group[idx++].valueLabel->setText(pmicTempStr.isEmpty() ? "N/A" : pmicTempStr);

      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1°C").arg(m_cache->deviceValues.intakeTempC, 0, 'f', 1));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1°C").arg(m_cache->deviceValues.exhaustTempC, 0, 'f', 1));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1°C").arg(m_cache->deviceValues.caseTempC, 0, 'f', 1));
      if (idx < group.size())
        group[idx++].valueLabel->setText(QString("%1°C").arg(m_cache->deviceValues.maxTempC, 0, 'f', 1));
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] updateDeviceLabels: Error updating System & Temperatures section:" << e.what() << std::endl;
  }
}


// Helper formatting functions with implementations
QString OtherDebugPanel::formatBool(bool value, const QString &trueText, const QString &falseText) { return value ? trueText : falseText; }

QString OtherDebugPanel::formatSafetyModel(int model) {
  QString modelText;
  switch (model) {
  case 0:
    modelText = "Silent";
    break;
  case 1:
    modelText = "Honda Nidec";
    break;
  case 2:
    modelText = "Toyota";
    break;
  case 3:
    modelText = "ELM327";
    break;
  case 4:
    modelText = "GM";
    break;
  case 5:
    modelText = "Honda Bosch Giraffe";
    break;
  case 6:
    modelText = "Ford";
    break;
  case 7:
    modelText = "Cadillac";
    break;
  case 8:
    modelText = "Hyundai";
    break;
  case 9:
    modelText = "Chrysler";
    break;
  case 10:
    modelText = "Tesla";
    break;
  case 11:
    modelText = "Subaru";
    break;
  case 12:
    modelText = "GM Passive";
    break;
  case 13:
    modelText = "Mazda";
    break;
  case 14:
    modelText = "Nissan";
    break;
  case 15:
    modelText = "Volkswagen";
    break;
  case 16:
    modelText = "Toyota IPAS";
    break;
  case 17:
    modelText = "All Output";
    break;
  case 18:
    modelText = "GM ASCM";
    break;
  case 19:
    modelText = "No Output";
    break;
  case 20:
    modelText = "Honda Bosch";
    break;
  case 21:
    modelText = "Volkswagen PQ";
    break;
  case 22:
    modelText = "Subaru Pre-Global";
    break;
  case 23:
    modelText = "Hyundai Legacy";
    break;
  case 24:
    modelText = "Hyundai Community";
    break;
  case 25:
    modelText = "Volkswagen MLB";
    break;
  case 26:
    modelText = "Hongqi";
    break;
  case 27:
    modelText = "Body";
    break;
  case 28:
    modelText = "Hyundai CanFD";
    break;
  case 29:
    modelText = "Volkswagen MQB Evo";
    break;
  case 30:
    modelText = "Chrysler CUSW";
    break;
  case 31:
    modelText = "PSA";
    break;
  case 32:
    modelText = "FCA Giorgio";
    break;
  case 33:
    modelText = "Rivian";
    break;
  case 34:
    modelText = "Volkswagen MEB";
    break;
  default:
    modelText = "Unknown";
    break;
  }

  // Highlight Ford safety model
  if (model == 6) { // Ford
    modelText = "<span style='color: #00FF00; font-weight: bold;'>" + modelText + "</span>";
  }

  return modelText;
}

QString OtherDebugPanel::formatEcu(int ecu) {
  switch (ecu) {
  case 0:
    return "EPS";
  case 1:
    return "ABS";
  case 2:
    return "FWD Radar";
  case 3:
    return "FWD Camera";
  case 4:
    return "Engine";
  case 5:
    return "Unknown";
  case 6:
    return "DSU";
  case 7:
    return "Parking ADAS";
  case 8:
    return "Transmission";
  case 9:
    return "SRS";
  case 10:
    return "Gateway";
  case 11:
    return "HUD";
  case 12:
    return "Combination Meter";
  case 13:
    return "VSA";
  case 14:
    return "PFI";
  case 15:
    return "Electric Brake Booster";
  case 16:
    return "Shift By Wire";
  case 17:
    return "Debug";
  case 18:
    return "Hybrid";
  case 19:
    return "ADAS";
  case 20:
    return "HVAC";
  case 21:
    return "Corner Radar";
  case 22:
    return "EPB";
  case 23:
    return "Telematics";
  case 24:
    return "Body";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatTransmissionType(int type) {
  switch (type) {
  case 0:
    return "Unknown";
  case 1:
    return "Automatic";
  case 2:
    return "Manual";
  case 3:
    return "Direct";
  case 4:
    return "CVT";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatFingerprintSource(int source) {
  switch (source) {
  case 0:
    return "CAN";
  case 1:
    return "FW";
  case 2:
    return "Fixed";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatNetworkLocation(int location) {
  switch (location) {
  case 0:
    return "FWD Camera";
  case 1:
    return "Gateway";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatLongControlState(int state) {
  switch (state) {
  case 0:
    return "Off";
  case 1:
    return "PID";
  case 2:
    return "Stopping";
  case 3:
    return "Starting";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatNetworkType(int type) {
  switch (type) {
  case 0:
    return "None";
  case 1:
    return "WiFi";
  case 2:
    return "Cell 2G";
  case 3:
    return "Cell 3G";
  case 4:
    return "Cell 4G";
  case 5:
    return "Cell 5G";
  case 6:
    return "Ethernet";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatNetworkStrength(int strength) {
  switch (strength) {
  case 0:
    return "Unknown";
  case 1:
    return "Poor";
  case 2:
    return "Moderate";
  case 3:
    return "Good";
  case 4:
    return "Great";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatDeviceType(int type) {
  switch (type) {
  case 0:
    return "Unknown";
  case 1:
    return "Neo";
  case 2:
    return "Chffr Android";
  case 3:
    return "Chffr iOS";
  case 4:
    return "Tici";
  case 5:
    return "PC";
  case 6:
    return "Tizi";
  case 7:
    return "Mici";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatThermalStatus(int status) {
  switch (status) {
  case 0:
    return "Green";
  case 1:
    return "Yellow";
  case 2:
    return "Red";
  case 3:
    return "Danger";
  default:
    return "Unknown";
  }
}

QString OtherDebugPanel::formatBytes(qint64 bytes) {
  const QStringList suffixes = {"B", "KB", "MB", "GB", "TB"};

  float num = bytes;
  int level = 0;

  while (num >= 1024.0 && level < suffixes.size() - 1) {
    num /= 1024.0;
    level++;
  }

  return QString("%1 %2").arg(num, 0, 'f', (level > 0) ? 2 : 0).arg(suffixes[level]);
}

void OtherDebugPanel::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Draw background with material design styling
  QPainterPath path;
  path.addRoundedRect(rect().adjusted(5, 5, -5, -5), 15, 15);

  // Material design background with slight gradient
  QLinearGradient gradient(0, 0, 0, height());
  gradient.setColorAt(0, QColor(30, 30, 30, 230));
  gradient.setColorAt(1, QColor(20, 20, 20, 230));

  p.fillPath(path, gradient);

  // Material design subtle border
  p.setPen(QPen(QColor(60, 60, 60, 150), 1));
  p.drawPath(path);

  QWidget::paintEvent(event);
}

void OtherDebugPanel::updateFirmwareLabels() {
  try {
    if (m_firmwareGroups.contains("Car Info")) {
      int idx = 0;
      auto &group = m_firmwareGroups["Car Info"];

      // Update car information
      if (idx < group.size())
        group[idx++].valueLabel->setText(m_cache->paramValues.carFingerprint.isEmpty() ? "Not Available" : m_cache->paramValues.carFingerprint);
      if (idx < group.size())
        group[idx++].valueLabel->setText(m_cache->paramValues.carVin.isEmpty() ? "Not Available" : m_cache->paramValues.carVin);
      if (idx < group.size())
        group[idx++].valueLabel->setText(m_cache->paramValues.brand.isEmpty() ? "Unknown" : m_cache->paramValues.brand);
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatTransmissionType(m_cache->paramValues.transmissionType));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatBool(m_cache->paramValues.fuzzyFingerprint));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatFingerprintSource(m_cache->paramValues.fingerprintSource));
      if (idx < group.size())
        group[idx++].valueLabel->setText(formatNetworkLocation(m_cache->paramValues.networkLocation));
    }
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.other.debug.panel] updateFirmwareLabels: Error updating Car Info group:" << e.what() << std::endl;
  }
}

void OtherDebugPanel::populateCANMessageTable() {
  if (!m_cache || !m_canMessageTable) {
    return;
  }

  // Store current selection to restore later
  int selectedId = -1;
  int currentRow = m_canMessageTable->currentRow();
  if (currentRow >= 0) {
    QTableWidgetItem *item = m_canMessageTable->item(currentRow, 1);
    if (item) selectedId = item->data(Qt::UserRole).toInt();
  }

  // Update message count
  if (m_canUpdateRateLabel) {
    m_canUpdateRateLabel->setText(QString("Messages: %1").arg(m_cache->canMessages.size()));
  }

  if (!m_cache->canDataAvailable || m_cache->canMessages.isEmpty()) {
    if (m_canMessageTable->rowCount() > 0) {
      m_canMessageTable->setRowCount(0);
    }
    return;
  }

  // Build list of messages to display (use discovery order if available)
  QList<OtherDataCache::CANMessage> messages;
  if (!m_cache->discoveryOrder.isEmpty()) {
    // Use discovery order (Cabana-style - maintains order messages were first seen)
    for (int id : m_cache->discoveryOrder) {
      if (m_cache->canMessages.contains(id)) {
        messages.append(m_cache->canMessages[id]);
      }
    }
  } else {
    // Fallback to sorted by ID
    QList<int> messageIds = m_cache->canMessages.keys();
    std::sort(messageIds.begin(), messageIds.end());
    for (int id : messageIds) {
      messages.append(m_cache->canMessages[id]);
    }
  }

  // Cabana-style: Only update existing rows, add new ones as needed
  int row = 0;
  for (const auto &message : messages) {
    // Add new row if needed
    if (row >= m_canMessageTable->rowCount()) {
      m_canMessageTable->setRowCount(row + 1);
    }

    // Column 0: Bus - only update if changed
    QTableWidgetItem *busItem = m_canMessageTable->item(row, 0);
    if (!busItem) {
      busItem = new QTableWidgetItem();
      busItem->setFont(QFont("Consolas", 22));
      busItem->setTextAlignment(Qt::AlignCenter);
      m_canMessageTable->setItem(row, 0, busItem);
    }
    QString busText = QString("0x%1").arg(message.id, 3, 16, QChar('0')).toUpper();
    if (busItem->text() != busText) {
      busItem->setText(busText);
      busItem->setData(Qt::UserRole, message.id);
    }

    // Column 1: Frequency
    QTableWidgetItem *freqItem = m_canMessageTable->item(row, 1);
    if (!freqItem) {
      freqItem = new QTableWidgetItem();
      freqItem->setFont(QFont("Consolas", 22));
      freqItem->setTextAlignment(Qt::AlignCenter);
      m_canMessageTable->setItem(row, 1, freqItem);
    }
    QString freqText = message.frequency > 0 ? QString("%1").arg((int)message.frequency) : "-";
    if (freqItem->text() != freqText) {
      freqItem->setText(freqText);
    }

    // Column 2: Name - only update if changed
    QTableWidgetItem *nameItem = m_canMessageTable->item(row, 2);
    if (!nameItem) {
      nameItem = new QTableWidgetItem();
      nameItem->setFont(QFont("Consolas", 22));
      m_canMessageTable->setItem(row, 2, nameItem);
    }
    QString nameText = message.name.isEmpty() ? "-" : message.name;
    if (nameItem->text() != nameText) {
      nameItem->setText(nameText);
    }

    // Color coding based on update status
    QColor textColor;
    if (message.hasNewData) {
      // Bright green for just updated
      textColor = QColor(50, 255, 50);
    } else {
      // Normal cyan for existing
      textColor = QColor(24, 180, 255);
    }

    // Apply color to show activity - Bus (CAN ID) column shows activity
    busItem->setForeground(textColor);

    // Other columns stay neutral
    freqItem->setForeground(QColor(150, 150, 150));
    nameItem->setForeground(message.name.isEmpty() ? QColor(100, 100, 100) : QColor(220, 220, 220));

    row++;
  }

  // Remove extra rows if list shrunk
  if (row < m_canMessageTable->rowCount()) {
    m_canMessageTable->setRowCount(row);
  }

  // Restore selection by ID
  if (selectedId >= 0) {
    for (int i = 0; i < m_canMessageTable->rowCount(); i++) {
      QTableWidgetItem *item = m_canMessageTable->item(i, 1);
      if (item && item->data(Qt::UserRole).toInt() == selectedId) {
        m_canMessageTable->setCurrentCell(i, 0);
        break;
      }
    }
  }
}

void OtherDebugPanel::updateCANSignals() {
  if (!m_cache || !m_canSignalTable || m_selectedCANMessage == -1) {
    return;
  }

  // Check if we have data for the selected message
  if (!m_cache->canMessages.contains(m_selectedCANMessage)) {
    m_canSignalTable->setRowCount(0);
    return;
  }

  const auto &message = m_cache->canMessages[m_selectedCANMessage];

  // Update signal label with message info
  QLabel *signalLabel = m_canTab->findChild<QLabel*>("signalLabel");
  if (signalLabel) {
    QString labelText = QString("Signals - 0x%1").arg(message.id, 3, 16, QChar('0')).toUpper();
    if (!message.name.isEmpty()) {
      labelText += QString(" (%1)").arg(message.name);
    }
    signalLabel->setText(labelText);
  }

  // Create a sorted copy of the signal list
  QList<OtherDataCache::CANSignal> sortedSignals = message.signalList;
  std::sort(sortedSignals.begin(), sortedSignals.end(), [](const OtherDataCache::CANSignal &a, const OtherDataCache::CANSignal &b) {
    return a.name < b.name;
  });

  // Cabana-style: Only update rows that changed
  int row = 0;
  for (const auto &signal : sortedSignals) {
    // Add new row if needed
    if (row >= m_canSignalTable->rowCount()) {
      m_canSignalTable->setRowCount(row + 1);
    }

    // Column 0: Signal name
    QTableWidgetItem *nameItem = m_canSignalTable->item(row, 0);
    if (!nameItem) {
      nameItem = new QTableWidgetItem();
      nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
      nameItem->setFont(QFont("Consolas", 16));
      m_canSignalTable->setItem(row, 0, nameItem);
    }
    if (nameItem->text() != signal.name) {
      nameItem->setText(signal.name);
      // Color based on signal type
      if (signal.name.contains("Unknown")) {
        nameItem->setForeground(QColor(100, 100, 100));
      } else {
        nameItem->setForeground(QColor(220, 220, 220));
      }
    }

    // Column 1: Value with change detection
    QTableWidgetItem *valueItem = m_canSignalTable->item(row, 1);
    if (!valueItem) {
      valueItem = new QTableWidgetItem();
      valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      valueItem->setFont(QFont("Consolas", 16, QFont::Bold));
      m_canSignalTable->setItem(row, 1, valueItem);
    }

    // Format value appropriately
    QString valueText;
    if (signal.value == std::floor(signal.value) && std::abs(signal.value) < 1000000) {
      valueText = QString::number(static_cast<qint64>(signal.value));
    } else {
      valueText = QString::number(signal.value, 'f', 2);
    }

    if (valueItem->text() != valueText) {
      valueItem->setText(valueText);
    }

    // Highlight if value changed
    if (signal.hasChanged) {
      valueItem->setForeground(QColor(50, 255, 50)); // Bright green
      valueItem->setBackground(QColor(50, 255, 50, 20)); // Light green background
    } else {
      valueItem->setForeground(QColor(24, 180, 255)); // Cyan
      valueItem->setBackground(QColor(0, 0, 0, 0)); // Clear background
    }

    // Column 2: Unit
    QTableWidgetItem *unitItem = m_canSignalTable->item(row, 2);
    if (!unitItem) {
      unitItem = new QTableWidgetItem();
      unitItem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
      unitItem->setFont(QFont("Consolas", 16));
      m_canSignalTable->setItem(row, 2, unitItem);
    }
    QString unitText = signal.unit.isEmpty() ? "-" : signal.unit;
    if (unitItem->text() != unitText) {
      unitItem->setText(unitText);
      unitItem->setForeground(QColor(150, 150, 150));
    }

    // Column 3: Range
    QTableWidgetItem *rangeItem = m_canSignalTable->item(row, 3);
    if (!rangeItem) {
      rangeItem = new QTableWidgetItem();
      rangeItem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
      rangeItem->setFont(QFont("Consolas", 14));
      m_canSignalTable->setItem(row, 3, rangeItem);
    }
    QString rangeText = QString("[%1, %2]").arg(signal.min, 0, 'f', 1).arg(signal.max, 0, 'f', 1);
    if (rangeItem->text() != rangeText) {
      rangeItem->setText(rangeText);
      rangeItem->setForeground(QColor(100, 100, 100));
    }

    row++;
  }

  // Remove extra rows if list shrunk
  if (row < m_canSignalTable->rowCount()) {
    m_canSignalTable->setRowCount(row);
  }

  // Resize columns to content
  m_canSignalTable->resizeColumnsToContents();

  // Make sure the last column stretches
  m_canSignalTable->horizontalHeader()->setStretchLastSection(true);

  // Update the signal label to show count
  if (m_canSignalTable->rowCount() > 0) {
    QString signalCountText = QString("Signal Details (%1 signals)").arg(m_canSignalTable->rowCount());
    // Find and update the signal label
    for (int i = 0; i < m_canTab->layout()->count(); ++i) {
      QWidget *widget = m_canTab->layout()->itemAt(i)->widget();
      if (widget && widget->objectName() == "signalLabel") {
        QLabel *label = qobject_cast<QLabel*>(widget);
        if (label) {
          label->setText(signalCountText);
        }
        break;
      }
    }
  }
}

