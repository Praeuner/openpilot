// bp_nested_view.h

#pragma once

class BPPanelBase;

#include <QDialog>
#include "bp_panel_base.h"

class BPNestedView : public QDialog {
  Q_OBJECT

public:
  explicit BPNestedView(QWidget *parent = nullptr);
  virtual ~BPNestedView() override;

  bool setupView(const QString &title, const QJsonObject &config);
  bool setupView(const QString &title, const QString &configPath);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

private:
  void setupDialogStyle();
  void setupHeader(const QString &title);
  void setupLayout();

  BPPanelBase *panel;
  QVBoxLayout *main_layout;
  QLabel *title_label;
  QPushButton *back_button;

  // Dialog-specific constants
  const int HEADER_HEIGHT = 130;
  const int DIALOG_WIDTH = 2160;
  const int DIALOG_HEIGHT = 1080;
};
