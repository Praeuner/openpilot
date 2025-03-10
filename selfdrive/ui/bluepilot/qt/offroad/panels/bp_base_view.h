// bp_base_view.h

#pragma once

#include "bp_panel_base.h"

class BPBaseView : public BPPanelBase {
  Q_OBJECT

public:
  explicit BPBaseView(QWidget *parent = nullptr);
  virtual ~BPBaseView() override;

  bool initialize(const QString &configPath);
  bool initialize(const QJsonObject &config);
  void createGroup(const QJsonObject &group) override;
  QGroupBox *createStyledGroupBox(const QString &title) override;

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void setupBaseViewStyle();
  void cleanupNestedViews();
  QString currentConfigPath;
};
