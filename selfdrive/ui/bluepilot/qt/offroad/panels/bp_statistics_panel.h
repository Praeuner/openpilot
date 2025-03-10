// selfdrive/ui/bluepilot/qt/offroad/panels/bp_statistics_panel.h

#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QProcess>
#include <QRegExp>
#include <QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QTableWidget>
#include <QHeaderView>
#include <QPainter>
#include <QStorageInfo>
#include <QHash>
#include <QMap>
#include <QProgressBar>
#include <QScrollArea>
#include <QProgressBar>
#include <QFrame>

class SystemStatCard;
class ConnectivityStatCard;
class StorageStatCard;

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#include <QPlatformSurfaceEvent>
#endif

struct GradientStop {
  float value;
  QVector<QString> colors;

  GradientStop() : value(0.0f) {}
  GradientStop(float v, QVector<QString> c) : value(v), colors(c) {}
};

inline QVector<GradientStop> DefaultGradientThreeColor() { return {{60.0f, {"#9ece6a", "#73c748"}}, {80.0f, {"#e0af68", "#ff9e43"}}, {100.0f, {"#f7768e", "#ff5555"}}}; }

inline QVector<GradientStop> DetailedGradientFiveColor() {
  return {{20.0f, {"#9ece6a", "#73c748", "#68b83f"}},
          {40.0f, {"#73d945", "#68c83f", "#5fb838"}},
          {60.0f, {"#e0af68", "#ff9e43", "#ff8f20"}},
          {80.0f, {"#f7768e", "#ff5555", "#ff3333"}},
          {100.0f, {"#ff0000", "#dd0000", "#bb0000"}}};
}
//-------------------------
// Data Structures
//-------------------------

struct ProcessInfo {
  QString name;
  float cpu;
  bool operator<(const ProcessInfo &other) const { return cpu > other.cpu; }
};

struct SystemMetrics {
  float cpuPercent;
  QList<ProcessInfo> topProcesses;
  qint64 memTotal;
  qint64 memUsed;
  qint64 memFree;
  qint64 swapTotal;
  qint64 swapUsed;
  qint64 swapFree;
};

struct NetworkStats {
  QString interface;
  QString ipAddress;
  qint64 rxBytes;
  qint64 txBytes;
  double rxSpeed;
  double txSpeed;
};

class BPStatisticsPanel : public QWidget {
  Q_OBJECT

public:
  explicit BPStatisticsPanel(QWidget *parent = nullptr);
  ~BPStatisticsPanel();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void paintEvent(QPaintEvent *) override;
  bool eventFilter(QObject *obj, QEvent *event) override {
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate || event->type() == QEvent::TouchEnd) {
      return false; // Let touch events pass through
    }
    return QWidget::eventFilter(obj, event);
  }

private:
  SystemStatCard *systemCard;
  ConnectivityStatCard *connectivityCard;
  StorageStatCard *storageCard;

  void setupUI();
  bool keepScreenAwake = true;
  bool isVisible = false;
  QTimer *activityTimer = nullptr;
  void simulateActivity();
};

/**
 * Simple pie chart widget for memory usage (used vs. free).
 */
class PieChartWidget : public QWidget {
  Q_OBJECT
public:
  explicit PieChartWidget(QWidget *parent = nullptr) : QWidget(parent) { setMinimumSize(150, 150); }

  void setValues(qint64 usedBytes, qint64 freeBytes) {
    used = usedBytes;
    free = freeBytes;
    total = used + free;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    float startAngle = 0.0f;
    float usedPercent = (total > 0) ? (static_cast<float>(used) / total) : 0.0f;

    // Draw pie slices
    QRectF circleRect(10, 10, width() - 20, height() - 20);
    // Free slice
    painter.setBrush(QColor("#9ece6a"));
    painter.setPen(Qt::NoPen);
    painter.drawPie(circleRect, static_cast<int>(startAngle * 16), static_cast<int>((1.0f - usedPercent) * 360.0f * 16));

    // Used slice
    painter.setBrush(QColor("#f7768e"));
    painter.drawPie(circleRect, static_cast<int>((startAngle + (1.0f - usedPercent) * 360.0f) * 16), static_cast<int>(usedPercent * 360.0f * 16));

    // Text in middle
    painter.setPen(QColor("#ffffff"));
    QString text = QString("%1%").arg(QString::number(usedPercent * 100.0f, 'f', 1));
    QFont f = painter.font();
    f.setBold(true);
    f.setPointSize(14);
    painter.setFont(f);
    painter.drawText(circleRect, Qt::AlignCenter, text);
  }

private:
  qint64 used = 0;
  qint64 free = 0;
  qint64 total = 0;
};

// BPial Design bar gauge
class BPBarGauge : public QWidget {
  Q_OBJECT
public:
  explicit BPBarGauge(QWidget *parent = nullptr);
  void setValue(float value);
  void setGradientStops(const QVector<GradientStop> &stops);

protected:
  void paintEvent(QPaintEvent *) override;

private:
  float currentValue = 0.0f;
  QVector<GradientStop> gradientStops;
  QString generateGradientStyle();
};

// BP styled progress bar with material design
class BPProgressBar : public QWidget {
  Q_OBJECT
public:
  explicit BPProgressBar(QWidget *parent = nullptr);
  void setValue(double value);
  void setGradient(const QLinearGradient &gradient);

protected:
  void paintEvent(QPaintEvent *) override;

private:
  double currentValue = 0;
  QLinearGradient gradient;
};

class StatCardBase : public QWidget {
  Q_OBJECT

public:
  explicit StatCardBase(const QString &title, const QString &desc = "", const QString &icon = "", QWidget *parent = nullptr);
  virtual void refresh() = 0;
  virtual void startUpdates();
  virtual void stopUpdates();

protected:
  QVBoxLayout *mainLayout = nullptr;
  QVBoxLayout *contentLayout = nullptr;
  QTimer *updateTimer = nullptr;
  bool isUpdating = false;
  QFrame *container = nullptr;
  bool initialized = false;
  bool isVisible = false;
  QList<QProcess *> activeProcesses;
  QLabel *titleLabel = nullptr;
  QLabel *descLabel = nullptr;

  QString formatBytes(qint64 bytes);
  QString formatPercentage(float percentage);
  QLabel *createDataLabel(const QString &text, int fontSize, const QString &color);
  QLabel *createDescriptionLabel(const QString &text, int fontSize, const QString &color);
  QLabel *createHeaderLabel(const QString &text, int fontSize, const QString &color);
  void updateCardStyle();

  template <typename T> void runAsync(std::function<T()> work, std::function<void(T)> callback) {
    QFutureWatcher<T> *watcher = new QFutureWatcher<T>(this);
    connect(watcher, &QFutureWatcher<T>::finished, [=]() {
      callback(watcher->result());
      watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(work));
  }

  void showEvent(QShowEvent *event) override {
    isVisible = true;
    if (isUpdating) {
      refresh();
      updateTimer->start();
    }
    QWidget::showEvent(event);
  }

  void hideEvent(QHideEvent *event) override {
    isVisible = false;
    if (updateTimer->isActive()) {
      updateTimer->stop();
    }
    QWidget::hideEvent(event);
  }

protected slots:
  virtual void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
  void cleanupProcesses();
};

//-----------------------------------
// SystemStatCard
//-----------------------------------
class SystemStatCard : public StatCardBase {
  Q_OBJECT
public:
  explicit SystemStatCard(QWidget *parent = nullptr);
  void refresh() override;

private:
  // CPU gauge and process tables
  BPBarGauge *cpuGauge = nullptr;
  QTableWidget *createProcessTable();
  QTableWidget *leftProcessTable = nullptr;
  QTableWidget *rightProcessTable = nullptr;

  // Memory usage
  BPBarGauge *memoryGauge = nullptr;
  QLabel *memDetailsLabel = nullptr;

  // Data methods
  SystemMetrics gatherMetrics();
  void updateDisplay(const SystemMetrics &metrics);
};

//-----------------------------------
// ConnectivityStatCard
//-----------------------------------
class ConnectivityStatCard : public StatCardBase {
  Q_OBJECT

public:
  explicit ConnectivityStatCard(QWidget *parent = nullptr);
  void refresh() override;

private:
  // WiFi Info Labels
  QLabel *wifiHeaderLabel;
  QLabel *wifiSSIDLabel;
  QLabel *wifiSignalStrengthLabel;
  QLabel *wifiIPAddressLabel;
  QLabel *wifiRXSpeedLabel;
  QLabel *wifiTXSpeedLabel;
  BPProgressBar *wifiSignalBar;

  // Cellular Info Labels
  QLabel *cellularHeaderLabel;
  QLabel *signalStrengthLabel;
  QLabel *carrierLabel;
  QLabel *networkTypeLabel;
  QLabel *registrationStateLabel;
  BPProgressBar *cellSignalBar;

  struct ConnectivityInfo {
    // Cellular Information
    struct CellularInfo {
      int signalStrength = -1;
      QString signalQuality = "Unknown";
      QString carrier = "Unknown";
      QString networkType = "UNKNOWN";
      QString registrationState = "Unknown";
    } cellular;

    // WiFi Information
    struct WiFiInfo {
      QString ssid = "Unknown";
      int signalStrength = -1;
      QString signalQuality = "Unknown";
      QString ipAddress = "N/A";
      double rxSpeed = 0.0;
      double txSpeed = 0.0;
    } wifi;

    // Network Interfaces
    struct NetworkInterface {
      QString name;
      QString ipAddress;
      double rxSpeed;
      double txSpeed;
    };
    QList<NetworkInterface> interfaces;

    // Previous stats for speed calculation
    qint64 lastRxBytes = 0;
    qint64 lastTxBytes = 0;
  };

  ConnectivityInfo gatherConnectivityInfo();
  void updateDisplay(const ConnectivityInfo &info);
};

class StorageStatCard : public StatCardBase {
  Q_OBJECT

public:
  explicit StorageStatCard(QWidget *parent = nullptr);
  void refresh() override;
  ~StorageStatCard();

private:
  QHash<QString, QFrame *> deviceFrames;
  QHash<QString, BPBarGauge *> gauges;
  QHash<QString, QLabel *> detailLabels;
  QHash<QString, QLabel *> mountLabels;
  QProcess *storageProcess;

  struct StorageInfo {
    QString name;
    QString mountPoint;
    qint64 total;
    qint64 used;
    qint64 free;
    float usagePercent;
  };

  StorageInfo parseStorageInfo(const QString &name, const QStorageInfo &storage);
  QList<StorageInfo> storageDevices;

private slots:
  void handleStorageDataReady();
  void updateStorageDisplay(const QList<StorageInfo> &devices);
};

// Factory class for creating statistics cards
class StatCardFactory {
public:
  static StatCardBase *createStatsCard(const QString &type, QWidget *parent = nullptr);
};
