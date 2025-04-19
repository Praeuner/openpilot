#pragma once

#include <QWidget>
#include <deque>
#include <utility>
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

class LateralGraphWidget;

// Global constants
static constexpr int LATERAL_MAX_DATA_POINTS = 100;
static constexpr uint64_t LATERAL_MIN_UPDATE_INTERVAL_MS = 50; // 20 Hz max update rate

// Data cache structure to store processed data
struct LateralDataCache {
  std::deque<std::pair<float, float>> steerData;
  float maxAngle = 20.0f;
  float actualSteerAngle = 0.0f;
  float desiredSteerAngle = 0.0f;
  float steerActuatorDelay = 0.0f;
  float actualCurvature = 0.0f;
  float desiredCurvature = 0.0f;
  float maxAbsPredictedCurvature = 0.0f;
  float predictedSteeringAngleDegSP = 0.0f;
  float pathAngleKp = 0.0f;
  bool hasFordVariables = false;
  uint64_t lastUpdateTime = 0;
  bool valid = false;

  // Comparison function to check if update is needed
  bool needsUpdate(const LateralDataCache &newCache, uint64_t minUpdateInterval) const {
    if (!valid)
      return true;

    // Force update every 100ms regardless of value changes
    if (newCache.lastUpdateTime - lastUpdateTime >= 100)
      return true;

    // Check if important values changed (with smaller thresholds)
    return (std::abs(newCache.actualSteerAngle - actualSteerAngle) > 0.01f || std::abs(newCache.desiredSteerAngle - desiredSteerAngle) > 0.01f ||
            std::abs(newCache.actualCurvature - actualCurvature) > 0.00001f || std::abs(newCache.desiredCurvature - desiredCurvature) > 0.00001f ||
            // Add Ford variables to comparison
            (newCache.hasFordVariables != hasFordVariables) ||
            (hasFordVariables && newCache.hasFordVariables &&
             (std::abs(newCache.maxAbsPredictedCurvature - maxAbsPredictedCurvature) > 0.00001f ||
              std::abs(newCache.predictedSteeringAngleDegSP - predictedSteeringAngleDegSP) > 0.01f || std::abs(newCache.pathAngleKp - pathAngleKp) > 0.001f)));
  }
};
Q_DECLARE_METATYPE(LateralDataCache)

// Worker class to process data off the UI thread
class LateralDataWorker : public QObject {
  Q_OBJECT
public:
  LateralDataWorker(QObject *parent = nullptr) : QObject(parent), m_abort(false) {}
  ~LateralDataWorker() {
    m_abort = true;
    m_condition.wakeAll();
  }

public slots:
  void processData(const UIState *s);

signals:
  void dataReady(const LateralDataCache &cache);

private:
  QMutex m_mutex;
  QWaitCondition m_condition;
  std::atomic<bool> m_abort;
  LateralDataCache m_lastCache;
};

class LateralDebugPanel : public QWidget {
  Q_OBJECT
public:
  LateralDebugPanel(QWidget *parent = nullptr);
  ~LateralDebugPanel();
  void updateState(const UIState &s);
  void paintEvent(QPaintEvent *event);

signals:
  void processStateUpdate(const UIState *s);

private slots:
  void updateFromWorker(const LateralDataCache &cache);
  void updateUI();

private:
  // UI components
  LateralGraphWidget *lateralGraph;

  // Data cache
  LateralDataCache m_cache;

  // Thread management
  QThread m_workerThread;
  LateralDataWorker *m_worker;
  std::atomic<bool> m_dataProcessing;
  QTimer m_updateTimer;

  // Cached drawing components
  QLinearGradient m_backgroundGradient;
  bool m_gradientInitialized = false;
};
