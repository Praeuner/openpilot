#pragma once

#include "selfdrive/ui/qt/sidebar.h"
#include "selfdrive/ui/bluepilot/qt/onroad/onroad_controls_debug_panel.h"
#include <QPropertyAnimation>
#include <memory>
#include "cereal/messaging/messaging.h"

class SidebarBP : public Sidebar {
  Q_OBJECT
  Q_PROPERTY(qreal hover_opacity MEMBER hover_opacity NOTIFY valueChanged);
  Q_PROPERTY(QString cpuTemp MEMBER cpu_temp NOTIFY valueChanged);
  Q_PROPERTY(QString cpuUsage MEMBER cpu_usage NOTIFY valueChanged);
  Q_PROPERTY(QString gpuTemp MEMBER gpu_temp NOTIFY valueChanged);
  Q_PROPERTY(QString gpuUsage MEMBER gpu_usage NOTIFY valueChanged);
  Q_PROPERTY(QString memoryUsage MEMBER memory_usage NOTIFY valueChanged);
  Q_PROPERTY(QString fanDemand MEMBER fan_demand NOTIFY valueChanged);
  Q_PROPERTY(bool recordingAudio MEMBER recording_audio NOTIFY valueChanged);
  Q_PROPERTY(int gpsSatelliteCount MEMBER gps_satellite_count NOTIFY valueChanged);

public:
  explicit SidebarBP(QWidget *parent = 0);
  ~SidebarBP();

signals:
  void debugPanelRequested();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void enterEvent(QEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void drawSidebar(QPainter &p) override;
  void drawNetworkCard(QPainter &p); // New method for custom network card
  void drawMetricBP(QPainter &p, const QString &label, const QString &mainValue, const QString &leftValue, const QString &rightValue, QColor c, int y, bool compactMode);
  void buildMetricCard(QPainter &p, const QString &label, const QString &mainValue, const QString &leftValue, const QString &rightValue, QColor color, int cardIndex, bool compactMode);
  void drawProgressBar(QPainter &p, int x, int y, int width, int height, float percentage, QColor color);

public slots:
  void updateStateBP(const UIState &s);

private:
  // Button images
  QPixmap home_img, flag_img, settings_img, mic_img, debug_img; // Added debug_img

  // Modern sidebar styling
  const QColor good_color = QColor(42, 199, 122);
  const QColor warning_color = QColor(255, 195, 0);
  const QColor danger_color = QColor(242, 72, 85);
  const QColor background_color = QColor(32, 33, 35);
  const QColor card_background = QColor(48, 49, 51);
  const QColor accent_color = QColor(24, 144, 255);

  // CPU card area for debug panel trigger
  const QRect cpu_card_area = QRect(30, 140, 240, 110);
  QRect memory_fan_btn = QRect(30, 360, 240, 100);

  // Recording audio button
  QRect mic_indicator_btn;
  bool recording_audio = false;
  bool mic_indicator_pressed = false;

  // Modern metrics
  QString cpu_temp = "0°C";
  QString cpu_usage = "0%";
  QString gpu_temp = "0°C";
  QString gpu_usage = "0%";
  QString memory_usage = "0%";
  QString fan_demand = "0%";
  bool show_fan_instead_memory = false;
  int gps_satellite_count = 0;

  // Hover animation
  bool is_hovering = false;
  QPropertyAnimation *hover_animation = nullptr;
  qreal hover_opacity = 0.0;

  // Performance metrics refresh control
  int metrics_refresh_counter = 0;
  const int METRICS_REFRESH_INTERVAL = 20;

  // Network data
  QString net_type;
  int net_strength = 0;
  QString net_carrier_ssid; // Store carrier name or WiFi SSID
  Networking *local_networking = nullptr;

  const QMap<cereal::DeviceState::NetworkType, QString> network_type = {
    {cereal::DeviceState::NetworkType::NONE, tr("--")},
    {cereal::DeviceState::NetworkType::WIFI, tr("Wi-Fi")},
    {cereal::DeviceState::NetworkType::ETHERNET, tr("ETH")},
    {cereal::DeviceState::NetworkType::CELL2_G, tr("2G")},
    {cereal::DeviceState::NetworkType::CELL3_G, tr("3G")},
    {cereal::DeviceState::NetworkType::CELL4_G, tr("LTE")},
    {cereal::DeviceState::NetworkType::CELL5_G, tr("5G")}
  };

  ItemStatus connect_status, panda_status, temp_status, gpu_status, memory_status, network_status;

  // Local button rectangles (since base class ones are const)
  QRect bp_settings_btn;
  QRect bp_home_btn;
  QRect bp_debug_btn; // Added debug button rect
  bool debug_pressed = false; // Added debug button pressed state
};
