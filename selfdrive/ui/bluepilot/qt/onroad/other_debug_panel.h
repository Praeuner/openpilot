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
#include <QListWidget>
#include <QSplitter>
#include <QHeaderView>
#include <QDateTime>
#include <QVBoxLayout>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <QPushButton>
#include <QCheckBox>
#include <atomic>
#include <map>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui.h"
#define UIState UIStateSP
#else
#include "selfdrive/ui/ui.h"
#endif

namespace UpdateRates {
static constexpr uint64_t MIN_UPDATE_INTERVAL_MS = 33;     // 30 Hz max update rate
static constexpr uint64_t DYNAMICS_UPDATE_RATE_MS = 50;    // 20 Hz
static constexpr uint64_t STEERING_UPDATE_RATE_MS = 50;    // 20 Hz
static constexpr uint64_t SYSTEMS_UPDATE_RATE_MS = 200;    // 5 Hz
static constexpr uint64_t DEVICE_UPDATE_RATE_MS = 500;     // 2 Hz
static constexpr uint64_t FIRMWARE_UPDATE_RATE_MS = 30000; // Every 30 seconds
} // namespace UpdateRates

// Forward declarations
struct OtherDataCache;

// Worker class to process data off the UI thread
class OtherDataWorker : public QObject {
  Q_OBJECT
public:
  OtherDataWorker(QObject *parent = nullptr);
  ~OtherDataWorker();

public slots:
  void processData(const UIState *s);

signals:
  void dataReady(const OtherDataCache &cache);

private:
  QMutex m_mutex;
  QWaitCondition m_condition;
  std::atomic<bool> m_abort;
  OtherDataCache *m_lastCache;

  // CAN subscription management
  std::unique_ptr<SubMaster> m_canSubMaster;
  std::atomic<bool> m_canSubscriptionActive;
  std::atomic<bool> m_canUpdatesPaused;
  QTimer *m_canUpdateTimer;
  uint64_t m_lastCANUpdate;

public:
  void setCANSubscriptionActive(bool active);
  void setCANUpdatesPaused(bool paused);

private:

  // Helper methods to process different data sections
  void processCarState(const UIState *s, OtherDataCache *cache);
  void processRadarState(const UIState *s, OtherDataCache *cache);
  void processCarOutput(const UIState *s, OtherDataCache *cache);
  void processCarParams(const UIState *s, OtherDataCache *cache);
  void processDeviceState(const UIState *s, OtherDataCache *cache);
  void processCANData(const UIState *s, OtherDataCache *cache);
};

class OtherDebugPanel : public QWidget {
  Q_OBJECT

public:
  OtherDebugPanel(QWidget *parent = nullptr);
  ~OtherDebugPanel();
  void updateState(const UIState &s);
  void setCANTabActive(bool active);
  void setCANUpdatesPaused(bool paused);

protected:
  void paintEvent(QPaintEvent *event) override;

signals:
  void processStateUpdate(const UIState *s);

private slots:
  void updateFromWorker(const OtherDataCache &cache);
  void updateUI();
  void updateVisibleTab();

private:
  // Setup functions
  void setupMaterialStyle();
  void setupLabelStyles();
  void setupTableStyle();
  void setupTabs();
  void setupMainTab();
  void setupRadarTab();
  void setupTuningTab();
  void setupFirmwareTab();
  void setupDeviceTab();
  void setupCANTab();
  QFrame *createLabelFrame(QGridLayout *layout, QString title);

  // UI update methods by section
  void updateMainLabels();
  void updateRadarLabels();
  void updateTuningLabels();
  void updateFirmwareLabels();
  void updateFirmwareTable();
  void updateDeviceLabels();
  void updateCANSignals();
  void populateCANMessageTable();

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

  // Update timestamp tracking
  struct UpdateTimestamps {
    uint64_t dynamicsLastUpdate = 0;
    uint64_t steeringLastUpdate = 0;
    uint64_t systemsLastUpdate = 0;
    uint64_t deviceLastUpdate = 0;
    uint64_t firmwareLastUpdate = 0;
    uint64_t lastPanelUpdate = 0;
  } m_lastUpdates;

  // Style properties
  QString nameStyle;
  QString valueStyle;

  // Thread management
  QThread m_workerThread;
  OtherDataWorker *m_worker;
  std::atomic<bool> m_dataProcessing;
  QTimer m_updateTimer;
  QTimer m_tabChangeTimer;

  // Cached drawing components
  QLinearGradient m_backgroundGradient;
  bool m_gradientInitialized = false;

  // UI components
  QTabWidget *m_tabWidget;
  QWidget *m_mainTab;
  QWidget *m_radarTab;
  QWidget *m_tuningTab;
  QWidget *m_firmwareTab;
  QWidget *m_deviceTab;
  QWidget *m_canTab;

  // Scroll areas
  QScrollArea *m_mainScrollArea;
  QScrollArea *m_radarScrollArea;
  QScrollArea *m_tuningScrollArea;
  QScrollArea *m_firmwareScrollArea;
  QScrollArea *m_deviceScrollArea;
  QScrollArea *m_canScrollArea;

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

  // Special widgets for CAN tab
  QTableWidget *m_canMessageTable;
  QTableWidget *m_canSignalTable;
  QLabel *m_canMessageLabel;
  QPushButton *m_canPauseButton;
  QLabel *m_canUpdateRateLabel;
  int m_selectedCANMessage = -1;

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

  OtherDataCache *m_cache;
};

// Data cache structure to store all the debug data
struct OtherDataCache {
  // All the existing data structures from the OtherDebugPanel
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
  } carValues;

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
  } radarValues;

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
  } outputValues;

  struct CarParameterValues {
    enum class LateralTuningType { PID, TORQUE };

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
  } paramValues;

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
  } deviceValues;

  // CAN data structures
  struct CANSignal {
    QString name;
    double value;
    double previousValue;  // Track previous value for change detection
    QString unit;
    double min;
    double max;
    bool hasChanged;  // Flag if value changed in last update
  };

  struct CANMessage {
    int id;
    QString name;
    int bus;
    int frequency;
    uint64_t lastSeen;
    uint64_t firstSeen;  // When message was first discovered
    uint64_t updateCount;  // Total updates received
    QByteArray lastData;  // Store raw data for comparison
    bool hasNewData;  // Flag if data changed in last update
    QList<CANSignal> signalList;  // Renamed from signals to avoid Qt macro conflict
  };

  QMap<int, CANMessage> canMessages;  // Key is message ID
  bool canDataAvailable = false;
  QList<int> discoveryOrder;  // Track order messages were discovered

  uint64_t lastUpdateTime = 0;
  bool valid = false;

  // Track if sections have been updated
  struct UpdateFlags {
    bool carState = false;
    bool radarState = false;
    bool carOutput = false;
    bool carParams = false;
    bool deviceState = false;
    bool canData = false;
  } updated;
};

// Register types for cross-thread use
Q_DECLARE_METATYPE(OtherDataCache)
