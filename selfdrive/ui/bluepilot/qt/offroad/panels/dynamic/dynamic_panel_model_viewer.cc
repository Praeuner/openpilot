// selfdrive/ui/bluepilot/qt/offroad/panels/dynamic/dynamic_panel_model_viewer.cc

#include "dynamic_panel_model_viewer.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QTimer>
#include <QTextStream>
#include <QFont>
#include <QObject>
#include <QWidget>
#include <QPushButton>

ModelDataViewer::ModelDataViewer(QWidget* parent) : QWidget(parent) {
    state = uiState();

    QVBoxLayout* layout = new QVBoxLayout(this);
    // Remove all margins to prevent extra spacing
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    dataDisplay = new QPlainTextEdit(this);
    dataDisplay->setReadOnly(true);
    dataDisplay->setFont(QFont("Monospace"));
    dataDisplay->setStyleSheet(R"(
        QPlainTextEdit {
            background-color: #1B1B1B;
            color: #C9C9C9;
            font-size: 35px;
            padding: 50px;
            border: none;
        }
    )");

    layout->addWidget(dataDisplay);

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &ModelDataViewer::updateModelData);
    updateTimer->start(100);
}

void ModelDataViewer::updateModelData() {
    // if (!state || !state->sm) return;
    // dataDisplay->setPlainText(formatModelData());
    if (!hasValidModelData()) return;

    // Update display
    dataDisplay->setPlainText(formatModelData());

    // Try to save file
    saveModelDataToFile();
}

QString ModelDataViewer::formatModelData() {
    QString text;
    QTextStream stream(&text);

    auto model = (*state->sm)["modelV2"].getModelV2();

    // Position Data
    stream << "=== Position Data ===\n";
    auto position = model.getPosition();
    stream << QString("X values: ");
    for (float x : position.getX()) stream << QString::number(x, 'f', 2) << " ";
    stream << "\n";

    stream << QString("Y values: ");
    for (float y : position.getY()) stream << QString::number(y, 'f', 2) << " ";
    stream << "\n";

    stream << QString("Z values: ");
    for (float z : position.getZ()) stream << QString::number(z, 'f', 2) << " ";
    stream << "\n\n";

    // Lane Lines
    stream << "=== Lane Lines ===\n";
    auto laneLines = model.getLaneLines();
    auto probs = model.getLaneLineProbs();
    for (int i = 0; i < laneLines.size(); i++) {
        stream << QString("Lane %1 (prob: %2):\n").arg(i).arg(probs[i], 0, 'f', 3);
        stream << "  X: ";
        for (float x : laneLines[i].getX()) stream << QString::number(x, 'f', 2) << " ";
        stream << "\n  Y: ";
        for (float y : laneLines[i].getY()) stream << QString::number(y, 'f', 2) << " ";
        stream << "\n  Z: ";
        for (float z : laneLines[i].getZ()) stream << QString::number(z, 'f', 2) << " ";
        stream << "\n\n";
    }

    // Road Edges
    stream << "=== Road Edges ===\n";
    auto roadEdges = model.getRoadEdges();
    auto roadEdgeStds = model.getRoadEdgeStds();
    for (int i = 0; i < roadEdges.size(); i++) {
        stream << QString("Edge %1 (std: %2):\n").arg(i).arg(roadEdgeStds[i], 0, 'f', 3);
        stream << "  X: ";
        for (float x : roadEdges[i].getX()) stream << QString::number(x, 'f', 2) << " ";
        stream << "\n  Y: ";
        for (float y : roadEdges[i].getY()) stream << QString::number(y, 'f', 2) << " ";
        stream << "\n  Z: ";
        for (float z : roadEdges[i].getZ()) stream << QString::number(z, 'f', 2) << " ";
        stream << "\n\n";
    }

    return text;
}

ModelDataViewerDialog::ModelDataViewerDialog(QWidget* parent) : DynamicPanelFullScreenDialog(parent) {
    viewer = new ModelDataViewer(this);

    // Set up the content with empty description to avoid extra space
    setupContent(tr("Model Data Viewer"), "");

    // Remove any unused widgets and spacing
    if (scroll) {
        main_layout->removeWidget(scroll);
        delete scroll;
        scroll = nullptr;
    }

    // Add viewer directly after the title
    main_layout->insertWidget(1, viewer, 1);

    // Ensure proper spacing
    main_layout->setSpacing(20);

    setupFullscreen();
}

bool ModelDataViewer::hasValidModelData() {
    if (!state || !state->sm) return false;
    auto model = (*state->sm)["modelV2"].getModelV2();
    return model.getPosition().getX().size() > 0;  // Basic validation
}

void ModelDataViewer::saveModelDataToFile() {
    if (savedFileCount >= MAX_FILES) {
        // Already saved maximum number of files
        updateTimer->stop();  // Stop timer to prevent further attempts
        return;
    }

    if (!hasValidModelData()) {
        return;  // Skip if no valid data
    }

    QString fileName = QString("/data/model_data_%1.txt").arg(savedFileCount + 1,
                                                            2, 10,
                                                            QChar('0'));
    QFile file(fileName);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("Failed to open file for writing: %s", qPrintable(fileName));
        return;
    }

    QTextStream fileStream(&file);
    fileStream << formatModelData();
    file.close();

    if (file.error() == QFile::NoError) {
        savedFileCount++;
        qDebug("Successfully saved model data to %s", qPrintable(fileName));
    } else {
        qWarning("Error writing to file: %s", qPrintable(fileName));
    }
}
