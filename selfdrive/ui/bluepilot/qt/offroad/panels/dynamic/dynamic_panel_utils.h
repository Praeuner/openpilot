// selfdrive/ui/bluepilot/qt/offroad/panels/dynamic/dynamic_panel_utils.h

#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <iostream>
#include "common/params.h"

class DynamicPanelDefaultParams {
public:
    static DynamicPanelDefaultParams& getInstance() {
        static DynamicPanelDefaultParams instance;
        return instance;
    }

    QString getDefault(const QString& key) const {
        Params params;
        std::string defaultKey = key.toStdString() + "_default";
        return QString::fromStdString(params.get(defaultKey));
    }

private:
    DynamicPanelDefaultParams() {}
};

class DynamicPanelConfig {
public:
    static DynamicPanelConfig& getInstance() {
        static DynamicPanelConfig instance;
        return instance;
    }

    bool loadConfig(const QString& filename) {
      std::cout << "Attempting to load config from: " << filename.toStdString() << std::endl;
      QFile file(filename);
      if (!file.open(QIODevice::ReadOnly)) {
          std::cerr << "Failed to open file: " << file.errorString().toStdString() << std::endl;
          return false;
      }
      QByteArray data = file.readAll();
      if (data.isEmpty()) {
          std::cerr << "File is empty" << std::endl;
          return false;
      }
      QJsonDocument doc = QJsonDocument::fromJson(data);
      if (doc.isNull()) {
          std::cerr << "Failed to parse JSON" << std::endl;
          return false;
      }
      std::cout << "Successfully loaded config" << std::endl;
      config = doc.object();
      return true;
    }

    const QJsonObject& getConfig() const { return config; }

private:
    DynamicPanelConfig() {}
    QJsonObject config;
};

class LogTreeFormatter {
private:
    int depth = 0;
    std::vector<bool> hasNextAtLevel;

public:
    void increaseDepth() {
        depth++;
        if (depth >= hasNextAtLevel.size()) {
            hasNextAtLevel.push_back(true);
        }
    }

    void decreaseDepth() {
        if (depth > 0) depth--;
    }

    void setHasNextAtCurrentLevel(bool hasNext) {
        if (depth < hasNextAtLevel.size()) {
            hasNextAtLevel[depth] = hasNext;
        }
    }

    std::string getPrefix() {
        std::string prefix;
        for (int i = 0; i < depth; i++) {
            prefix += (i < hasNextAtLevel.size() && hasNextAtLevel[i]) ? "│   " : "    ";
        }
        return prefix;
    }

    std::string getItemPrefix(bool hasNext) {
        return getPrefix() + (hasNext ? "├─ " : "└─ ");
    }
};
