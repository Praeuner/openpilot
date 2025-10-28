// selfdrive/ui/bluepilot/qt/widgets/bp_updater_client.h
// BluePilot Updater Client - Qt interface to Python updater command system

#pragma once

#include <QObject>
#include <QTimer>
#include <QJsonObject>
#include <QString>
#include <QDateTime>

#include "common/params.h"

/**
 * BPUpdaterClient - Qt client for communicating with the Python updater
 *
 * Provides a clean interface for sending commands to the updater and
 * receiving real-time status updates via params.
 */
class BPUpdaterClient : public QObject {
  Q_OBJECT

public:
  explicit BPUpdaterClient(QObject *parent = nullptr);

  // Command sending
  QString sendCommand(const QString &cmd, const QJsonObject &args = {});
  void watchCommand(const QString &commandID);
  void stopWatching();
  void cancelCommand();

  // Status queries
  QString getCurrentCommandID() const { return currentCommandID; }
  QString getCurrentStatus() const { return currentStatus; }
  int getCurrentProgress() const { return currentProgress; }

signals:
  void commandStatusChanged(QString status);
  void commandProgressChanged(int progress);
  void commandOutputReceived(QString output);
  void commandCompleted(QJsonObject result);
  void commandFailed(QString error);

private slots:
  void pollStatus();

private:
  Params params;
  QTimer *statusPollTimer;

  QString currentCommandID;
  QString currentStatus;
  int currentProgress;
  QString lastOutput;

  void clearStatus();
};
