#include "selfdrive/ui/bluepilot/qt/sidebar.h"
#include "selfdrive/ui/qt/util.h"
#include <QMouseEvent>
#include <QPainterPath>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QSysInfo>

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

SidebarBP::SidebarBP(QWidget *parent) : Sidebar(parent) {
  // Load button images with correct paths
  home_img = loadPixmap("../assets/images/button_home.png", QSize(120, 120));
  flag_img = loadPixmap("../assets/offroad/icon_flag.png", QSize(120, 120));
  settings_img = loadPixmap("../assets/offroad/icon_settings.png", QSize(120, 120), Qt::IgnoreAspectRatio);

  // Setup hover animation
  hover_animation = new QPropertyAnimation(this, "hover_opacity");
  hover_animation->setDuration(300);
  hover_animation->setStartValue(0.0);
  hover_animation->setEndValue(1.0);
  hover_animation->setEasingCurve(QEasingCurve::OutCubic);

  connect(this, &SidebarBP::valueChanged, [=] { update(); });
}

SidebarBP::~SidebarBP() {
  delete hover_animation;
}

void SidebarBP::enterEvent(QEvent *event) {
  is_hovering = true;
  hover_animation->setDirection(QAbstractAnimation::Forward);
  hover_animation->start();
  QFrame::enterEvent(event);
}

void SidebarBP::leaveEvent(QEvent *event) {
  is_hovering = false;
  hover_animation->setDirection(QAbstractAnimation::Backward);
  hover_animation->start();
  QFrame::leaveEvent(event);
}

void SidebarBP::drawMetric(QPainter &p, const QString &label, const QString &mainValue, const QString &leftValue, const QString &rightValue, QColor c, int y, bool compactMode) {
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

void SidebarBP::drawProgressBar(QPainter &p, int x, int y, int width, int height, float percentage, QColor color) {
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

void SidebarBP::mousePressEvent(QMouseEvent *event) {
  // Update button positions for current frame
  const int bottomY = height() - 140;
  const int buttonSize = 120;
  const int totalWidth = buttonSize * 2 + 30;
  const int startX = (width() - totalWidth) / 2;

  bp_settings_btn = QRect(startX, bottomY, buttonSize, buttonSize);
  bp_home_btn = QRect(startX + buttonSize + 30, bottomY, buttonSize, buttonSize);

  if (onroad && bp_home_btn.contains(event->pos())) {
    flag_pressed = true;
    update();
  } else if (bp_settings_btn.contains(event->pos())) {
    settings_pressed = true;
    update();
  } else if (memory_fan_btn.contains(event->pos())) {
    // Toggle between memory and fan display
    show_fan_instead_memory = !show_fan_instead_memory;
    update();
  }
}

void SidebarBP::mouseReleaseEvent(QMouseEvent *event) {
  if (flag_pressed || settings_pressed) {
    flag_pressed = settings_pressed = false;
    update();
  }

  // Handle debug panel first
  if (cpu_card_area.contains(event->pos()) && onroad) {
    emit debugPanelRequested();
    return;
  }

  // Handle settings button click directly
  if (bp_settings_btn.contains(event->pos())) {
    emit openSettings();
    return;
  }

  // Let parent handle flag/home button and other events
  Sidebar::mouseReleaseEvent(event);
}

void SidebarBP::updateState(const UIState &s) {
  if (!isVisible()) return;

  auto &sm = *(s.sm);
  auto deviceState = sm["deviceState"].getDeviceState();

  // Network status (from your original code)
  local_networking = local_networking ? local_networking : window()->findChild<Networking *>("");
  bool tethering_on = local_networking && local_networking->wifi->tethering_on;
  net_type = tethering_on ? "Hotspot" : network_type[deviceState.getNetworkType()];
  net_strength = tethering_on ? 4 : (int)deviceState.getNetworkStrength();
  net_strength = net_strength > 0 ? net_strength + 1 : 0;

  // Connection status
  auto last_ping = deviceState.getLastAthenaPingTime();
  if (last_ping == 0) {
    connect_status = {{tr("CONNECT"), tr("OFFLINE")}, warning_color};
  } else {
    bool is_online = nanos_since_boot() - last_ping < 80e9;
    QString status = is_online ? tr("ONLINE") : tr("ERROR");
    QColor color = is_online ? good_color : danger_color;
    connect_status = {{tr("CONNECT"), status}, color};
  }

  // Vehicle status
  QString vehicle_status = tr("ONLINE");
  QColor vehicle_color = good_color;
  if (s.scene.pandaType == cereal::PandaState::PandaType::UNKNOWN) {
    vehicle_status = tr("OFFLINE");
    vehicle_color = danger_color;
  }
  panda_status = {{tr("VEHICLE"), vehicle_status}, vehicle_color};

  // Performance metrics
  metrics_refresh_counter++;
  if (metrics_refresh_counter >= METRICS_REFRESH_INTERVAL) {
    // CPU
    float max_temp = deviceState.getMaxTempC();
    cpu_temp = QString("%1°C").arg(QString::number(max_temp, 'f', 1));

    QStringList cpuUsageValues;
    for (auto usage : deviceState.getCpuUsagePercent()) {
      cpuUsageValues.append(QString::number(usage));
    }

    if (!cpuUsageValues.isEmpty()) {
      float totalUsage = 0;
      for (const QString &val : cpuUsageValues) {
        totalUsage += val.toFloat();
      }
      float avgCpuUsage = totalUsage / cpuUsageValues.size();
      cpu_usage = QString("%1%").arg(QString::number(avgCpuUsage, 'f', 0));
    } else {
      cpu_usage = "0%";
    }

    // GPU
    if (deviceState.getGpuTempC().size() > 0) {
      float gpu_temp_val = deviceState.getGpuTempC()[0];
      gpu_temp = QString("%1°C").arg(QString::number(gpu_temp_val, 'f', 1));
    } else {
      gpu_temp = "N/A";
    }

    float gpu_usage_val = deviceState.getGpuUsagePercent();
    gpu_usage = QString("%1%").arg(QString::number(gpu_usage_val, 'f', 0));

    // Memory
    float memory_usage_val = deviceState.getMemoryUsagePercent();
    memory_usage = QString("%1%").arg(QString::number(memory_usage_val, 'f', 0));

    // Fan
    float fan_demand_val = deviceState.getFanSpeedPercentDesired();
    fan_demand = QString("%1%").arg(QString::number(fan_demand_val, 'f', 0));

    metrics_refresh_counter = 0;
  }
}

void SidebarBP::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  drawSidebar(p);
}

void SidebarBP::drawSidebar(QPainter &p) {
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
  if (gpu_temp != "N/A") {
    float gpuTempValue = gpu_temp.mid(0, gpu_temp.length() - 2).toFloat();
    if (gpuTempValue > 75.0) {
      gpuColor = danger_color;
    } else if (gpuTempValue > 65.0) {
      gpuColor = warning_color;
    }
  }
  drawMetric(p, tr("GPU"), tr(""), gpu_usage, gpu_temp, gpuColor, metricsY + 110, true);

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
  bp_settings_btn = QRect(startX, bottomY, buttonSize, buttonSize);
  bp_home_btn = QRect(startX + buttonSize + 30, bottomY, buttonSize, buttonSize);

  // Draw settings button background and border
  p.setPen(QPen(QColor(255, 255, 255, 80), 2));
  p.setBrush(settings_pressed ? QColor(60, 60, 60, 120) : QColor(60, 60, 60, 80));
  p.drawEllipse(bp_settings_btn);

  // Draw settings icon (centered with more padding)
  p.setOpacity(settings_pressed ? 0.65 : 1.0);
  p.drawPixmap(bp_settings_btn.x() + (bp_settings_btn.width() - settings_img.width() * scale) / 2, bp_settings_btn.y() + (bp_settings_btn.height() - settings_img.height() * scale) / 2,
               settings_img.scaled(settings_img.width() * scale, settings_img.height() * scale, Qt::KeepAspectRatio));
  p.setOpacity(1.0);

  // Only show flag button when onroad
  if (onroad) {
    // Draw button background
    p.setPen(QPen(QColor(255, 255, 255, 80), 2));
    p.setBrush(flag_pressed ? QColor(60, 60, 60, 120) : QColor(60, 60, 60, 80));
    p.drawEllipse(bp_home_btn);

    // Draw flag icon
    p.setOpacity(flag_pressed ? 0.65 : 1.0);
    p.drawPixmap(bp_home_btn.x() + (bp_home_btn.width() - flag_img.width() * scale) / 2, bp_home_btn.y() + (bp_home_btn.height() - flag_img.height() * scale) / 2,
                 flag_img.scaled(flag_img.width() * scale, flag_img.height() * scale, Qt::KeepAspectRatio));
    p.setOpacity(1.0);
  }
}