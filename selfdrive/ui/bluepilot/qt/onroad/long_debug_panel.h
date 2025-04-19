#pragma once

#include <QWidget>
#include <deque>
#include <utility>
#include <vector>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <atomic>

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui.h"
#define UIState UIStateSP
#else
#include "selfdrive/ui/ui.h"
#endif

class AccelGraphWidget;
class ControlGraphWidget;

// Global constants
static constexpr int LONG_MAX_DATA_POINTS = 100;
static constexpr uint64_t LONG_MIN_UPDATE_INTERVAL_MS = 50; // 20 Hz max update rate

// Data cache structure to store processed data
struct LongDataCache {
  std::deque<std::pair<float, float>> accelData;
  std::deque<std::pair<float, float>> controlData;
  std::vector<float> accelTrajectory;

  float maxAccel = 2.0f;
  float actualAccel = 0.0f;
  float desiredAccel = 0.0f;
  float longitudinalActuatorDelay = 0.0f;
  float currentSpeed = 0.0f;
  float targetSpeed = 0.0f;
  float gasSignal = 0.0f;
  float brakeSignal = 0.0f;
  bool shouldStop = false;
  bool allowThrottle = true;
  bool allowBrake = true;

  uint64_t lastUpdateTime = 0;
  bool valid = false;

  // Comparison function to check if update is needed
  bool needsUpdate(const LongDataCache &newCache, uint64_t minUpdateInterval) const {
    if (!valid)
      return true;

    // Force update every 100ms regardless of value changes
    if (newCache.lastUpdateTime - lastUpdateTime >= 100)
      return true;

    // Check if important values changed (with smaller thresholds)
    return (std::abs(newCache.actualAccel - actualAccel) > 0.01f ||   // was 0.1f
            std::abs(newCache.desiredAccel - desiredAccel) > 0.01f || // was 0.1f
            std::abs(newCache.gasSignal - gasSignal) > 0.01f ||       // was 0.05f
            std::abs(newCache.brakeSignal - brakeSignal) > 0.01f);    // was 0.05f
  }
};
Q_DECLARE_METATYPE(LongDataCache)
// Worker class to process data off the UI thread
class LongDataWorker : public QObject {
  Q_OBJECT
public:
  LongDataWorker(QObject *parent = nullptr) : QObject(parent), m_abort(false) {}
  ~LongDataWorker() {
    m_abort = true;
    m_condition.wakeAll();
  }

public slots:
  void processData(const UIState *s);

signals:
  void dataReady(const LongDataCache &cache);

private:
  QMutex m_mutex;
  QWaitCondition m_condition;
  std::atomic<bool> m_abort;
  LongDataCache m_lastCache;
};

class LongDebugPanel : public QWidget {
  Q_OBJECT
public:
  LongDebugPanel(QWidget *parent = nullptr);
  ~LongDebugPanel();
  void updateState(const UIState &s);
  void paintEvent(QPaintEvent *event);

signals:
  void processStateUpdate(const UIState *s);

private slots:
  void updateFromWorker(const LongDataCache &cache);
  void updateUI();

private:
  // UI components
  AccelGraphWidget *accelGraph;
  ControlGraphWidget *controlGraph;

  // Data cache
  LongDataCache m_cache;

  // Thread management
  QThread m_workerThread;
  LongDataWorker *m_worker;
  std::atomic<bool> m_dataProcessing;
  QTimer m_updateTimer;

  // Cached drawing components
  QLinearGradient m_backgroundGradient;
  bool m_gradientInitialized = false;
};