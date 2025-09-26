#include "bp_thumbnail_loader.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QProcess>
#include <QDebug>

BPThumbnailLoader::BPThumbnailLoader(QObject *parent)
    : QObject(parent)
    , m_cache(std::make_unique<QCache<QString, QPixmap>>(DEFAULT_CACHE_SIZE)) {

  printf("BPThumbnailLoader: Starting constructor\n");

  try {
    // Set up cache directory
    printf("BPThumbnailLoader: Setting up cache directory\n");
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    printf("BPThumbnailLoader: Cache dir: %s\n", m_cacheDir.toStdString().c_str());

    QDir().mkpath(m_cacheDir);
    printf("BPThumbnailLoader: Constructor completed successfully\n");
  } catch (const std::exception& e) {
    printf("Exception in BPThumbnailLoader constructor: %s\n", e.what());
    throw;
  } catch (...) {
    printf("Unknown exception in BPThumbnailLoader constructor\n");
    throw;
  }
}

BPThumbnailLoader::~BPThumbnailLoader() {
  // Cancel all active watchers
  for (auto watcher : m_activeWatchers) {
    if (watcher->isRunning()) {
      watcher->cancel();
      watcher->waitForFinished();
    }
    delete watcher;
  }
}

void BPThumbnailLoader::requestThumbnail(const QString &routePath, const QString &routeId) {
  // Check memory cache first
  {
    QMutexLocker lock(&m_cacheMutex);
    if (m_cache->contains(routeId)) {
      emit thumbnailReady(routeId, *m_cache->object(routeId));
      return;
    }
  }

  // Check disk cache
  QString cachePath = getCachePath(routeId);
  if (QFile::exists(cachePath)) {
    QPixmap thumbnail = loadFromDisk(cachePath);
    if (!thumbnail.isNull()) {
      QMutexLocker lock(&m_cacheMutex);
      m_cache->insert(routeId, new QPixmap(thumbnail));
      emit thumbnailReady(routeId, thumbnail);
      return;
    }
  }

  // Generate thumbnail asynchronously
  ThumbnailRequest request{routePath, routeId};

  auto future = QtConcurrent::run([this, request]() {
    return this->extractFrameFromVideo(this->findBestVideoFile(request.routePath));
  });

  auto *watcher = new QFutureWatcher<QPixmap>(this);
  m_activeWatchers.append(watcher);

  connect(watcher, &QFutureWatcher<QPixmap>::finished, this, [this, watcher, request]() {
    QPixmap thumbnail = watcher->result();

    if (!thumbnail.isNull()) {
      // Save to both caches
      saveToDisk(thumbnail, getCachePath(request.routeId));

      {
        QMutexLocker lock(&m_cacheMutex);
        m_cache->insert(request.routeId, new QPixmap(thumbnail));
      }

      emit thumbnailReady(request.routeId, thumbnail);
    } else {
      emit thumbnailFailed(request.routeId, tr("Failed to generate thumbnail"));
    }

    // Clean up watcher
    m_activeWatchers.removeAll(watcher);
    watcher->deleteLater();
  });

  watcher->setFuture(future);
}

QString BPThumbnailLoader::findBestVideoFile(const QString &routePath) const {
  QDir dir(routePath);

  // Priority order for video files
  QStringList videoFiles = {
    "fcamera.hevc",  // Front camera HQ
    "ecamera.hevc",  // Wide camera
    "dcamera.hevc",  // Driver camera HQ
    "qcamera.ts"     // Front camera LQ
  };

  // Check each segment directory
  QStringList segments = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

  for (const QString &segment : segments) {
    QString segmentPath = routePath + "/" + segment;

    for (const QString &videoFile : videoFiles) {
      QString fullPath = segmentPath + "/" + videoFile;
      if (QFile::exists(fullPath)) {
        return fullPath;
      }
    }
  }

  // If no segments, check root directory
  for (const QString &videoFile : videoFiles) {
    QString fullPath = routePath + "/" + videoFile;
    if (QFile::exists(fullPath)) {
      return fullPath;
    }
  }

  return QString();
}

QPixmap BPThumbnailLoader::extractFrameFromVideo(const QString &videoPath) const {
  if (videoPath.isEmpty()) {
    return QPixmap();
  }

  // Use ffmpeg to extract a frame
  QProcess ffmpeg;
  QString tempFile = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                     "/thumb_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".jpg";

  QStringList args;
  args << "-i" << videoPath
       << "-ss" << "00:00:05"  // Seek to 5 seconds
       << "-frames:v" << "1"    // Extract 1 frame
       << "-vf" << QString("scale=%1:%2").arg(THUMBNAIL_WIDTH).arg(THUMBNAIL_HEIGHT)
       << "-y"                   // Overwrite output
       << tempFile;

  ffmpeg.start("ffmpeg", args);

  if (!ffmpeg.waitForFinished(5000)) {  // 5 second timeout
    ffmpeg.kill();
    return QPixmap();
  }

  if (ffmpeg.exitCode() != 0) {
    qWarning() << "ffmpeg failed:" << ffmpeg.readAllStandardError();
    return QPixmap();
  }

  QPixmap thumbnail(tempFile);
  QFile::remove(tempFile);  // Clean up temp file

  // If ffmpeg failed, create a placeholder
  if (thumbnail.isNull()) {
    thumbnail = QPixmap(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
    thumbnail.fill(Qt::darkGray);

    QPainter painter(&thumbnail);
    painter.setPen(Qt::white);
    painter.drawText(thumbnail.rect(), Qt::AlignCenter, tr("No Preview"));
  }

  return thumbnail;
}

QString BPThumbnailLoader::getCachePath(const QString &routeId) const {
  return m_cacheDir + "/" + routeId + ".jpg";
}

QPixmap BPThumbnailLoader::loadFromDisk(const QString &cachePath) const {
  return QPixmap(cachePath);
}

bool BPThumbnailLoader::saveToDisk(const QPixmap &pixmap, const QString &cachePath) const {
  return pixmap.save(cachePath, "JPG", 85);  // 85% quality
}

void BPThumbnailLoader::clearCache() {
  QMutexLocker lock(&m_cacheMutex);
  m_cache->clear();

  // Also clear disk cache
  QDir dir(m_cacheDir);
  for (const QString &file : dir.entryList(QStringList() << "*.jpg", QDir::Files)) {
    dir.remove(file);
  }
}

void BPThumbnailLoader::setMaxCacheSize(int size) {
  QMutexLocker lock(&m_cacheMutex);
  m_cache->setMaxCost(size);
}