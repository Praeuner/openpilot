#include "bp_route_card.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QStyleOption>
#include <QDateTime>

#include "selfdrive/ui/qt/util.h"

BPRouteCard::BPRouteCard(const RouteInfo &route, QWidget *parent)
    : QWidget(parent)
    , m_route(route) {

  // Build route path
  m_routePath = parent->property("routesDir").toString() + "/" + route.baseName;

  setupUI();

  // Request thumbnail
  emit thumbnailRequested(m_routePath, m_route.baseName);
}

void BPRouteCard::setupUI() {
  setFixedHeight(CARD_HEIGHT);
  setCursor(Qt::PointingHandCursor);
  setStyleSheet(R"(
    BPRouteCard {
      background-color: #1C1C1E;
      border-radius: 10px;
      padding: 10px;
    }
    BPRouteCard:hover {
      background-color: #2C2C2E;
    }
    QLabel {
      color: white;
      font-size: 14px;
    }
    QPushButton {
      background-color: transparent;
      border: none;
      font-size: 20px;
    }
  )");

  auto *mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(CARD_MARGIN, CARD_MARGIN, CARD_MARGIN, CARD_MARGIN);
  mainLayout->setSpacing(15);

  // Thumbnail
  createThumbnailWidget();
  mainLayout->addWidget(m_thumbnailLabel);

  // Info section
  createInfoWidget();
  auto *infoWidget = findChild<QWidget*>("infoWidget");
  if (infoWidget) {
    mainLayout->addWidget(infoWidget, 1);
  }

  // Star button
  createStarButton();
  mainLayout->addWidget(m_starButton);
}

void BPRouteCard::createThumbnailWidget() {
  m_thumbnailLabel = new QLabel(this);
  m_thumbnailLabel->setFixedSize(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
  m_thumbnailLabel->setScaledContents(true);
  m_thumbnailLabel->setStyleSheet(R"(
    QLabel {
      background-color: #3C3C3E;
      border-radius: 8px;
      border: 1px solid #4C4C4E;
    }
  )");

  // Default placeholder
  QPixmap placeholder(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
  placeholder.fill(QColor("#3C3C3E"));
  QPainter painter(&placeholder);
  painter.setPen(Qt::white);
  painter.setFont(QFont("Inter", 12));
  painter.drawText(placeholder.rect(), Qt::AlignCenter, tr("Loading..."));
  m_thumbnailLabel->setPixmap(placeholder);
}

void BPRouteCard::createInfoWidget() {
  auto *widget = new QWidget(this);
  widget->setObjectName("infoWidget");
  auto *layout = new QVBoxLayout(widget);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(5);

  // Date and time
  m_dateLabel = new QLabel(m_route.timestamp, this);
  m_dateLabel->setStyleSheet("font-size: 16px; font-weight: 500;");
  layout->addWidget(m_dateLabel);

  // Duration and segments row
  auto *statsLayout = new QHBoxLayout();
  statsLayout->setSpacing(15);

  m_durationLabel = new QLabel(QString("⏱ %1").arg(m_route.duration), this);
  m_segmentsLabel = new QLabel(QString("📹 %1 segments").arg(m_route.segments), this);
  m_sizeLabel = new QLabel(QString("💾 %1").arg(m_route.size), this);

  statsLayout->addWidget(m_durationLabel);
  statsLayout->addWidget(m_segmentsLabel);
  statsLayout->addWidget(m_sizeLabel);
  statsLayout->addStretch();

  layout->addLayout(statsLayout);

  // Trip distance if available
  if (m_route.tripMiles > 0) {
    auto *tripLabel = new QLabel(QString("🚗 %1 miles").arg(m_route.tripMiles, 0, 'f', 1), this);
    tripLabel->setStyleSheet("color: #8E8E93;");
    layout->addWidget(tripLabel);
  }

  layout->addStretch();
}

void BPRouteCard::createStarButton() {
  m_starButton = new QPushButton(this);
  m_starButton->setFixedSize(40, 40);
  updateStarStatus(m_route.isStarred);

  connect(m_starButton, &QPushButton::clicked, this, [this]() {
    emit starClicked(m_routePath);
  });
}

void BPRouteCard::setThumbnail(const QPixmap &thumbnail) {
  if (!thumbnail.isNull()) {
    m_thumbnailLabel->setPixmap(thumbnail.scaled(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT,
                                                  Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation));
  }
}

void BPRouteCard::updateStarStatus(bool starred) {
  m_route.isStarred = starred;
  m_starButton->setText(starred ? "⭐" : "☆");
  m_starButton->setStyleSheet(starred ? "color: #FFD700;" : "color: #8E8E93;");
}

void BPRouteCard::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    // Check if star button was clicked
    QPoint starPos = m_starButton->mapFromGlobal(event->globalPos());
    if (m_starButton->rect().contains(starPos)) {
      return;  // Let the button handle it
    }

    emit cardClicked(m_routePath);
  }
  QWidget::mousePressEvent(event);
}

void BPRouteCard::paintEvent(QPaintEvent *event) {
  QStyleOption opt;
  opt.init(this);
  QPainter painter(this);
  style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);
}

void BPRouteCard::enterEvent(QEvent *event) {
  m_isHovered = true;
  update();
  QWidget::enterEvent(event);
}

void BPRouteCard::leaveEvent(QEvent *event) {
  m_isHovered = false;
  update();
  QWidget::leaveEvent(event);
}

QString BPRouteCard::formatRoute() const {
  return QString("%1 - %2 segments - %3")
      .arg(m_route.timestamp)
      .arg(m_route.segments)
      .arg(m_route.size);
}