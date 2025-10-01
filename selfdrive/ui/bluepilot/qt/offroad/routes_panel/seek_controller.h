#pragma once

#include <QObject>
#include <QTimer>

/// @brief Coalesces high-frequency seek requests to avoid overwhelming the
///        worker thread.
class SeekController : public QObject {
  Q_OBJECT

public:
  explicit SeekController(QObject *parent = nullptr);
  ~SeekController() override = default;

  void requestSeek(int64_t target_ms);

signals:
  void seekReady(int64_t target_ms);

private slots:
  void executePendingSeek();

private:
  QTimer *coalesce_timer_ = nullptr;
  int64_t pending_seek_ms_ = -1;
  bool has_pending_seek_ = false;

  static constexpr int kCoalesceDelayMs = 80;
};
