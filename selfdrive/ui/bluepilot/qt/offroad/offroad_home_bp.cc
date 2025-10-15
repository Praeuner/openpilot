/**
 * BluePilot version badge extensions for offroad home screen
 */

#include "selfdrive/ui/bluepilot/qt/offroad/offroad_home_bp.h"

#include <QFile>
#include <QTextStream>
#include <QPainter>
#include <QPainterPath>
#include <QHBoxLayout>

#include "selfdrive/ui/qt/util.h"

// Custom badge widget with sidebar card styling
class BadgeWidget : public QWidget {
public:
  BadgeWidget(const QString &text, const QColor &accentColor, QWidget *parent = nullptr)
    : QWidget(parent), text_(text), accentColor_(accentColor) {
    setFixedHeight(58);
    setMinimumWidth(100);
    setCursor(Qt::PointingHandCursor);
  }

  void setText(const QString &text) {
    text_ = text;
    updateGeometry();
    update();
  }

protected:
  void paintEvent(QPaintEvent *event) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect rect = this->rect();

    // Draw card shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawRoundedRect(rect.adjusted(2, 2, 0, 2), 12, 12);

    // Draw card background
    QColor cardBg(48, 49, 51);
    p.setBrush(cardBg);
    QPainterPath path;
    path.addRoundedRect(rect, 12, 12);
    p.drawPath(path);

    // Draw accent bar on left
    p.setBrush(accentColor_);
    p.setClipRect(rect.x(), rect.y(), 6, rect.height());
    p.drawRoundedRect(rect, 12, 12);
    p.setClipping(false);

    // Draw text
    p.setPen(Qt::white);
    QFont font("Inter", 32, QFont::DemiBold);
    p.setFont(font);
    p.drawText(rect.adjusted(18, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, text_);
  }

  QSize sizeHint() const override {
    QFont font("Inter", 32, QFont::DemiBold);
    QFontMetrics fm(font);
    int width = fm.horizontalAdvance(text_) + 35;
    return QSize(width, 58);
  }

private:
  QString text_;
  QColor accentColor_;
};

// Create version badge widget for BluePilot
QWidget* createBluePilotVersionWidget(QWidget *parent) {
  QWidget* version_widget = new QWidget(parent);
  QHBoxLayout* version_layout = new QHBoxLayout(version_widget);
  version_layout->setContentsMargins(0, 0, 0, 0);
  version_layout->setSpacing(8);

  BadgeWidget* brand_badge = new BadgeWidget("", QColor(74, 144, 226), version_widget);
  BadgeWidget* branch_badge = new BadgeWidget("", QColor(155, 89, 182), version_widget);
  BadgeWidget* commit_badge = new BadgeWidget("", QColor(230, 126, 34), version_widget);
  BadgeWidget* date_badge = new BadgeWidget("", QColor(149, 165, 166), version_widget);

  brand_badge->setObjectName("brand_badge");
  branch_badge->setObjectName("branch_badge");
  commit_badge->setObjectName("commit_badge");
  date_badge->setObjectName("date_badge");

  version_layout->addWidget(brand_badge);
  version_layout->addWidget(branch_badge);
  version_layout->addWidget(commit_badge);
  version_layout->addWidget(date_badge);

  return version_widget;
}

// Refresh version badge widget with current version info
void refreshBluePilotVersion(QWidget *widget, Params &params) {
  if (!widget) return;

  BadgeWidget* brand_badge = widget->findChild<BadgeWidget*>("brand_badge");
  BadgeWidget* branch_badge = widget->findChild<BadgeWidget*>("branch_badge");
  BadgeWidget* commit_badge = widget->findChild<BadgeWidget*>("commit_badge");
  BadgeWidget* date_badge = widget->findChild<BadgeWidget*>("date_badge");

  if (!brand_badge || !branch_badge || !commit_badge || !date_badge) return;

  // Parse version info from UpdaterCurrentDescription
  QString desc = QString::fromStdString(params.get("UpdaterCurrentDescription"));
  QStringList parts = desc.split(" / ");

  // Read BP version from BPVERSION file (UI runs from selfdrive/ui/)
  QString bpVersion;
  QFile versionFile("../../BPVERSION");
  if (versionFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&versionFile);
    QString line = in.readLine();
    if (!line.isEmpty()) {
      bpVersion = line.trimmed();
    }
    versionFile.close();
  }

  // If BPVERSION not found, fall back to openpilot version from UpdaterCurrentDescription
  if (bpVersion.isEmpty() && parts.size() >= 1) {
    bpVersion = parts[0].trimmed();
  }

  // Combine brand and version into one badge
  QString brandVersion = bpVersion.isEmpty() ? getBrand() : getBrand() + " v" + bpVersion;
  brand_badge->setText(brandVersion);

  if (parts.size() >= 4) {
    branch_badge->setText(parts[1].trimmed());
    commit_badge->setText(parts[2].trimmed());
    date_badge->setText(parts[3].trimmed());
  } else {
    branch_badge->setVisible(false);
    commit_badge->setVisible(false);
    date_badge->setVisible(false);
  }
}
