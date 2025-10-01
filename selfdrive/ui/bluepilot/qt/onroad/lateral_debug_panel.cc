// selfdrive/ui/bluepilot/qt/onroad/lateral_debug_panel.cc

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include "lateral_debug_panel.h"
#include "widgets/debug/LateralGraphWidget.h"
#include "selfdrive/ui/qt/util.h"
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

    // Set default values - always show steer delay by default
    cache.lateralDelay = 0.0f;
    cache.lateralDelayEstimate = 0.0f;
    cache.lateralDelayEstimateStd = 0.0f;
    cache.hasLiveDelay = false;

    // Let's add a compile flag check - this will allow the code to compile
    // regardless of whether liveDelay exists
#ifdef HAS_LIVE_DELAY
    // This section will only be compiled if HAS_LIVE_DELAY is defined
    try {
      if (sm.valid("liveDelay")) {
        auto liveDelay = sm["liveDelay"];
        cache.lateralDelay = liveDelay.getLateralDelay();
        cache.lateralDelayEstimate = liveDelay.getLateralDelayEstimate();
        cache.lateralDelayEstimateStd = liveDelay.getLateralDelayEstimateStd();
        cache.hasLiveDelay = true;
      }
    } catch (...) {
      // Silently ignore errors
      cache.hasLiveDelay = false;
    }
#endif

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
    BPLog::bpWarn() << "[bp.lateral.debug.panel] processData | Error processing lateral debug data: " << e.what() << std::endl;
  }
}

LateralDebugPanel::LateralDebugPanel(QWidget *parent) : QWidget(parent), m_dataProcessing(false) {
  // Register UIState* type for cross-thread signal/slot usage
  qRegisterMetaType<const UIState *>("const UIState*");
  qRegisterMetaType<LateralDataCache>("LateralDataCache");

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 20);

  // Title with automotive styling
  QLabel *title = new QLabel("Lateral Control", this);
  title->setStyleSheet("font-size: 34px; font-weight: bold; color: #ecf0f1; margin: 0px; text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.6);");
  title->setFont(InterFont(34, QFont::Bold));
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

  // Initialize automotive-style background gradient
  m_backgroundGradient = QLinearGradient(0, 0, 0, height());
  m_backgroundGradient.setColorAt(0, QColor(44, 62, 80, 240));  // Metallic blue-gray
  m_backgroundGradient.setColorAt(0.5, QColor(32, 33, 35, 240)); // Dark center
  m_backgroundGradient.setColorAt(1, QColor(26, 37, 47, 240));   // Dark edge
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
                        m_cache.actualCurvature, m_cache.hasFordVariables, m_cache.maxAbsPredictedCurvature, m_cache.predictedSteeringAngleDegSP, m_cache.pathAngleKp,
                        m_cache.lateralDelay, m_cache.lateralDelayEstimate, m_cache.lateralDelayEstimateStd, m_cache.hasLiveDelay);

  m_dataProcessing.store(false);
  update(); // Trigger a repaint
}

void LateralDebugPanel::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Draw automotive-style card background
  QPainterPath path;
  path.addRoundedRect(rect().adjusted(5, 5, -5, -5), 20, 20);

  // Draw shadow for depth
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 60));
  p.drawPath(path.translated(3, 3));

  // Use metallic gradient background
  p.fillPath(path, m_backgroundGradient);

  // Add inner highlight for metallic effect
  QPainterPath highlightPath;
  QRect highlightRect = rect().adjusted(8, 8, -8, -rect().height()/2);
  highlightPath.addRoundedRect(highlightRect, 18, 18);
  QLinearGradient highlight(highlightRect.topLeft(), highlightRect.bottomLeft());
  highlight.setColorAt(0, QColor(255, 255, 255, 25));
  highlight.setColorAt(0.3, QColor(255, 255, 255, 10));
  highlight.setColorAt(1, QColor(255, 255, 255, 0));
  p.fillPath(highlightPath, highlight);

  // Automotive-style border with gradient
  QLinearGradient borderGradient(rect().topLeft(), rect().bottomLeft());
  borderGradient.setColorAt(0, QColor(100, 149, 237, 180));  // Cornflower blue
  borderGradient.setColorAt(0.5, QColor(70, 130, 180, 180)); // Steel blue
  borderGradient.setColorAt(1, QColor(44, 62, 80, 180));     // Dark blue-gray
  p.setPen(QPen(QBrush(borderGradient), 2));
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);

  // Add accent line at top
  QRect accentRect = rect().adjusted(10, 7, -10, 0);
  accentRect.setHeight(3);
  QLinearGradient accentGradient(accentRect.topLeft(), accentRect.topRight());
  accentGradient.setColorAt(0, QColor(24, 144, 255, 0));
  accentGradient.setColorAt(0.5, QColor(24, 144, 255, 200));
  accentGradient.setColorAt(1, QColor(24, 144, 255, 0));
  p.fillRect(accentRect, accentGradient);

  QWidget::paintEvent(event);
}
