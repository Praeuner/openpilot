#pragma once

#include <QWidget>
#include <QTimer>

#include "selfdrive/ui/ui.h"

// ============================================================================
// BluePilot Alert Rendering System
// ============================================================================
//
// This module implements a simplified alert rendering system with two modes:
//
// 1. FULLSCREEN - Critical safety alerts (e.g., "BRAKE!", "TAKE CONTROL")
// 2. PILL - All other alerts as bottom-centered pills
//
// Alert Properties:
// - Status: NORMAL (info), USER_PROMPT (warning), CRITICAL (danger)
// - Size: SMALL (compact), MID (two-line), FULL (fullscreen)
// ============================================================================

// Pill alert sizes
enum class PillAlertSize {
  PILL_SMALL,   // 1-line pill: 74pt font, 70px horizontal padding
  PILL_MEDIUM,  // 2-line pill: 88pt/66pt fonts, 80px horizontal padding
};

// Pill dimensions with dynamically scaled fonts
struct PillDimensions {
  int width;       // Total pill width including padding
  int height;      // Dynamic height based on text height + vertical padding
  int fontSize1;   // Font size for primary text (scaled if needed)
  int fontSize2;   // Font size for secondary text (0 if single-line, scaled if needed)
};

class OnroadAlertsBP : public QWidget {
  Q_OBJECT

public:
  OnroadAlertsBP(QWidget *parent = 0);
  ~OnroadAlertsBP();
  void updateState(const UIState &s);
  void clear();

protected:
  void resizeEvent(QResizeEvent *event) override;
  struct Alert {
    QString text1;
    QString text2;
    QString type;
    cereal::SelfdriveState::AlertSize size;
    cereal::SelfdriveState::AlertStatus status;

    bool equal(const Alert &other) const {
      return text1 == other.text1 && text2 == other.text2 && type == other.type;
    }
  };

  // Fullscreen alert colors - opaque, matching pill alert style
  const QMap<cereal::SelfdriveState::AlertStatus, QColor> alert_colors = {
    {cereal::SelfdriveState::AlertStatus::NORMAL, QColor(45, 46, 48, 255)},        // Dark neutral (matches pills)
    {cereal::SelfdriveState::AlertStatus::USER_PROMPT, QColor(220, 100, 20, 255)}, // Orange warning (matches pills)
    {cereal::SelfdriveState::AlertStatus::CRITICAL, QColor(201, 34, 49, 255)},     // Red critical (opaque)
  };

  void paintEvent(QPaintEvent*) override;
  OnroadAlertsBP::Alert getAlert(const SubMaster &sm, uint64_t started_frame);

  // Pill rendering methods
  PillAlertSize getPillSize(const Alert &alert) const;
  PillDimensions calculatePillDimensions(const QString &text1, const QString &text2, PillAlertSize size) const;
  QRect calculatePillRect(int pillWidth, int pillHeight) const;
  void drawPillAlert(QPainter &p, const QRect &rect, const PillDimensions &dims, float pulseOpacity);
  void drawFullscreenAlert(QPainter &p);
  QColor getPillBackgroundColor(cereal::SelfdriveState::AlertStatus status) const;
  QColor getPillBorderColor(cereal::SelfdriveState::AlertStatus status) const;

  QColor bg;
  Alert alert = {};

  // Pulsing animation for warning pills
  QTimer *pulse_timer = nullptr;
  float pulse_opacity = 1.0;
  bool pulse_increasing = false;
};
