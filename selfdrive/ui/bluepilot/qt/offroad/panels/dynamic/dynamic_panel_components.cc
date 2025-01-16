// selfdrive/ui/bluepilot/qt/offroad/panels/dynamic/dynamic_panel_components.cc

#include "dynamic_panel_components.h"
#include <QVBoxLayout>

namespace DynamicPanelComponents {

BarGauge::BarGauge(QWidget *parent, const BarGaugeConfig &config)
    : QWidget(parent), config(config) {

    progress = new QProgressBar(this);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(progress);

    // Apply initial configuration
    if (config.maxWidth > 0) {
        setMaximumWidth(config.maxWidth);
    }
    setHeight(config.height);
    updateStyleSheet();
}

void BarGauge::setValue(float newValue) {
    currentValue = qBound(0.0f, newValue, 100.0f);
    progress->setValue(currentValue);
    progress->setFormat(QString("%1%").arg(currentValue, 0, 'f', 1));
    updateStyleSheet();
}

void BarGauge::setConfig(const BarGaugeConfig &newConfig) {
    config = newConfig;
    if (config.maxWidth > 0) {
        setMaximumWidth(config.maxWidth);
    }
    setHeight(config.height);
    updateStyleSheet();
}

void BarGauge::setGradientStops(const QVector<GradientStop> &stops) {
    config.gradientStops = stops;
    updateStyleSheet();
}

void BarGauge::setHeight(int height) {
    config.height = height;
    progress->setFixedHeight(height);
}

void BarGauge::setMaxWidth(int width) {
    config.maxWidth = width;
    if (width > 0) {
        setMaximumWidth(width);
    }
}

void BarGauge::setFontSize(int size) {
    config.fontSize = size;
    updateStyleSheet();
}

void BarGauge::setTextColor(const QString &color) {
    config.textColor = color;
    updateStyleSheet();
}

QString BarGauge::generateGradientStyle() {
    // Find the appropriate gradient based on current value
    QString gradient;
    for (int i = 0; i < config.gradientStops.size(); i++) {
        if (currentValue <= config.gradientStops[i].value) {
            // Get colors for gradient
            QVector<QString> gradientColors;
            if (i > 0) {
                gradientColors = config.gradientStops[i-1].colors;
            } else {
                gradientColors = config.gradientStops[0].colors;
            }

            // Build gradient string
            gradient = "qlineargradient(x1:0, y1:0, x2:1, y2:0";

            // Add color stops
            float step = 1.0f / (gradientColors.size() - 1);
            for (int j = 0; j < gradientColors.size(); j++) {
                float stopPosition = j * step;
                gradient += QString(", stop:%1 %2")
                    .arg(stopPosition, 0, 'f', 2)
                    .arg(gradientColors[j]);
            }

            gradient += ")";
            break;
        }
    }

    // If no match found, use the last gradient
    if (gradient.isEmpty() && !config.gradientStops.isEmpty()) {
        const auto& lastColors = config.gradientStops.last().colors;
        gradient = "qlineargradient(x1:0, y1:0, x2:1, y2:0";
        float step = 1.0f / (lastColors.size() - 1);
        for (int j = 0; j < lastColors.size(); j++) {
            float stopPosition = j * step;
            gradient += QString(", stop:%1 %2")
                .arg(stopPosition, 0, 'f', 2)
                .arg(lastColors[j]);
        }
        gradient += ")";
    }

    return gradient;
}

void BarGauge::updateStyleSheet() {
    QString gradient = generateGradientStyle();

    progress->setStyleSheet(QString(R"(
        QProgressBar {
            border: 2px solid #414868;
            border-radius: 8px;
            background: #1a1b26;
            height: %1px;
            text-align: center;
            font-size: %2px;
            font-weight: bold;
            color: %3;
        }
        QProgressBar::chunk {
            border-radius: 6px;
            background: %4;
        }
    )").arg(config.height)
       .arg(config.fontSize)
       .arg(config.textColor)
       .arg(gradient));
}

// ------------------------------------------------------------------------------------------------
// Pie Chart Widget
// ------------------------------------------------------------------------------------------------
PieChartWidget::PieChartWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(150, 150);
}

void PieChartWidget::setValues(qint64 usedBytes, qint64 freeBytes) {
    used = usedBytes;
    free = freeBytes;
    total = used + free;
    update();
}

void PieChartWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    float startAngle = 0.0f;
    float usedPercent = (total > 0) ? (static_cast<float>(used) / total) : 0.0f;

    // Draw the pie slices
    QRectF circleRect(10, 10, width() - 20, height() - 20);
    // Free slice
    painter.setBrush(QColor("#9ece6a"));
    painter.setPen(Qt::NoPen);
    painter.drawPie(circleRect, static_cast<int>(startAngle * 16),
                    static_cast<int>((1.0f - usedPercent) * 360.0f * 16));

    // Used slice
    painter.setBrush(QColor("#f7768e"));
    painter.drawPie(circleRect,
                    static_cast<int>((startAngle + (1.0f - usedPercent) * 360.0f) * 16),
                    static_cast<int>(usedPercent * 360.0f * 16));

    // Text in the middle
    painter.setPen(QColor("#ffffff"));
    QString text = QString("%1%").arg(QString::number(usedPercent * 100.0f, 'f', 1));
    QFont f = painter.font();
    f.setBold(true);
    f.setPointSize(14);
    painter.setFont(f);
    painter.drawText(circleRect, Qt::AlignCenter, text);
}

} // namespace DynamicPanelComponents
