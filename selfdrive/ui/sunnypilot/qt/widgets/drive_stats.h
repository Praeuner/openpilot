/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include <QJsonDocument>
#include <QLabel>
#include <QVBoxLayout>

class DriveStats : public QFrame {
  Q_OBJECT

public:
  explicit DriveStats(QWidget* parent = 0);

private:
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void updateStats();
  void updateFontSizes();
  inline QString getDistanceUnit() const { return metric_ ? tr("KM") : tr("Miles"); }

  bool metric_;
  QJsonDocument stats_;
  struct StatsLabels {
    QLabel *routes, *distance, *distance_unit, *hours;
  } all_, week_;
  QLabel *all_title_, *week_title_;
  QVBoxLayout *main_layout_;

private slots:
  void parseResponse(const QString &response, bool success);
};
