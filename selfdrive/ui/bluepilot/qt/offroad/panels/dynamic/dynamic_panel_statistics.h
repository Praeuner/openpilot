// selfdrive/ui/bluepilot/qt/offroad/panels/dynamic/dynamic_panel_statistics.h

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

#include "selfdrive/ui/qt/widgets/controls.h"
#include "dynamic_panel_components.h"

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

class StatCardBase : public AbstractControl {
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
  QList<QProcess*> activeProcesses;

  QString formatBytes(qint64 bytes);
  QString formatPercentage(float percentage);
  QLabel* createDataLabel(const QString &text, int fontSize, const QString &color);
  QLabel* createDescriptionLabel(const QString &text, int fontSize, const QString &color);
  QLabel* createHeaderLabel(const QString &text, int fontSize, const QString &color);
  void updateCardStyle();

  template<typename T>
  void runAsync(std::function<T()> work, std::function<void(T)> callback) {
    QFutureWatcher<T>* watcher = new QFutureWatcher<T>(this);
    connect(watcher, &QFutureWatcher<T>::finished, [=]() {
      callback(watcher->result());
      watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(work));
  }

  // Override QWidget's visibility events
  void showEvent(QShowEvent* event) override {
    isVisible = true;
    if (isUpdating) {
      refresh();  // Initial refresh when becoming visible
      updateTimer->start();
    }
    QWidget::showEvent(event);
  }

  void hideEvent(QHideEvent* event) override {
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



/**
 * Simple pie chart widget for memory usage (used vs. free).
 */
class PieChartWidget : public QWidget {
  Q_OBJECT
public:
  explicit PieChartWidget(QWidget *parent = nullptr) : QWidget(parent) {
    setMinimumSize(150, 150);
  }

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

    // Draw the pie slices
    QRectF circleRect(10, 10, width() - 20, height() - 20);
    // Free slice
    painter.setBrush(QColor("#9ece6a"));
    painter.setPen(Qt::NoPen);
    painter.drawPie(circleRect, static_cast<int>(startAngle * 16),
                    static_cast<int>((1.0f - usedPercent) * 360.0f * 16));

    // Used slice
    painter.setBrush(QColor("#f7768e"));
    painter.drawPie(circleRect,
                    static_cast<int>((startAngle + (1.0f - usedPercent) * 360.0f) * 16),
                    static_cast<int>(usedPercent * 360.0f * 16));

    // Text in the middle
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

//-----------------------------------
// SystemStatCard
//-----------------------------------
class SystemStatCard : public StatCardBase {
  Q_OBJECT
public:
  explicit SystemStatCard(QWidget *parent = nullptr);
  void refresh() override;

private:
  // CPU gauge
  DynamicPanelComponents::BarGauge *cpuGauge = nullptr;
  QTableWidget* createProcessTable();
  QTableWidget* leftProcessTable = nullptr;
  QTableWidget* rightProcessTable = nullptr;

  // Memory usage bar gauge
  DynamicPanelComponents::BarGauge *memoryGauge = nullptr;

  // Memory textual details
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
    // Wi-Fi Info Labels
    QLabel *wifiHeaderLabel;
    QLabel *wifiSSIDLabel;
    QLabel *wifiSignalStrengthLabel;
    QLabel *wifiIPAddressLabel;
    QLabel *wifiRXSpeedLabel;
    QLabel *wifiTXSpeedLabel;

    // Cellular Info Labels
    QLabel *cellularHeaderLabel;
    QLabel *signalStrengthLabel;
    QLabel *carrierLabel;
    QLabel *networkTypeLabel;
    QLabel *registrationStateLabel;

    // Data Structures
    struct ConnectivityInfo {
        // Cellular Information
        struct CellularInfo {
            int signalStrength = -1;
            QString signalQuality = "Unknown";
            QString carrier = "Unknown";
            QString networkType = "UNKNOWN";
            QString registrationState = "Unknown";
        } cellular;

        // Wi-Fi Information
        struct WiFiInfo {
            QString ssid = "Unknown";
            int signalStrength = -1;
            QString signalQuality = "Unknown";
            QString ipAddress = "N/A";
            double rxSpeed = 0.0; // in MB/s
            double txSpeed = 0.0; // in MB/s
        } wifi;

        // Network Interfaces
        struct NetworkInterface {
            QString name;
            QString ipAddress;
            double rxSpeed; // in MB/s
            double txSpeed; // in MB/s
        };
        QList<NetworkInterface> interfaces;
    };

    ConnectivityInfo gatherConnectivityInfo();
    void updateDisplay(const ConnectivityInfo &info);
};

class StorageStatCard : public StatCardBase {
    Q_OBJECT

public:
    explicit StorageStatCard(QWidget *parent = nullptr);
    void refresh() override;

private:
    QHash<QString, QFrame*> deviceFrames; // Cache for device frames
    QHash<QString, DynamicPanelComponents::BarGauge*> gauges; // Cache for gauges
    QHash<QString, QLabel*> detailLabels; // Cache for detail labels
    QHash<QString, QLabel*> mountLabels;
    QLabel *storageUsageLabel;
    QLabel *storageDetailsLabel;
    QProcess *storageProcess;

    ~StorageStatCard();

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
