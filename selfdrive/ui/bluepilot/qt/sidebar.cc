#include "selfdrive/ui/bluepilot/qt/sidebar.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/qt/network/wifi_manager.h"
#include <QMouseEvent>
#include <QPainterPath>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QSysInfo>
#include <stdexcept>
#include "cereal/messaging/messaging.h"
#include "common/util.h"

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

// Helper function to map carrier codes to names
QString getCarrierName(const QString &operatorCode) {
  // If operator code is empty or just whitespace, return "Cellular"
  if (operatorCode.isEmpty() || operatorCode.trimmed().isEmpty()) {
    return "Cellular";
  }

  static const QMap<QString, QString> carrierMap = {
    // US Carriers
    {"310410", "AT&T"},
    {"310150", "AT&T"},
    {"310070", "AT&T"},
    {"310560", "AT&T"},
    {"310680", "AT&T"},
    {"310380", "AT&T"},
    {"311180", "AT&T"},
    {"310260", "T-Mobile"},
    {"310200", "T-Mobile"},
    {"310210", "T-Mobile"},
    {"310220", "T-Mobile"},
    {"310230", "T-Mobile"},
    {"310240", "T-Mobile"},
    {"310250", "T-Mobile"},
    {"310270", "T-Mobile"},
    {"310660", "T-Mobile"},
    {"310800", "T-Mobile"},
    {"311660", "T-Mobile"},
    {"311882", "T-Mobile"},
    {"312530", "T-Mobile"},
    {"311480", "Verizon"},
    {"311481", "Verizon"},
    {"311482", "Verizon"},
    {"311483", "Verizon"},
    {"311484", "Verizon"},
    {"311485", "Verizon"},
    {"311486", "Verizon"},
    {"311487", "Verizon"},
    {"311488", "Verizon"},
    {"311489", "Verizon"},
    {"310004", "Verizon"},
    {"310005", "Verizon"},
    {"310006", "Verizon"},
    {"310010", "Verizon"},
    {"310012", "Verizon"},
    {"310013", "Verizon"},
    {"310590", "Verizon"},
    {"310890", "Verizon"},
    {"310910", "Verizon"},
    {"311110", "Verizon"},
    {"311270", "Verizon"},
    {"311271", "Verizon"},
    {"311272", "Verizon"},
    {"311273", "Verizon"},
    {"311274", "Verizon"},
    {"311275", "Verizon"},
    {"311276", "Verizon"},
    {"311277", "Verizon"},
    {"311278", "Verizon"},
    {"311279", "Verizon"},
    {"311280", "Verizon"},
    {"311281", "Verizon"},
    {"311282", "Verizon"},
    {"311283", "Verizon"},
    {"311284", "Verizon"},
    {"311285", "Verizon"},
    {"311286", "Verizon"},
    {"311287", "Verizon"},
    {"311288", "Verizon"},
    {"311289", "Verizon"},
    {"311390", "Verizon"},
    {"310120", "Sprint"},
    {"312190", "Sprint"},
    {"311490", "Sprint"},
    {"311870", "Sprint"},
    {"311880", "Sprint"},
    {"312420", "US Cellular"},
    {"311580", "US Cellular"},

    // Canadian Carriers
    {"302220", "Telus"},
    {"302221", "Telus"},
    {"302860", "Telus"},
    {"302610", "Bell"},
    {"302640", "Bell"},
    {"302651", "Bell"},
    {"302720", "Rogers"},
    {"302721", "Rogers"},

    // UK Carriers
    {"23415", "Vodafone UK"},
    {"23410", "O2 UK"},
    {"23433", "EE"},
    {"23434", "EE"},
    {"23430", "EE"},
    {"23431", "EE"},
    {"23432", "EE"},
    {"23420", "Three UK"},

    // European Carriers
    {"26201", "Deutsche Telekom"},
    {"26202", "Vodafone DE"},
    {"26203", "O2 DE"},
    {"20801", "Orange FR"},
    {"20810", "SFR"},
    {"20820", "Bouygues"},
    {"20815", "Free Mobile"},
    {"22201", "TIM"},
    {"22210", "Vodafone IT"},
    {"22288", "Wind Tre"},
    {"21401", "Vodafone ES"},
    {"21403", "Orange ES"},
    {"21407", "Movistar"},

    // Australian Carriers
    {"50501", "Telstra"},
    {"50502", "Optus"},
    {"50503", "Vodafone AU"},

    // Asian Carriers
    {"44010", "NTT DoCoMo"},
    {"44020", "SoftBank"},
    {"44050", "KDDI"},
    {"46000", "China Mobile"},
    {"46001", "China Unicom"},
    {"46003", "China Telecom"},
    {"46697", "Taiwan Mobile"},
    {"46692", "Chunghwa"},
    {"46601", "Far EasTone"},
    {"45005", "SK Telecom"},
    {"45008", "KT"},
    {"45006", "LG U+"},
    {"52501", "Singtel"},
    {"52502", "Singtel"},
    {"52503", "StarHub"},
    {"52505", "StarHub"},
    {"52504", "M1"},
    {"51010", "Telkomsel"},
    {"51011", "XL Axiata"},
    {"51089", "Smartfren"},
    {"40401", "Vodafone India"},
    {"40402", "Airtel"},
    {"40403", "Airtel"},
    {"40410", "Airtel"},
    {"40411", "Vodafone India"},
    {"40420", "Airtel"},
    {"405840", "Jio"},
    {"405854", "Jio"},
    {"405855", "Jio"},
    {"405856", "Jio"},
    {"405857", "Jio"},
    {"405858", "Jio"},
    {"405859", "Jio"},
    {"405860", "Jio"},
    {"405861", "Jio"},
    {"405862", "Jio"},
    {"405863", "Jio"},
    {"405864", "Jio"},
    {"405865", "Jio"},
    {"405866", "Jio"},
    {"405867", "Jio"},
    {"405868", "Jio"},
    {"405869", "Jio"},
    {"405870", "Jio"},
    {"405871", "Jio"},
    {"405872", "Jio"},
    {"405873", "Jio"},
    {"405874", "Jio"}
  };

  // Debug log the operator code to see what we're getting
  qDebug() << "Operator Code:" << operatorCode;

  // Check if operator code exists in map
  if (carrierMap.contains(operatorCode)) {
    return carrierMap[operatorCode];
  }

  // If not found, return "Cellular" as fallback
  return "Cellular";
}

SidebarBP::SidebarBP(QWidget *parent) : Sidebar(parent) {
  // Disconnect base class signal connection to avoid conflicts
  QObject::disconnect(uiState(), &UIState::uiUpdate, this, &Sidebar::updateState);

  // Connect our own updateState method
  QObject::connect(uiState(), &UIState::uiUpdate, this, &SidebarBP::updateStateBP);

  // Initialize GPS satellite count to 0 (no GPS available initially)
  gps_satellite_count = 0;
  setProperty("gpsSatelliteCount", gps_satellite_count);

  // Load button images with correct paths
  home_img = loadPixmap("../assets/images/button_home.png", QSize(120, 120));
  flag_img = loadPixmap("../assets/offroad/icon_flag.png", QSize(120, 120));
  settings_img = loadPixmap("../assets/offroad/icon_settings.png", QSize(120, 120), Qt::IgnoreAspectRatio);
  mic_img = loadPixmap("../assets/icons/microphone.png", QSize(30, 30));
  debug_img = loadPixmap("../assets/offroad/icon_debug.png", QSize(120, 120)); // Load debug icon

  // Network type icons removed - will show text instead

  // Setup hover animation
  hover_animation = new QPropertyAnimation(this, "hover_opacity");
  hover_animation->setDuration(300);
  hover_animation->setStartValue(0.0);
  hover_animation->setEndValue(1.0);
  hover_animation->setEasingCurve(QEasingCurve::OutCubic);

  // Set the appropriate width for the new sidebar layout
  // Cards: 280px + Right margin: 30px + Button width: 120px + Right margin: 30px = 460px
  setFixedWidth(460);

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

void SidebarBP::drawNetworkCard(QPainter &p) {
  // Custom network card with consistent spacing and graphical layout
  const int cardY = 20;
  const int cardHeight = 130;
  const QRect rect = {30, cardY, 280, cardHeight};

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
  p.setBrush(accent_color);
  p.setClipRect(rect.x(), rect.y(), 6, rect.height(), Qt::ClipOperation::ReplaceClip);
  p.drawRoundedRect(rect, 12, 12);
  p.setClipping(false);

  p.setPen(QColor(0xff, 0xff, 0xff));

  // Draw "NETWORK" label (top-left)
  p.setFont(InterFont(24, QFont::DemiBold));
  p.drawText(rect.adjusted(20, 15, 0, 0), Qt::AlignLeft | Qt::AlignTop, tr("NETWORK"));

  // Network type text will be drawn at bottom left later
  bool tethering_on = local_networking && local_networking->wifi && local_networking->wifi->tethering_on;

  // Draw carrier/SSID (below title, full width allowed then truncated) - NOW IN BLUE
  p.setFont(InterFont(28, QFont::Bold));
  p.setPen(accent_color); // Changed from white to blue
  QString displayName = net_carrier_ssid;

  // Calculate available width (from left padding to right padding minus some margin)
  int availableWidth = rect.width() - 40; // 20px padding on each side
  QFontMetrics fm2(p.font());

  // Truncate if needed
  if (fm2.horizontalAdvance(displayName) > availableWidth) {
    displayName = fm2.elidedText(displayName, Qt::ElideRight, availableWidth);
  }

  p.drawText(rect.adjusted(20, 50, -20, 0), Qt::AlignLeft | Qt::AlignTop, displayName);

  // Reset pen color for subsequent drawing
  p.setPen(QColor(0xff, 0xff, 0xff));

  // Draw signal strength bars (bottom right) - TALLER with better visibility
  int barX = rect.right() - 90;
  int barY = rect.bottom() - 15; // Moved closer to bottom
  int barWidth = 10;
  int spacing = 5;
  int maxHeight = 35; // Taller bars

  for (int i = 0; i < 5; i++) {
    int barHeight = (i + 1) * maxHeight / 5;
    QColor barColor = i < net_strength ? accent_color : QColor(120, 120, 120, 120); // More visible unfilled bars

    // Draw shadow first
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 20));
    p.drawRoundedRect(barX + i * (barWidth + spacing) + 1, barY - barHeight + 2, barWidth, barHeight, 2, 2);

    // Draw bar with gradient
    QLinearGradient gradient(barX + i * (barWidth + spacing), barY - barHeight, barX + i * (barWidth + spacing), barY);
    if (i < net_strength) {
      gradient.setColorAt(0, barColor.lighter(115));
      gradient.setColorAt(1, barColor);
    } else {
      gradient.setColorAt(0, QColor(140, 140, 140, 120));
      gradient.setColorAt(1, QColor(100, 100, 100, 120));
    }

    p.setBrush(gradient);

    // Add border for active bars
    if (i < net_strength) {
      p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    } else {
      p.setPen(QPen(QColor(160, 160, 160, 100), 1)); // Border for unfilled bars
    }

    p.drawRoundedRect(barX + i * (barWidth + spacing), barY - barHeight, barWidth, barHeight, 2, 2);
  }

  // Draw network type text at bottom left
  p.setFont(InterFont(20, QFont::DemiBold));
  p.setPen(QColor(200, 200, 200));
  QString networkTypeText = tethering_on ? "Hotspot" : net_type;
  p.drawText(rect.adjusted(20, 0, 0, -15), Qt::AlignLeft | Qt::AlignBottom, networkTypeText);
}

void SidebarBP::drawMetricBP(QPainter &p, const QString &label, const QString &mainValue, const QString &leftValue, const QString &rightValue, QColor c, int y, bool compactMode) {
  // Use larger card sizes for better visibility
  const QRect rect = {30, y, 280, compactMode ? 120 : 140}; // Increased width and height

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
    // 2-row layout (compact mode for CPU/GPU/Memory/GPS)
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

void SidebarBP::buildMetricCard(QPainter &p, const QString &label, const QString &mainValue,
                                const QString &leftValue, const QString &rightValue,
                                QColor color, int cardIndex, bool compactMode) {
  // Calculate consistent positioning and spacing
  const int cardSpacing = 20; // Consistent gap between all cards
  const int networkCardHeight = 130;

  // Calculate Y position starting after network card
  const int startY = 20 + networkCardHeight + cardSpacing; // Position after NETWORK card ends with consistent spacing

  // Calculate Y position: startY + (previous cards height + spacing)
  int yPosition = startY;
  for (int i = 0; i < cardIndex; i++) {
    int prevCardHeight = (i < 4) ? 120 : 140; // First 4 cards are compact (120px), rest are 140px
    yPosition += prevCardHeight + cardSpacing;
  }

  // Draw the metric card at calculated position
  drawMetricBP(p, label, mainValue, leftValue, rightValue, color, yPosition, compactMode);
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
  // Handle button presses for the new vertical column layout
  if (onroad && bp_home_btn.contains(event->pos())) {
    flag_pressed = true;
    update();
  } else if (bp_settings_btn.contains(event->pos())) {
    settings_pressed = true;
    update();
  } else if (recording_audio && mic_indicator_btn.contains(event->pos())) {
    mic_indicator_pressed = true;
    update();
  } else if (bp_debug_btn.contains(event->pos()) && onroad) {
    debug_pressed = true;
    update();
  } else if (memory_fan_btn.contains(event->pos())) {
    // Toggle between memory and fan display
    show_fan_instead_memory = !show_fan_instead_memory;
    update();
  }
}

void SidebarBP::mouseReleaseEvent(QMouseEvent *event) {
  if (flag_pressed || settings_pressed || mic_indicator_pressed || debug_pressed) {
    flag_pressed = settings_pressed = mic_indicator_pressed = debug_pressed = false;
    update();
  }

  // Handle debug button (new location)
  if (bp_debug_btn.contains(event->pos()) && onroad) {
    emit debugPanelRequested();
    return;
  }

  // Handle mic indicator button (when recording)
  if (recording_audio && mic_indicator_btn.contains(event->pos())) {
    emit openSettings(3, "RecordAudio");
    return;
  }

  // Handle settings button click directly
  if (bp_settings_btn.contains(event->pos())) {
    emit openSettings();
    return;
  }

  // Handle home/flag button for bookmark functionality (compatible with base Sidebar changes)
  if (onroad && bp_home_btn.contains(event->pos())) {
    // Let the base class handle the bookmark button message
    // This ensures compatibility with the userBookmark system
    Sidebar::mouseReleaseEvent(event);
    return;
  }

  // Let parent handle other events
  Sidebar::mouseReleaseEvent(event);
}

void SidebarBP::updateStateBP(const UIState &s) {
  if (!isVisible()) return;

  auto &sm = *(s.sm);
  auto deviceState = sm["deviceState"].getDeviceState();

  // Network status (from your original code)
  local_networking = local_networking ? local_networking : window()->findChild<Networking *>("");
  bool tethering_on = local_networking && local_networking->wifi->tethering_on;
  net_type = tethering_on ? "Hotspot" : network_type[deviceState.getNetworkType()];
  net_strength = tethering_on ? 4 : (int)deviceState.getNetworkStrength();
  net_strength = net_strength > 0 ? net_strength + 1 : 0;

  // Get carrier/SSID information
  if (tethering_on) {
    net_carrier_ssid = "Hotspot Active";
  } else if (deviceState.getNetworkType() == cereal::DeviceState::NetworkType::WIFI) {
    // For WiFi, try to get SSID from networking object
    if (local_networking && local_networking->wifi) {
      QString current_ssid = "";
      // Find the currently connected network
      for (const auto &network : local_networking->wifi->seenNetworks) {
        if (network.connected == ConnectedType::CONNECTED) {
          current_ssid = QString::fromUtf8(network.ssid);
          break;
        }
      }
      // If no connected network found, try to get from active access point
      if (current_ssid.isEmpty() && !local_networking->wifi->ipv4_address.isEmpty()) {
        // We have an IP address, so we're connected to something
        // Try to get the first available SSID
        if (!local_networking->wifi->seenNetworks.isEmpty()) {
          current_ssid = QString::fromUtf8(local_networking->wifi->seenNetworks.first().ssid);
        }
      }
      net_carrier_ssid = current_ssid.isEmpty() ? "Wi-Fi" : current_ssid;
    } else {
      net_carrier_ssid = "Wi-Fi";
    }
  } else if (deviceState.getNetworkType() >= cereal::DeviceState::NetworkType::CELL2_G &&
             deviceState.getNetworkType() <= cereal::DeviceState::NetworkType::CELL5_G) {
    // For cellular, get carrier name from network info and convert to readable name
    if (deviceState.hasNetworkInfo()) {
      auto netInfo = deviceState.getNetworkInfo();
      QString operatorCode = QString::fromStdString(netInfo.getOperator());
      net_carrier_ssid = getCarrierName(operatorCode); // Use the mapping function!
    } else {
      net_carrier_ssid = net_type;
    }
  } else {
    net_carrier_ssid = "No Connection";
  }

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

  // Recording audio status
  recording_audio = s.scene.recording_audio;
  setProperty("recordingAudio", recording_audio);

  // GPS satellite count - handle safely since services might not be running
  try {
    // Check if GPS services are available before trying to access them
    if (sm.alive("gpsLocation") && sm.updated("gpsLocation")) {
      auto gpsData = sm["gpsLocation"].getGpsLocation();
      gps_satellite_count = gpsData.getSatelliteCount();
    } else if (sm.alive("gpsLocationExternal") && sm.updated("gpsLocationExternal")) {
      auto gpsData = sm["gpsLocationExternal"].getGpsLocationExternal();
      gps_satellite_count = gpsData.getSatelliteCount();
    } else {
      // If no GPS services are alive, keep previous value or set to 0
      if (gps_satellite_count == 0) {
        gps_satellite_count = 0; // Already 0, no change needed
      }
    }
  } catch (const std::exception& e) {
    // GPS service not available or error occurred, set to 0
    gps_satellite_count = 0;
  }

  setProperty("gpsSatelliteCount", gps_satellite_count);

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

  // Update properties so QML/Qt can access them
  setProperty("cpuTemp", cpu_temp);
  setProperty("cpuUsage", cpu_usage);
  setProperty("gpuTemp", gpu_temp);
  setProperty("gpuUsage", gpu_usage);
  setProperty("memoryUsage", memory_usage);
  setProperty("fanDemand", fan_demand);

  // Trigger UI update
  emit valueChanged();
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

  // Draw custom network card
  drawNetworkCard(p);

  // Draw microphone recording indicator if recording
  if (recording_audio) {
    // Position mic button near the network card for better visibility
    const int buttonSize = 60; // Even smaller size to fit better
    const int micX = 30 + 280 - buttonSize - 20; // Position on the right side of network card (30 + 280 = network card right edge)
    const int micY = 20 + 130 + 20; // Position below network card (20 + 130 + 20 = 170px from top)

    mic_indicator_btn = QRect(micX, micY, buttonSize, buttonSize);

    // Draw mic button with red background to indicate recording is active
    p.setPen(QPen(QColor(255, 255, 255, 80), 2));
    p.setBrush(mic_indicator_pressed ? danger_color.darker(120) : danger_color);
    p.drawEllipse(mic_indicator_btn);

    // Draw mic icon with appropriate scaling
    const float scale = 0.7; // Scale for smaller button
    p.setOpacity(mic_indicator_pressed ? 0.65 : 1.0);
    p.drawPixmap(mic_indicator_btn.x() + (mic_indicator_btn.width() - mic_img.width() * scale) / 2,
                 mic_indicator_btn.y() + (mic_indicator_btn.height() - mic_img.height() * scale) / 2,
                 mic_img.scaled(mic_img.width() * scale, mic_img.height() * scale, Qt::KeepAspectRatio));
    p.setOpacity(1.0);
  }

  // Draw metrics with modern styling - new order, make cards larger

  // CPU card with compact layout, larger size (120px height)
  QColor cpuColor = good_color;
  // Make a temporary copy for value comparison
  float cpuTempValue = cpu_temp.mid(0, cpu_temp.length() - 2).toFloat();
  if (cpuTempValue > 80.0) {
    cpuColor = danger_color;
  } else if (cpuTempValue > 70.0) {
    cpuColor = warning_color;
  }
  buildMetricCard(p, tr("CPU"), tr(""), cpu_usage, cpu_temp, cpuColor, 0, true);

  // GPU card, larger size (120px height)
  QColor gpuColor = good_color;
  if (gpu_temp != "N/A") {
    float gpuTempValue = gpu_temp.mid(0, gpu_temp.length() - 2).toFloat();
    if (gpuTempValue > 75.0) {
      gpuColor = danger_color;
    } else if (gpuTempValue > 65.0) {
      gpuColor = warning_color;
    }
  }
  buildMetricCard(p, tr("GPU"), tr(""), gpu_usage, gpu_temp, gpuColor, 1, true);

  // Memory/Fan card with toggle functionality, larger size (120px height)
  QColor memoryFanColor = good_color;
  if (show_fan_instead_memory) {
    // Show fan demand
    float fanDemandValue = fan_demand.mid(0, fan_demand.length() - 1).toFloat();
    if (fanDemandValue > 80.0) {
      memoryFanColor = danger_color;
    } else if (fanDemandValue > 60.0) {
      memoryFanColor = warning_color;
    }
    buildMetricCard(p, tr("FAN"), tr(""), fan_demand, tr(""), memoryFanColor, 2, true);
  } else {
    // Show memory as before
    float memoryValue = memory_usage.mid(0, memory_usage.length() - 1).toFloat();
    if (memoryValue > 85) {
      memoryFanColor = danger_color;
    } else if (memoryValue > 70) {
      memoryFanColor = warning_color;
    }
    buildMetricCard(p, tr("MEMORY"), tr(""), memory_usage, tr(""), memoryFanColor, 2, true);
  }

  // GPS card - new addition (120px height)
  QColor gpsColor = good_color;
  QString gpsValue = QString("%1").arg(gps_satellite_count);
  QString gpsLabel = tr("SAT");

  if (gps_satellite_count == 0) {
    gpsColor = danger_color;
    gpsValue = tr("NO GPS");
    gpsLabel = tr("");
  } else if (gps_satellite_count < 4) {
    gpsColor = danger_color;
  } else if (gps_satellite_count < 6) {
    gpsColor = warning_color;
  }

  buildMetricCard(p, tr("GPS"), tr(""), gpsValue, gpsLabel, gpsColor, 3, true);

  // Vehicle card - standard 3-row layout, larger size (140px height)
  buildMetricCard(p, tr("VEHICLE"), panda_status.first.second, tr(""), tr(""), panda_status.second, 4, false);

  // Connect card - standard 3-row layout, larger size (140px height)
  buildMetricCard(p, tr("CONNECT"), connect_status.first.second, tr(""), tr(""), connect_status.second, 5, false);

  // SunnyLink card - standard 3-row layout, larger size (140px height)
  if (property("sunnylinkStatus").isValid()) {
    ItemStatus sunnylink_status = property("sunnylinkStatus").value<ItemStatus>();
    // Position it with consistent spacing
    buildMetricCard(p, tr("SUNNYLINK"), sunnylink_status.first.second, tr(""), tr(""), sunnylink_status.second, 6, false);
  }

  // Buttons in vertical column on the right side, stacked from bottom to top
  const int buttonSize = 100; // Larger buttons for better visibility
  const int buttonSpacing = 20; // Spacing between buttons
  const int rightMargin = 25;
  const int buttonX = width() - buttonSize - rightMargin;

  // Create horizontal gradient background that transitions after the cards
  // Draw horizontal gradient from cards area to button area
  QLinearGradient sidebarGradient(0, 0, width(), 0);
  sidebarGradient.setColorAt(0, background_color); // Original sidebar color for cards area
  sidebarGradient.setColorAt(0.7, background_color); // Keep original color until 70% of width
  sidebarGradient.setColorAt(0.8, QColor(70, 70, 80, 100)); // Start transition to subtle dark blue-grey
  sidebarGradient.setColorAt(1.0, QColor(80, 85, 95, 140)); // Subtle dark blue-grey for button area

  // Apply gradient to the right side of the sidebar (where buttons are)
  // Start gradient transition after the cards end
  int gradientStartX = width() * 0.7; // Start gradient at 70% of sidebar width
  p.setPen(Qt::NoPen);
  p.setBrush(sidebarGradient);
  p.drawRect(gradientStartX, 0, width() - gradientStartX, height());

  // Start from bottom and stack upwards
  int currentButtonY = height() - buttonSize - 30; // Bottom button

  // Settings button (bottom)
  bp_settings_btn = QRect(buttonX, currentButtonY, buttonSize, buttonSize);

  // Draw settings button background and border with different background
  p.setPen(QPen(QColor(255, 255, 255, 80), 2));
  p.setBrush(settings_pressed ? QColor(100, 110, 130, 180) : QColor(100, 110, 130, 140)); // Muted blue-grey background
  p.drawEllipse(bp_settings_btn);

  // Draw settings icon (centered with proper scaling for larger button)
  const float settings_scale = 0.55; // Adjusted for 100px button
  p.setOpacity(settings_pressed ? 0.65 : 1.0);
  p.drawPixmap(bp_settings_btn.x() + (bp_settings_btn.width() - settings_img.width() * settings_scale) / 2,
               bp_settings_btn.y() + (bp_settings_btn.height() - settings_img.height() * settings_scale) / 2,
               settings_img.scaled(settings_img.width() * settings_scale, settings_img.height() * settings_scale, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  p.setOpacity(1.0);

  // Move up for next button
  currentButtonY -= (buttonSize + buttonSpacing);

  // Debug button (above settings, only onroad)
  if (onroad) {
    bp_debug_btn = QRect(buttonX, currentButtonY, buttonSize, buttonSize);

    p.setPen(QPen(QColor(255, 255, 255, 80), 2));
    p.setBrush(debug_pressed ? QColor(100, 110, 130, 180) : QColor(100, 110, 130, 140)); // Muted blue-grey background
    p.drawEllipse(bp_debug_btn);

    // Draw debug icon (scaled and centered for larger button)
    float debug_scale = 0.65; // Adjusted for 100px button
    p.setOpacity(debug_pressed ? 0.65 : 1.0);
    p.drawPixmap(bp_debug_btn.x() + (bp_debug_btn.width() - debug_img.width() * debug_scale) / 2,
                 bp_debug_btn.y() + (bp_debug_btn.height() - debug_img.height() * debug_scale) / 2,
                 debug_img.scaled(debug_img.width() * debug_scale, debug_img.height() * debug_scale, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    p.setOpacity(1.0);

    // Move up for next button
    currentButtonY -= (buttonSize + buttonSpacing);
  }

  // Home/Flag button (top, only when onroad)
  if (onroad) {
    bp_home_btn = QRect(buttonX, currentButtonY, buttonSize, buttonSize);

    // Draw button background with different background
    p.setPen(QPen(QColor(255, 255, 255, 80), 2));
    p.setBrush(flag_pressed ? QColor(100, 110, 130, 180) : QColor(100, 110, 130, 140)); // Muted blue-grey background
    p.drawEllipse(bp_home_btn);

    // Draw flag icon with proper scaling to fit nicely in the circular button
    float flag_scale = 0.45; // Smaller scale for flag to fit properly in circle
    p.setOpacity(flag_pressed ? 0.65 : 1.0);
    p.drawPixmap(bp_home_btn.x() + (bp_home_btn.width() - flag_img.width() * flag_scale) / 2,
                 bp_home_btn.y() + (bp_home_btn.height() - flag_img.height() * flag_scale) / 2,
                 flag_img.scaled(flag_img.width() * flag_scale, flag_img.height() * flag_scale, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    p.setOpacity(1.0);
  }
}
