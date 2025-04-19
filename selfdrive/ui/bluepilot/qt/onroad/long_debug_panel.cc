#include "long_debug_panel.h"
#include "widgets/debug/AccelGraphWidget.h"
#include "widgets/debug/LongControlGraphWidget.h"
#include <QVBoxLayout>
#include <QLabel>
#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QLinearGradient>
#include <QDateTime>

void LongDataWorker::processData(const UIState *s) {
  QMutexLocker locker(&m_mutex);
  if (m_abort)
    return;

  LongDataCache cache;

  if (!s->scene.started || !s->sm)
    return;

  auto &sm = *(s->sm);

  try {
    // Update car state data
    if (sm.valid("carState")) {
      auto car = sm["carState"].getCarState();
      cache.actualAccel = car.getAEgo();
      cache.currentSpeed = car.getVEgo();
    }

    // Update control data
    if (sm.valid("carControl")) {
      try {
        auto control = sm["carControl"].getCarControl();
        cache.gasSignal = control.getActuators().getGas();
        cache.brakeSignal = control.getActuators().getBrake();

        // Initialize with last cache data then add new point
        cache.controlData = m_lastCache.controlData;
        cache.controlData.push_front({cache.gasSignal, cache.brakeSignal});
        if (cache.controlData.size() > LONG_MAX_DATA_POINTS)
          cache.controlData.pop_back();

        cache.targetSpeed = control.getVCruise();
      } catch (const std::exception &) {
        cache.gasSignal = 0.0f;
        cache.brakeSignal = 0.0f;
      }
    }

    // Update longitudinal plan data
    if (sm.valid("longitudinalPlan")) {
      try {
        const auto &plan = sm["longitudinalPlan"].getLongitudinalPlan();

        // Get accel trajectory
        cache.accelTrajectory.clear();
        for (int i = 0; i < std::min(20, (int)plan.getAccels().size()); ++i) {
          cache.accelTrajectory.push_back(plan.getAccels()[i]);
        }

        if (!cache.accelTrajectory.empty()) {
          cache.desiredAccel = cache.accelTrajectory[0];
        }

        cache.shouldStop = plan.getShouldStop();
        cache.allowThrottle = plan.getAllowThrottle();
        cache.allowBrake = plan.getAllowBrake();
      } catch (const std::exception &) {
        cache.desiredAccel = cache.actualAccel;
      }
    }

    // Update actuator delay
    if (sm.valid("carParams")) {
      try {
        cache.longitudinalActuatorDelay = sm["carParams"].getCarParams().getLongitudinalActuatorDelay();
      } catch (const std::exception &) {
        cache.longitudinalActuatorDelay = 0.0f;
      }
    }

    // Initialize with last cache data then add new point
    cache.accelData = m_lastCache.accelData;
    cache.accelData.push_front({cache.desiredAccel, cache.actualAccel});
    if (cache.accelData.size() > LONG_MAX_DATA_POINTS)
      cache.accelData.pop_back();

    // Auto-adjust acceleration scale (use the existing logic)
    cache.maxAccel = m_lastCache.maxAccel;
    for (const auto &data : cache.accelData) {
      float maxAbs = std::max(std::abs(data.first), std::abs(data.second));
      if (maxAbs > cache.maxAccel)
        cache.maxAccel = maxAbs * 1.2f;
    }

    if (cache.accelData.size() > 10) {
      float currentMax = 0;
      for (const auto &data : cache.accelData) {
        currentMax = std::max(currentMax, std::max(std::abs(data.first), std::abs(data.second)));
      }
      if (currentMax < cache.maxAccel * 0.7f)
        cache.maxAccel *= 0.99f;
    }

    if (cache.maxAccel < 1.0f)
      cache.maxAccel = 1.0f;

    cache.lastUpdateTime = QDateTime::currentMSecsSinceEpoch();
    cache.valid = true;

    m_lastCache = cache;
    emit dataReady(cache);
  } catch (const std::exception &e) {
    qWarning() << "Error processing longitudinal debug data:" << e.what();
  }
}

LongDebugPanel::LongDebugPanel(QWidget *parent) : QWidget(parent), m_dataProcessing(false) {
  // Register pointer type for cross-thread signal/slot usage
  qRegisterMetaType<const UIState *>("const UIState*");
  qRegisterMetaType<LongDataCache>("LongDataCache");
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 20);

  // Title
  QLabel *title = new QLabel("Longitudinal Control", this);
  title->setStyleSheet("font-size: 34px; font-weight: bold; color: white; margin: 0px;");
  title->setFont(QFont("Arial", 34, QFont::Bold));
  title->setFixedHeight(60);
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  // Create container widgets with padding for each graph
  auto createGraphContainer = [this](QWidget *graphWidget) -> QWidget * {
    QWidget *container = new QWidget(this);
    QVBoxLayout *containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 15, 0, 15); // Add 15px padding at top and bottom
    containerLayout->addWidget(graphWidget);
    return container;
  };

  // Graph widgets
  accelGraph = new AccelGraphWidget(this);
  controlGraph = new ControlGraphWidget(this);

  // Add each graph inside its container
  layout->addWidget(createGraphContainer(accelGraph));
  layout->addWidget(createGraphContainer(controlGraph));

  layout->setSpacing(10); // Reduced spacing between containers since they have internal padding

  // Initialize background gradient
  m_backgroundGradient = QLinearGradient(0, 0, 0, height());
  m_backgroundGradient.setColorAt(0, QColor(30, 30, 30, 230));
  m_backgroundGradient.setColorAt(1, QColor(20, 20, 20, 230));
  m_backgroundGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
  m_gradientInitialized = true;

  // Setup worker thread
  m_worker = new LongDataWorker();
  m_worker->moveToThread(&m_workerThread);
  connect(this, &LongDebugPanel::processStateUpdate, m_worker, &LongDataWorker::processData);
  connect(m_worker, &LongDataWorker::dataReady, this, &LongDebugPanel::updateFromWorker);
  m_workerThread.start();

  // Setup update timer
  m_updateTimer.setSingleShot(true);
  m_updateTimer.setInterval(LONG_MIN_UPDATE_INTERVAL_MS);
  connect(&m_updateTimer, &QTimer::timeout, this, &LongDebugPanel::updateUI);
}

LongDebugPanel::~LongDebugPanel() {
  m_workerThread.quit();
  m_workerThread.wait();
  delete m_worker;
}

void LongDebugPanel::updateState(const UIState &s) {
  if (!isVisible())
    return;
  if (!s.scene.started || !s.sm)
    return;

  // If we're already processing data and the timer isn't active, start it
  if (m_dataProcessing.load() && !m_updateTimer.isActive()) {
    m_updateTimer.start();
    return;
  }

  // Otherwise, process this update - pass pointer instead of reference
  m_dataProcessing.store(true);
  emit processStateUpdate(&s);
}

void LongDebugPanel::updateFromWorker(const LongDataCache &cache) {
    // Store the new data in our cache
    if (cache.valid && cache.needsUpdate(m_cache, LONG_MIN_UPDATE_INTERVAL_MS)) {
        m_cache = cache;
        updateUI();
    } else {
        m_dataProcessing.store(false);
    }
}

void LongDebugPanel::updateUI() {
    if (!isVisible()) {
        m_dataProcessing.store(false);
        return;
    }

    // Update the graphs with the latest data
    accelGraph->setData(
        m_cache.accelData,
        m_cache.maxAccel,
        m_cache.actualAccel,
        m_cache.desiredAccel,
        m_cache.longitudinalActuatorDelay,
        m_cache.accelTrajectory
    );

    controlGraph->setData(
        m_cache.controlData,
        m_cache.gasSignal,
        m_cache.brakeSignal,
        m_cache.allowThrottle,
        m_cache.allowBrake,
        m_cache.shouldStop
    );

    m_dataProcessing.store(false);
    update(); // Trigger a repaint
}

void LongDebugPanel::paintEvent(QPaintEvent *event) {
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
