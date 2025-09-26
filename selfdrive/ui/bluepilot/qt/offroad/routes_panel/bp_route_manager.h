#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QCache>
#include <QMutex>
#include <QFutureWatcher>
#include <vector>
#include <memory>

struct RouteInfo {
  QString baseName;
  QString timestamp;
  QString endTimestamp;
  QString duration;
  QString elapsedTime;
  QString displayDate;
  int segments = 0;
  QString size;
  double tripMiles = 0.0;
  bool hasVideo = false;
  bool hasRLog = false;
  bool hasQLog = false;
  bool hasFrontVideo = false;
  bool hasWideVideo = false;
  bool hasDriverVideo = false;
  bool hasLQVideo = false;
  bool hasFrontHQVideo = false;
  bool hasFrontLQVideo = false;
  bool hasDriverHQVideo = false;
  bool isStarred = false;
  QDateTime dateTime;
};

struct RouteCache {
  std::vector<RouteInfo> routes;
  QDateTime lastModified;
  bool isValid = false;
};

class BPRouteManager : public QObject {
  Q_OBJECT

public:
  explicit BPRouteManager(QObject *parent = nullptr);
  ~BPRouteManager();

  // Core operations
  void loadRoutes();
  void deleteRoute(const QString &routePath);
  void toggleStar(const QString &routePath);

  // Getters
  QString getRoutesDir() const;
  RouteInfo getRouteInfo(const QString &routePath);
  std::vector<RouteInfo> getRoutes() const;

  // Cache management
  void invalidateCache();

signals:
  void routesLoaded(const std::vector<RouteInfo> &routes);
  void routeDeleted(const QString &routePath);
  void routeStarred(const QString &routePath, bool starred);
  void loadingStarted();
  void loadingFinished();
  void errorOccurred(const QString &error);

private:
  void loadRoutesAsync();
  RouteInfo parseRoute(const QString &path);
  bool isValidRoute(const QString &path);
  void saveCache();
  bool loadCache();
  QString formatDuration(int totalSeconds);
  QString formatElapsedTime(const QDateTime &start, const QDateTime &end);
  QString formatSize(qint64 bytes);
  double calculateTripMiles(const QString &routePath);

  // Utility functions
  bool isCommaDevice();
  QString getAbsolutePath(const QString &path);

  QString m_routesDir;
  mutable QMutex m_cacheMutex;
  std::unique_ptr<RouteCache> m_cache;
  QFutureWatcher<std::vector<RouteInfo>> *m_loadWatcher;
};