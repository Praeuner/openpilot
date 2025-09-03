/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "selfdrive/ui/sunnypilot/qt/widgets/drive_stats.h"

#include <QDebug>
#include <QGridLayout>
#include <QVBoxLayout>

#include "common/params.h"
#include "selfdrive/ui/qt/request_repeater.h"
#include "selfdrive/ui/qt/util.h"

static QLabel* newLabel(const QString& text, const QString &type) {
  QLabel* label = new QLabel(text);
  label->setProperty("type", type);
  return label;
}

DriveStats::DriveStats(QWidget* parent) : QFrame(parent) {
  metric_ = Params().getBool("IsMetric");

  QVBoxLayout* main_layout = new QVBoxLayout(this);
  // Use minimal margins to maximize width usage
  main_layout->setContentsMargins(20, 30, 20, 30);
  main_layout->setSpacing(10);

  auto add_stats_layouts = [=](const QString &title, StatsLabels& labels) {
    QVBoxLayout* section_layout = new QVBoxLayout;
    section_layout->setContentsMargins(0, 0, 0, 0);
    section_layout->setSpacing(10);

    // Title
    section_layout->addWidget(newLabel(title, "title"));

    // Single background container for all stats
    QFrame* stats_container = new QFrame;
    stats_container->setProperty("type", "stats_container");
    QHBoxLayout* stats_layout = new QHBoxLayout(stats_container);
    stats_layout->setContentsMargins(15, 15, 15, 15);
    stats_layout->setSpacing(0);

    // Drives section
    QVBoxLayout* drives_layout = new QVBoxLayout;
    drives_layout->setContentsMargins(0, 0, 0, 0);
    drives_layout->setSpacing(5);
    drives_layout->addWidget(labels.routes = newLabel("0", "number"), 0, Qt::AlignCenter);
    drives_layout->addWidget(newLabel(tr("Drives"), "unit"), 0, Qt::AlignCenter);
    stats_layout->addLayout(drives_layout);

    // Distance section
    QVBoxLayout* distance_layout = new QVBoxLayout;
    distance_layout->setContentsMargins(0, 0, 0, 0);
    distance_layout->setSpacing(5);
    distance_layout->addWidget(labels.distance = newLabel("0", "number"), 0, Qt::AlignCenter);
    distance_layout->addWidget(labels.distance_unit = newLabel(getDistanceUnit(), "unit"), 0, Qt::AlignCenter);
    stats_layout->addLayout(distance_layout);

    // Hours section
    QVBoxLayout* hours_layout = new QVBoxLayout;
    hours_layout->setContentsMargins(0, 0, 0, 0);
    hours_layout->setSpacing(5);
    hours_layout->addWidget(labels.hours = newLabel("0", "number"), 0, Qt::AlignCenter);
    hours_layout->addWidget(newLabel(tr("Hours"), "unit"), 0, Qt::AlignCenter);
    stats_layout->addLayout(hours_layout);

    section_layout->addWidget(stats_container);
    main_layout->addLayout(section_layout);
  };

  add_stats_layouts(tr("ALL TIME"), all_);
  main_layout->addSpacing(5);
  add_stats_layouts(tr("PAST WEEK"), week_);

  if (auto dongleId = getDongleId()) {
    QString url = CommaApi::BASE_URL + "/v1.1/devices/" + *dongleId + "/stats";
    RequestRepeater* repeater = new RequestRepeater(this, url, "ApiCache_DriveStats", 30);
    QObject::connect(repeater, &RequestRepeater::requestDone, this, &DriveStats::parseResponse);
  }

  setStyleSheet(R"(
    DriveStats {
      background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                  stop: 0 #2c2c2c, stop: 1 #1a1a1a);
      border-radius: 15px;
      border: 1px solid rgba(255, 255, 255, 0.1);
    }

    QLabel[type="title"] {
      font-size: 48px;
      font-weight: 600;
      color: #ffffff;
      padding: 10px 0px;
    }

    QLabel[type="number"] {
      font-size: 66px;
      font-weight: 700;
      color: #18b4ff;
      padding: 8px 0px;
    }

    QLabel[type="unit"] {
      font-size: 42px;
      font-weight: 400;
      color: #b0b0b0;
      padding: 5px 0px;
    }

    QFrame[type="stats_container"] {
      background-color: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 12px;
    }
  )");
}

void DriveStats::updateStats() {
  auto update = [=](const QJsonObject& obj, StatsLabels& labels) {
    labels.routes->setText(QString::number((int)obj["routes"].toDouble()));
    labels.distance->setText(QString::number(int(obj["distance"].toDouble() * (metric_ ? MILE_TO_KM : 1))));
    labels.distance_unit->setText(getDistanceUnit());
    labels.hours->setText(QString::number((int)(obj["minutes"].toDouble() / 60)));
  };

  QJsonObject json = stats_.object();
  update(json["all"].toObject(), all_);
  update(json["week"].toObject(), week_);
}

void DriveStats::parseResponse(const QString& response, bool success) {
  if (!success) return;

  QJsonDocument doc = QJsonDocument::fromJson(response.trimmed().toUtf8());
  if (doc.isNull()) {
    qDebug() << "JSON Parse failed on getting past drives statistics";
    return;
  }
  stats_ = doc;
  updateStats();
}

void DriveStats::showEvent(QShowEvent* event) {
  bool metric = Params().getBool("IsMetric");
  if (metric_ != metric) {
    metric_ = metric;
    updateStats();
  }
}
