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

  main_layout_ = new QVBoxLayout(this);
  // Use minimal margins to maximize width usage
  main_layout_->setContentsMargins(20, 30, 20, 30);
  main_layout_->setSpacing(10);

  auto add_stats_layouts = [=](const QString &title, StatsLabels& labels, QLabel** title_label) {
    QVBoxLayout* section_layout = new QVBoxLayout;
    section_layout->setContentsMargins(0, 0, 0, 0);
    section_layout->setSpacing(10);

    // Title
    *title_label = newLabel(title, "title");
    section_layout->addWidget(*title_label);

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
    main_layout_->addLayout(section_layout);
  };

  add_stats_layouts(tr("ALL TIME"), all_, &all_title_);
  main_layout_->addSpacing(5);
  add_stats_layouts(tr("PAST WEEK"), week_, &week_title_);

  // Set initial height to full size, but allow dynamic scaling down to ~192px (0.35 scale)
  setMinimumHeight(200);
  resize(width(), 550);

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
      font-weight: 600;
      color: #ffffff;
      padding: 5px 0px;
    }

    QLabel[type="number"] {
      font-weight: 700;
      color: #18b4ff;
      padding: 2px 0px;
    }

    QLabel[type="unit"] {
      font-weight: 400;
      color: #b0b0b0;
      padding: 2px 0px;
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

void DriveStats::resizeEvent(QResizeEvent* event) {
  QFrame::resizeEvent(event);
  updateFontSizes();
}

void DriveStats::updateFontSizes() {
  int available_height = height();
  if (available_height <= 0) return;

  // Calculate scale factor based on available height
  // Base sizes: title=48, number=66, unit=42
  // Total base content height: ~550px (2 sections with padding)
  // Use the actual height to determine scale, capping at 1.0
  float scale = available_height / 550.0f;

  // Cap maximum at 1.0 (original size), minimum at 0.35
  scale = std::max(0.35f, std::min(1.0f, scale));

  int title_size = static_cast<int>(48 * scale);
  int number_size = static_cast<int>(66 * scale);
  int unit_size = static_cast<int>(42 * scale);

  // Ensure minimum readable sizes and maximum original sizes
  title_size = std::min(48, std::max(20, title_size));
  number_size = std::min(66, std::max(26, number_size));
  unit_size = std::min(42, std::max(18, unit_size));

  // Adjust margins and spacing based on scale
  int top_bottom_margin = static_cast<int>(30 * scale);
  int side_margin = static_cast<int>(20 * scale);
  int main_spacing = static_cast<int>(10 * scale);

  top_bottom_margin = std::min(30, std::max(10, top_bottom_margin));
  side_margin = std::min(20, std::max(10, side_margin));
  main_spacing = std::min(10, std::max(3, main_spacing));

  main_layout_->setContentsMargins(side_margin, top_bottom_margin, side_margin, top_bottom_margin);
  main_layout_->setSpacing(main_spacing);

  // Update title fonts
  QFont title_font = all_title_->font();
  title_font.setPixelSize(title_size);
  all_title_->setFont(title_font);
  week_title_->setFont(title_font);

  // Update number fonts
  QFont number_font;
  number_font.setPixelSize(number_size);
  all_.routes->setFont(number_font);
  all_.distance->setFont(number_font);
  all_.hours->setFont(number_font);
  week_.routes->setFont(number_font);
  week_.distance->setFont(number_font);
  week_.hours->setFont(number_font);

  // Update unit fonts
  QFont unit_font;
  unit_font.setPixelSize(unit_size);

  // Find and update all unit labels
  QList<QLabel*> all_labels = findChildren<QLabel*>();
  for (QLabel* label : all_labels) {
    if (label->property("type").toString() == "unit") {
      label->setFont(unit_font);
    }
  }
}
