// selfdrive/ui/bluepilot/qt/offroad/panels/dynamic/dynamic_panel_components.h

#pragma once

#include <QWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QPainter>
#include <QColor>
#include <QVector>

namespace DynamicPanelComponents {

// Defines a gradient stop with multiple colors
struct GradientStop {
    float value;              // The threshold value for this stop
    QVector<QString> colors;  // Array of colors for this stop's gradient

    // Add default constructor
    GradientStop() : value(0.0f) {}

    // Existing constructor
    GradientStop(float v, QVector<QString> c) : value(v), colors(c) {}
};


inline QVector<GradientStop> DefaultGradientThreeColor() {
    return {
        {60.0f, {"#9ece6a", "#73c748"}},
        {80.0f, {"#e0af68", "#ff9e43"}},
        {100.0f, {"#f7768e", "#ff5555"}}
    };
}

inline QVector<GradientStop> DetailedGradientFiveColor() {
    return {
        {20.0f, {"#9ece6a", "#73c748", "#68b83f"}},
        {40.0f, {"#73d945", "#68c83f", "#5fb838"}},
        {60.0f, {"#e0af68", "#ff9e43", "#ff8f20"}},
        {80.0f, {"#f7768e", "#ff5555", "#ff3333"}},
        {100.0f, {"#ff0000", "#dd0000", "#bb0000"}}
    };
}

// Configuration for the bar gauge
struct BarGaugeConfig {
    int height = 45;           // Height of the gauge
    int maxWidth = -1;         // Optional max width (-1 for no limit)
    int fontSize = 40;         // Font size for the value display
    QString textColor = "#ffffff"; // Color of the value text

    // Default gradient configuration
    QVector<GradientStop> gradientStops = DefaultGradientThreeColor();
};

class BarGauge : public QWidget {
    Q_OBJECT
public:
    explicit BarGauge(QWidget *parent = nullptr, const BarGaugeConfig &config = BarGaugeConfig());

    void setValue(float newValue);
    float value() const { return currentValue; }

    // Setters for runtime configuration changes
    void setConfig(const BarGaugeConfig &newConfig);
    void setGradientStops(const QVector<GradientStop> &stops);
    void setHeight(int height);
    void setMaxWidth(int width);
    void setFontSize(int size);
    void setTextColor(const QString &color);

private:
    void updateStyleSheet();
    QString generateGradientStyle();

    QProgressBar *progress;
    BarGaugeConfig config;
    float currentValue = 0.0f;
};

// ------------------------------------------------------------------------------------------------
// Pie Chart Widget
// ------------------------------------------------------------------------------------------------
class PieChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit PieChartWidget(QWidget *parent = nullptr);
    void setValues(qint64 usedBytes, qint64 freeBytes);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    qint64 used = 0;
    qint64 free = 0;
    qint64 total = 0;
};

// ------------------------------------------------------------------------------------------------
// Circular Close Button
// ------------------------------------------------------------------------------------------------
class CircularCloseButton : public QPushButton {
    Q_OBJECT
public:
    explicit CircularCloseButton(QWidget *parent = nullptr) : QPushButton(parent) {
        setFixedSize(64, 64);  // Makes it a perfect circle
        setText("×");  // Unicode multiplication sign works well as an X
        setStyleSheet(R"(
            QPushButton {
                background-color: #465BEA;
                border-radius: 32px;
                color: white;
                font-size: 48px;
                font-weight: bold;
                border: none;
            }
            QPushButton:pressed {
                background-color: #444444;
            }
        )");
    }
};

} // namespace DynamicPanelComponents
