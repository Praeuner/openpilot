#include "selfdrive/ui/qt/sidebar.h"

#include <QMouseEvent>
#include <QPainterPath>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QSysInfo>
#include <iostream>

#include "selfdrive/ui/qt/util.h"

// Helper function to read system information files
QString readSystemFile(const QString &path) {
  QFile file(path);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();
    return content.trimmed();
  }
  return "";
}

// Helper function to run a process and get output
QString runProcess(const QString &program, const QStringList &arguments) {
  QProcess process;
  process.start(program, arguments);
  process.waitForFinished(500); // 500ms timeout - keep it short
  if (process.exitStatus() == QProcess::NormalExit) {
    return QString(process.readAllStandardOutput()).trimmed();
  }
  return "";
}

void Sidebar::drawMetric(QPainter &p, const QString &label, const QString &mainValue, const QString &leftValue, const QString &rightValue, QColor c, int y,
                         bool compactMode = false) {
  // Use a more compact layout for CPU/GPU/Memory cards
  const QRect rect = {30, y, 240, compactMode ? 100 : 120};

  // Create card background with rounded corners
  QPainterPath path;
  path.addRoundedRect(rect, 12, 12);

  // Draw card shadow
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 40));
  p.drawRoundedRect(rect.adjusted(2, 2, 2, 2), 12, 12);

  // Draw card background
  p.setBrush(card_background);
  p.drawPath(path);

  // Draw status indicator bar on left side
  p.setBrush(c);
  p.setClipRect(rect.x(), rect.y(), 6, rect.height(), Qt::ClipOperation::ReplaceClip);
  p.drawRoundedRect(rect, 12, 12);
  p.setClipping(false);

  if (compactMode) {
    // 2-row layout (compact mode for CPU/GPU/Memory)
    p.setPen(QColor(0xff, 0xff, 0xff));

    // Draw label text (top-left)
    p.setFont(InterFont(24, QFont::DemiBold));
    p.drawText(rect.adjusted(20, 10, 0, 0), Qt::AlignLeft | Qt::AlignTop, label);

    // Draw values side by side in the bottom row
    if (!leftValue.isEmpty()) {
      p.setFont(InterFont(33, QFont::Bold)); // Even larger font for usage
      p.drawText(rect.adjusted(20, rect.height() / 2 - 10, 0, 0), Qt::AlignLeft | Qt::AlignTop, leftValue);
    }

    if (!rightValue.isEmpty()) {
      p.setFont(InterFont(33, QFont::Bold)); // Even larger font for temperature
      p.drawText(rect.adjusted(0, rect.height() / 2 - 10, -20, 0), Qt::AlignRight | Qt::AlignTop, rightValue);
    }
  } else {
    // 3-row layout (original mode for VEHICLE/CONNECT/SUNNYLINK)
    // Draw label text (top)
    p.setPen(QColor(0xff, 0xff, 0xff));
    p.setFont(InterFont(24, QFont::DemiBold));
    p.drawText(rect.adjusted(20, 10, 0, -rect.height() / 2), Qt::AlignLeft | Qt::AlignVCenter, label);

    // Draw main value (center)
    p.setFont(InterFont(30, QFont::Bold));
    p.drawText(rect.adjusted(20, rect.height() / 5, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, mainValue);

    // Draw bottom values (left and right)
    if (!leftValue.isEmpty()) {
      p.setFont(InterFont(32, QFont::Bold));
      p.drawText(rect.adjusted(20, rect.height() / 2 + 20, 0, -10), Qt::AlignLeft | Qt::AlignBottom, leftValue);
    }

    if (!rightValue.isEmpty()) {
      p.setFont(InterFont(32, QFont::Bold));
      p.drawText(rect.adjusted(0, rect.height() / 2 + 20, -20, -10), Qt::AlignRight | Qt::AlignBottom, rightValue);
    }
  }
}

void Sidebar::drawProgressBar(QPainter &p, int x, int y, int width, int height, float percentage, QColor color) {
  // Draw background
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(80, 80, 80, 100));
  p.drawRoundedRect(x, y, width, height, height / 2, height / 2);

  // Draw filled portion
  if (percentage > 0) {
    int fill_width = std::max(int(width * percentage), 10); // Minimum visible width
    p.setBrush(color);
    p.drawRoundedRect(x, y, fill_width, height, height / 2, height / 2);

    // Create gradient overlay
    QLinearGradient gradient(x, y, x + fill_width, y);
    gradient.setColorAt(0.0, QColor(255, 255, 255, 30));
    gradient.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(gradient);
    p.drawRoundedRect(x, y, fill_width, height, height / 2, height / 2);
  }

  // Draw percentage text
  p.setPen(Qt::white);
  p.setFont(InterFont(14, QFont::Bold));
  QString percentText = QString("%1%").arg(int(percentage * 100));
  p.drawText(x + width + 10, y + height - 2, percentText);
}

Sidebar::Sidebar(QWidget *parent) : QFrame(parent), onroad(false), flag_pressed(false), settings_pressed(false) {
  home_img = loadPixmap("../assets/images/button_home.png", home_btn_size);
  flag_img = loadPixmap("../assets/offroad/icon_flag.png", home_btn_size);
  settings_img = loadPixmap("../assets/offroad/icon_settings.png", settings_btn_size, Qt::IgnoreAspectRatio);

  connect(this, &Sidebar::valueChanged, [=] { update(); });

  setAttribute(Qt::WA_OpaquePaintEvent);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  setFixedWidth(300);

  // Setup hover animation
  hover_animation = new QPropertyAnimation(this, "hover_opacity");
  hover_animation->setDuration(300);
  hover_animation->setStartValue(0.0);
  hover_animation->setEndValue(1.0);
  hover_animation->setEasingCurve(QEasingCurve::OutCubic);

  QObject::connect(uiState(), &UIState::uiUpdate, this, &Sidebar::updateState);

  pm = std::make_unique<PubMaster>(std::vector<const char *>{"userFlag"});
}

Sidebar::~Sidebar() { delete hover_animation; }

void Sidebar::enterEvent(QEvent *event) {
  is_hovering = true;
  hover_animation->setDirection(QAbstractAnimation::Forward);
  hover_animation->start();
  QFrame::enterEvent(event);
}

void Sidebar::leaveEvent(QEvent *event) {
  is_hovering = false;
  hover_animation->setDirection(QAbstractAnimation::Backward);
  hover_animation->start();
  QFrame::leaveEvent(event);
}

void Sidebar::mousePressEvent(QMouseEvent *event) {
  if (onroad && home_btn.contains(event->pos())) {
    flag_pressed = true;
    update();
  } else if (settings_btn.contains(event->pos())) {
    settings_pressed = true;
    update();
  } else if (memory_fan_btn.contains(event->pos())) {
    // Toggle between memory and fan display
    show_fan_instead_memory = !show_fan_instead_memory;
    update();
  }
}

void Sidebar::mouseReleaseEvent(QMouseEvent *event) {
  if (flag_pressed || settings_pressed) {
    flag_pressed = settings_pressed = false;
    update();
  }
  if (onroad && home_btn.contains(event->pos())) {
    MessageBuilder msg;
    msg.initEvent().initUserFlag();
    pm->send("userFlag", msg);
  } else if (settings_btn.contains(event->pos())) {
    emit openSettings();
  } else if (temp_btn.contains(event->pos()) && onroad) {
    emit debugPanelRequested();
  }
}

void Sidebar::offroadTransition(bool offroad) {
  onroad = !offroad;
  update();
}

void Sidebar::updateState(const UIState &s) {
  if (!isVisible())
    return;

  auto &sm = *(s.sm);

  networking = networking ? networking : window()->findChild<Networking *>("");
  bool tethering_on = networking && networking->wifi->tethering_on;
  auto deviceState = sm["deviceState"].getDeviceState();
  QString current_net_type = tethering_on ? "Hotspot" : network_type[deviceState.getNetworkType()];
  setProperty("netType", current_net_type);
  int strength = tethering_on ? 4 : (int)deviceState.getNetworkStrength();
  setProperty("netStrength", strength > 0 ? strength + 1 : 0);

  // Calculate network card color based on signal strength
  QColor network_color = strength > 3 ? good_color : (strength > 1 ? warning_color : danger_color);
  ItemStatus networkStatus = {{tr("NETWORK"), current_net_type}, network_color};
  setProperty("networkStatus", QVariant::fromValue(networkStatus));

  // Update connection status
  ItemStatus connectStatus;
  auto last_ping = deviceState.getLastAthenaPingTime();
  QString connection_status;
  if (last_ping == 0) {
    connection_status = tr("OFFLINE");
    connectStatus = {{tr("CONNECT"), connection_status}, warning_color};
    setProperty("connectStatus", QVariant::fromValue(connectStatus));
  } else {
    bool is_online = nanos_since_boot() - last_ping < 80e9;
    connection_status = is_online ? tr("ONLINE") : tr("ERROR");
    QColor status_color = is_online ? good_color : danger_color;
    connectStatus = {{tr("CONNECT"), connection_status}, status_color};
    setProperty("connectStatus", QVariant::fromValue(connectStatus));
  }

  // Update performance metrics at specified intervals
  metrics_refresh_counter++;
  if (metrics_refresh_counter >= METRICS_REFRESH_INTERVAL) {
    // Get CPU metrics using deviceState like in OtherDebugPanel
    // Get max CPU temperature
    float max_temp = deviceState.getMaxTempC();
    QString cpu_temp_str = QString("%1°C").arg(QString::number(max_temp, 'f', 1));
    setProperty("cpuTemp", cpu_temp_str);

    // Get CPU usage from deviceState
    QStringList cpuUsageValues;
    for (auto usage : deviceState.getCpuUsagePercent()) {
      cpuUsageValues.append(QString::number(usage));
    }

    // Calculate average CPU usage if values are available
    if (!cpuUsageValues.isEmpty()) {
      float totalUsage = 0;
      for (const QString &val : cpuUsageValues) {
        totalUsage += val.toFloat();
      }
      float avgCpuUsage = totalUsage / cpuUsageValues.size();
      QString cpu_usage_str = QString("%1%").arg(QString::number(avgCpuUsage, 'f', 0));
      setProperty("cpuUsage", cpu_usage_str);
    } else {
      setProperty("cpuUsage", "0%");
    }

    // Set CPU status color based on thermal status
    QColor cpu_color = good_color;
    auto ts = deviceState.getThermalStatus();
    if (ts == cereal::DeviceState::ThermalStatus::GREEN) {
      cpu_color = good_color;
    } else if (ts == cereal::DeviceState::ThermalStatus::YELLOW) {
      cpu_color = warning_color;
    } else {
      cpu_color = danger_color;
    }

    ItemStatus tempStatus = {{tr("CPU"), tr("")}, cpu_color};
    setProperty("tempStatus", QVariant::fromValue(tempStatus));

    // Get GPU metrics using deviceState like in OtherDebugPanel
    // Get GPU temperature
    if (deviceState.getGpuTempC().size() > 0) {
      float gpu_temp_val = deviceState.getGpuTempC()[0];
      QString gpu_temp_str = QString("%1°C").arg(QString::number(gpu_temp_val, 'f', 1));
      setProperty("gpuTemp", gpu_temp_str);

      // Set GPU color based on temperature
      QColor gpu_color = good_color;
      if (gpu_temp_val > 75.0) {
        gpu_color = danger_color;
      } else if (gpu_temp_val > 65.0) {
        gpu_color = warning_color;
      }

      ItemStatus gpuStatus = {{tr("GPU"), tr("")}, gpu_color};
      setProperty("gpuStatus", QVariant::fromValue(gpuStatus));
    } else {
      setProperty("gpuTemp", "N/A");
    }

    // Get GPU usage from deviceState
    float gpu_usage_val = deviceState.getGpuUsagePercent();
    QString gpu_usage_str = QString("%1%").arg(QString::number(gpu_usage_val, 'f', 0));
    setProperty("gpuUsage", gpu_usage_str);

    /// Get fan data from deviceState and pandaStates
    float fan_demand_val = deviceState.getFanSpeedPercentDesired();
    QString fan_demand_str = QString("%1%").arg(QString::number(fan_demand_val, 'f', 0));
    setProperty("fanDemand", fan_demand_str);

    // Get memory usage from deviceState
    float memory_usage_val = deviceState.getMemoryUsagePercent() / 100.0f; // Convert to 0-1 range
    QString memory_usage_str = QString("%1%").arg(QString::number(memory_usage_val * 100, 'f', 0));
    setProperty("memoryUsage", memory_usage_str);

    QColor memory_color = good_color;
    if (memory_usage_val > 0.85) {
      memory_color = danger_color;
    } else if (memory_usage_val > 0.7) {
      memory_color = warning_color;
    }

    ItemStatus memoryStatus = {{tr("MEMORY"), memory_usage_str}, memory_color};
    setProperty("memoryStatus", QVariant::fromValue(memoryStatus));

    // Reset counter
    metrics_refresh_counter = 0;
  }

  // Update vehicle status
  QString vehicle_status = tr("ONLINE");
  QColor vehicle_color = good_color;
  if (s.scene.pandaType == cereal::PandaState::PandaType::UNKNOWN) {
    vehicle_status = tr("OFFLINE");
    vehicle_color = danger_color;
  }
  ItemStatus pandaStatus = {{tr("VEHICLE"), vehicle_status}, vehicle_color};
  setProperty("pandaStatus", QVariant::fromValue(pandaStatus));
}

void Sidebar::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  drawSidebar(p);
}

void Sidebar::drawSidebar(QPainter &p) {
  p.setPen(Qt::NoPen);
  p.setRenderHint(QPainter::Antialiasing);

  // Draw background with subtle gradient
  QLinearGradient backgroundGradient(0, 0, 0, height());
  backgroundGradient.setColorAt(0, background_color);
  backgroundGradient.setColorAt(1, background_color.darker(110));
  p.fillRect(rect(), backgroundGradient);

  // Subtle side border
  p.setPen(QColor(0, 0, 0, 60));
  p.drawLine(width() - 1, 0, width() - 1, height());

  // Draw network card - at the top
  const int networkCardY = 20; // Start from the top
  QRect networkCardRect = QRect(30, networkCardY, 240, 110);

  // Create card background
  QPainterPath networkPath;
  networkPath.addRoundedRect(networkCardRect, 12, 12);

  // Card shadow
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 40));
  p.drawRoundedRect(networkCardRect.adjusted(2, 2, 2, 2), 12, 12);

  // Card background
  p.setBrush(card_background);
  p.drawPath(networkPath);

  // Status indicator bar
  QColor networkColor = accent_color;
  p.setBrush(networkColor);
  p.setClipRect(networkCardRect.x(), networkCardRect.y(), 6, networkCardRect.height(), Qt::ClipOperation::ReplaceClip);
  p.drawRoundedRect(networkCardRect, 12, 12);
  p.setClipping(false);

  // Network label
  p.setPen(QColor(0xff, 0xff, 0xff));
  p.setFont(InterFont(24, QFont::DemiBold));
  p.drawText(networkCardRect.adjusted(20, 10, 0, -networkCardRect.height() / 2), Qt::AlignLeft | Qt::AlignVCenter, tr("NETWORK"));

  // Connection type
  p.setFont(InterFont(34, QFont::Bold));
  p.drawText(networkCardRect.adjusted(20, networkCardRect.height() / 5, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, net_type);

  // Material design signal strength bars
  int barX = networkCardRect.right() - 90;
  int barY = networkCardRect.bottom() - 40; // Positioned higher to accommodate taller bars
  int barWidth = 10;
  int spacing = 5;
  int maxHeight = 32; // Increased height for taller bars

  for (int i = 0; i < 5; i++) {
    int barHeight = (i + 1) * maxHeight / 5;
    QColor barColor = i < net_strength ? accent_color : QColor(80, 80, 80, 80);

    // Draw shadow first (material design effect)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 20));
    p.drawRoundedRect(barX + i * (barWidth + spacing) + 1, barY - barHeight + 2, barWidth, barHeight, 2, 2);

    // Draw bar with subtle gradient for material look
    QLinearGradient gradient(barX + i * (barWidth + spacing), barY - barHeight, barX + i * (barWidth + spacing), barY);
    if (i < net_strength) {
      gradient.setColorAt(0, barColor.lighter(115));
      gradient.setColorAt(1, barColor);
    } else {
      gradient.setColorAt(0, QColor(100, 100, 100, 80));
      gradient.setColorAt(1, QColor(80, 80, 80, 80));
    }

    p.setBrush(gradient);

    // Add subtle border for active bars
    if (i < net_strength) {
      p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    } else {
      p.setPen(Qt::NoPen);
    }

    p.drawRoundedRect(barX + i * (barWidth + spacing), barY - barHeight, barWidth, barHeight, 2, 2);
  }

  // Draw metrics with modern styling - new order
  int metricsY = 140; // Start metrics below network card

  // CPU card with compact layout
  QColor cpuColor = good_color;
  // Make a temporary copy for value comparison
  float cpuTempValue = cpu_temp.mid(0, cpu_temp.length() - 2).toFloat();
  if (cpuTempValue > 80.0) {
    cpuColor = danger_color;
  } else if (cpuTempValue > 70.0) {
    cpuColor = warning_color;
  }
  drawMetric(p, tr("CPU"), tr(""), cpu_usage, cpu_temp, cpuColor, metricsY, true);

  // GPU card
  QColor gpuColor = good_color;
  float gpuTempValue = gpu_temp.mid(0, gpu_temp.length() - 2).toFloat();
  if (gpuTempValue > 75.0) {
    gpuColor = danger_color;
  } else if (gpuTempValue > 65.0) {
    gpuColor = warning_color;
  }

  // Memory/Fan card with toggle functionality
  QColor memoryFanColor = good_color;
  if (show_fan_instead_memory) {
    // Show fan demand
    float fanDemandValue = fan_demand.mid(0, fan_demand.length() - 1).toFloat();
    if (fanDemandValue > 80.0) {
      memoryFanColor = danger_color;
    } else if (fanDemandValue > 60.0) {
      memoryFanColor = warning_color;
    }
    drawMetric(p, tr("FAN"), tr(""), fan_demand, tr(""), memoryFanColor, metricsY + 220, true);
  } else {
    // Show memory as before
    float memoryValue = memory_usage.mid(0, memory_usage.length() - 1).toFloat();
    if (memoryValue > 85) {
      memoryFanColor = danger_color;
    } else if (memoryValue > 70) {
      memoryFanColor = warning_color;
    }
    drawMetric(p, tr("MEMORY"), tr(""), memory_usage, tr(""), memoryFanColor, metricsY + 220, true);
  }

  // Vehicle card - standard 3-row layout
  drawMetric(p, tr("VEHICLE"), panda_status.first.second, tr(""), tr(""), panda_status.second, metricsY + 330, false);

  // Connect card - standard 3-row layout
  drawMetric(p, tr("CONNECT"), connect_status.first.second, tr(""), tr(""), connect_status.second, metricsY + 460, false);

  // SunnyLink card - standard 3-row layout
  if (property("sunnylinkStatus").isValid()) {
    ItemStatus sunnylink_status = property("sunnylinkStatus").value<ItemStatus>();
    // Position it closer to avoid truncation
    drawMetric(p, tr("SUNNYLINK"), sunnylink_status.first.second, tr(""), tr(""), sunnylink_status.second, metricsY + 590, false);
  }

  // Buttons at bottom side by side
  const int bottomY = height() - 140; // Position closer to bottom
  const float scale = 0.5;            // Reduced scale for more padding
  const int buttonSize = 120;         // Button size

  // Calculate the starting position to center both buttons
  const int totalWidth = buttonSize * 2 + 30; // 30px spacing between buttons
  const int startX = (width() - totalWidth) / 2;

  // Position buttons centered horizontally
  settings_btn = QRect(startX, bottomY, buttonSize, buttonSize);
  home_btn = QRect(startX + buttonSize + 30, bottomY, buttonSize, buttonSize);

  // Draw settings button background and border
  p.setPen(QPen(QColor(255, 255, 255, 80), 2));
  p.setBrush(settings_pressed ? QColor(60, 60, 60, 120) : QColor(60, 60, 60, 80));
  p.drawEllipse(settings_btn);

  // Draw settings icon (centered with more padding)
  p.setOpacity(settings_pressed ? 0.65 : 1.0);
  p.drawPixmap(settings_btn.x() + (settings_btn.width() - settings_img.width() * scale) / 2, settings_btn.y() + (settings_btn.height() - settings_img.height() * scale) / 2,
               settings_img.scaled(settings_img.width() * scale, settings_img.height() * scale, Qt::KeepAspectRatio));
  p.setOpacity(1.0);

  // Only show flag button when onroad
  if (onroad) {
    // Draw button background
    p.setPen(QPen(QColor(255, 255, 255, 80), 2));
    p.setBrush(flag_pressed ? QColor(60, 60, 60, 120) : QColor(60, 60, 60, 80));
    p.drawEllipse(home_btn);

    // Draw flag icon
    p.setOpacity(flag_pressed ? 0.65 : 1.0);
    p.drawPixmap(home_btn.x() + (home_btn.width() - flag_img.width() * scale) / 2, home_btn.y() + (home_btn.height() - flag_img.height() * scale) / 2,
                 flag_img.scaled(flag_img.width() * scale, flag_img.height() * scale, Qt::KeepAspectRatio));
    p.setOpacity(1.0);
  }
}