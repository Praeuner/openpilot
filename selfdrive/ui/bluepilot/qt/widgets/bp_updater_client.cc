// selfdrive/ui/bluepilot/qt/widgets/bp_updater_client.cc

#include "bp_updater_client.h"

#include <QJsonDocument>
#include <QJsonObject>

BPUpdaterClient::BPUpdaterClient(QObject *parent)
  : QObject(parent),
    currentProgress(0) {

  // Setup status polling timer
  statusPollTimer = new QTimer(this);
  statusPollTimer->setInterval(250);  // Poll every 250ms for responsive UI
  connect(statusPollTimer, &QTimer::timeout, this, &BPUpdaterClient::pollStatus);
}

QString BPUpdaterClient::sendCommand(const QString &cmd, const QJsonObject &args) {
  // Generate unique command ID
  QString commandID = QString("cmd_%1").arg(QDateTime::currentMSecsSinceEpoch());

  // Build command JSON
  QJsonObject cmdObj;
  cmdObj["cmd"] = cmd;
  cmdObj["args"] = args;
  cmdObj["id"] = commandID;

  // Write to param
  QJsonDocument doc(cmdObj);
  params.put("UpdaterCommand", doc.toJson(QJsonDocument::Compact).toStdString());

  qDebug() << "[BPUpdaterClient] Sent command:" << cmd << "ID:" << commandID;

  return commandID;
}

void BPUpdaterClient::watchCommand(const QString &commandID) {
  currentCommandID = commandID;
  currentStatus = "";
  currentProgress = 0;
  lastOutput = "";

  // Start polling for status
  statusPollTimer->start();

  qDebug() << "[BPUpdaterClient] Watching command:" << commandID;
}

void BPUpdaterClient::stopWatching() {
  statusPollTimer->stop();
  clearStatus();

  qDebug() << "[BPUpdaterClient] Stopped watching";
}

void BPUpdaterClient::cancelCommand() {
  params.putBool("UpdaterCommandCancel", true);

  qDebug() << "[BPUpdaterClient] Cancelled command:" << currentCommandID;
}

void BPUpdaterClient::pollStatus() {
  // Check if this is our command
  std::string cmdID = params.get("UpdaterCommandID");
  if (QString::fromStdString(cmdID) != currentCommandID) {
    // Not our command yet
    return;
  }

  // Read status
  std::string status = params.get("UpdaterCommandStatus");
  QString newStatus = QString::fromStdString(status);

  if (newStatus != currentStatus) {
    currentStatus = newStatus;
    emit commandStatusChanged(currentStatus);
  }

  // Read progress
  std::string progressStr = params.get("UpdaterCommandProgress");
  int newProgress = QString::fromStdString(progressStr).toInt();

  if (newProgress != currentProgress) {
    currentProgress = newProgress;
    emit commandProgressChanged(currentProgress);
  }

  // Read output (incremental)
  std::string output = params.get("UpdaterCommandOutput");
  QString newOutput = QString::fromStdString(output);

  if (newOutput != lastOutput) {
    // Only emit the new lines
    if (newOutput.startsWith(lastOutput)) {
      QString newLines = newOutput.mid(lastOutput.length());
      if (!newLines.isEmpty()) {
        emit commandOutputReceived(newLines);
      }
    } else {
      // Full update (output buffer may have rolled over)
      emit commandOutputReceived(newOutput);
    }
    lastOutput = newOutput;
  }

  // Check for completion
  if (currentStatus == "completed" || currentStatus == "failed") {
    // Read final result
    std::string resultStr = params.get("UpdaterCommandResult");
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(resultStr));
    QJsonObject result = doc.object();

    if (currentStatus == "completed") {
      emit commandCompleted(result);
    } else {
      QString error = result["error"].toString("Unknown error");
      emit commandFailed(error);
    }

    // Stop polling
    stopWatching();
  }
}

void BPUpdaterClient::clearStatus() {
  currentCommandID = "";
  currentStatus = "";
  currentProgress = 0;
  lastOutput = "";
}
