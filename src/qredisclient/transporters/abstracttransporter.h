#pragma once
#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QSharedPointer>
#include <QTimer>
#include <functional>

#include "qredisclient/command.h"
#include "qredisclient/responseparser.h"

namespace RedisClient {

class ResponseEmitter;
class Connection;

/**
 * @brief The AbstractTransporter class
 * Provides basic abstraction for transporting commands to redis-server.
 * THIS IS IMPLEMENTATION CLASS AND SHOULDN'T BE USED DIRECTLY.
 */
class AbstractTransporter : public QObject {
  Q_OBJECT
 public:
  AbstractTransporter(Connection* c);  // TODO: replace raw pointer by WeakPtr
  virtual ~AbstractTransporter();

  virtual int pipelineCommandsLimit() const;

 signals:
  void errorOccurred(const QString&);
  void logEvent(const QString&);
  void connected();
  void commandAdded();
  void queueIsEmpty();

 public slots:
  virtual void init();
  virtual void disconnectFromHost();
  virtual void addCommands(const QList<Command>&);
  virtual void cancelCommands(QObject*);
  virtual void readyRead();

 protected slots:
  virtual void executionTimeout();
  virtual void reconnect() = 0;
  virtual void reconnectTo(const QString& host, int port);
  virtual void processCommandQueue();
  virtual void cancelRunningCommands();

 protected:
  virtual bool isInitialized() const = 0;
  virtual bool isSocketReconnectRequired() const = 0;
  virtual bool canReadFromSocket() = 0;
  virtual QByteArray readFromSocket() = 0;
  virtual void initSocket() = 0;
  virtual bool connectToHost() = 0;
  virtual void runCommand(const Command& command);
  virtual void sendCommand(const QByteArray& cmd) = 0;
  virtual void sendResponse(const Response& response);
  void resetDbIndex();
  Command pickNextCommandForCurrentNode();
  void pickClusterNodeForNextCommand();

  virtual bool validateSystemProxy();

 protected:
  class RunningCommand {
   public:
    RunningCommand(const Command& cmd);
    Command cmd;
    QSharedPointer<ResponseEmitter> emitter;
    qint64 sentAt;
  };

  void reAddRunningCommandToQueue();

 private:
  void logResponse(const Response& response);
  void processClusterRedirect(QSharedPointer<RunningCommand> runningCommand,
                              const Response& r);
  void addSubscriptionsFromRunningCommand(
      QSharedPointer<RunningCommand> runningCommand);

 protected:
  Connection* m_connection;
  QQueue<QSharedPointer<RunningCommand>> m_runningCommands;
  QQueue<Command> m_commands;
  QQueue<Command> m_internalCommands;
  typedef QHash<QByteArray, QSharedPointer<ResponseEmitter>> Subscriptions;
  Subscriptions m_subscriptions;
  bool m_reconnectEnabled;
  bool m_pendingClusterRedirect;
  bool m_connectionInitialized;
  ResponseParser m_parser;
  uint m_followedClusterRedirects;
};
}  // namespace RedisClient
