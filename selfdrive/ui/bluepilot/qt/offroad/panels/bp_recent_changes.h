// bp_recent_changes.h

#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QPushButton>
#include <QTimer>

#include "bp_panel_dialogs.h"
#include "bp_panel_controls.h"
#include "bp_utils.h"
#include "common/params.h"

class BPRecentChangesDialog : public BPDialogBase {
  Q_OBJECT

public:
  explicit BPRecentChangesDialog(QWidget *parent = nullptr);
  ~BPRecentChangesDialog();

  // Static method to check if changes should be shown and display them
  static bool checkAndShowChanges(QWidget *parent = nullptr);

  // Load and display changes for a specific version
  bool loadAndDisplayChanges(const QString &version);

  // Version management - public for RecentChangesManager
  static QString getCurrentVersion();
  static QString getStoredVersion();
  static void setStoredVersion(const QString &version);

private:
  void setupDialogStyle();
  void setupUI();
  void createVersionSection(const QString &version, const QJsonObject &versionData);
  void createCategorySection(QVBoxLayout *layout, const QString &title, const QJsonArray &items, const QString &color);
  QWidget *createChangeItem(const QString &text, const QString &bulletColor);

  // Helper methods
  static QJsonObject loadChangesJson();
  static QString getGitRootPath();

  QVBoxLayout *main_layout;
  QLabel *title_label;
  QScrollArea *scroll_area;
  QWidget *content_widget;
  QVBoxLayout *content_layout;
  BPButton *closeButton;

  // Constants for styling
  static constexpr int DIALOG_WIDTH = 2160;
  static constexpr int DIALOG_HEIGHT = 1080;
  static constexpr int HEADER_HEIGHT = 130;
  static constexpr int CONTENT_MARGIN = 40;
  static constexpr int SECTION_SPACING = 30;
};

class RecentChangesManager {
public:
  static RecentChangesManager& getInstance() {
    static RecentChangesManager instance;
    return instance;
  }

  // Check if changes dialog should be shown on startup
  bool shouldShowChanges();

  // Show changes dialog and mark as seen
  void showChangesDialog(QWidget *parent = nullptr);

private:
  RecentChangesManager() = default;
  bool changes_shown = false;
};