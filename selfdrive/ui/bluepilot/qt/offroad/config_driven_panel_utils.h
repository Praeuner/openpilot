// selfdrive/ui/bluepilot/qt/offroad/config_driven_panel_utils.h
#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <iostream>
#include "common/params.h"

class ConfigDrivenDefaultParams {
public:
    static ConfigDrivenDefaultParams& getInstance() {
        static ConfigDrivenDefaultParams instance;
        return instance;
    }

    QString getDefault(const QString& key) const {
        Params params;
        std::string defaultKey = key.toStdString() + "_default";
        return QString::fromStdString(params.get(defaultKey));
    }

private:
    ConfigDrivenDefaultParams() {}
};

class ConfigDrivenPanelConfig {
public:
    static ConfigDrivenPanelConfig& getInstance() {
        static ConfigDrivenPanelConfig instance;
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
    ConfigDrivenPanelConfig() {}
    QJsonObject config;
};
