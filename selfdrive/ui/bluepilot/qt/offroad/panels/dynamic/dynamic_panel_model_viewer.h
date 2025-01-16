// selfdrive/ui/bluepilot/qt/offroad/panels/dynamic/dynamic_panel_model_viewer.h

#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QScrollArea>
#include <QPlainTextEdit>
#include <QDialog>
#include <QTextStream>
#include "dynamic_panel_dialogs.h"

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/ui.h"
#define UIState UIStateSP
#else
#include "selfdrive/ui/ui.h"
#endif

class ModelDataViewer : public QWidget {
  Q_OBJECT

public:
  explicit ModelDataViewer(QWidget* parent = nullptr);

private slots:
  void updateModelData();

private:
  QPlainTextEdit* dataDisplay;
  QTimer* updateTimer;
  UIState* state;

  QString formatModelData();

  int savedFileCount = 0;
  static const int MAX_FILES = 10;
  bool hasValidModelData();
  void saveModelDataToFile();
};

class ModelDataViewerDialog : public DynamicPanelFullScreenDialog {
  Q_OBJECT

public:
  explicit ModelDataViewerDialog(QWidget* parent = nullptr);

private:
  ModelDataViewer* viewer;
};
