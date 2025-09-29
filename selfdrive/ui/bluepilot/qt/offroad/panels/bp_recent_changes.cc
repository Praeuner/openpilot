// bp_recent_changes.cc

#include "bp_recent_changes.h"
#include <QScroller>
#include <QGraphicsDropShadowEffect>
#include "selfdrive/ui/bluepilot/bp_logging.h"

BPRecentChangesDialog::BPRecentChangesDialog(QWidget *parent) : BPDialogBase(parent) {
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_DeleteOnClose);

  setupDialogStyle();
  setupUI();
  setFixedSize(DIALOG_WIDTH, DIALOG_HEIGHT);
}

BPRecentChangesDialog::~BPRecentChangesDialog() {
  BPLog::bpDebugGeneral() << "[bp.recent.changes] BPRecentChangesDialog destructor called" << std::endl;
}

void BPRecentChangesDialog::setupDialogStyle() {
  setStyleSheet(R"(
    BPRecentChangesDialog {
      background-color: #000000;
    }
    QWidget {
      background-color: #000000;
      color: white;
    }
    QLabel {
      color: #FFFFFF;
      background-color: transparent;
    }
    QScrollArea {
      background-color: #000000;
      border: none;
    }
    QScrollArea > QWidget {
      background-color: #000000;
    }
    QScrollArea > QWidget > QWidget {
      background-color: transparent;
    }
  )");
}

void BPRecentChangesDialog::setupUI() {
  main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);

  // Header widget
  QWidget *header_widget = new QWidget(this);
  header_widget->setFixedHeight(HEADER_HEIGHT);
  header_widget->setStyleSheet("background-color: #202020;");

  QHBoxLayout *header_layout = new QHBoxLayout(header_widget);
  header_layout->setContentsMargins(20, 15, 20, 15);

  // Close button (styled as back button)
  BPBackButton *close_btn = new BPBackButton(this, "✕ Close");
  connect(close_btn, &BPBackButton::clicked, this, [this]() {
    // Mark version as seen when closing
    setStoredVersion(getCurrentVersion());
    accept();
  });

  // Title label
  title_label = new QLabel("Recent Changes", this);
  title_label->setStyleSheet("font-size: 42px; font-weight: 600; color: white;");
  title_label->setAlignment(Qt::AlignCenter);

  // Header layout
  header_layout->addWidget(close_btn);
  header_layout->addStretch(1);
  header_layout->addWidget(title_label);
  header_layout->addStretch(1);

  // Spacer to balance close button
  QWidget *spacer = new QWidget();
  spacer->setFixedSize(close_btn->size());
  header_layout->addWidget(spacer);

  main_layout->addWidget(header_widget);

  // Content scroll area
  scroll_area = new QScrollArea(this);
  scroll_area->setStyleSheet(R"(
    QScrollArea {
      background-color: #000000;
      border: none;
    }
    QScrollBar:vertical {
      width: 12px;
      margin: 0px;
      padding: 2px;
      background: transparent;
    }
    QScrollBar::handle:vertical {
      background: #666666;
      min-height: 30px;
      border-radius: 6px;
      margin: 0 2px;
    }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
      height: 0px;
    }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
      background: none;
    }
  )");

  scroll_area->setWidgetResizable(true);
  scroll_area->setFrameShape(QFrame::NoFrame);
  scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // Enable touch scrolling
  QScroller::grabGesture(scroll_area->viewport(), QScroller::TouchGesture);

  // Content widget
  content_widget = new QWidget();
  content_widget->setStyleSheet("background-color: #000000;");

  content_layout = new QVBoxLayout(content_widget);
  content_layout->setContentsMargins(CONTENT_MARGIN, CONTENT_MARGIN, CONTENT_MARGIN, CONTENT_MARGIN);
  content_layout->setSpacing(SECTION_SPACING);
  content_layout->setAlignment(Qt::AlignTop);

  scroll_area->setWidget(content_widget);
  main_layout->addWidget(scroll_area, 1);
}

bool BPRecentChangesDialog::loadAndDisplayChanges(const QString &version) {
  QJsonObject changesData = loadChangesJson();

  if (changesData.isEmpty()) {
    BPLog::bpError() << "[bp.recent.changes] Failed to load changes data" << std::endl;
    return false;
  }

  QJsonObject versions = changesData["versions"].toObject();
  if (!versions.contains(version)) {
    BPLog::bpInfo() << "[bp.recent.changes] No changes found for version: " << version.toStdString() << std::endl;
    return false;
  }

  // Clear existing content
  QLayoutItem *child;
  while ((child = content_layout->takeAt(0)) != nullptr) {
    delete child->widget();
    delete child;
  }

  // Add version section
  createVersionSection(version, versions[version].toObject());

  // Add stretch at the end
  content_layout->addStretch();

  return true;
}

void BPRecentChangesDialog::createVersionSection(const QString &version, const QJsonObject &versionData) {
  // Version header with pill/badge styling
  QWidget *versionContainer = new QWidget();
  versionContainer->setStyleSheet("background-color: transparent;");

  QHBoxLayout *versionLayout = new QHBoxLayout(versionContainer);
  versionLayout->setContentsMargins(0, 15, 0, 25);
  versionLayout->setAlignment(Qt::AlignLeft);

  QLabel *versionLabel = new QLabel(QString("Version %1").arg(version));
  versionLabel->setStyleSheet(R"(
    QLabel {
      font-size: 38px;
      font-weight: 600;
      color: #FFFFFF;
      background-color: #2196F3;
      border-radius: 25px;
      padding: 12px 24px;
      margin: 0px;
    }
  )");
  versionLabel->setAlignment(Qt::AlignCenter);

  versionLayout->addWidget(versionLabel);
  content_layout->addWidget(versionContainer);

  // Changes section
  if (versionData.contains("changes") && versionData["changes"].toArray().size() > 0) {
    createCategorySection(content_layout, "✨ New Features & Changes",
                         versionData["changes"].toArray(), "#50d332");
  }

  // Fixes section
  if (versionData.contains("fixes") && versionData["fixes"].toArray().size() > 0) {
    createCategorySection(content_layout, "🔧 Bug Fixes",
                         versionData["fixes"].toArray(), "#2196F3");
  }

  // Removals section
  if (versionData.contains("removals") && versionData["removals"].toArray().size() > 0) {
    createCategorySection(content_layout, "🗑️ Removed Features",
                         versionData["removals"].toArray(), "#ff7c30");
  }

  // Known issues section
  if (versionData.contains("known_issues") && versionData["known_issues"].toArray().size() > 0) {
    createCategorySection(content_layout, "\u26A0 Known Issues",
                         versionData["known_issues"].toArray(), "#EA4646");
  }
}

void BPRecentChangesDialog::createCategorySection(QVBoxLayout *layout, const QString &title,
                                                 const QJsonArray &items, const QString &color) {
  // Category container
  QFrame *categoryFrame = new QFrame();
  categoryFrame->setStyleSheet(R"(
    QFrame {
      background-color: #242424;
      border-radius: 15px;
      padding: 5px;
      margin: 2px 0px 15px 0px;
    }
  )");

  QVBoxLayout *frameLayout = new QVBoxLayout(categoryFrame);
  frameLayout->setContentsMargins(15, 15, 15, 10);
  frameLayout->setSpacing(0);

  // Category header inside container
  QLabel *categoryLabel = new QLabel(title);
  categoryLabel->setStyleSheet(QString(R"(
    QLabel {
      font-size: 34px;
      font-weight: 600;
      color: %1;
      padding: 0px 0px 8px 0px;
      margin: 0px;
    }
  )").arg(color));
  frameLayout->addWidget(categoryLabel);

  // Add items
  for (const auto &item : items) {
    QString itemText = item.toString();
    if (!itemText.isEmpty()) {
      QWidget *changeItem = createChangeItem(itemText, color);
      frameLayout->addWidget(changeItem);
    }
  }

  layout->addWidget(categoryFrame);
}

QWidget *BPRecentChangesDialog::createChangeItem(const QString &text, const QString &bulletColor) {
  QWidget *itemWidget = new QWidget();
  itemWidget->setStyleSheet("background-color: transparent;");

  QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
  itemLayout->setContentsMargins(0, 0, 0, 0); // Zero margins
  itemLayout->setSpacing(10);

  // Bullet point
  QLabel *bullet = new QLabel("•");
  bullet->setStyleSheet(QString(R"(
    QLabel {
      font-size: 30px;
      font-weight: bold;
      color: %1;
      min-width: 18px;
      max-width: 18px;
    }
  )").arg(bulletColor));
  bullet->setAlignment(Qt::AlignTop);

  // Change text
  QLabel *textLabel = new QLabel(text);
  textLabel->setStyleSheet(R"(
    QLabel {
      font-size: 30px;
      color: #E4E4E4;
      line-height: 1.3;
    }
  )");
  textLabel->setWordWrap(true);
  textLabel->setAlignment(Qt::AlignTop);

  itemLayout->addWidget(bullet);
  itemLayout->addWidget(textLabel, 1);

  return itemWidget;
}

QString BPRecentChangesDialog::getCurrentVersion() {
  QString gitRoot = getGitRootPath();
  QString versionPath = QDir(gitRoot).filePath("BPVERSION");

  QFile file(versionPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    BPLog::bpError() << "[bp.recent.changes] Failed to read BPVERSION file: " << versionPath.toStdString() << std::endl;
    return "unknown";
  }

  QString version = QString::fromUtf8(file.readAll()).trimmed();
  return version.isEmpty() ? "unknown" : version;
}

QString BPRecentChangesDialog::getStoredVersion() {
  Params params;
  std::string storedVersion = params.get("BPLastSeenVersion");
  QString result = QString::fromStdString(storedVersion);
  BPLog::bpInfo() << "[bp.recent.changes] Retrieved stored version: '" << storedVersion << "' (length: " << storedVersion.length() << ")" << std::endl;
  return result;
}

void BPRecentChangesDialog::setStoredVersion(const QString &version) {
  Params params;
  params.put("BPLastSeenVersion", version.toStdString());
  BPLog::bpInfo() << "[bp.recent.changes] Stored version updated to: " << version.toStdString() << std::endl;
}

QJsonObject BPRecentChangesDialog::loadChangesJson() {
  QString gitRoot = getGitRootPath();
  QString changesPath = QDir(gitRoot).filePath("BP_CHANGES.json");

  QFile file(changesPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    BPLog::bpError() << "[bp.recent.changes] Failed to open BP_CHANGES.json: " << changesPath.toStdString() << std::endl;
    return QJsonObject();
  }

  QByteArray data = file.readAll();
  QJsonDocument doc = QJsonDocument::fromJson(data);

  if (doc.isNull() || !doc.isObject()) {
    BPLog::bpError() << "[bp.recent.changes] Invalid JSON in BP_CHANGES.json" << std::endl;
    return QJsonObject();
  }

  return doc.object();
}

QString BPRecentChangesDialog::getGitRootPath() {
  // Start from current application directory and search upward for .git directory
  QString currentPath = QCoreApplication::applicationDirPath();
  QDir dir(currentPath);

  // Look for .git directory or go up directories until found
  while (!dir.exists(".git") && dir.cdUp()) {
    // Continue searching upward
  }

  if (dir.exists(".git")) {
    return dir.absolutePath();
  }

  // Fallback to project root if .git not found
  BPLog::bpWarn() << "[bp.recent.changes] Git root not found, falling back to project root" << std::endl;
  return FileUtils::getProjectRootPath();
}

bool BPRecentChangesDialog::checkAndShowChanges(QWidget *parent) {
  QString currentVersion = getCurrentVersion();
  QString storedVersion = getStoredVersion();

  BPLog::bpInfo() << "[bp.recent.changes] Current version: '" << currentVersion.toStdString() << "' (length: " << currentVersion.length() << ")" << std::endl;
  BPLog::bpInfo() << "[bp.recent.changes] Stored version: '" << storedVersion.toStdString() << "' (length: " << storedVersion.length() << ")" << std::endl;
  BPLog::bpInfo() << "[bp.recent.changes] Versions equal: " << (currentVersion == storedVersion) << std::endl;
  BPLog::bpInfo() << "[bp.recent.changes] Current != unknown: " << (currentVersion != "unknown") << std::endl;

  if (currentVersion != storedVersion && currentVersion != "unknown") {
    auto *dialog = new BPRecentChangesDialog(parent);

    if (dialog->loadAndDisplayChanges(currentVersion)) {
      dialog->setupFullscreen();
      return true;
    } else {
      // If no changes found, still update the stored version
      setStoredVersion(currentVersion);
      dialog->deleteLater();
      return false;
    }
  }

  return false;
}

// RecentChangesManager implementation
bool RecentChangesManager::shouldShowChanges() {
  if (changes_shown) return false;

  QString currentVersion = BPRecentChangesDialog::getCurrentVersion();
  QString storedVersion = BPRecentChangesDialog::getStoredVersion();

  return (currentVersion != storedVersion && currentVersion != "unknown");
}

void RecentChangesManager::showChangesDialog(QWidget *parent) {
  if (changes_shown) return;

  if (BPRecentChangesDialog::checkAndShowChanges(parent)) {
    changes_shown = true;
  }
}
