#include "abstracttransporter.h"

#include <QDateTime>
#include <QDebug>
#include <QNetworkProxy>
#include <QSettings>

#include "qredisclient/connection.h"
#include "qredisclient/private/responseemmiter.h"
#include "qredisclient/utils/text.h"

#define MAX_CLUSTER_REDIRECTS 5

RedisClient::AbstractTransporter::AbstractTransporter(
    RedisClient::Connection *connection)
    : m_connection(connection),
      m_reconnectEnabled(true),
      m_pendingClusterRedirect(false),
      m_connectionInitialized(false),
      m_followedClusterRedirects(0) {
  // connect signals & slots between connection & transporter
  connect(connection, SIGNAL(addCommandsToWorker(const QList<Command> &)), this,
          SLOT(addCommands(const QList<Command> &)));
  connect(connection, SIGNAL(reconnectTo(const QString &, int)), this,
          SLOT(reconnectTo(const QString &, int)));
  connect(this, SIGNAL(logEvent(const QString &)), connection,
          SIGNAL(log(const QString &)));

  connect(this, &AbstractTransporter::errorOccurred, this,
          &AbstractTransporter::cancelRunningCommands);

  connect(m_connection, &Connection::authOk, this, [this]() {
      m_connectionInitialized = true;
      QTimer::singleShot(0, this, &AbstractTransporter::processCommandQueue);
  });
}

RedisClient::AbstractTransporter::~AbstractTransporter() {
  disconnectFromHost();
}

int RedisClient::AbstractTransporter::pipelineCommandsLimit() const {
  return 1000;
}

void RedisClient::AbstractTransporter::init() {
  if (isInitialized()) return;

  initSocket();
  connectToHost();
}

void RedisClient::AbstractTransporter::disconnectFromHost() {
  cancelRunningCommands();
  m_commands.clear();
  m_internalCommands.clear();
  m_pendingClusterRedirect = false;
  m_followedClusterRedirects = 0;
  m_connectionInitialized = false;
}

void RedisClient::AbstractTransporter::addCommands(
    const QList<Command> &commands) {
  for (auto cmd : commands) {
    if (cmd.isHiPriorityCommand())
      m_internalCommands.enqueue(cmd);
    else
      m_commands.enqueue(cmd);
  }

  emit commandAdded();

  if (isInitialized())
    QTimer::singleShot(0, this, &AbstractTransporter::processCommandQueue);
}

void RedisClient::AbstractTransporter::cancelCommands(QObject *owner) {
  if (!owner) return;

  // Cancel running commands
  for (auto curr = m_runningCommands.begin();
       curr != m_runningCommands.end();) {
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

  if (response.isMessage() ||
      m_connection->m_currentMode == Connection::Mode::Monitor) {
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

  if (m_runningCommands.first()->cmd.isPipelineCommand()) {
    auto pipelineCmd = m_runningCommands.first();

    if (pipelineCmd->cmd.isTransaction() &&
        (response.isOkMessage() || response.isQueuedMessage())) {
      return;
    }

    if (!pipelineCmd->cmd.isTransaction() && pipelineCmd->cmd.length() > 1) {
      pipelineCmd->cmd.removeFirstPipelineCmdFromQueue();
      return;
    }
  }

  auto runningCommand = m_runningCommands.dequeue();

  // Re-try on protocol errors
  if (response.isProtocolErrorMessage()) {
    m_commands.prepend(runningCommand->cmd);
    return;
  }

  // Reconnect to different server in cluster and reissue current
  // command if needed
  if (m_connection->mode() == Connection::Mode::Cluster) {
    if (response.isAskRedirect() || response.isMovedRedirect()) {
      return processClusterRedirect(runningCommand, response);
    }

    // Reset cluster redirections counter on first successful reply
    bool isKeyCmd = runningCommand->cmd.getKeyName().size() > 0;
    if (isKeyCmd) {
      m_followedClusterRedirects = 0;
    }
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

    if (runningCommand->cmd.isMonitorCommand()) {
        m_connection->m_currentMode = Connection::Mode::Monitor;
        m_subscriptions.insert(QByteArray(), runningCommand->emitter);
    }
  }
  runningCommand.clear();
}

void RedisClient::AbstractTransporter::resetDbIndex() {
    m_connection->changeCurrentDbNumber(0);
}

RedisClient::Command
RedisClient::AbstractTransporter::pickNextCommandForCurrentNode() {
  if (isSocketReconnectRequired()) {
    return Command();
  }

  auto config = m_connection->getConfig();
  int index = 0;

  for (auto cmd : m_commands) {
    bool keylessCmd = cmd.getKeyName().isEmpty();
    Connection::Host cmdHost = m_connection->getClusterHost(cmd);

    if (keylessCmd ||
        (config.overrideClusterHost() && cmdHost.first == config.host() &&
         cmdHost.second == config.port()) ||
        cmdHost.second == config.port()) {
      m_commands.removeAt(index);

      return cmd;
    }
    ++index;
  }

  return Command();
}

void RedisClient::AbstractTransporter::pickClusterNodeForNextCommand()
{
    if (m_pendingClusterRedirect)
        return;

    auto config = m_connection->getConfig();

    auto nextClusterHost = m_connection->getClusterHost(m_commands.first());

    QString host;
    int port = nextClusterHost.second;

    if (m_connection->m_config.overrideClusterHost()) {
      host = nextClusterHost.first;
    } else {
      host = config.host();
    }

    m_pendingClusterRedirect = true;

    emit logEvent(QString("Cluster node picked for next command: %1:%2")
                      .arg(host)
                      .arg(port));

    QTimer::singleShot(0, this, [this, host, port]() {      
      reconnectTo(host, port);
      m_pendingClusterRedirect = false;
    });
}

bool RedisClient::AbstractTransporter::validateSystemProxy() {
  QNetworkProxyQuery query;
  query.setQueryType(QNetworkProxyQuery::TcpSocket);
  auto proxy = QNetworkProxyFactory::systemProxyForQuery(query).constFirst();

  QSettings settings;
  bool disableProxy =
      settings.value("app/disableProxyForRedisConnections", false).toBool();

  qDebug() << "disableProxyForRedisConnections:" << disableProxy;

  qDebug() << "proxy type:" << proxy.type();

  return proxy.type() == QNetworkProxy::Socks5Proxy && !disableProxy;
}

void RedisClient::AbstractTransporter::reAddRunningCommandToQueue() {
  qDebug() << "Running commands: " << m_runningCommands.size();

  for (auto curr : m_runningCommands) {
      if (curr->cmd.isHiPriorityCommand()) {
        m_internalCommands.prepend(curr->cmd);
      } else {
        m_commands.prepend(curr->cmd);
      }

  }
  m_runningCommands.clear();

  qDebug() << "Running commands were re-added to queue";
  emit logEvent("Running commands were re-added to queue.");
}

void RedisClient::AbstractTransporter::cancelRunningCommands() {
  if (m_runningCommands.size() == 0) return;

  qDebug() << "Cancel running commands" << this;

  emit logEvent("Cancel running commands");
  m_runningCommands.clear();
}

void RedisClient::AbstractTransporter::processCommandQueue() {
  if (m_pendingClusterRedirect) {
    QTimer::singleShot(10, this, &AbstractTransporter::processCommandQueue);
    return;
  }

  if (m_internalCommands.isEmpty() && m_commands.isEmpty()) {
    emit queueIsEmpty();
    return;
  }  

  Command nextCmd;

  auto executeCmd = [this](const Command &cmd) {
    if (m_connection->mode() != Connection::Mode::Cluster && cmd.hasDbIndex()) {
      Command selectCmd(
          {"SELECT", QString::number(cmd.getDbIndex()).toLatin1()});
      runCommand(selectCmd);
    }

    runCommand(cmd);
    QTimer::singleShot(0, this, &AbstractTransporter::processCommandQueue);
  };

  if (m_internalCommands.size() > 0) {
      return executeCmd(m_internalCommands.dequeue());
  }

  for (auto runningCmd : m_runningCommands) {
      if (runningCmd->cmd.isHiPriorityCommand()) {
          QTimer::singleShot(0, this, &AbstractTransporter::processCommandQueue);
          return;
      }
  }  

  if (!m_connectionInitialized) {
      QTimer::singleShot(0, this, &AbstractTransporter::processCommandQueue);
      return;
  }

  if (m_connection->mode() == Connection::Mode::Cluster) {
    if (m_connection->m_clusterSlots.size() == 0 || m_runningCommands.size() > 0) {
        QTimer::singleShot(1, this, &AbstractTransporter::processCommandQueue);
        return;
    }

    if (m_connection->m_clusterSlots.size() > 0) {
      nextCmd = pickNextCommandForCurrentNode();

      if (!nextCmd.isValid() && m_runningCommands.size() == 0) {
        return pickClusterNodeForNextCommand();
      }
    } else {
      qWarning() << "Blind cluster connection";
    }
  }

  if (!nextCmd.isValid()) nextCmd = m_commands.dequeue();

  executeCmd(nextCmd);
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
}

void RedisClient::AbstractTransporter::processClusterRedirect(
    QSharedPointer<RunningCommand> runningCommand,
    const RedisClient::Response &response) {
  Q_ASSERT(runningCommand);

  if (m_followedClusterRedirects >= MAX_CLUSTER_REDIRECTS) {
      emit errorOccurred("Too many cluster redirects. Connection aborted.");
      disconnectFromHost();
      return;
  }

  m_commands.prepend(runningCommand->cmd);
  runningCommand.clear();

  if (m_pendingClusterRedirect) {
      return;
  }

  m_pendingClusterRedirect = true;

  QString host;
  int port = response.getRedirectionPort();

  if (m_connection->m_config.overrideClusterHost()) {
    host = response.getRedirectionHost();
  } else {
    host = m_connection->m_config.host();
  }

  // Information about cluster slots is outdated - trigger update
  m_connection->m_serverInfo = ServerInfo();
  m_connection->m_clusterSlots = Connection::ClusterSlots();

  emit logEvent(QString("Cluster redirect to  %1:%2").arg(host).arg(port));

  QTimer::singleShot(1, this, [this, host, port]() {        
    reconnectTo(host, port);

    m_pendingClusterRedirect = false;
    m_followedClusterRedirects += 1;
  });
}

void RedisClient::AbstractTransporter::reconnectTo(const QString &host,
                                                   int port) {
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
    qDebug() << "Cannot feed parsing buffer";
    return;
  }

  QList<RedisClient::Response> responses;
  RedisClient::Response resp;

  do {
    resp = m_parser.getNextResponse();

    if (resp.isValid()) responses.append(resp);
  } while (resp.isValid());

  for (auto r : responses) {
    if (m_connection->m_stoppingTransporter) {
      break;
    }
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
    m_commands.prepend(command);
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
    : cmd(cmd), emitter(nullptr), sentAt(QDateTime::currentMSecsSinceEpoch()) {
  auto callback = cmd.getCallBack();
  auto owner = cmd.getOwner();
  if (callback && owner) {
    emitter =
        QSharedPointer<ResponseEmitter>(new ResponseEmitter(owner, callback));
  }
}
