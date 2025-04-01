#pragma once

#include <QWidget>
#include <QPainter>
#include <QScrollArea>
#include <QGridLayout>
#include <QPainterPath>
#include <QLabel>
#include <QFrame>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QDateTime>
#include <QVBoxLayout>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui.h"
#define UIState UIStateSP
#else
#include "selfdrive/ui/ui.h"
#endif

class OtherDebugPanel : public QWidget {
  Q_OBJECT

public:
  OtherDebugPanel(QWidget *parent = nullptr);
  void updateState(const UIState &s);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  uint64_t m_lastFirmwareUpdateTime = 0;
  static constexpr uint64_t FIRMWARE_UPDATE_INTERVAL_MS = 30000; // 30 seconds
  enum class LateralTuningType { PID, TORQUE };

  void setupMaterialStyle();
  void setupLabelStyles();
  void setupTableStyle();

  // Functions to setup UI components
  void setupTabs();
  void setupMainTab();
  void setupRadarTab();
  void setupTuningTab();
  void setupFirmwareTab();
  void setupDeviceTab();
  QFrame *createLabelFrame(QGridLayout *layout, QString title);
  void updateLabels();

  QString nameStyle;
  QString valueStyle;

  // Helper functions for formatting
  QString formatBool(bool value, const QString &trueText = "Yes", const QString &falseText = "No");
  QString formatSafetyModel(int model);
  QString formatEcu(int ecu);
  QString formatTransmissionType(int type);
  QString formatFingerprintSource(int source);
  QString formatNetworkLocation(int location);
  QString formatLongControlState(int state);
  QString formatNetworkType(int type);
  QString formatNetworkStrength(int strength);
  QString formatDeviceType(int type);
  QString formatThermalStatus(int status);
  QString formatBytes(qint64 bytes);

  // Data structures to hold vehicle information
  struct CarStateValues {
    // Basic vehicle dynamics
    float vEgo = 0.0f;       // Vehicle speed in m/s
    float aEgo = 0.0f;       // Vehicle acceleration in m/s²
    float vEgoRaw = 0.0f;    // Raw speed from wheel sensors
    float yawRate = 0.0f;    // Yaw rate
    bool standstill = false; // Vehicle standstill
    float engineRpm = 0.0f;  // Engine RPM

    // Steering
    float steeringAngleDeg = 0.0f;    // Steering angle in degrees
    float steeringRateDeg = 0.0f;     // Steering rate in degrees/s
    float steeringTorque = 0.0f;      // Steering torque
    float steeringTorqueEps = 0.0f;   // EPS torque
    bool steeringPressed = false;     // Is steering wheel being used
    bool steerFaultTemporary = false; // Temporary steering fault
    bool steerFaultPermanent = false; // Permanent steering fault

    // Pedals
    float brake = 0.0f;         // Brake pedal
    float gas = 0.0f;           // Gas pedal
    bool gasPressed = false;    // Is gas pressed
    bool brakePressed = false;  // Is brake pressed
    bool regenBraking = false;  // Regenerative braking active
    bool clutchPressed = false; // Clutch pressed (manual transmission)

    // Vehicle systems
    bool parkingBrake = false;    // Parking brake
    bool brakeHoldActive = false; // Brake hold feature
    bool espDisabled = false;     // ESP disabled
    bool espActive = false;       // ESP actively intervening

    // Cruise control
    bool cruiseEnabled = false;     // Cruise control enabled
    float cruiseSpeed = 0.0f;       // Cruise control set speed
    bool cruiseAvailable = false;   // Cruise control available
    bool cruiseStandstill = false;  // Cruise at standstill
    bool cruiseNonAdaptive = false; // Non-adaptive cruise
    float cruiseSpeedLimit = 0.0f;  // Cruise speed limit

    // Indicators and signals
    bool leftBlinker = false;  // Left blinker active
    bool rightBlinker = false; // Right blinker active
    int gearShifter = 0;       // Gear shifter position

    // Safety
    bool stockAeb = false;              // Stock AEB active
    bool stockFcw = false;              // Stock FCW active
    bool invalidLkasSetting = false;    // Invalid LKAS setting
    bool doorOpen = false;              // Door open
    bool seatbeltUnlatched = false;     // Seatbelt not latched
    bool vehicleSensorsInvalid = false; // Vehicle sensors invalid

    // Battery/fuel
    float fuelGauge = 0.0f; // Fuel gauge level (0-1)
    bool charging = false;  // EV charging
  };

  struct RadarValues {
    struct RadarError {
      bool canError = false;
      bool radarFault = false;
      bool wrongConfig = false;
      bool radarUnavailableTemporary = false;
    } errors;

    struct LeadData {
      float dRel = 0.0f;      // Distance to lead car
      float yRel = 0.0f;      // Lateral position of lead car
      float vRel = 0.0f;      // Relative velocity of lead car
      float aRel = 0.0f;      // Relative acceleration of lead car
      float vLead = 0.0f;     // Absolute velocity of lead car
      float dPath = 0.0f;     // Path distance to lead car
      float vLat = 0.0f;      // Lateral velocity
      float vLeadK = 0.0f;    // Velocity of lead car (kalman filter)
      float aLeadK = 0.0f;    // Acceleration of lead car (kalman filter)
      bool fcw = false;       // Forward collision warning
      bool status = false;    // Lead is valid
      float aLeadTau = 0.0f;  // Lead acceleration time constant
      float modelProb = 0.0f; // Model probability
      bool radar = false;     // Detected by radar
      int radarTrackId = -1;  // Radar track ID
    };

    LeadData leadOne;
    LeadData leadTwo;
  };

  struct OutputValues {
    float accel = 0.0f;            // Acceleration command
    float gas = 0.0f;              // Gas command
    float brake = 0.0f;            // Brake command
    float speed = 0.0f;            // Speed command
    float torque = 0.0f;           // Steering torque command
    float steeringAngleDeg = 0.0f; // Steering angle command
    float curvature = 0.0f;        // Curvature command
    float torqueOutputCan = 0.0f;  // Torque sent to CAN
    int longControlState = 0;      // Longitudinal control state
  };

  struct CarParameterValues {
    // Car parameters
    float mass = 0.0f;                      // Vehicle mass
    float wheelbase = 0.0f;                 // Wheelbase
    float steerRatio = 0.0f;                // Steering ratio
    float steerActuatorDelay = 0.0f;        // Steering actuator delay
    float longitudinalActuatorDelay = 0.0f; // Longitudinal actuator delay
    float vEgoStopping = 0.0f;              // Speed for stopping state
    float vEgoStarting = 0.0f;              // Speed for starting state
    float tireStiffnessFactor = 0.0f;       // Tire stiffness factor
    bool radarUnavailable = false;          // Is radar unavailable
    float steerLimitTimer = 0.0f;           // Steer limit timer
    float steerRateCost = 0.0f;             // Steer rate cost

    // Tuning
    LateralTuningType lateralTuningType = LateralTuningType::TORQUE;

    // PID tuning
    float pidKp = 0.0f;
    float pidKi = 0.0f;
    float pidKf = 0.0f;

    // Torque tuning
    bool torqueUseSteeringAngle = false;
    float torqueKp = 0.0f;
    float torqueKi = 0.0f;
    float torqueKf = 0.0f;
    float torqueFriction = 0.0f;
    float torqueLatAccelFactor = 0.0f;
    float torqueLatAccelOffset = 0.0f;

    // Longitudinal tuning
    QList<float> longKpBP;
    QList<float> longKpV;
    QList<float> longKiBP;
    QList<float> longKiV;
    float longKf = 0.0f;

    // Safety
    int safetyModel = 0;
    uint16_t safetyParam = 0;
    int16_t alternativeExperience = 0;

    // Car identification
    QString carFingerprint;
    QString carVin;
    QString brand;
    bool fuzzyFingerprint = false;
    int fingerprintSource = 0;
    int networkLocation = 0;
    int transmissionType = 0;

    // Firmware
    struct CarFirmware {
      int ecu = 0;
      QString fwVersion;
      uint32_t address = 0;
      uint8_t subAddress = 0;
      uint8_t bus = 0;
    };

    QList<CarFirmware> carFw;
  };

  struct DeviceStateValues {
    // General
    int deviceType = 0;
    bool started = false;
    uint64_t startedMonoTime = 0;

    // System utilization
    float freeSpacePercent = 0.0f;
    int8_t memoryUsagePercent = 0;
    int8_t gpuUsagePercent = 0;
    QList<int8_t> cpuUsagePercent;

    // Power
    uint32_t offroadPowerUsageUwh = 0;
    uint32_t carBatteryCapacityUwh = 0;
    float powerDrawW = 0.0f;
    float somPowerDrawW = 0.0f;

    // Temperatures
    QList<float> cpuTempC;
    QList<float> gpuTempC;
    float memoryTempC = 0.0f;
    QList<float> nvmeTempC;
    QList<float> modemTempC;
    QList<float> pmicTempC;
    float intakeTempC = 0.0f;
    float exhaustTempC = 0.0f;
    float caseTempC = 0.0f;
    float maxTempC = 0.0f;

    // Device state
    int thermalStatus = 0;
    uint16_t fanSpeedPercentDesired = 0;
    int8_t screenBrightnessPercent = 0;

    // Network
    int networkType = 0;
    int networkStrength = 0;
    bool networkMetered = false;
    uint64_t lastAthenaPingTime = 0;

    struct NetworkInfoStruct {
      QString technology;
      QString operator_;
      QString band;
      uint16_t channel = 0;
      QString state;
    } networkInfo;

    struct NetworkStatsStruct {
      int64_t wwanTx = 0;
      int64_t wwanRx = 0;
    } networkStats;
  };

  // Tab widgets
  QTabWidget *m_tabWidget;
  QWidget *m_mainTab;
  QWidget *m_radarTab;
  QWidget *m_tuningTab;
  QWidget *m_firmwareTab;
  QWidget *m_deviceTab;

  // Scroll areas
  QScrollArea *m_mainScrollArea;
  QScrollArea *m_radarScrollArea;
  QScrollArea *m_tuningScrollArea;
  QScrollArea *m_firmwareScrollArea;
  QScrollArea *m_deviceScrollArea;

  // Scroll content widgets
  QWidget *m_mainScrollContent;
  QWidget *m_radarScrollContent;
  QWidget *m_tuningScrollContent;
  QWidget *m_firmwareScrollContent;
  QWidget *m_deviceScrollContent;

  // Layouts
  QGridLayout *m_mainLayout;
  QGridLayout *m_radarLayout;
  QGridLayout *m_tuningLayout;
  QGridLayout *m_deviceLayout;
  QVBoxLayout *m_firmwareLayout;

  // Special widgets for firmware tab
  QLabel *m_vinLabel;
  QTableWidget *m_firmwareTable;

  // Stored values for each tab
  CarStateValues m_carValues;
  RadarValues m_radarValues;
  OutputValues m_outputValues;
  CarParameterValues m_paramValues;
  DeviceStateValues m_deviceValues;

  // Organized by groups
  struct LabelPair {
    QLabel *nameLabel;
    QLabel *valueLabel;
  };

  // Label groups for each tab
  QMap<QString, QList<LabelPair>> m_groups;         // Main tab
  QMap<QString, QList<LabelPair>> m_radarGroups;    // Radar tab
  QMap<QString, QList<LabelPair>> m_tuningGroups;   // Tuning tab
  QMap<QString, QList<LabelPair>> m_firmwareGroups; // Firmware tab
  QMap<QString, QList<LabelPair>> m_deviceGroups;   // Device tab
};