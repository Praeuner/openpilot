// selfdrive/ui/bluepilot/qt/offroad/panels/bp_utils.h

#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QCoreApplication>
#include <QCursor>
#include <QMouseEvent>
#include <QProcess>
#include <QWidget>
#include <QDir>
#include <QPoint>
#include <QString>
#include <iostream>
#include "common/params.h"

class CommaTools {
public:
  static bool isCommaDevice() {
#ifdef QCOM2
    return true;
#else
    return false;
#endif
  }
};

class FileUtils {
public:
  static QString getProjectRootPath() {
    QString appPath = QCoreApplication::applicationDirPath();
    QDir dir(appPath);
    dir.cdUp();
    dir.cdUp();
    return dir.absolutePath();
  }
};

class DefaultParams {
public:
  static DefaultParams &getInstance() {
    static DefaultParams instance;
    return instance;
  }

  QString getDefault(const QString &key) const {
    Params params;
    std::string defaultKey = key.toStdString() + "_default";
    return QString::fromStdString(params.get(defaultKey));
  }

private:
  DefaultParams() {}
};

class ConfigManager {
public:
  static ConfigManager &getInstance() {
    static ConfigManager instance;
    return instance;
  }

  bool loadConfig(const QString &filename) {
    std::cout << "Attempting to load config from: " << filename.toStdString() << std::endl;
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
      std::cerr << "Failed to open file: " << file.errorString().toStdString() << std::endl;
      return false;
    }
    QByteArray data = file.readAll();
    if (data.isEmpty()) {
      std::cerr << "File is empty" << std::endl;
      return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
      std::cerr << "Failed to parse JSON" << std::endl;
      return false;
    }
    std::cout << "Successfully loaded config" << std::endl;
    config = doc.object();
    return true;
  }

  const QJsonObject &getConfig() const { return config; }

private:
  ConfigManager() {}
  QJsonObject config;
};

class LogTreeFormatter {
private:
  int depth = 0;
  std::vector<bool> hasNextAtLevel;

public:
  void increaseDepth() {
    depth++;
    if (depth >= hasNextAtLevel.size()) {
      hasNextAtLevel.push_back(true);
    }
  }

  void decreaseDepth() {
    if (depth > 0)
      depth--;
  }

  void setHasNextAtCurrentLevel(bool hasNext) {
    if (depth < hasNextAtLevel.size()) {
      hasNextAtLevel[depth] = hasNext;
    }
  }

  std::string getPrefix() {
    std::string prefix;
    for (int i = 0; i < depth; i++) {
      prefix += (i < hasNextAtLevel.size() && hasNextAtLevel[i]) ? "│   " : "    ";
    }
    return prefix;
  }

  std::string getItemPrefix(bool hasNext) { return getPrefix() + (hasNext ? "├─ " : "└─ "); }
};

class ActivitySimulator : public QObject {
  Q_OBJECT

public:
  // Singleton instance getter
  static ActivitySimulator &getInstance() {
    static ActivitySimulator instance;
    // std::cout << "ActivitySimulator: Singleton instance accessed" << std::endl;
    return instance;
  }

  // Request to start or update activity simulation for a widget
  void requestSimulation(QWidget *widget) {
    // Safety check for null widget
    if (!widget) {
      // std::cout << "ActivitySimulator: Request ignored - null widget provided" << std::endl;
      return;
    }

    // Log simulation request
    // std::cout << "ActivitySimulator: Simulation requested for widget '" << widget->objectName().toStdString() << "'" << std::endl;

    // If we're already simulating for this widget, just reset the timer
    if (activeWidget == widget) {
      // std::cout << "ActivitySimulator: Widget already active, resetting max duration timer" << std::endl;
      resetMaxDurationTimer();
      return;
    }

    // Stop any existing simulation before starting new one
    if (activeWidget) {
      // std::cout << "ActivitySimulator: Stopping existing simulation for widget '" << activeWidget->objectName().toStdString() << "'" << std::endl;
    }

    // Set up new simulation
    stopSimulationInternal();
    activeWidget = widget;
    startTimers();

    // std::cout << "ActivitySimulator: New simulation started for widget '" << widget->objectName().toStdString() << "'" << std::endl;
  }

  // Request to stop simulation for a specific widget
  void stopSimulation(QWidget *widget) {
    // std::cout << "ActivitySimulator: Stop requested for widget '" << (widget ? widget->objectName().toStdString() : "null") << "'" << std::endl;

    // Only stop if we're simulating for this specific widget
    if (widget && widget == activeWidget) {
      // std::cout << "ActivitySimulator: Stopping active simulation" << std::endl;
      stopSimulationInternal();
    } else {
      // std::cout << "ActivitySimulator: Stop request ignored - widget is not active" << std::endl;
    }
  }

  // Check if simulation is currently active for a specific widget
  bool isSimulatingFor(QWidget *widget) const {
    bool result = (activeWidget == widget);
    // std::cout << "ActivitySimulator: Checking simulation status for widget '" << (widget ? widget->objectName().toStdString() : "null") << "': " << (result ? "active" :
    // "inactive")
    // << std::endl;
    return result;
  }

  // Temporarily pause the current simulation
  void pauseSimulation() {
    // std::cout << "ActivitySimulator: Pause requested" << std::endl;

    if (activeWidget) {
      // Store current widget and stop simulation
      // std::cout << "ActivitySimulator: Pausing simulation for widget '" << activeWidget->objectName().toStdString() << "'" << std::endl;
      pausedWidget = activeWidget;
      stopSimulationInternal();
    } else {
      // std::cout << "ActivitySimulator: No active simulation to pause" << std::endl;
    }
  }

  // Resume previously paused simulation
  void resumeSimulation() {
    // std::cout << "ActivitySimulator: Resume requested" << std::endl;

    if (pausedWidget) {
      // Restart simulation for the paused widget
      // std::cout << "ActivitySimulator: Resuming simulation for widget '" << pausedWidget->objectName().toStdString() << "'" << std::endl;
      requestSimulation(pausedWidget);
      pausedWidget = nullptr;
    } else {
      // std::cout << "ActivitySimulator: No paused simulation to resume" << std::endl;
    }
  }

  // Check if any simulation is currently active
  bool isSimulating() const {
    bool result = (activeWidget != nullptr && activityTimer->isActive());
    // std::cout << "ActivitySimulator: Checking simulation status: " << (result ? "active" : "inactive") << std::endl;
    return result;
  }

  // Get the currently active widget being simulated
  QWidget *getActiveWidget() const {
    // std::cout << "ActivitySimulator: Getting active widget: " << (activeWidget ? ("'" + activeWidget->objectName().toStdString() + "'") : "none") << std::endl;
    return activeWidget;
  }

private:
  // Private constructor for singleton pattern
  ActivitySimulator(QObject *parent = nullptr) : QObject(parent) {
    // std::cout << "ActivitySimulator: Initializing simulator" << std::endl;

    // Create timers for activity simulation
    activityTimer = new QTimer(this);
    maxDurationTimer = new QTimer(this);

    // Set timer intervals:
    // - Activity timer fires every 5 seconds to simulate user activity
    // - Max duration timer limits total simulation time to 4.5 minutes
    activityTimer->setInterval(5000);
    maxDurationTimer->setSingleShot(true);
    maxDurationTimer->setInterval(270000);

    // Connect timer signals to appropriate slots
    connect(activityTimer, &QTimer::timeout, this, &ActivitySimulator::simulateActivity);
    connect(maxDurationTimer, &QTimer::timeout, this, &ActivitySimulator::stopSimulationInternal);

    // std::cout << "ActivitySimulator: Initialization complete" << std::endl;
  }

  // Start both activity and max duration timers
  void startTimers() {
    // std::cout << "ActivitySimulator: Starting timers" << std::endl;

    // Only start activity timer if it's not already running
    if (!activityTimer->isActive()) {
      activityTimer->start();
      // std::cout << "ActivitySimulator: Activity timer started" << std::endl;
    }
    // Reset/start max duration timer
    resetMaxDurationTimer();
    // std::cout << "ActivitySimulator: Timers started successfully" << std::endl;
  }

  // Stop all simulation activity and clear state
  void stopSimulationInternal() {
    // std::cout << "ActivitySimulator: Stopping simulation internally" << std::endl;

    // Stop activity timer if running
    if (activityTimer->isActive()) {
      activityTimer->stop();
      // std::cout << "ActivitySimulator: Activity timer stopped" << std::endl;
    }
    // Stop max duration timer
    maxDurationTimer->stop();
    // std::cout << "ActivitySimulator: Max duration timer stopped" << std::endl;

    // Clear active widget reference
    activeWidget = nullptr;
    // std::cout << "ActivitySimulator: Active widget cleared" << std::endl;
  }

  // Reset the max duration timer to allow simulation to continue
  void resetMaxDurationTimer() {
    // std::cout << "ActivitySimulator: Resetting max duration timer" << std::endl;
    maxDurationTimer->stop();
    maxDurationTimer->start();
    // std::cout << "ActivitySimulator: Max duration timer reset complete" << std::endl;
  }

  // Simulate user activity by generating mouse events
  void simulateActivity() {
    if (!activeWidget) {
      // std::cout << "ActivitySimulator: No active widget, stopping simulation" << std::endl;
      stopSimulationInternal();
      return;
    }

    // std::cout << "ActivitySimulator: Simulating activity for widget '" << activeWidget->objectName().toStdString() << "'" << std::endl;

    // Get the widget's current geometry relative to its parent
    QRect widgetGeometry = activeWidget->rect(); // Use rect() instead of geometry()

    // Generate random x,y coordinates within the widget's bounds
    int x = widgetGeometry.width() / 2; // Center of the widget
    int y = 10;                         // Fixed 10 pixels from the top

    // Convert local coordinates to screen coordinates
    QPoint localPos(x, y);
    QPoint globalPos = activeWidget->mapToGlobal(localPos);

    // Create mouse press and release events
    // This simulates a complete click action
    QMouseEvent pressEvent(QEvent::MouseButtonPress, // Event type
                           localPos,                 // Position relative to widget
                           globalPos,                // Position relative to screen
                           Qt::LeftButton,           // Button that was pressed
                           Qt::LeftButton,           // Buttons currently held
                           Qt::NoModifier            // Keyboard modifiers
    );

    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, // Event type
                             localPos,                   // Position relative to widget
                             globalPos,                  // Position relative to screen
                             Qt::LeftButton,             // Button that was released
                             Qt::NoButton,               // Buttons currently held
                             Qt::NoModifier              // Keyboard modifiers
    );

    // Send the events to the widget through the application event queue
    QCoreApplication::sendEvent(activeWidget, &pressEvent);
    QCoreApplication::sendEvent(activeWidget, &releaseEvent);
  }

  // Member variables
  QTimer *activityTimer;           // Timer for regular activity simulation
  QTimer *maxDurationTimer;        // Timer to limit total simulation duration
  QWidget *activeWidget = nullptr; // Currently active widget being simulated
  QWidget *pausedWidget = nullptr; // Stores widget reference when simulation is paused
};

class TimerManager {
public:
  static TimerManager &getInstance() {
    static TimerManager instance;
    return instance;
  }

  QTimer *createTimer(const QString &id, bool singleShot = false) {
    if (timers.contains(id)) {
      return timers[id];
    }

    QTimer *timer = new QTimer();
    timer->setSingleShot(singleShot);
    timers[id] = timer;
    return timer;
  }

  QTimer *getTimer(const QString &id) { return timers.value(id, nullptr); }

  void removeTimer(const QString &id) {
    if (timers.contains(id)) {
      delete timers[id];
      timers.remove(id);
    }
  }

  void stopAllTimers() {
    for (QTimer *timer : timers) {
      timer->stop();
    }
  }

  ~TimerManager() { qDeleteAll(timers); }

private:
  TimerManager() {}
  QMap<QString, QTimer *> timers;
};

class ParamUtils {
public:
  // Initialize a parameter if it doesn't exist, using default value if available
  static bool initializeParam(const std::string &paramName) {
    Params params;
    std::string currentValue = params.get(paramName);

    if (currentValue.empty()) {
      // Check for default value
      DefaultParams &defaults = DefaultParams::getInstance();
      QString defaultKey = QString::fromStdString(paramName);
      QString defaultValue = defaults.getDefault(defaultKey);

      if (!defaultValue.isEmpty()) {
        // Apply the default value
        params.put(paramName, defaultValue.toStdString());
        std::cout << "Parameter initialized - " << paramName << ": " << defaultValue.toStdString() << " (from default)" << std::endl;
        return true;
      }
    }
    return false; // No initialization was needed or no default available
  }

  // Initialize a numeric parameter with range validation
  static bool initializeNumericParam(const std::string &paramName, double min, double max, bool isFloat, double div = 1.0, const QString &constructorDefault = "") {
    Params params;
    bool initialized = false;
    bool valueOutOfRange = false;
    std::string currentValue = params.get(paramName);

    // Check if param exists
    bool paramExists = !currentValue.empty();

    // If it exists, check if it's within range
    if (paramExists) {
      if (isFloat) {
        double numValue = QString::fromStdString(currentValue).toDouble();
        valueOutOfRange = (numValue < min || numValue > max);
      } else {
        int numValue = QString::fromStdString(currentValue).toInt();
        valueOutOfRange = (numValue < min || numValue > max);
      }
    }

    // If parameter doesn't exist or is out of range, check for default
    if (!paramExists || valueOutOfRange) {
      DefaultParams &defaults = DefaultParams::getInstance();
      QString defaultKey = QString::fromStdString(paramName);
      QString defaultValue = defaults.getDefault(defaultKey);

      if (!defaultValue.isEmpty()) {
        // Apply default value (ensure it's within range)
        if (isFloat) {
          double defaultNumeric = defaultValue.toDouble();
          defaultNumeric = std::clamp(defaultNumeric, min, max);
          params.putFloat(paramName, defaultNumeric);

          int decimals = div > 1.0 ? static_cast<int>(log10(div)) : 0;
          std::cout << "Parameter ";
          if (!paramExists) {
            std::cout << "initialized";
          } else {
            std::cout << "adjusted (out of range)";
          }
          std::cout << " - " << paramName << ": " << QString::number(defaultNumeric, 'f', decimals).toStdString() << " (from default)" << std::endl;
        } else {
          int defaultNumeric = defaultValue.toInt();
          defaultNumeric = std::clamp(defaultNumeric, static_cast<int>(min), static_cast<int>(max));
          params.putInt(paramName, defaultNumeric);

          std::cout << "Parameter ";
          if (!paramExists) {
            std::cout << "initialized";
          } else {
            std::cout << "adjusted (out of range)";
          }
          std::cout << " - " << paramName << ": " << defaultNumeric << " (from default)" << std::endl;
        }
        initialized = true;
      } else if (!constructorDefault.isEmpty() && !paramExists) {
        // Use constructor-provided default
        if (isFloat) {
          double defaultNumeric = constructorDefault.toDouble();
          defaultNumeric = std::clamp(defaultNumeric, min, max);
          params.putFloat(paramName, defaultNumeric);

          int decimals = div > 1.0 ? static_cast<int>(log10(div)) : 0;
          std::cout << "Parameter initialized - " << paramName << ": " << QString::number(defaultNumeric, 'f', decimals).toStdString() << " (from constructor default)"
                    << std::endl;
        } else {
          int defaultNumeric = constructorDefault.toInt();
          defaultNumeric = std::clamp(defaultNumeric, static_cast<int>(min), static_cast<int>(max));
          params.putInt(paramName, defaultNumeric);

          std::cout << "Parameter initialized - " << paramName << ": " << defaultNumeric << " (from constructor default)" << std::endl;
        }
        initialized = true;
      } else if (valueOutOfRange) {
        // No default but value is out of range - clamp it
        if (isFloat) {
          double numValue = QString::fromStdString(currentValue).toDouble();
          double clampedValue = std::clamp(numValue, min, max);
          params.putFloat(paramName, clampedValue);

          int decimals = div > 1.0 ? static_cast<int>(log10(div)) : 0;
          std::cout << "Parameter adjusted (clamped) - " << paramName << ": " << QString::number(numValue, 'f', decimals).toStdString() << " -> "
                    << QString::number(clampedValue, 'f', decimals).toStdString() << std::endl;
        } else {
          int numValue = QString::fromStdString(currentValue).toInt();
          int clampedValue = std::clamp(numValue, static_cast<int>(min), static_cast<int>(max));
          params.putInt(paramName, clampedValue);

          std::cout << "Parameter adjusted (clamped) - " << paramName << ": " << numValue << " -> " << clampedValue << std::endl;
        }
        initialized = true;
      }
    }

    return initialized;
  }

  // Log parameter changes for toggles and selection controls
  static void logParamChange(const std::string &paramName, const std::string &oldValue, const std::string &newValue) {
    std::cout << "Parameter changed - " << paramName << ": " << oldValue << " -> " << newValue << std::endl;
  }

  // Log numeric parameter changes with proper formatting
  static void logNumericParamChange(const std::string &paramName, double oldValue, double newValue, bool isFloat, double div = 1.0) {
    if (isFloat) {
      int decimals = div > 1.0 ? static_cast<int>(log10(div)) : 0;
      std::cout << "Parameter changed - " << paramName << ": " << QString::number(oldValue, 'f', decimals).toStdString() << " -> "
                << QString::number(newValue, 'f', decimals).toStdString() << std::endl;
    } else {
      int intOldValue = static_cast<int>(oldValue);
      int intNewValue = static_cast<int>(newValue);
      std::cout << "Parameter changed - " << paramName << ": " << intOldValue << " -> " << intNewValue << std::endl;
    }
  }
};