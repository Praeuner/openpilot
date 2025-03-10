// stats_styles_constants.h

#pragma once
#include <QString>

namespace StatCardStyles {

// Colors
namespace Colors {
const QString SUCCESS = "#9ece6a";
const QString SUCCESS_DARK = "#73c748";
const QString WARNING = "#e0af68";
const QString WARNING_DARK = "#ff9e43";
const QString DANGER = "#f7768e";
const QString DANGER_DARK = "#ff5555";
const QString INFO = "#7aa2f7";
const QString NEUTRAL = "#C9C9C9";
const QString WHITE = "#ffffff";
const QString BACKGROUND = "#1B1B1B";
const QString CARD_BG = "rgba(80, 80, 80, 0.3)";
const QString BORDER = "rgba(255, 255, 255, 0.2)";
} // namespace Colors

// Gauge Color Stops
const QString GRADIENT_STOPS = R"(
    GradientStop {
        {20.0f, {"#9ece6a", "#73c748", "#68b83f"}},
        {40.0f, {"#73d945", "#68c83f", "#5fb838"}},
        {60.0f, {"#e0af68", "#ff9e43", "#ff8f20"}},
        {80.0f, {"#f7768e", "#ff5555", "#ff3333"}},
        {100.0f, {"#ff0000", "#dd0000", "#bb0000"}}
    }
)";

// Progress Bar Style
const QString PROGRESS_BAR = R"(
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
)";

// Card Frame Style
const QString CARD_FRAME = R"(
    #%1 {
        background: transparent;
        border-radius: 15px;
        border: 2px solid rgba(255, 255, 255, 0.2);
        padding: 15px;
    }
)";

// Table Style
const QString TABLE = R"(
    QTableWidget {
        background: transparent;
        border: none;
        font-size: 31px;
    }
    QTableWidget::item {
        padding: 5px;
        border: none;
        font-size: 31px;
    }
)";

// Label Styles
namespace Labels {
const QString HEADER = R"(
        font-size: 42px;
        font-weight: bold;
        color: #ffffff;
    )";

const QString SUBHEADER = R"(
        font-size: 36px;
        color: #888888;
    )";

const QString DATA = R"(
        font-size: 34px;
        color: #C9C9C9;
    )";

const QString METRIC = R"(
        font-size: 31px;
        background: rgba(56, 62, 90, 0.6);
        border-radius: 12px;
        padding: 15px;
        margin: 5px 0;
    )";
} // namespace Labels

} // namespace StatCardStyles
