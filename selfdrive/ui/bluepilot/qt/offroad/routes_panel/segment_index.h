#ifndef SELFDRIVE_UI_BLUEPILOT_QT_OFFROAD_ROUTES_PANEL_SEGMENT_INDEX_H_
#define SELFDRIVE_UI_BLUEPILOT_QT_OFFROAD_ROUTES_PANEL_SEGMENT_INDEX_H_

#include <QString>
#include <QStringList>
#include <tuple>
#include <vector>

/// @brief Indexes route segments and provides conversions between global and
///        segment-local timestamps.
class SegmentIndex {
public:
  SegmentIndex();
  ~SegmentIndex();

  bool loadRoute(const QString &route_id, const QString &routes_dir);

  std::tuple<int, int64_t> lookupGlobalMs(int64_t global_ms) const;
  int segmentForMs(int64_t global_ms) const;
  int64_t getGlobalMs(int segment_idx, int64_t local_ms) const;
  QString getSegmentPath(int segment_idx) const;
  int64_t totalDurationMs() const;
  int totalSegments() const;

private:
  struct SegmentInfo {
    QString path;
    int64_t start_ms = 0;
    int64_t duration_ms = 0;
  };

  QString route_id_;
  QString routes_dir_;
  std::vector<SegmentInfo> segments_;
  int64_t total_duration_ms_ = 0;
};

#endif  // SELFDRIVE_UI_BLUEPILOT_QT_OFFROAD_ROUTES_PANEL_SEGMENT_INDEX_H_
