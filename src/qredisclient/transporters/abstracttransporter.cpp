#include "abstracttransporter.h"
#include <QDebug>
#include "qredisclient/connection.h"
#include "qredisclient/private/responseemmiter.h"
#include "qredisclient/utils/text.h"

RedisClient::AbstractTransporter::AbstractTransporter(
    RedisClient::Connection *connection)
    : m_connection(connection), m_reconnectEnabled(true) {
  // connect signals & slots between connection & transporter
  connect(connection, SIGNAL(addCommandToWorker(const Command &)), this,
          SLOT(addCommand(const Command &)));
  connect(connection, SIGNAL(reconnectTo(const QString &, int)), this,
          SLOT(reconnectTo(const QString &, int)));
  connect(this, SIGNAL(logEvent(const QString &)), connection,
          SIGNAL(log(const QString &)));

  connect(this, &AbstractTransporter::errorOccurred, this,
          &AbstractTransporter::cancelRunningCommands);

  connect(m_connection, &Connection::authOk, this,
          &AbstractTransporter::processCommandQueue);
}

RedisClient::AbstractTransporter::~AbstractTransporter() {
  disconnectFromHost();
}

void RedisClient::AbstractTransporter::init() {
  if (isInitialized()) return;

  qDebug() << "Init transporter";

  initSocket();
  connectToHost();
}

void RedisClient::AbstractTransporter::disconnectFromHost() {
  cancelRunningCommands();
  m_commands.clear();
}

void RedisClient::AbstractTransporter::addCommand(const Command &cmd) {
  if (cmd.isHiPriorityCommand())
    m_commands.prepend(cmd);
  else
    m_commands.enqueue(cmd);

  emit commandAdded();

  if (isInitialized())
    QTimer::singleShot(0, this, &AbstractTransporter::processCommandQueue);
}

void RedisClient::AbstractTransporter::cancelCommands(QObject *owner) {
  if (!owner) return;

  // Cancel running commands
  for (auto curr = m_runningCommands.begin(); curr != m_runningCommands.end();) {
    if ((*curr)->cmd.getOwner() == owner) {
      curr = m_runningCommands.erase(curr);
    } else {
      ++curr;
    }
  }

  // Remove subscriptions
  Subscriptions::iterator i = m_subscriptions.begin();
  while (i != m_subscriptions.constEnd()) {
    if (i.value()->owner == owner) {
      i = m_subscriptions.erase(i);
      emit logEvent("Subscription was canceled.");
    } else {
      ++i;
    }
  }

  if (m_commands.size() == 0) return;

  // Cancel command in queue
  for (auto curr = m_commands.begin(); curr != m_commands.end();) {
    if (curr->getOwner() == owner) {
      curr = m_commands.erase(curr);
      emit logEvent("Command was canceled.");
    } else {
      ++curr;
    }
  }
}

void RedisClient::AbstractTransporter::sendResponse(
    const RedisClient::Response &response) {
  //logResponse(response);

  if (response.isMessage()) {
    QByteArray channel = response.getChannel();

    if (m_subscriptions.contains(channel))
      m_subscriptions[channel]->sendResponse(response, QString());

    return;
  }

  if (m_runningCommands.size() == 0) {
    qDebug() << "Response recieved but no commands are running";

    if (response.isErrorStateMessage()) {
      m_reconnectEnabled = false;
      emit errorOccurred(response.value().toByteArray());
    }
    return;
  }

  if (m_runningCommands.first()->cmd.isPipelineCommand() &&
      (response.isOkMessage() || response.isQueuedMessage())) {
    return;
  }

  auto runningCommand = m_runningCommands.dequeue();

  // Reconnect to different server in cluster and reissue current
  // command if needed
  if (m_connection->mode() == Connection::Mode::Cluster &&
      (response.isAskRedirect() || response.isMovedRedirect())) {
    return processClusterRedirect(runningCommand, response);
  }

  if (runningCommand->cmd.isUnSubscriptionCommand()) {
    QList<QByteArray> channels =
        runningCommand->cmd.getSplitedRepresentattion().mid(1);
    for (QByteArray channel : channels) {
      m_subscriptions.remove(channel);
    }
  }

  if (runningCommand->cmd.isSelectCommand() && response.isOkMessage()) {
    m_connection->changeCurrentDbNumber(
        runningCommand->cmd.getPartAsString(1).toInt());
  }

  runningCommand->cmd.getDeferred().complete(response);

  if (runningCommand->emitter) {
    runningCommand->emitter->sendResponse(response, QString());

    if (runningCommand->cmd.isSubscriptionCommand())
      addSubscriptionsFromRunningCommand(runningCommand);
  }
  runningCommand.clear();
}

void RedisClient::AbstractTransporter::resetDbIndex() {
  m_connection->changeCurrentDbNumber(0);
}

void RedisClient::AbstractTransporter::reAddRunningCommandToQueue() {
  for (auto curr = m_runningCommands.begin(); curr != m_runningCommands.end();) {
    m_commands.prepend((*curr)->cmd);
    curr = m_runningCommands.erase(curr);
  }
  qDebug() << "Running commands were re-added to queue";
  emit logEvent("Running commands were re-added to queue.");
}

void RedisClient::AbstractTransporter::cancelRunningCommands() {
  emit logEvent("Cancel running commands");
  m_runningCommands.clear();
}

void RedisClient::AbstractTransporter::processCommandQueue() {
  if (m_commands.isEmpty()) {
    emit queueIsEmpty();
    return;
  }

  if (m_connection->mode() != Connection::Mode::Cluster
          && m_commands.head().hasDbIndex()) {
    QList<QByteArray> selectCmdRaw = {
        "SELECT", QString::number(m_commands.head().getDbIndex()).toLatin1()};
    Command selectCmd(selectCmdRaw);
    runCommand(selectCmd);
  }

  runCommand(m_commands.dequeue());

  QTimer::singleShot(0, this, &AbstractTransporter::processCommandQueue);
}

void RedisClient::AbstractTransporter::logResponse(
    const RedisClient::Response &response) {
  QString result;

  if (response.type() == RedisClient::Response::Type::Status ||
      response.type() == RedisClient::Response::Type::Error) {
    result = response.value().toByteArray();
  } else if (response.type() == RedisClient::Response::Type::String) {
    result = QString("Bulk");
  } else if (response.type() == RedisClient::Response::Type::Array) {
    result = QString("Array");
  }

  emit logEvent(QString("%1 > Response received : %2")
                    .arg(m_connection->getConfig().name())
                    .arg(result));

  // qDebug() << "Response:" << response.source();
}

void RedisClient::AbstractTransporter::processClusterRedirect(
    QSharedPointer<RunningCommand> runningCommand,
    const RedisClient::Response &response) {
  Q_ASSERT(runningCommand);
  qDebug() << "Cluster redirect";

  m_commands.prepend(runningCommand->cmd);
  runningCommand.clear();

  QString host;
  int port = response.getRedirectionPort();

  if (m_connection->m_config.overrideClusterHost()) {
    host = response.getRedirectionHost();
  } else {
    host = m_connection->m_config.host();
  }

  QTimer::singleShot(1, this,
                     [this, host, port]() { reconnectTo(host, port); });
}

void RedisClient::AbstractTransporter::reconnectTo(const QString &host,
                                                   int port) {
  qDebug() << "ReConnect to:" << host << port;
  emit logEvent(QString("Reconnect to %1:%2").arg(host).arg(port));

  auto config = m_connection->getConfig();
  config.setHost(host);
  config.setPort(port);
  m_connection->setConnectionConfig(config);

  reconnect();
}

void RedisClient::AbstractTransporter::addSubscriptionsFromRunningCommand(
    QSharedPointer<RunningCommand> runningCommand) {
  Q_ASSERT(runningCommand);

  if (!runningCommand->emitter) return;

  QList<QByteArray> channels =
      runningCommand->cmd.getSplitedRepresentattion().mid(1);

  for (QByteArray channel : channels) {
    m_subscriptions.insert(channel, runningCommand->emitter);
  }
}

void RedisClient::AbstractTransporter::executionTimeout() {
  qDebug() << "Command execution/download timeout";
  emit errorOccurred("Execution timeout");
}

void RedisClient::AbstractTransporter::readyRead() {
  if (!canReadFromSocket()) return;

  if (!m_parser.feedBuffer(readFromSocket())) {
    // TODO: reset???!
    return;
  }

  QList<RedisClient::Response> responses;
  RedisClient::Response resp;

  do {
    resp = m_parser.getNextResponse();

    if (resp.isValid()) responses.append(resp);
  } while (resp.isValid());

  for (auto r : responses) {
    sendResponse(r);
  }
}

void RedisClient::AbstractTransporter::runCommand(
    const RedisClient::Command &command) {
  if (isSocketReconnectRequired()) {
    if (!m_reconnectEnabled) {
      emit errorOccurred("Cannot run command. Reconnect is required.");
      return;
    }
    qDebug() << "Reconnecting";
    m_commands.enqueue(command);
    reconnect();
    return;
  }

  emit logEvent(QString("%1 > [runCommand] %2")
                    .arg(m_connection->getConfig().name())
                    .arg(printableString(command.getRawString())));

  // m_response.reset();
  auto runningCommand =
      QSharedPointer<RunningCommand>(new RunningCommand(command));
  m_runningCommands.enqueue(runningCommand);

  sendCommand(runningCommand->cmd.getByteRepresentation());
}

RedisClient::AbstractTransporter::RunningCommand::RunningCommand(
    const RedisClient::Command &cmd)
    : cmd(cmd), emitter(nullptr) {
  auto callback = cmd.getCallBack();
  auto owner = cmd.getOwner();
  if (callback && owner) {
    emitter =
        QSharedPointer<ResponseEmitter>(new ResponseEmitter(owner, callback));
  }
}
