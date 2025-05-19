#include "lateral_debug_panel.h"
#include "widgets/debug/LateralGraphWidget.h"
#include <QVBoxLayout>
#include <QLabel>
#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QLinearGradient>
#include <QDateTime>

void LateralDataWorker::processData(const UIState *s) {
  QMutexLocker locker(&m_mutex);
  if (m_abort)
    return;

  LateralDataCache cache;

  if (!s->scene.started || !s->sm)
    return;

  auto &sm = *(s->sm);

  try {
    if (sm.valid("carState")) {
      cache.actualSteerAngle = sm["carState"].getCarState().getSteeringAngleDeg();
    }

    if (sm.valid("carControl")) {
      try {
        auto actuators = sm["carControl"].getCarControl().getActuators();
        cache.desiredSteerAngle = actuators.getSteeringAngleDeg();
        cache.hasFordVariables = false;
      } catch (const std::exception &) {
        cache.desiredSteerAngle = cache.actualSteerAngle;
        cache.hasFordVariables = false;
      }
    }

    if (sm.valid("carParams")) {
      try {
        cache.steerActuatorDelay = sm["carParams"].getCarParams().getSteerActuatorDelay();
      } catch (const std::exception &) {
        cache.steerActuatorDelay = 0.0;
      }
    }

    if (sm.valid("controlsState")) {
      try {
        auto controls = sm["controlsState"].getControlsState();
        cache.actualCurvature = controls.getCurvature();
        cache.desiredCurvature = controls.getDesiredCurvature();
      } catch (const std::exception &) {
        cache.actualCurvature = 0.0;
        cache.desiredCurvature = 0.0;
      }
    }

    // Copy the existing data from the cache and add new point
    cache.steerData = m_lastCache.steerData;
    cache.steerData.push_front({cache.desiredSteerAngle, cache.actualSteerAngle});

    if (cache.steerData.size() > LATERAL_MAX_DATA_POINTS)
      cache.steerData.pop_back();

    // Calculate max angle with damping
    cache.maxAngle = m_lastCache.maxAngle;
    for (const auto &data : cache.steerData) {
      float maxAbs = std::max(std::abs(data.first), std::abs(data.second));
      if (maxAbs > cache.maxAngle)
        cache.maxAngle = maxAbs * 1.2f;
    }

    if (cache.steerData.size() > 10) {
      float currentMax = 0;
      for (const auto &data : cache.steerData) {
        currentMax = std::max(currentMax, std::max(std::abs(data.first), std::abs(data.second)));
      }
      if (currentMax < cache.maxAngle * 0.7f)
        cache.maxAngle *= 0.99f;
    }

    if (cache.maxAngle < 5.0f)
      cache.maxAngle = 5.0f;

    cache.lastUpdateTime = QDateTime::currentMSecsSinceEpoch();
    cache.valid = true;

    m_lastCache = cache;
    emit dataReady(cache);
  } catch (const std::exception &e) {
    qWarning() << "Error processing lateral debug data:" << e.what();
  }
}

LateralDebugPanel::LateralDebugPanel(QWidget *parent) : QWidget(parent), m_dataProcessing(false) {
  // Register UIState* type for cross-thread signal/slot usage
  qRegisterMetaType<const UIState *>("const UIState*");
  qRegisterMetaType<LateralDataCache>("LateralDataCache");

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 20);

  // Title
  QLabel *title = new QLabel("Lateral Control", this);
  title->setStyleSheet("font-size: 34px; font-weight: bold; color: white; margin: 0px;");
  title->setFont(QFont("Arial", 34, QFont::Bold));
  title->setFixedHeight(60);
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  // Create a container widget with padding for the graph
  QWidget *graphContainer = new QWidget(this);
  QVBoxLayout *containerLayout = new QVBoxLayout(graphContainer);
  containerLayout->setContentsMargins(0, 15, 0, 15); // Reduced from 30px to 15px padding

  // Graph widget
  lateralGraph = new LateralGraphWidget(this);
  containerLayout->addWidget(lateralGraph);

  // Add the container to the main layout
  layout->addWidget(graphContainer);
  layout->setSpacing(10); // Reduced from 20 to 10

  // Initialize background gradient
  m_backgroundGradient = QLinearGradient(0, 0, 0, height());
  m_backgroundGradient.setColorAt(0, QColor(30, 30, 30, 230));
  m_backgroundGradient.setColorAt(1, QColor(20, 20, 20, 230));
  m_backgroundGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
  m_gradientInitialized = true;

  // Setup worker thread
  m_worker = new LateralDataWorker();
  m_worker->moveToThread(&m_workerThread);
  connect(this, &LateralDebugPanel::processStateUpdate, m_worker, &LateralDataWorker::processData);
  connect(m_worker, &LateralDataWorker::dataReady, this, &LateralDebugPanel::updateFromWorker);
  m_workerThread.start();

  // Setup update timer
  m_updateTimer.setSingleShot(true);
  m_updateTimer.setInterval(LATERAL_MIN_UPDATE_INTERVAL_MS);
  connect(&m_updateTimer, &QTimer::timeout, this, &LateralDebugPanel::updateUI);
}

LateralDebugPanel::~LateralDebugPanel() {
  m_workerThread.quit();
  m_workerThread.wait();
  delete m_worker;
}

void LateralDebugPanel::updateState(const UIState &s) {
  if (!isVisible())
    return;
  if (!s.scene.started || !s.sm)
    return;

  // If we're already processing data and the timer isn't active, start it
  if (m_dataProcessing.load() && !m_updateTimer.isActive()) {
    m_updateTimer.start();
    return;
  }

  // Otherwise, process this update
  m_dataProcessing.store(true);
  emit processStateUpdate(&s); // Pass a pointer to the state
}

void LateralDebugPanel::updateFromWorker(const LateralDataCache &cache) {
  // Store the new data in our cache
  if (cache.valid && cache.needsUpdate(m_cache, LATERAL_MIN_UPDATE_INTERVAL_MS)) {
    m_cache = cache;
    updateUI();
  } else {
    m_dataProcessing.store(false);
  }
}

void LateralDebugPanel::updateUI() {
  if (!isVisible()) {
    m_dataProcessing.store(false);
    return;
  }

  // Update the graph with the latest data
  lateralGraph->setData(m_cache.steerData, m_cache.maxAngle, m_cache.desiredSteerAngle, m_cache.actualSteerAngle, m_cache.steerActuatorDelay, m_cache.desiredCurvature,
                        m_cache.actualCurvature, m_cache.hasFordVariables, m_cache.maxAbsPredictedCurvature, m_cache.predictedSteeringAngleDegSP, m_cache.pathAngleKp);

  m_dataProcessing.store(false);
  update(); // Trigger a repaint
}

void LateralDebugPanel::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Draw background with material design styling
  QPainterPath path;
  path.addRoundedRect(rect().adjusted(5, 5, -5, -5), 15, 15);

  // Use cached gradient
  p.fillPath(path, m_backgroundGradient);

  // Material design subtle border
  p.setPen(QPen(QColor(60, 60, 60, 150), 1));
  p.drawPath(path);

  QWidget::paintEvent(event);
}
