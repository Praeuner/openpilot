#pragma once

#include <QObject>
#include <QPixmap>
#include <QString>
#include <QCache>
#include <QMutex>
#include <QFutureWatcher>
#include <QProcess>
#include <memory>

class BPThumbnailLoader : public QObject {
  Q_OBJECT

public:
  explicit BPThumbnailLoader(QObject *parent = nullptr);
  ~BPThumbnailLoader();

  // Request a thumbnail for a route
  void requestThumbnail(const QString &routePath, const QString &routeId);

  // Clear cache
  void clearCache();

  // Set maximum cache size (in items)
  void setMaxCacheSize(int size);

signals:
  void thumbnailReady(const QString &routeId, const QPixmap &thumbnail);
  void thumbnailFailed(const QString &routeId, const QString &error);

private:
  struct ThumbnailRequest {
    QString routePath;
    QString routeId;
  };

  void generateThumbnail(const ThumbnailRequest &request);
  QString getCachePath(const QString &routeId) const;
  QPixmap loadFromDisk(const QString &cachePath) const;
  bool saveToDisk(const QPixmap &pixmap, const QString &cachePath) const;
  QPixmap extractFrameFromVideo(const QString &videoPath) const;
  QString findBestVideoFile(const QString &routePath) const;

  mutable QMutex m_cacheMutex;
  std::unique_ptr<QCache<QString, QPixmap>> m_cache;
  QList<QFutureWatcher<QPixmap>*> m_activeWatchers;
  QString m_cacheDir;

  static constexpr int DEFAULT_CACHE_SIZE = 100;
  static constexpr int THUMBNAIL_WIDTH = 320;
  static constexpr int THUMBNAIL_HEIGHT = 180;
};