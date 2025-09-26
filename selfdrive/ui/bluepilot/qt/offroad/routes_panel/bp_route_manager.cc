#include "bp_route_manager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent>
#include <QStandardPaths>
#include <QDirIterator>
#include <algorithm>
#include <chrono>

BPRouteManager::BPRouteManager(QObject *parent)
    : QObject(parent)
    , m_cache(std::make_unique<RouteCache>())
    , m_loadWatcher(new QFutureWatcher<std::vector<RouteInfo>>(this)) {

  printf("BPRouteManager: Starting constructor\n");

  try {
    // Set up routes directory - using realdata path like original
    printf("BPRouteManager: Setting routes directory\n");
    m_routesDir = getAbsolutePath(isCommaDevice() ? "/data/media/0/realdata" : "~/comma_data/media/0/realdata");
    printf("BPRouteManager: Routes dir set to: %s\n", m_routesDir.toStdString().c_str());

    // Only create directory when actually needed (in loadRoutes), not in constructor
    printf("BPRouteManager: Setting up future watcher\n");

    // Connect future watcher for async loading
    connect(m_loadWatcher, &QFutureWatcher<std::vector<RouteInfo>>::finished,
            this, [this]() {
              try {
                auto routes = m_loadWatcher->result();
                {
                  QMutexLocker lock(&m_cacheMutex);
                  m_cache->routes = routes;
                  m_cache->lastModified = QDateTime::currentDateTime();
                  m_cache->isValid = true;
                }
                saveCache();
                emit routesLoaded(routes);
                emit loadingFinished();
              } catch (const std::exception& e) {
                printf("Exception in route loading callback: %s\n", e.what());
              } catch (...) {
                printf("Unknown exception in route loading callback\n");
              }
            });

    printf("BPRouteManager: Constructor completed successfully\n");
  } catch (const std::exception& e) {
    printf("Exception in BPRouteManager constructor: %s\n", e.what());
    throw;
  } catch (...) {
    printf("Unknown exception in BPRouteManager constructor\n");
    throw;
  }
}

BPRouteManager::~BPRouteManager() {
  if (m_loadWatcher->isRunning()) {
    m_loadWatcher->cancel();
    m_loadWatcher->waitForFinished();
  }
}

void BPRouteManager::loadRoutes() {
  emit loadingStarted();

  // Try to load from cache first
  if (loadCache()) {
    QMutexLocker lock(&m_cacheMutex);
    emit routesLoaded(m_cache->routes);
    emit loadingFinished();
    return;
  }

  // Load routes asynchronously
  loadRoutesAsync();
}

void BPRouteManager::loadRoutesAsync() {
  auto future = QtConcurrent::run([this]() {
    std::vector<RouteInfo> routes;

    // Create directory if it doesn't exist
    if (!QDir(m_routesDir).exists()) {
      QDir().mkpath(m_routesDir);
    }

    QDir dir(m_routesDir);
    if (!dir.exists()) {
      printf("BPRouteManager: Routes directory doesn't exist and couldn't be created: %s\n", m_routesDir.toStdString().c_str());
      return routes;
    }

    // Get all directories
    QStringList folders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    routes.reserve(folders.size());

    for (const QString &folder : folders) {
      QString path = m_routesDir + "/" + folder;
      if (isValidRoute(path)) {
        RouteInfo info = parseRoute(path);
        routes.push_back(info);
      }
    }

    // Sort by date, newest first
    std::sort(routes.begin(), routes.end(),
              [](const RouteInfo &a, const RouteInfo &b) {
                return a.dateTime > b.dateTime;
              });

    return routes;
  });

  m_loadWatcher->setFuture(future);
}

RouteInfo BPRouteManager::parseRoute(const QString &path) {
  RouteInfo info;
  QDir dir(path);
  info.baseName = dir.dirName();

  // Parse timestamp from folder name (format: YYYY-MM-DD--HH-MM-SS--SegmentCount)
  QStringList parts = info.baseName.split("--");
  if (parts.size() >= 3) {
    QString dateStr = parts[0];
    QString timeStr = parts[1];
    info.segments = parts[2].toInt();

    // Create QDateTime
    info.dateTime = QDateTime::fromString(dateStr + " " + timeStr, "yyyy-MM-dd HH-mm-ss");
    info.timestamp = info.dateTime.toString("yyyy-MM-dd HH:mm:ss");

    // Calculate display date
    info.displayDate = info.dateTime.toString("dddd - MMMM d, yyyy");
  }

  // Check for starred status
  info.isStarred = QFile::exists(path + "/starred");

  // Check for video files
  QStringList videoFiles = dir.entryList({"*.hevc", "*.ts"}, QDir::Files);
  for (const QString &file : videoFiles) {
    if (file == "fcamera.hevc") info.hasFrontHQVideo = true;
    else if (file == "qcamera.ts") info.hasFrontLQVideo = true;
    else if (file == "dcamera.hevc") info.hasDriverHQVideo = true;
    else if (file == "ecamera.hevc") info.hasWideVideo = true;
  }

  info.hasVideo = info.hasFrontHQVideo || info.hasFrontLQVideo ||
                  info.hasDriverHQVideo || info.hasWideVideo;

  // Check for log files
  info.hasRLog = QFile::exists(path + "/rlog.bz2");
  info.hasQLog = QFile::exists(path + "/qlog.bz2");

  // Calculate total size
  qint64 totalSize = 0;
  QDirIterator it(path, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    totalSize += it.fileInfo().size();
  }
  info.size = formatSize(totalSize);

  // Calculate duration and end time
  if (info.segments > 0) {
    int durationSeconds = info.segments * 60;
    info.duration = formatDuration(durationSeconds);
    info.endTimestamp = info.dateTime.addSecs(durationSeconds).toString("yyyy-MM-dd HH:mm:ss");
    info.elapsedTime = formatElapsedTime(info.dateTime, info.dateTime.addSecs(durationSeconds));
  }

  // Calculate trip miles (simplified - would need actual telemetry data)
  info.tripMiles = calculateTripMiles(path);

  return info;
}

bool BPRouteManager::isValidRoute(const QString &path) {
  QDir dir(path);
  if (!dir.exists()) return false;

  // Must have at least one video or log file
  QStringList videoAndLogFiles = dir.entryList(
    {"*.hevc", "*.ts", "*.bz2"}, QDir::Files);

  return !videoAndLogFiles.isEmpty();
}

void BPRouteManager::deleteRoute(const QString &routePath) {
  QDir dir(routePath);
  if (dir.removeRecursively()) {
    invalidateCache();
    emit routeDeleted(routePath);
    loadRoutes(); // Reload the list
  } else {
    emit errorOccurred(tr("Failed to delete route: %1").arg(routePath));
  }
}

void BPRouteManager::toggleStar(const QString &routePath) {
  QString starFile = routePath + "/starred";
  bool isStarred = QFile::exists(starFile);

  if (isStarred) {
    QFile::remove(starFile);
  } else {
    QFile file(starFile);
    file.open(QIODevice::WriteOnly);
    file.close();
  }

  invalidateCache();
  emit routeStarred(routePath, !isStarred);
  loadRoutes(); // Reload to update star status
}

QString BPRouteManager::getRoutesDir() const {
  return m_routesDir;
}

RouteInfo BPRouteManager::getRouteInfo(const QString &routePath) {
  // First check cache
  {
    QMutexLocker lock(&m_cacheMutex);
    if (m_cache->isValid) {
      for (const auto &route : m_cache->routes) {
        if (m_routesDir + "/" + route.baseName == routePath) {
          return route;
        }
      }
    }
  }

  // If not in cache, parse directly
  return parseRoute(routePath);
}

std::vector<RouteInfo> BPRouteManager::getRoutes() const {
  QMutexLocker lock(&m_cacheMutex);
  return m_cache->routes;
}

void BPRouteManager::invalidateCache() {
  QMutexLocker lock(&m_cacheMutex);
  m_cache->isValid = false;
}

void BPRouteManager::saveCache() {
  QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  QDir().mkpath(cacheDir);
  QString cacheFile = cacheDir + "/routes_cache.json";

  QJsonDocument doc;
  QJsonObject root;

  // Save cache metadata
  root["lastModified"] = m_cache->lastModified.toString(Qt::ISODate);

  // Save routes (simplified - would need full serialization)
  // For now, we'll skip this as cache loading is fast enough

  QFile file(cacheFile);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(doc.toJson());
  }
}

bool BPRouteManager::loadCache() {
  // For now, always return false to force reload
  // Can implement proper cache later if needed
  return false;
}

QString BPRouteManager::formatDuration(int totalSeconds) {
  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;

  if (hours > 0) {
    return QString("%1h %2m %3s").arg(hours).arg(minutes).arg(seconds);
  } else if (minutes > 0) {
    return QString("%1m %2s").arg(minutes).arg(seconds);
  } else {
    return QString("%1s").arg(seconds);
  }
}

QString BPRouteManager::formatElapsedTime(const QDateTime &start, const QDateTime &end) {
  qint64 seconds = start.secsTo(end);

  if (seconds < 60) {
    return tr("just now");
  } else if (seconds < 3600) {
    return tr("%1 minutes").arg(seconds / 60);
  } else if (seconds < 86400) {
    return tr("%1 hours").arg(seconds / 3600);
  } else {
    return tr("%1 days").arg(seconds / 86400);
  }
}

QString BPRouteManager::formatSize(qint64 bytes) {
  const qint64 KB = 1024;
  const qint64 MB = KB * 1024;
  const qint64 GB = MB * 1024;

  if (bytes >= GB) {
    return QString("%1 GB").arg(bytes / double(GB), 0, 'f', 2);
  } else if (bytes >= MB) {
    return QString("%1 MB").arg(bytes / double(MB), 0, 'f', 2);
  } else if (bytes >= KB) {
    return QString("%1 KB").arg(bytes / double(KB), 0, 'f', 2);
  } else {
    return QString("%1 B").arg(bytes);
  }
}

double BPRouteManager::calculateTripMiles(const QString &routePath) {
  // Simplified calculation - would need to read actual telemetry data
  // For now, estimate based on segments (assuming 1 mile per minute average)
  QDir dir(routePath);
  QString baseName = dir.dirName();
  QStringList parts = baseName.split("--");

  if (parts.size() >= 3) {
    int segments = parts[2].toInt();
    return segments * 1.0; // Very rough estimate
  }

  return 0.0;
}

// Utility functions
bool BPRouteManager::isCommaDevice() {
#ifdef QCOM2
  return true;
#else
  return false;
#endif
}

QString BPRouteManager::getAbsolutePath(const QString &path) {
  if (path.startsWith('~')) {
    return QDir::homePath() + path.mid(1);
  }
  return path;
}