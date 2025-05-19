#pragma once

#include <memory>

#include <QFrame>
#include <QMap>
#include <QPropertyAnimation>

#include "selfdrive/ui/qt/network/networking.h"

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui.h"
#else
#include "selfdrive/ui/ui.h"
#endif

typedef QPair<QPair<QString, QString>, QColor> ItemStatus;
Q_DECLARE_METATYPE(ItemStatus);

class Sidebar : public QFrame {
  Q_OBJECT
  Q_PROPERTY(qreal hover_opacity MEMBER hover_opacity NOTIFY valueChanged);
  Q_PROPERTY(ItemStatus connectStatus MEMBER connect_status NOTIFY valueChanged);
  Q_PROPERTY(ItemStatus pandaStatus MEMBER panda_status NOTIFY valueChanged);
  Q_PROPERTY(ItemStatus tempStatus MEMBER temp_status NOTIFY valueChanged);
  Q_PROPERTY(ItemStatus gpuStatus MEMBER gpu_status NOTIFY valueChanged);
  Q_PROPERTY(ItemStatus memoryStatus MEMBER memory_status NOTIFY valueChanged);
  Q_PROPERTY(ItemStatus networkStatus MEMBER network_status NOTIFY valueChanged);
  Q_PROPERTY(QString netType MEMBER net_type NOTIFY valueChanged);
  Q_PROPERTY(int netStrength MEMBER net_strength NOTIFY valueChanged);
  Q_PROPERTY(QString cpuTemp MEMBER cpu_temp NOTIFY valueChanged);
  Q_PROPERTY(QString cpuUsage MEMBER cpu_usage NOTIFY valueChanged);
  Q_PROPERTY(QString gpuTemp MEMBER gpu_temp NOTIFY valueChanged);
  Q_PROPERTY(QString gpuUsage MEMBER gpu_usage NOTIFY valueChanged);
  Q_PROPERTY(QString memoryUsage MEMBER memory_usage NOTIFY valueChanged);

public:
  explicit Sidebar(QWidget *parent = 0);
  ~Sidebar();

signals:
  void openSettings(int index = 0, const QString &param = "");
  void valueChanged();
  void debugPanelRequested();

public slots:
  void offroadTransition(bool offroad);
  void updateState(const UIState &s);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void enterEvent(QEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void drawMetric(QPainter &p, const QString &label, const QString &mainValue, const QString &leftValue, const QString &rightValue, QColor c, int y, bool compactMode);
  virtual void drawSidebar(QPainter &p);
  void drawProgressBar(QPainter &p, int x, int y, int width, int height, float percentage, QColor color);

  QPixmap home_img, flag_img, settings_img;
  bool onroad, flag_pressed, settings_pressed;
  const QMap<cereal::DeviceState::NetworkType, QString> network_type = {{cereal::DeviceState::NetworkType::NONE, tr("--")},
                                                                        {cereal::DeviceState::NetworkType::WIFI, tr("Wi-Fi")},
                                                                        {cereal::DeviceState::NetworkType::ETHERNET, tr("ETH")},
                                                                        {cereal::DeviceState::NetworkType::CELL2_G, tr("2G")},
                                                                        {cereal::DeviceState::NetworkType::CELL3_G, tr("3G")},
                                                                        {cereal::DeviceState::NetworkType::CELL4_G, tr("LTE")},
                                                                        {cereal::DeviceState::NetworkType::CELL5_G, tr("5G")}};

  // Button rectangles will be set dynamically in drawSidebar
  QRect settings_btn;
  QRect home_btn;

  // Button dimensions - increased by 25%
  const QSize settings_btn_size = QSize(150, 150);
  const QSize flag_btn_size = QSize(150, 150);
  const QSize home_btn_size = QSize(150, 150);

  // Updated temp_btn to match new size
  QRect temp_btn = QRect(35, 165, 290, 130);

  const QColor good_color = QColor(42, 199, 122);
  const QColor warning_color = QColor(255, 195, 0);
  const QColor danger_color = QColor(242, 72, 85);
  const QColor background_color = QColor(32, 33, 35);
  const QColor card_background = QColor(48, 49, 51);
  const QColor accent_color = QColor(24, 144, 255);

  ItemStatus connect_status, panda_status, temp_status, gpu_status, memory_status, network_status;
  QString net_type;
  int net_strength = 0;
  QString cpu_temp = "0°C";
  QString cpu_usage = "0%";
  QString gpu_temp = "0°C";
  QString gpu_usage = "0%";
  QString memory_usage = "0%";

  // Hover effect
  bool is_hovering = false;
  QPropertyAnimation *hover_animation = nullptr;
  qreal hover_opacity = 0.0;

  // Performance metrics refresh control
  int metrics_refresh_counter = 0;
  const int METRICS_REFRESH_INTERVAL = 20; // Update metrics every 20 UI updates

private:
  float getGpuUsage();
  float getGpuTemperature();
  float getMemoryUsage();
  float getCpuUsage();
  std::unique_ptr<PubMaster> pm;
  Networking *networking = nullptr;
};