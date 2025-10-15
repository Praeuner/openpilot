/**
 * BluePilot version badge extensions for offroad home screen
 */

#pragma once

#include <QWidget>
#include "common/params.h"

// Create version badge widget for BluePilot
QWidget* createBluePilotVersionWidget(QWidget *parent);

// Refresh version badge widget with current version info
void refreshBluePilotVersion(QWidget *widget, Params &params);
