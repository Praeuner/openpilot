// selfdrive/ui/bluepilot/qt/onroad/long_debug_panel.cc

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include "long_debug_panel.h"
#include "widgets/debug/AccelGraphWidget.h"
#include "widgets/debug/LongControlGraphWidget.h"
#include "selfdrive/ui/qt/util.h"
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

        cache.targetSpeed = control.getHudControl().getSetSpeed();
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
    BPLog::bpWarn() << "[bp.long.debug.panel] processData | Error processing longitudinal debug data: " << e.what() << std::endl;
  }
}

LongDebugPanel::LongDebugPanel(QWidget *parent) : QWidget(parent), m_dataProcessing(false) {
  // Register pointer type for cross-thread signal/slot usage
  qRegisterMetaType<const UIState *>("const UIState*");
  qRegisterMetaType<LongDataCache>("LongDataCache");
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 20);

  // Title with automotive styling
  QLabel *title = new QLabel("Longitudinal Control", this);
  title->setStyleSheet("font-size: 34px; font-weight: bold; color: #ecf0f1; margin: 0px; text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.6);");
  title->setFont(InterFont(34, QFont::Bold));
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

  // Initialize automotive-style background gradient
  m_backgroundGradient = QLinearGradient(0, 0, 0, height());
  m_backgroundGradient.setColorAt(0, QColor(44, 62, 80, 240));  // Metallic blue-gray
  m_backgroundGradient.setColorAt(0.5, QColor(32, 33, 35, 240)); // Dark center
  m_backgroundGradient.setColorAt(1, QColor(26, 37, 47, 240));   // Dark edge
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
  // Properly cleanup worker thread
  m_workerThread.quit();
  m_workerThread.wait(5000); // Add timeout for safety

  // Use deleteLater since worker is on another thread
  if (m_worker) {
    m_worker->deleteLater();
    m_worker = nullptr;
  }
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
    borderGradient.setColorAt(0, QColor(46, 204, 113, 180));   // Green for accel
    borderGradient.setColorAt(0.5, QColor(241, 196, 15, 180)); // Yellow transition
    borderGradient.setColorAt(1, QColor(231, 76, 60, 180));    // Red for brake
    p.setPen(QPen(QBrush(borderGradient), 2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Add accent line at top
    QRect accentRect = rect().adjusted(10, 7, -10, 0);
    accentRect.setHeight(3);
    QLinearGradient accentGradient(accentRect.topLeft(), accentRect.topRight());
    accentGradient.setColorAt(0, QColor(46, 204, 113, 0));
    accentGradient.setColorAt(0.5, QColor(46, 204, 113, 200));
    accentGradient.setColorAt(1, QColor(46, 204, 113, 0));
    p.fillRect(accentRect, accentGradient);

    QWidget::paintEvent(event);
}
