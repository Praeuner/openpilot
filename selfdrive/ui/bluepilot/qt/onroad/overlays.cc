#include "selfdrive/ui/bluepilot/qt/onroad/overlays.h"
#include "selfdrive/ui/bluepilot/qt/onroad/widgets/hybrid_drive_gauge.h"
#include "selfdrive/ui/qt/util.h"
#include "common/timing.h"

// Static member definitions
int BluepilotOverlays::blinker_frame = 0;
int BluepilotOverlays::blinker_state = 0;
double BluepilotOverlays::standstill_start_time = 0.0;
float BluepilotOverlays::standstillElapsedTime = 0.0;

void BluepilotOverlays::drawAll(QPainter &painter, const QRect &rect, const UIState &s) {
  const SubMaster &sm = *(s.sm);
  const auto car_state = sm["carState"].getCarState();

  // Draw blinkers if active
  bool left_blinker = car_state.getLeftBlinker();
  bool right_blinker = car_state.getRightBlinker();
  if (left_blinker || right_blinker) {
    drawBlinkers(painter, rect, s);
  } else {
    blinker_frame = 0;
  }

  // Draw standstill timer
  if (s.scene.stand_still_timer && car_state.getStandstill()) {
    drawStandstillTimer(painter, rect, s);
  }

  // Draw hybrid gauges if available
  if (s.scene.show_hybrid_drive_overlay && sm.valid("carStateBP")) {
    const auto car_state_bp = sm["carStateBP"].getCarStateBP();
    const auto hybrid_drive = car_state_bp.getHybridDrive();

    if (hybrid_drive.getDataAvailable()) {
      int gauge_width = rect.width() * 0.39;
      int gauge_height = 130;
      int bottom_margin = 30;
      int y_position = rect.height() - gauge_height - bottom_margin;

      QRect gauge_rect((rect.width() - gauge_width) / 2, y_position, gauge_width, gauge_height);

      HybridDriveGauge::drawGauge(painter, gauge_rect,
                                  hybrid_drive.getThrottleDemandPercent(),
                                  hybrid_drive.getThrottleThresholdPercent(),
                                  QString::fromStdString(hybrid_drive.getPowerFlowMode()),
                                  QString::fromStdString(hybrid_drive.getEngineOnReason()));

      // Draw battery gauge if enabled
      if (s.scene.show_hybrid_battery_overlay) {
        const auto hybrid_battery = car_state_bp.getHybridBattery();
        if (hybrid_battery.getDataAvailable()) {
          int batt_width = gauge_width * 0.25;
          QRect battery_rect(gauge_rect.right() + 10, y_position, batt_width, gauge_height);

          HybridBatteryGauge::drawGauge(painter, battery_rect,
                                        hybrid_battery.getSocActual(),
                                        hybrid_battery.getSocMinPerc(),
                                        hybrid_battery.getSocMaxPerc(),
                                        hybrid_battery.getVoltActual(),
                                        hybrid_battery.getVoltLowLimit(),
                                        hybrid_battery.getVoltHighLimit(),
                                        hybrid_battery.getAmpsActual());
        }
      }
    }
  }
}

void BluepilotOverlays::drawBlinkers(QPainter &painter, const QRect &rect, const UIState &s) {
  const SubMaster &sm = *(s.sm);
  const auto car_state = sm["carState"].getCarState();

  bool left_blinker = car_state.getLeftBlinker();
  bool right_blinker = car_state.getRightBlinker();
  // bool left_blindspot = car_state.getLeftBlindspot();
  // bool right_blindspot = car_state.getRightBlindspot();

  blinker_frame++;
  int state = blinkerPulse(blinker_frame);
  int blinker_x = 180;
  int blinker_y = 90;
  int blinker_size = 120;

  if (left_blinker) {
    drawLeftTurnSignal(painter, rect.center().x() - (blinker_x + blinker_size),
                       blinker_y, blinker_size, state);
  }
  if (right_blinker) {
    drawRightTurnSignal(painter, rect.center().x() + blinker_x,
                        blinker_y, blinker_size, state);
  }
}

void BluepilotOverlays::drawStandstillTimer(QPainter &painter, const QRect &rect, const UIState &s) {
  const SubMaster &sm = *(s.sm);
  const auto car_state = sm["carState"].getCarState();

  static bool prev_standstill = false;
  bool standstill = car_state.getStandstill();
  double current_time = millis_since_boot() / 1000.0;

  if (!prev_standstill && standstill) {
    standstill_start_time = current_time;
    standstillElapsedTime = 0.0;
  } else if (standstill) {
    standstillElapsedTime = current_time - standstill_start_time;
  } else {
    standstillElapsedTime = 0.0;
  }

  prev_standstill = standstill;

  if (standstillElapsedTime > 0.1) {
    int x = rect.right() - 200;
    int y = rect.center().y() - 45;

    int minute = (int)(standstillElapsedTime / 60);
    int second = (int)(standstillElapsedTime) - (minute * 60);

    QString labelText = "STOP";
    QString timeText = QString("%1:%2").arg(minute).arg(second, 2, 10, QChar('0'));

    // Background
    QRect backgroundRect(x - 120, y - 70, 240, 180);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 120));
    painter.drawRoundedRect(backgroundRect, 15, 15);

    // Draw label
    painter.setFont(InterFont(80, QFont::DemiBold));
    drawColoredText(painter, x, y, labelText, QColor(255, 175, 3, 240));

    // Draw time
    painter.setFont(InterFont(95, QFont::DemiBold));
    drawColoredText(painter, x, y + 90, timeText, QColor(255, 255, 255, 240));
  }
}

int BluepilotOverlays::blinkerPulse(int frame) {
  return (frame % UI_FREQ < (UI_FREQ / 2)) ? 1 : 0;
}

void BluepilotOverlays::drawColoredText(QPainter &p, int x, int y, const QString &text, QColor color) {
  QRect real_rect = p.fontMetrics().boundingRect(text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});

  p.setPen(color);
  p.drawText(real_rect.x(), real_rect.bottom(), text);
}

void BluepilotOverlays::drawLeftTurnSignal(QPainter &painter, int x, int y, int size, int state) {
  painter.setRenderHint(QPainter::Antialiasing, true);

  QColor circle_color = (state == 1) ? QColor(30, 215, 96) : QColor(22, 156, 69);
  QColor arrow_color = (state == 1) ? QColor(255, 255, 255) : QColor(9, 56, 27);

  // Draw circle
  painter.setPen(Qt::NoPen);
  painter.setBrush(circle_color);
  painter.drawEllipse(x, y, size, size);

  // Draw arrow
  int arrowSize = 50;
  int arrowX = x + (size - arrowSize) / 4;
  int arrowY = y + (size - arrowSize) / 2;
  painter.setBrush(arrow_color);

  QPolygon arrowPolygon;
  arrowPolygon << QPoint(arrowX + 10, arrowY + arrowSize / 2)
               << QPoint(arrowX + arrowSize - 3, arrowY)
               << QPoint(arrowX + arrowSize, arrowY)
               << QPoint(arrowX + arrowSize, arrowY + arrowSize)
               << QPoint(arrowX + arrowSize - 3, arrowY + arrowSize)
               << QPoint(arrowX + 10, arrowY + arrowSize / 2);
  painter.drawPolygon(arrowPolygon);

  // Draw tail
  int tailWidth = arrowSize / 2.25;
  int tailHeight = arrowSize / 2;
  QRect tailRect(arrowX + arrowSize - 3, arrowY + arrowSize / 4, tailWidth, tailHeight);
  painter.fillRect(tailRect, arrow_color);
}

void BluepilotOverlays::drawRightTurnSignal(QPainter &painter, int x, int y, int size, int state) {
  painter.setRenderHint(QPainter::Antialiasing, true);

  QColor circle_color = (state == 1) ? QColor(30, 215, 96) : QColor(22, 156, 69);
  QColor arrow_color = (state == 1) ? QColor(255, 255, 255) : QColor(9, 56, 27);

  // Draw circle
  painter.setPen(Qt::NoPen);
  painter.setBrush(circle_color);
  painter.drawEllipse(x, y, size, size);

  // Draw arrow
  int arrowSize = 50;
  int arrowX = x + (size - arrowSize) / 2 + (arrowSize / 2.5) - 3;
  int arrowY = y + (size - arrowSize) / 2;
  painter.setBrush(arrow_color);

  QPolygon arrowPolygon;
  arrowPolygon << QPoint(arrowX + arrowSize - 10, arrowY + arrowSize / 2)
               << QPoint(arrowX + 3, arrowY)
               << QPoint(arrowX, arrowY)
               << QPoint(arrowX, arrowY + arrowSize)
               << QPoint(arrowX + 3, arrowY + arrowSize)
               << QPoint(arrowX + arrowSize - 10, arrowY + arrowSize / 2);
  painter.drawPolygon(arrowPolygon);

  // Draw tail
  int tailWidth = arrowSize / 2.25;
  int tailHeight = arrowSize / 2;
  QRect tailRect(arrowX - tailWidth + 3, arrowY + arrowSize / 4, tailWidth, tailHeight);
  painter.fillRect(tailRect, arrow_color);
}