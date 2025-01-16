// selfdrive/ui/bluepilot/qt/offroad/panels/dynamic/dynamic_panel_statistics.cc

#include "dynamic_panel_statistics.h"
#include "dynamic_panel_components.h"
#include <QFile>
#include <iostream>
#include <QTextStream>
#include <QDir>
#include <QRandomGenerator>
#include <QProcess>
#include <QProgressBar>
#include <QStorageInfo>
#include <unistd.h>

//---------------------------------------
// StatCardBase Implementation
//---------------------------------------
StatCardBase::StatCardBase(const QString &title, const QString &desc, const QString &icon, QWidget *parent)
  : AbstractControl(title, desc, icon, parent) {

  // Remove the toggle from AbstractControl since we don't need it
  if (hlayout) {
    QLayoutItem* item;
    try {
      while ((item = hlayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
          item->widget()->hide();
          item->widget()->deleteLater();
        }
        delete item;
      }
    } catch (std::exception& e) {
      qDebug() << "Exception in StatCardBase constructor: " << e.what();
    }
  }

  // Create our own layout
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Title section
  if (!title.isEmpty()) {
    QLabel* titleLabel = new QLabel(title, this);
    titleLabel->setAttribute(Qt::WA_TranslucentBackground);
    titleLabel->setAttribute(Qt::WA_NoSystemBackground);
    titleLabel->setStyleSheet("font-size: 42px; font-weight: bold; color: #ffffff;");
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    mainLayout->addWidget(titleLabel);
  }

  // Description section
  if (!desc.isEmpty()) {
    QLabel* descLabel = new QLabel(desc, this);
    descLabel->setAttribute(Qt::WA_TranslucentBackground);
    descLabel->setAttribute(Qt::WA_NoSystemBackground);
    descLabel->setStyleSheet("font-size: 28px; color: #888888;");
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);
  }

  // Create container frame for stats
  container = new QFrame(this);
  container->setObjectName("statsCard");

  // Create content layout
  contentLayout = new QVBoxLayout(container);
  contentLayout->setSpacing(10);
  contentLayout->setContentsMargins(10, 10, 10, 10);

  mainLayout->addWidget(container);

  // Set up the update timer
  updateTimer = new QTimer(this);
  updateTimer->setInterval(2000);
  connect(updateTimer, &QTimer::timeout, this, &StatCardBase::refresh);

  initialized = true;
}

void StatCardBase::startUpdates() {
  if (!initialized) {
    qWarning() << "Cannot start updates - card not initialized";
    return;
  }
  isUpdating = true;
  if (isVisible) {
    refresh();
    updateTimer->start();
  }
}

void StatCardBase::stopUpdates() {
  if (isUpdating) {
    updateTimer->stop();
    isUpdating = false;
    cleanupProcesses();
  }
}

void StatCardBase::updateCardStyle() {
  container->setStyleSheet(R"(
    #statsCard {
      /* background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 #1a1b26, stop:1 #24283b); */
      background: rgba(80, 80, 80, 0.3);
      border-radius: 20px;
      padding: 20px;
      margin: 10px;
      /* border: 1px solid #414868; */
    }
    QLabel {
      background: transparent;
    }
    .header {
      color: rgba(255, 255, 255, 0.4);;
      font-size: 42px;
      font-weight: bold;
      margin-bottom: 10px;
    }
    .data {
      color: #c0caf5;
      font-size: 34px;
      padding: 5px 0;
    }
    .metric {
      background: rgba(56, 62, 90, 0.6);
      border-radius: 12px;
      padding: 15px;
      margin: 5px 0;
    }
    .highlight {
      color: #9ece6a;
      font-weight: bold;
    }
  )");
}

void StatCardBase::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
  QProcess* process = qobject_cast<QProcess*>(sender());
  if (process) {
    activeProcesses.removeOne(process);
    process->deleteLater();
  }
}

void StatCardBase::cleanupProcesses() {
  for (QProcess* process : activeProcesses) {
    process->kill();
    process->deleteLater();
  }
  activeProcesses.clear();
}

QString StatCardBase::formatBytes(qint64 bytes) {
  const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  int unitIndex = 0;
  double size = bytes;

  while (size >= 1024 && unitIndex < 4) {
    size /= 1024;
    unitIndex++;
  }

  return QString("%1 %2").arg(size, 0, 'f', 1).arg(units[unitIndex]);
}

QString StatCardBase::formatPercentage(float percentage) {
  return QString("%1%").arg(percentage, 0, 'f', 1);
}

QLabel* StatCardBase::createHeaderLabel(const QString &text, int fontSize = 34, const QString &color = "#dddddd") {
    QLabel *label = new QLabel(text, container);
    label->setAttribute(Qt::WA_TranslucentBackground);
    label->setAttribute(Qt::WA_NoSystemBackground);
    label->setProperty("class", "header");
    label->setStyleSheet(QString("font-size: %1px; color: %2;").arg(fontSize).arg(color));
    return label;
}

QLabel* StatCardBase::createDescriptionLabel(const QString &text, int fontSize = 32, const QString &color = "#dddddd") {
    QLabel *label = new QLabel(text, container);
    label->setAttribute(Qt::WA_TranslucentBackground);
    label->setAttribute(Qt::WA_NoSystemBackground);
    label->setProperty("class", "description");
    label->setStyleSheet(QString("font-size: %1px; color: %2;").arg(fontSize).arg(color));
    return label;
}

QLabel* StatCardBase::createDataLabel(const QString &text, int fontSize = 32, const QString &color = "#dddddd") {
    QLabel *label = new QLabel(text, container);
    label->setAttribute(Qt::WA_TranslucentBackground);
    label->setAttribute(Qt::WA_NoSystemBackground);
    label->setProperty("class", "data");
    label->setStyleSheet(QString("font-size: %1px; color: %2;").arg(fontSize).arg(color));
    return label;
}

//---------------------------------------
// SystemStatCard Implementation
//---------------------------------------
SystemStatCard::SystemStatCard(QWidget *parent)
    : StatCardBase(tr("System Statistics"), "", "", parent) {

    using namespace DynamicPanelComponents;
    QTimer::singleShot(0, this, [this]() {
        this->updateCardStyle();

        // Create main horizontal layout to hold CPU and Memory cards
        QHBoxLayout *mainStatsLayout = new QHBoxLayout();
        mainStatsLayout->setSpacing(20);

        //===================
        // CPU Card (Now on left)
        //===================
        QFrame *cpuFrame = new QFrame(this);
        cpuFrame->setObjectName("cpuCard");
        cpuFrame->setStyleSheet(R"(
            #cpuCard {
                background: transparent;
                border-radius: 15px;
                border: 2px solid rgba(255, 255, 255, 0.2);;
                padding: 15px;
            }
        )");

        QVBoxLayout *cpuLayout = new QVBoxLayout(cpuFrame);
        cpuLayout->setSpacing(10);

        QLabel *cpuHeader = createHeaderLabel(tr("CPU Usage"), 36);
        cpuLayout->addWidget(cpuHeader);

        cpuGauge = new BarGauge(this);
        BarGaugeConfig cpuConfig;
        cpuConfig.height = 45;
        cpuConfig.fontSize = 40;
        cpuConfig.gradientStops = DetailedGradientFiveColor();
        cpuGauge->setConfig(cpuConfig);
        cpuLayout->addWidget(cpuGauge);

        // Container for process columns
        QFrame *processFrame = new QFrame(this);
        QHBoxLayout *processColumnsLayout = new QHBoxLayout(processFrame);
        processColumnsLayout->setSpacing(20);
        processColumnsLayout->setContentsMargins(0, 10, 0, 0);

        // Left process table
        leftProcessTable = createProcessTable();
        processColumnsLayout->addWidget(leftProcessTable);

        // Right process table
        rightProcessTable = createProcessTable();
        processColumnsLayout->addWidget(rightProcessTable);

        cpuLayout->addWidget(processFrame);
        // cpuLayout->addStretch();

        //===================
        // Memory Card
        //===================
        QFrame *memFrame = new QFrame(this);
        memFrame->setObjectName("memCard");
        memFrame->setStyleSheet(R"(
            #memCard {
                background: transparent;
                border-radius: 15px;
                border: 2px solid rgba(255, 255, 255, 0.2);;
                padding: 15px;
            }
        )");

        QVBoxLayout *memLayout = new QVBoxLayout(memFrame);
        memLayout->setSpacing(10);

        QLabel *memHeader = createHeaderLabel(tr("Memory Usage"), 36);
        memLayout->addWidget(memHeader);

        memoryGauge = new BarGauge(this);
        BarGaugeConfig memConfig;
        memConfig.height = 45;
        memConfig.fontSize = 40;
        memConfig.gradientStops = DetailedGradientFiveColor();
        memoryGauge->setConfig(memConfig);
        memLayout->addWidget(memoryGauge);

        // Memory details section
        memDetailsLabel = createDescriptionLabel("");
        memDetailsLabel->setWordWrap(true);
        memLayout->addWidget(memDetailsLabel);
        memLayout->addStretch();

        // Add cards to main layout with appropriate sizing - CPU first
        mainStatsLayout->addWidget(cpuFrame, 4); // CPU section wider
        mainStatsLayout->addWidget(memFrame, 2);
        contentLayout->addLayout(mainStatsLayout);
    });
}

QTableWidget* SystemStatCard::createProcessTable() {
    QTableWidget* table = new QTableWidget(this);
    table->setColumnCount(2);  // Process name and CPU %
    table->setRowCount(5);     // 5 processes per column
    table->setShowGrid(false);
    table->setFrameShape(QFrame::NoFrame);
    table->horizontalHeader()->hide();
    table->verticalHeader()->hide();
    table->setStyleSheet(R"(
        QTableWidget {
            background: transparent;
            border: none;
            font-size: 31px;
        }
        QTableWidget::item {
            padding: 5px;
            border: none;
            font-size: 31px;
        }
    )");

    // Fixed width for CPU % column (wide enough for "100.0%")
    table->setColumnWidth(1, 120);

    // Process name column takes remaining space
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);

    // Set size policy
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    table->setFixedHeight(5 * 30);  // 5 rows * 30 pixels per row

    // Disable scroll bars
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Make sure cells use full width
    table->setWordWrap(false);
    table->setTextElideMode(Qt::ElideRight);

    return table;
}


void SystemStatCard::refresh() {
  // If not visible, skip refreshing
  if (!isVisible) return;

  runAsync<SystemMetrics>(
    [this]() {
      return gatherMetrics();
    },
    [this](const SystemMetrics &metrics) {
      updateDisplay(metrics);
    }
  );
}

SystemMetrics SystemStatCard::gatherMetrics() {
  SystemMetrics metrics;
  metrics.cpuPercent = 0;
  metrics.memTotal = 0;
  metrics.memUsed = 0;
  metrics.memFree = 0;
  metrics.swapTotal = 0;
  metrics.swapUsed = 0;
  metrics.swapFree = 0;

#ifdef __APPLE__
  // Example macOS CPU usage
  QProcess top;
  top.start("top", QStringList() << "-l" << "1" << "-n" << "0");
  top.waitForFinished();
  QString topOutput = top.readAllStandardOutput();

  QRegExp cpuRx("CPU usage: ([0-9.]+)% user, ([0-9.]+)% sys, ([0-9.]+)% idle");
  if (cpuRx.indexIn(topOutput) != -1) {
    float user = cpuRx.cap(1).toFloat();
    float sys = cpuRx.cap(2).toFloat();
    metrics.cpuPercent = user + sys;
  }

  // Top processes
  QProcess ps;
  ps.start("ps", QStringList() << "-arcx" << "-o" << "comm,%cpu" << "-c");
  ps.waitForFinished();
  QString psOut = ps.readAllStandardOutput();
  QStringList lines = psOut.split("\n", QString::SkipEmptyParts);
  if (!lines.isEmpty()) {
    lines.removeFirst(); // remove header
  }
  for (int i = 0; i < 10 && i < lines.size(); i++) {
    QStringList parts = lines[i].trimmed().split(" ", QString::SkipEmptyParts);
    if (parts.size() >= 2) {
      ProcessInfo p;
      p.name = parts[0];
      p.cpu = parts.last().toFloat();
      metrics.topProcesses.append(p);
    }
  }

  // Memory usage
  QProcess vm_stat;
  vm_stat.start("vm_stat");
  vm_stat.waitForFinished();
  QString memOutput = vm_stat.readAllStandardOutput();
  QMap<QString, unsigned long> values;
  QStringList memLines = memOutput.split("\n", QString::SkipEmptyParts);

  for (const QString &line : memLines) {
    if (line.contains(":")) {
      QStringList parts = line.split(":");
      QString key = parts[0].trimmed();
      unsigned long val = parts[1].trimmed().split(" ")[0].remove(".").toULong() * 4096;
      values[key] = val;
    }
  }

  metrics.memTotal = values["Pages free"] + values["Pages active"] +
                     values["Pages inactive"] + values["Pages speculative"] +
                     values["Pages wired down"];
  metrics.memFree  = values["Pages free"] + values["Pages inactive"];
  metrics.memUsed  = metrics.memTotal - metrics.memFree;

  // (Swap usage might be retrieved from "sysctl vm.swapusage" or similar)

#else
  // Linux CPU usage
  {
    static unsigned long long lastTotalUser = 0, lastTotalUserLow = 0,
                             lastTotalSys = 0, lastTotalIdle = 0;

    QFile statFile("/proc/stat");
    if (statFile.open(QIODevice::ReadOnly)) {
        QString line = statFile.readLine();
        QStringList vals = line.split(" ", QString::SkipEmptyParts);

        // CPU times in this order: user, nice, system, idle
        unsigned long long totalUser = vals[1].toLongLong();     // user
        unsigned long long totalUserLow = vals[2].toLongLong();  // nice
        unsigned long long totalSys = vals[3].toLongLong();      // system
        unsigned long long totalIdle = vals[4].toLongLong();     // idle
        unsigned long long totalIoWait = vals[5].toLongLong();   // iowait

        // Calculate total CPU time
        unsigned long long idle = totalIdle + totalIoWait;
        unsigned long long nonIdle = totalUser + totalUserLow + totalSys;
        unsigned long long total = idle + nonIdle;

        // Calculate the difference from last measurement
        if (lastTotalUser > 0) {  // Skip first measurement
            unsigned long long totalDiff = total - (lastTotalUser + lastTotalUserLow +
                                                  lastTotalSys + lastTotalIdle);
            unsigned long long idleDiff = idle - lastTotalIdle;

            if (totalDiff > 0) {  // Avoid division by zero
                metrics.cpuPercent = 100.0f * (totalDiff - idleDiff) / totalDiff;
            }
        }

        // Store current values for next measurement
        lastTotalUser = totalUser;
        lastTotalUserLow = totalUserLow;
        lastTotalSys = totalSys;
        lastTotalIdle = idle;

        statFile.close();
    }
  }

  // Linux top processes
  {
    QDir procDir("/proc");
    QMap<pid_t, QPair<QString, float>> processCPU;
    static QMap<pid_t, QPair<unsigned long, unsigned long>> lastStats;
    static unsigned long long lastTotalTime = 0;
    unsigned long long totalTime = 0;

    // Add this line to get CPU cores
    int numCores = sysconf(_SC_NPROCESSORS_ONLN);
    if (numCores < 1) numCores = 1;

    // Get system uptime for CPU time calculation
    QFile uptimeFile("/proc/uptime");
    if (uptimeFile.open(QIODevice::ReadOnly)) {
        QString uptime = uptimeFile.readLine();
        totalTime = uptime.split('.')[0].toULongLong() * sysconf(_SC_CLK_TCK);
        uptimeFile.close();
    }

    // Read all PIDs from /proc
    for (const QString &pidStr : procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok;
        pid_t pid = pidStr.toInt(&ok);
        if (!ok) continue;

        // Read process stats
        QFile statFile(QString("/proc/%1/stat").arg(pid));
        if (!statFile.open(QIODevice::ReadOnly)) continue;

        QString statLine = QString::fromUtf8(statFile.readAll());
        statFile.close();

        // Find last closing parenthesis to handle names with spaces
        int nameEnd = statLine.lastIndexOf(')');
        if (nameEnd == -1) continue;

        // Extract process name (between first '(' and last ')')
        QString name = statLine.mid(statLine.indexOf('(') + 1, nameEnd - statLine.indexOf('(') - 1);

        // Split remaining stats after process name
        QStringList stats = statLine.mid(nameEnd + 2).split(' ');
        if (stats.size() < 20) continue;

        // utime(14) and stime(15) in stat file
        unsigned long utime = stats[11].toULong();
        unsigned long stime = stats[12].toULong();
        unsigned long total = utime + stime;

        // Calculate CPU usage percentage - modify this part
        float cpuUsage = 0.0f;
        if (lastStats.contains(pid) && lastTotalTime > 0) {
            unsigned long lastTotal = lastStats[pid].first + lastStats[pid].second;
            unsigned long timeElapsed = totalTime - lastTotalTime;
            if (timeElapsed > 0) {
                // Divide by number of cores here
                cpuUsage = (100.0f * (total - lastTotal) / (float)timeElapsed) / numCores;
            }
        }

        lastStats[pid] = qMakePair(utime, stime);
        processCPU[pid] = qMakePair(name, cpuUsage);
    }

    // Update last total time
    lastTotalTime = totalTime;

    // Sort processes by CPU usage
    QList<QPair<float, QString>> sortedProcesses;
    for (auto it = processCPU.begin(); it != processCPU.end(); ++it) {
        sortedProcesses.append(qMakePair(it.value().second, it.value().first));
    }
    std::sort(sortedProcesses.begin(), sortedProcesses.end(),
        [](const auto &a, const auto &b) { return a.first > b.first; });

    // Take top 5 processes
    metrics.topProcesses.clear();
    for (int i = 0; i < qMin(5, sortedProcesses.size()); ++i) {
        ProcessInfo p;
        p.name = sortedProcesses[i].second;
        p.cpu = sortedProcesses[i].first;
        metrics.topProcesses.append(p);
    }
  }


 // Linux memory usage
  {
    QFile meminfo("/proc/meminfo");
    if (meminfo.open(QIODevice::ReadOnly)) {
      QByteArray data = meminfo.readAll();
      QString memData = QString::fromLatin1(data);
      QStringList lines = memData.split("\n");

      for (const QString& line : lines) {
        if (line.startsWith("MemTotal:"))
          metrics.memTotal = line.split(" ", QString::SkipEmptyParts)[1].toULong() * 1024;
        else if (line.startsWith("MemFree:"))
          metrics.memFree = line.split(" ", QString::SkipEmptyParts)[1].toULong() * 1024;
        else if (line.startsWith("SwapTotal:"))
          metrics.swapTotal = line.split(" ", QString::SkipEmptyParts)[1].toULong() * 1024;
        else if (line.startsWith("SwapFree:"))
          metrics.swapFree = line.split(" ", QString::SkipEmptyParts)[1].toULong() * 1024;
      }

      metrics.memUsed = metrics.memTotal - metrics.memFree;
      metrics.swapUsed = metrics.swapTotal - metrics.swapFree;

      meminfo.close();
    }
  }
#endif

  return metrics;
}

void SystemStatCard::updateDisplay(const SystemMetrics &metrics) {
    // Update CPU gauge
    cpuGauge->setValue(metrics.cpuPercent);

    // Update process tables
    QList<ProcessInfo> topProcesses = metrics.topProcesses.mid(0, 10);

    // Update left table (first 5 processes)
    for (int i = 0; i < 5; i++) {
        if (i < topProcesses.size()) {
            // Process name cell
            QTableWidgetItem *nameItem = new QTableWidgetItem(topProcesses[i].name);
            nameItem->setForeground(QColor("#ffffff"));
            nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            leftProcessTable->setItem(i, 0, nameItem);

            // CPU percentage cell
            QString cpuText = QString("%1%").arg(topProcesses[i].cpu, 0, 'f', 1);
            QTableWidgetItem *cpuItem = new QTableWidgetItem(cpuText);
            cpuItem->setForeground(QColor("#f7768e"));
            cpuItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            cpuItem->setFlags(cpuItem->flags() & ~Qt::ItemIsEditable);
            leftProcessTable->setItem(i, 1, cpuItem);
        } else {
            leftProcessTable->setItem(i, 0, new QTableWidgetItem(""));
            leftProcessTable->setItem(i, 1, new QTableWidgetItem(""));
        }
    }

    // Update right table (next 5 processes)
    for (int i = 0; i < 5; i++) {
        int processIndex = i + 5;
        if (processIndex < topProcesses.size()) {
            // Process name cell
            QTableWidgetItem *nameItem = new QTableWidgetItem(topProcesses[processIndex].name);
            nameItem->setForeground(QColor("#ffffff"));
            nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            rightProcessTable->setItem(i, 0, nameItem);

            // CPU percentage cell
            QString cpuText = QString("%1%").arg(topProcesses[processIndex].cpu, 0, 'f', 1);
            QTableWidgetItem *cpuItem = new QTableWidgetItem(cpuText);
            cpuItem->setForeground(QColor("#f7768e"));
            cpuItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            cpuItem->setFlags(cpuItem->flags() & ~Qt::ItemIsEditable);
            rightProcessTable->setItem(i, 1, cpuItem);
        } else {
            rightProcessTable->setItem(i, 0, new QTableWidgetItem(""));
            rightProcessTable->setItem(i, 1, new QTableWidgetItem(""));
        }
    }

    // Update Memory gauge and details
    float memPercent = (metrics.memTotal > 0)
                       ? (float(metrics.memUsed) / metrics.memTotal) * 100.0f
                       : 0.0f;
    memoryGauge->setValue(memPercent);

    memDetailsLabel->setText(QString(R"(
        <span style='color: #f7768e;'>Used: %1</span><br>
        <span style='color: #9ece6a;'>Free: %2</span><br>
        <span style='color: #7aa2f7;'>Total: %3</span>
    )").arg(formatBytes(metrics.memUsed))
       .arg(formatBytes(metrics.memFree))
       .arg(formatBytes(metrics.memTotal)));
}

//---------------------------------------
// ConnectivityStatCard
//---------------------------------------
ConnectivityStatCard::ConnectivityStatCard(QWidget *parent)
    : StatCardBase(tr("Connectivity Statistics"), "", "", parent) {

    QTimer::singleShot(0, this, [this]() {
        this->updateCardStyle();

        // Create main horizontal layout
        QHBoxLayout *mainStatsLayout = new QHBoxLayout();
        mainStatsLayout->setSpacing(40);

        //===================
        // Wi-Fi Column
        //===================
        QVBoxLayout *wifiColumn = new QVBoxLayout();
        wifiColumn->setSpacing(5);
        wifiColumn->setContentsMargins(10, 0, 10, 0);

        wifiHeaderLabel = createHeaderLabel(tr("Wi-Fi Connection"), 36);
        wifiHeaderLabel->setAlignment(Qt::AlignLeft);
        wifiColumn->addWidget(wifiHeaderLabel);

        // Wi-Fi details frame
        QFrame *wifiFrame = new QFrame(this);
        wifiFrame->setObjectName("wifiFrame");
        wifiFrame->setStyleSheet(R"(
            #wifiFrame {
                background: transparent;
                border: 2px solid rgba(255, 255, 255, 0.2);
                border-radius: 12px;
                padding: 15px;
                margin: 5px 0;
            }
        )");

        QVBoxLayout *wifiFrameLayout = new QVBoxLayout(wifiFrame);
        wifiFrameLayout->setSpacing(10);

        // Create labels with appropriate types
        wifiSSIDLabel = createDescriptionLabel("");
        wifiSignalStrengthLabel = createDescriptionLabel("");
        wifiIPAddressLabel = createDescriptionLabel("");

        QHBoxLayout *wifiSpeedLayout = new QHBoxLayout();
        wifiRXSpeedLabel = createDataLabel("");
        wifiTXSpeedLabel = createDataLabel("");
        wifiSpeedLayout->addWidget(wifiRXSpeedLabel);
        wifiSpeedLayout->addWidget(wifiTXSpeedLabel);

        wifiFrameLayout->addWidget(wifiSSIDLabel);
        wifiFrameLayout->addWidget(wifiSignalStrengthLabel);
        wifiFrameLayout->addWidget(wifiIPAddressLabel);
        wifiFrameLayout->addLayout(wifiSpeedLayout);

        wifiColumn->addWidget(wifiFrame);
        wifiColumn->addStretch();

        //===================
        // Cellular Column
        //===================
        QVBoxLayout *cellularColumn = new QVBoxLayout();
        cellularColumn->setSpacing(5);
        cellularColumn->setContentsMargins(10, 0, 10, 0);

        cellularHeaderLabel = createHeaderLabel(tr("Cellular Connection"), 36);
        cellularHeaderLabel->setAlignment(Qt::AlignLeft);
        cellularColumn->addWidget(cellularHeaderLabel);

        // Cellular details frame
        QFrame *cellularFrame = new QFrame(this);
        cellularFrame->setObjectName("cellularFrame");
        cellularFrame->setStyleSheet(R"(
            #cellularFrame {
                background: transparent;
                border: 2px solid rgba(255, 255, 255, 0.2);
                border-radius: 12px;
                padding: 15px;
                margin: 5px 0;
            }
        )");

        QVBoxLayout *cellularFrameLayout = new QVBoxLayout(cellularFrame);
        cellularFrameLayout->setSpacing(10);

        // Create labels with appropriate types
        signalStrengthLabel = createDescriptionLabel("");
        carrierLabel = createDescriptionLabel("");
        networkTypeLabel = createDescriptionLabel("");
        registrationStateLabel = createDescriptionLabel("");

        cellularFrameLayout->addWidget(signalStrengthLabel);
        cellularFrameLayout->addWidget(carrierLabel);
        cellularFrameLayout->addWidget(networkTypeLabel);
        cellularFrameLayout->addWidget(registrationStateLabel);

        cellularColumn->addWidget(cellularFrame);
        cellularColumn->addStretch();

        // Add columns to main layout
        mainStatsLayout->addLayout(wifiColumn, 1);
        mainStatsLayout->addLayout(cellularColumn, 1);

        contentLayout->addLayout(mainStatsLayout);
    });
}

void ConnectivityStatCard::refresh() {
    if (!isVisible) return;

    runAsync<ConnectivityInfo>(
        [this]() {
            return gatherConnectivityInfo();
        },
        [this](const ConnectivityInfo &info) {
            updateDisplay(info);
        }
    );
}

ConnectivityStatCard::ConnectivityInfo ConnectivityStatCard::gatherConnectivityInfo() {
    ConnectivityInfo info;

    // ---------- Cellular Information ----------
#ifdef __APPLE__
    // Mock data for macOS
    info.cellular.signalStrength = QRandomGenerator::global()->bounded(101);
    info.cellular.signalQuality = info.cellular.signalStrength > 80 ? "Excellent" :
                                  info.cellular.signalStrength > 60 ? "Good" :
                                  info.cellular.signalStrength > 40 ? "Fair" : "Poor";
    QStringList networkTypes = {"5G", "LTE", "4G", "3G", "EDGE"};
    info.cellular.networkType = networkTypes[QRandomGenerator::global()->bounded(networkTypes.size())].toUpper();
    QStringList carriers = {"AT&T", "Verizon", "T-Mobile", "Sprint", "Rogers", "Bell"};
    info.cellular.carrier = carriers[QRandomGenerator::global()->bounded(carriers.size())];
    info.cellular.registrationState = "Home";
#else
    // Linux Cellular Information
    QProcess cellularProcess;
    cellularProcess.start("mmcli", QStringList() << "-m" << "0");
    cellularProcess.waitForFinished(1000);
    QString output = cellularProcess.readAllStandardOutput();

    // Parse Signal Strength
    QRegularExpression signalRx(R"(signal quality\s*:\s*(\d+)% )", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = signalRx.match(output);
    if (match.hasMatch()) {
        bool ok;
        int rawSignal = match.captured(1).toInt(&ok);
        if (ok && rawSignal >= 0 && rawSignal <= 100) {
            info.cellular.signalStrength = rawSignal;
            info.cellular.signalQuality = info.cellular.signalStrength > 80 ? "Excellent" :
                                          info.cellular.signalStrength > 60 ? "Good" :
                                          info.cellular.signalStrength > 40 ? "Fair" : "Poor";
        }
    }

    // Parse Carrier
    QRegularExpression operatorRx(R"(operator name:\s*([\w\s&-]+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch operatorMatch = operatorRx.match(output);
    if (operatorMatch.hasMatch()) {
        info.cellular.carrier = operatorMatch.captured(1).trimmed();
    }

    // Parse Network Type
    QRegularExpression networkRx(R"(access tech\s*:\s+([^\n]+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch networkMatch = networkRx.match(output);
    if (networkMatch.hasMatch()) {
        info.cellular.networkType = networkMatch.captured(1).trimmed().toUpper();
    }

    // Parse Registration State
    QRegularExpression regRx(R"(registration:\s*([\w-]+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch regMatch = regRx.match(output);
    if (regMatch.hasMatch()) {
        info.cellular.registrationState = regMatch.captured(1).trimmed();
    }
#endif

    // ---------- Wi-Fi Information ----------
#ifdef __APPLE__
    // Mock data for macOS
    info.wifi.ssid = "Home_WiFi";
    info.wifi.signalStrength = QRandomGenerator::global()->bounded(101);
    info.wifi.signalQuality = info.wifi.signalStrength > 80 ? "Excellent" :
                              info.wifi.signalStrength > 60 ? "Good" :
                              info.wifi.signalStrength > 40 ? "Fair" : "Poor";
    info.wifi.ipAddress = "192.168.1.10";
    info.wifi.rxSpeed = QRandomGenerator::global()->bounded(100) / 10.0; // MB/s
    info.wifi.txSpeed = QRandomGenerator::global()->bounded(100) / 10.0; // MB/s
#else
    // Linux Wi-Fi Information
    QProcess wifiProcess;
    wifiProcess.start("iwgetid", QStringList() << "-r");
    wifiProcess.waitForFinished(500);
    QString ssid = wifiProcess.readAllStandardOutput().trimmed();
    info.wifi.ssid = ssid.isEmpty() ? "N/A" : ssid;

    // Signal Strength
    QProcess nmcliProcess;
    nmcliProcess.start("nmcli", QStringList() << "-f" << "IN-USE,SSID,SIGNAL" << "dev" << "wifi");
    nmcliProcess.waitForFinished(1000);
    QString nmcliOutput = nmcliProcess.readAllStandardOutput();
    QStringList lines = nmcliOutput.split('\n', QString::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.startsWith('*')) { // Connected network
            QStringList parts = line.split(QRegExp("\\s+"), QString::SkipEmptyParts);
            if (parts.size() >= 3) {
                int signal = parts[2].toInt();
                info.wifi.signalStrength = signal;
                info.wifi.signalQuality = signal > 80 ? "Excellent" :
                                          signal > 60 ? "Good" :
                                          signal > 40 ? "Fair" : "Poor";
                break;
            }
        }
    }

    // IP Address
    QProcess interfaceIpProcess;
    interfaceIpProcess.start("ip", QStringList() << "addr" << "show" << "wlan0"); // Dynamically detect interface if possible
    interfaceIpProcess.waitForFinished(500);
    QString interfaceIpOutput = interfaceIpProcess.readAllStandardOutput();
    QRegExp ipRx("inet\\s+(\\d+\\.\\d+\\.\\d+\\.\\d+)/");
    if (ipRx.indexIn(interfaceIpOutput) != -1) {
        info.wifi.ipAddress = ipRx.cap(1);
    }

    // RX and TX Speeds (Placeholder: Implement actual speed calculations)
    info.wifi.rxSpeed = QRandomGenerator::global()->bounded(100) / 10.0; // MB/s
    info.wifi.txSpeed = QRandomGenerator::global()->bounded(100) / 10.0; // MB/s
#endif

    // ---------- Network Interfaces ----------
    QFile netDev("/proc/net/dev");
    if (netDev.open(QIODevice::ReadOnly)) {
        QString line;
        while (!netDev.atEnd()) {
            line = netDev.readLine();
            QStringList fields = line.split(QRegExp("\\s+"), QString::SkipEmptyParts);

            if (fields.size() >= 10) {
                QString interface = fields[0].remove(":");

                // Skip virtual interfaces
                if (interface == "lo" || interface.startsWith("veth") ||
                    interface.startsWith("docker") || interface.startsWith("br-")) {
                    continue;
                }

                // Get IP Address
                QString ipAddress = "N/A";
                QProcess ipProcess;
                ipProcess.start("ip", QStringList() << "addr" << "show" << interface);
                ipProcess.waitForFinished(500);
                QString ipOutput = ipProcess.readAllStandardOutput();
                QRegExp interfaceIpRx("inet\\s+(\\d+\\.\\d+\\.\\d+\\.\\d+)/");
                if (interfaceIpRx.indexIn(ipOutput) != -1) {
                    ipAddress = interfaceIpRx.cap(1);
                }

                // Calculate Speeds (Placeholder values; implement actual speed calculation as needed)
                double rxSpeed = QRandomGenerator::global()->bounded(100) / 10.0; // MB/s
                double txSpeed = QRandomGenerator::global()->bounded(100) / 10.0; // MB/s

                ConnectivityInfo::NetworkInterface netIf;
                netIf.name = interface;
                netIf.ipAddress = ipAddress;
                netIf.rxSpeed = rxSpeed;
                netIf.txSpeed = txSpeed;

                info.interfaces.append(netIf);
            }
        }
        netDev.close();
    }

    return info;
}

void ConnectivityStatCard::updateDisplay(const ConnectivityInfo &info) {
    // ---------- Update Cellular Information ----------
    // Signal Strength
    if (info.cellular.signalStrength != -1) {
        QString signalColor;
        if (info.cellular.signalQuality == "Excellent") {
            signalColor = "#9ece6a"; // Green
        } else if (info.cellular.signalQuality == "Good") {
            signalColor = "#e0af68"; // Orange
        } else if (info.cellular.signalQuality == "Fair") {
            signalColor = "#7aa2f7"; // Blue
        } else {
            signalColor = "#f7768e"; // Red
        }
        signalStrengthLabel->setText(QString("Signal Strength: <span style='color: %1;'>%2% (%3)</span>")
            .arg(signalColor)
            .arg(info.cellular.signalStrength)
            .arg(info.cellular.signalQuality));
    } else {
        signalStrengthLabel->setText("Signal Strength: <span style='color: #f7768e;'>Unknown</span>");
    }

    // Carrier
    carrierLabel->setText(QString("Carrier: <span style='color: #7aa2f7;'>%1</span>").arg(info.cellular.carrier));

    // Network Type
    networkTypeLabel->setText(QString("Network Type: <span style='color: #7aa2f7;'>%1</span>").arg(info.cellular.networkType.toUpper()));

    // Registration State
    QString regColor;
    if (info.cellular.registrationState.toLower() == "home") {
        regColor = "#9ece6a"; // Green
    } else if (info.cellular.registrationState.toLower() == "roaming") {
        regColor = "#e0af68"; // Orange
    } else if (info.cellular.registrationState.toLower() == "searching") {
        regColor = "#7aa2f7"; // Blue
    } else {
        regColor = "#f7768e"; // Red
    }
    registrationStateLabel->setText(QString("Registration State: <span style='color: %1;'>%2</span>")
        .arg(regColor)
        .arg(info.cellular.registrationState));

    // ---------- Update Wi-Fi Information ----------
    // SSID
    wifiSSIDLabel->setText(QString("SSID: <span style='color: #7aa2f7;'>%1</span>").arg(info.wifi.ssid));

    // Signal Strength
    if (info.wifi.signalStrength != -1) {
        QString wifiSignalColor;
        if (info.wifi.signalQuality == "Excellent") {
            wifiSignalColor = "#9ece6a"; // Green
        } else if (info.wifi.signalQuality == "Good") {
            wifiSignalColor = "#e0af68"; // Orange
        } else if (info.wifi.signalQuality == "Fair") {
            wifiSignalColor = "#7aa2f7"; // Blue
        } else {
            wifiSignalColor = "#f7768e"; // Red
        }
        wifiSignalStrengthLabel->setText(QString("Signal Strength: <span style='color: %1;'>%2% (%3)</span>")
            .arg(wifiSignalColor)
            .arg(info.wifi.signalStrength)
            .arg(info.wifi.signalQuality));
    } else {
        wifiSignalStrengthLabel->setText("Signal Strength: <span style='color: #f7768e;'>Unknown</span>");
    }

    // IP Address
    wifiIPAddressLabel->setText(QString("IP Address: <span style='color: #7aa2f7;'>%1</span>").arg(info.wifi.ipAddress));

    // RX and TX Speeds
    wifiRXSpeedLabel->setText(QString("↓ <span style='color: #9ece6a;'>%1 MB/s</span>")
        .arg(QString::number(info.wifi.rxSpeed, 'f', 1)));
    wifiTXSpeedLabel->setText(QString("↑ <span style='color: #f7768e;'>%1 MB/s</span>")
        .arg(QString::number(info.wifi.txSpeed, 'f', 1)));
}

//---------------------------------------
// StorageStatCard Implementation
//---------------------------------------
StorageStatCard::StorageStatCard(QWidget *parent)
    : StatCardBase(tr("Storage Statistics"), "", "", parent) {

    QTimer::singleShot(0, this, [this]() {
        this->updateCardStyle();

        // Create header section
        // QLabel *headerLabel = createHeaderLabel("");
        // headerLabel->setAttribute(Qt::WA_TranslucentBackground);
        // headerLabel->setAttribute(Qt::WA_NoSystemBackground);
        // headerLabel->setAlignment(Qt::AlignLeft);
        // contentLayout->addWidget(headerLabel);

        // Create a scrollable area for storage devices
        QScrollArea *scrollArea = new QScrollArea(this);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollArea->setStyleSheet("QScrollArea { background: transparent; }");

        // Container widget for storage devices
        QWidget *storageContainer = new QWidget(scrollArea);
        storageContainer->setObjectName("storageContainer");
        storageContainer->setStyleSheet("QWidget#storageContainer { background: transparent; }");

        // Grid layout for storage devices
        QGridLayout *gridLayout = new QGridLayout(storageContainer);
        gridLayout->setSpacing(20);
        gridLayout->setContentsMargins(0, 0, 0, 0);

        scrollArea->setWidget(storageContainer);
        contentLayout->addWidget(scrollArea);

        storageProcess = new QProcess(this);
        connect(storageProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &StorageStatCard::handleStorageDataReady);

        refresh();
    });
}

StorageStatCard::StorageInfo StorageStatCard::parseStorageInfo(const QString &name, const QStorageInfo &storage) {
    StorageInfo info;
    info.name = name;
    info.mountPoint = storage.rootPath();
    info.total = storage.bytesTotal();
    info.free = storage.bytesFree();
    info.used = info.total - info.free;
    info.usagePercent = info.total > 0 ? (static_cast<float>(info.used) / info.total) * 100.0 : 0.0;
    return info;
}

void StorageStatCard::refresh() {
    #ifdef __APPLE__
    storageProcess->start("df", QStringList() << "-h");
    #else
    runAsync<QList<StorageInfo>>([this]() {
        QList<StorageInfo> devices;

        // Get root filesystem
        QStorageInfo root = QStorageInfo::root();
        if (root.isValid()) {
            devices.append(parseStorageInfo("Root", root));
        }

        // Get all mounted filesystems
        for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
            if (!storage.isValid() || storage.rootPath() == root.rootPath()) {
                continue;
            }

            // Skip system mounts and temporary filesystems
            QString device = storage.device();
            if (device.startsWith("/dev/") &&
                !storage.isReadOnly() &&
                !storage.fileSystemType().contains("tmpfs")) {
                devices.append(parseStorageInfo(storage.displayName(), storage));
            }
        }

        return devices;
    },
    [this](const QList<StorageInfo> &devices) {
        updateStorageDisplay(devices);
    });
    #endif
}

void StorageStatCard::handleStorageDataReady() {
    #ifdef __APPLE__
    QString output = storageProcess->readAllStandardOutput();
    QStringList lines = output.split('\n', QString::SkipEmptyParts);
    QList<StorageInfo> devices;

    for (int i = 1; i < lines.size(); i++) {
        QStringList fields = lines[i].split(QRegExp("\\s+"), QString::SkipEmptyParts);
        if (fields.size() >= 9) {
            QString mountPoint = fields[8];
            if (mountPoint == "/" || mountPoint.startsWith("/Volumes/")) {
                QStorageInfo storage(mountPoint);
                if (storage.isValid()) {
                    QString name = mountPoint == "/" ? "System" :
                                 QDir(mountPoint).dirName();
                    devices.append(parseStorageInfo(name, storage));
                }
            }
        }
    }
    updateStorageDisplay(devices);
    #endif
}

void StorageStatCard::updateStorageDisplay(const QList<StorageInfo> &devices) {
    using namespace DynamicPanelComponents;
    if (devices.isEmpty()) {
        QLabel *noDevicesLabel = new QLabel(tr("No storage devices found"));
        noDevicesLabel->setAttribute(Qt::WA_TranslucentBackground);
        noDevicesLabel->setAttribute(Qt::WA_NoSystemBackground);
        noDevicesLabel->setStyleSheet("color: #f7768e;");
        contentLayout->addWidget(noDevicesLabel);
        return;
    }

    QScrollArea *scrollArea = findChild<QScrollArea*>();
    if (!scrollArea) return;

    QWidget *storageContainer = scrollArea->widget();
    if (!storageContainer) return;

    // Create layout if it doesn't exist
    QGridLayout *gridLayout = qobject_cast<QGridLayout*>(storageContainer->layout());
    if (!gridLayout) {
        gridLayout = new QGridLayout(storageContainer);
        gridLayout->setSpacing(20);
        gridLayout->setContentsMargins(0, 0, 0, 0);
    }

    // Track which devices are still present
    QSet<QString> currentDevices;

    int numColumns = qMax(2, storageContainer->width() / 400);
    int currentRow = 0;
    int currentCol = 0;

    for (const StorageInfo &device : devices) {
        QString deviceKey = device.mountPoint;
        currentDevices.insert(deviceKey);

        QFrame *deviceFrame;
        BarGauge *gauge;
        QLabel *mountLabel;
        QLabel *detailsLabel;

        if (!deviceFrames.contains(deviceKey)) {
            // Create new frame and widgets if they don't exist
            deviceFrame = new QFrame(storageContainer);
            deviceFrame->setObjectName("deviceCard");
            deviceFrame->setStyleSheet(R"(
                #deviceCard {
                    background: transparent;
                    border-radius: 15px;
                    border: 2px solid rgba(255, 255, 255, 0.2);;
                    padding: 15px;
                }
            )");

            QVBoxLayout *cardLayout = new QVBoxLayout(deviceFrame);
            cardLayout->setSpacing(10);

            mountLabel = createHeaderLabel("", 32);
            cardLayout->addWidget(mountLabel);

            BarGaugeConfig storageConfig;
            storageConfig.height = 45;
            storageConfig.fontSize = 40;
            storageConfig.gradientStops = DetailedGradientFiveColor();
            gauge = new BarGauge(deviceFrame, storageConfig);
            cardLayout->addWidget(gauge);

            detailsLabel = createDescriptionLabel("");
            cardLayout->addWidget(detailsLabel);

            deviceFrames[deviceKey] = deviceFrame;
            gauges[deviceKey] = gauge;
            mountLabels[deviceKey] = mountLabel;
            detailLabels[deviceKey] = detailsLabel;
        } else {
            // Reuse existing widgets
            deviceFrame = deviceFrames[deviceKey];
            gauge = gauges[deviceKey];
            mountLabel = mountLabels[deviceKey];
            detailsLabel = detailLabels[deviceKey];
        }

        // Update widget content
        mountLabel->setText(QString("<span style='color: white;'>%1</span>").arg(device.mountPoint));
        gauge->setValue(device.usagePercent);
        detailsLabel->setText(QString(R"(
            <span style='color: #f7768e;'>Used: %1 (%2%)</span><br>
            <span style='color: #9ece6a;'>Free: %3</span><br>
            <span style='color: #7aa2f7;'>Total: %4</span>
        )").arg(formatBytes(device.used))
           .arg(QString::number(device.usagePercent, 'f', 1))
           .arg(formatBytes(device.free))
           .arg(formatBytes(device.total)));

        // Update grid position
        gridLayout->addWidget(deviceFrame, currentRow, currentCol);
        deviceFrame->show();

        currentCol++;
        if (currentCol >= numColumns) {
            currentCol = 0;
            currentRow++;
        }
    }

    // Remove any devices that are no longer present
    QList<QString> devicesToRemove;
    for (const QString &key : deviceFrames.keys()) {
        if (!currentDevices.contains(key)) {
            delete deviceFrames[key];
            deviceFrames.remove(key);
            gauges.remove(key);
            mountLabels.remove(key);
            detailLabels.remove(key);
        }
    }

    // Add stretch to the last row
    gridLayout->setRowStretch(currentRow + 1, 1);
}

StorageStatCard::~StorageStatCard() {
    qDeleteAll(deviceFrames);
    deviceFrames.clear();
    gauges.clear();
    mountLabels.clear();
    detailLabels.clear();
}
