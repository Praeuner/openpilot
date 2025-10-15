// bp_base_view.cc

#include "bp_base_view.h"
#include <QScrollArea>
#include <QVBoxLayout>
#include "selfdrive/ui/bluepilot/bp_logging.h"

BPBaseView::BPBaseView(QWidget *parent) : BPPanelBase(parent) { setupBaseViewStyle(); }

BPBaseView::~BPBaseView() { cleanupNestedViews(); }

bool BPBaseView::initialize(const QString &configPath) {
  currentConfigPath = configPath;
  if (!loadConfig(configPath)) {
    BPLog::bpError() << "[bp.base.view] Failed to initialize BPBaseView with config: " << configPath.toStdString() << std::endl;
    return false;
  }
  return true;
}

bool BPBaseView::initialize(const QJsonObject &config) {
  // Store the config JSON
  configJson = config;

  QString panelName = config.contains("menuName") && !config["menuName"].isNull() ? config["menuName"].toString() : "Unnamed Panel";
  setObjectName(panelName);

  // Process groups in the config
  QJsonArray groupsArray = config["groups"].toArray();
  for (const auto &groupValue : groupsArray) {
    createGroup(groupValue.toObject());
  }

  return true;
}

void BPBaseView::setupBaseViewStyle() {
  // Base view specific styling
  // Note: Scrollbar styling is now handled by the parent ScrollView wrapper
  setStyleSheet(QString(R"(
        BPBaseView {
            background: transparent;
        }
    )"));

  // Ensure proper sizing
  setMinimumWidth(1000);
  setMaximumWidth(1920);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
}

void BPBaseView::showEvent(QShowEvent *event) {
  BPLog::bpDebugGeneral() << "[bp.base.view] BPBaseView::showEvent" << std::endl;
  try {
    // Clean up any lingering nested views first
    cleanupNestedViews();
    BPPanelBase::showEvent(event);
    refresh();
  } catch (const std::exception &e) {
    BPLog::bpError() << "[bp.base.view] Exception in BPBaseView::showEvent: " << e.what() << std::endl;
  }
}

void BPBaseView::hideEvent(QHideEvent *event) {
  BPLog::bpDebugGeneral() << "[bp.base.view] BPBaseView::hideEvent" << std::endl;
  cleanupNestedViews();
  BPPanelBase::hideEvent(event);
}

void BPBaseView::cleanupNestedViews() {
  QList<BPNestedView *> nestedViews = findChildren<BPNestedView *>();
  for (auto *view : nestedViews) {
    if (view) {
      // Disconnect all signals first
      view->disconnect();
      // Set parent to nullptr to prevent double deletion
      view->setParent(nullptr);
      // Delete immediately instead of using deleteLater()
      delete view;
    }
  }
}

void BPBaseView::createGroup(const QJsonObject &group) {
  // First call base implementation
  BPPanelBase::createGroup(group);

  // Add any base view specific group handling
  QString groupName = group["groupName"].toString();
  auto it = groups.find(groupName);
  if (it != groups.end()) {
    QGroupBox *groupBox = it->second.groupBox;
    if (groupBox) {
      // Additional base view specific group setup if needed
      groupBox->setMaximumWidth(1920);
    }
  }
}

QGroupBox *BPBaseView::createStyledGroupBox(const QString &title) {
  // First call base implementation
  QGroupBox *groupBox = BPPanelBase::createStyledGroupBox(title);

  // Add any base view specific styling
  QString additionalStyle = R"(
        QGroupBox {
            margin-left: 20px;
            margin-right: 20px;
        }
    )";

  groupBox->setStyleSheet(groupBox->styleSheet() + additionalStyle);

  return groupBox;
}
