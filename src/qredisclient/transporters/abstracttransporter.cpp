#include "abstracttransporter.h"
#include "qredisclient/connection.h"
#include "qredisclient/utils/text.h"
#include <QDebug>

RedisClient::AbstractTransporter::AbstractTransporter(RedisClient::Connection *connection)
    : m_connection(connection), m_reconnectEnabled(true)
{
    //connect signals & slots between connection & transporter
    connect(connection, SIGNAL(addCommandToWorker(const Command&)), this, SLOT(addCommand(const Command&)));
    connect(connection, SIGNAL(reconnectTo(const QString&, int)), this, SLOT(reconnectTo(const QString&, int)));
    connect(this, SIGNAL(logEvent(const QString&)), connection, SIGNAL(log(const QString&)));

    connect(this, &AbstractTransporter::errorOccurred, this, &AbstractTransporter::cancelRunningCommands);
}

RedisClient::AbstractTransporter::~AbstractTransporter()
{
    disconnectFromHost();
    m_loopTimer.clear();
}

void RedisClient::AbstractTransporter::init()
{
    if (isInitialized())
        return;

    qDebug() << "Init transporter";

    m_loopTimer = QSharedPointer<QTimer>(new QTimer);
    m_loopTimer->setSingleShot(false);
    m_loopTimer->setInterval(1000);
    connect(m_loopTimer.data(), SIGNAL(timeout()), this, SLOT(processCommandQueue()));
    connect(m_connection, &Connection::authOk, this, &AbstractTransporter::runProcessingLoop);

    initSocket();
    connectToHost();
}

void RedisClient::AbstractTransporter::addCommand(const Command& cmd)
{
    if (cmd.isHiPriorityCommand())
        m_commands.prepend(cmd);
    else
        m_commands.enqueue(cmd);

    if ((cmd.isHiPriorityCommand() && isInitialized()) || m_loopTimer->isActive())
        processCommandQueue();

    emit commandAdded();
}

void RedisClient::AbstractTransporter::cancelCommands(QObject *owner)
{    
    if (!owner)
        return;

    reAddRunningCommandToQueue(owner);

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

    if (m_commands.size() == 0)
        return;

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

void RedisClient::AbstractTransporter::sendResponse(const RedisClient::Response& response)
{
    logResponse(response);

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
            emit errorOccurred(response.toRawString());
        }
        return;
    }

    auto runningCommand = m_runningCommands.dequeue();

    // Reconnect to different server in cluster and reissue current
    // command if needed
    if (m_connection->mode() == Connection::Mode::Cluster
            && (response.isAskRedirect() || response.isMovedRedirect())) {
        return processClusterRedirect(runningCommand, response);
    }

    if (runningCommand->cmd.isUnSubscriptionCommand()) {
        // TODO: remove channels from m_subscriptions
        // TODO: send error to callbacks
    }

    if (runningCommand->cmd.isSelectCommand() && response.isOkMessage()) {
        m_connection->changeCurrentDbNumber(runningCommand->cmd.getPartAsString(1).toInt());
    }

    if (runningCommand->emitter) {
       runningCommand->emitter->sendResponse(response, QString());

       if (runningCommand->cmd.isSubscriptionCommand())
            addSubscriptionsFromRunningCommand(runningCommand);
    }
    runningCommand.clear();
}

void RedisClient::AbstractTransporter::resetDbIndex()
{
   m_connection->changeCurrentDbNumber(0);
}

void RedisClient::AbstractTransporter::reAddRunningCommandToQueue(QObject *ignoreOwner)
{
    for (auto curr = m_runningCommands.end(); curr != m_runningCommands.begin();) {
        --curr;

        auto rCmd = *curr;
        if (ignoreOwner == nullptr || rCmd->cmd.getOwner() != ignoreOwner) {
            m_commands.prepend(rCmd->cmd);
            qDebug() << "Running command was re-added to queue";
            emit logEvent("Running command was re-added to queue.");
        }

        curr = m_runningCommands.erase(curr);        
    }
}

void RedisClient::AbstractTransporter::cancelRunningCommands()
{
    emit logEvent("Cancel running commands");
    m_runningCommands.clear();
}

void RedisClient::AbstractTransporter::processCommandQueue()
{    
    if (m_commands.isEmpty()) {
        emit queueIsEmpty();
        return;
    }

    // Do not allow commands execution if we selecting db now
    for (auto curr = m_runningCommands.begin(); curr != m_runningCommands.end();) {
        auto rCmd = *curr;

        if (rCmd->cmd.isSelectCommand()) {
            qDebug() << "Block commands. Wait for SELECT finish.";
            return;
        }

        ++curr;
    }    

    if (m_runningCommands.size() > 0 && m_commands.head().isSelectCommand()) {
        qDebug() << "Wait for regular commands before db SELECT";
        return;
    }

    if (m_commands.head().hasDbIndex() && m_connection->m_dbNumber != m_commands.head().getDbIndex()) {
        if (m_runningCommands.size() > 0) {
            qDebug() << "Wait for regular commands before db SELECT";
            return;
        }

        QList<QByteArray> selectCmdRaw = {"SELECT", QString::number(m_commands.head().getDbIndex()).toLatin1()};
        Command selectCmd(selectCmdRaw);
        qDebug() << "SELECT proper DB for running command.";
        runCommand(selectCmd);
        return;
    }

    runCommand(m_commands.dequeue());
}

void RedisClient::AbstractTransporter::runProcessingLoop()
{
    qDebug() << "Start processing loop in transporter";
    m_loopTimer->start();
}

void RedisClient::AbstractTransporter::logResponse(const RedisClient::Response& response)
{
    QString result;

    if (response.getType() == RedisClient::Response::Type::Status
            || response.getType() == RedisClient::Response::Type::Error) {
        result = response.toRawString();
    } else if (response.getType() == RedisClient::Response::Type::Bulk) {
        result = QString("Bulk");
    } else if (response.getType() == RedisClient::Response::Type::MultiBulk) {
        result = QString("Array");
    }

    emit logEvent(QString("%1 > Response received : %2")
                  .arg(m_connection->getConfig().name())                  
                  .arg(result));

    //qDebug() << "Response:" << response.source();
}

void RedisClient::AbstractTransporter::processClusterRedirect(QSharedPointer<RunningCommand> runningCommand,
                                                              const RedisClient::Response &response)
{
    Q_ASSERT(runningCommand);
    qDebug() << "Cluster redirect";

    m_commands.prepend(runningCommand->cmd);
    runningCommand.clear();
    m_loopTimer->stop();

    QString host;
    int port = response.getRedirectionPort();

    if (m_connection->m_config.overrideClusterHost()) {
        host = response.getRedirectionHost();
    } else {
        host = m_connection->m_config.host();
    }

    QTimer::singleShot(1, this, [this, host, port]() {
        reconnectTo(host, port);
    });
}

void RedisClient::AbstractTransporter::reconnectTo(const QString &host, int port)
{
    qDebug() << "ReConnect to:" << host << port;

    auto config = m_connection->getConfig();
    config.setHost(host);
    config.setPort(port);
    m_connection->setConnectionConfig(config);

    reconnect();
}

void RedisClient::AbstractTransporter::addSubscriptionsFromRunningCommand(QSharedPointer<RunningCommand> runningCommand)
{
    Q_ASSERT(runningCommand);

    if (!runningCommand->emitter)
        return;

    QList<QByteArray> channels = runningCommand->cmd.getSplitedRepresentattion().mid(1);

    for (QByteArray channel : channels) {
        m_subscriptions.insert(channel, runningCommand->emitter);
    }
}

void RedisClient::AbstractTransporter::executionTimeout()
{
    qDebug() << "Command execution/download timeout";
    emit errorOccurred("Execution timeout");
}

void RedisClient::AbstractTransporter::readyRead()
{
    if (!canReadFromSocket())
        return;

    m_response.appendToSource(readFromSocket());

    if (!m_response.isValid()) {
        return;
    }

    QList<RedisClient::Response> responses;
    responses.append(m_response);

    while (m_response.hasUnusedBuffer()) {
        m_response = RedisClient::Response(m_response.getUnusedBuffer());

        if (m_response.isValid())
            responses.append(m_response);
    }

    if (m_response.isValid())
        m_response.reset();

    for (auto r: responses) {
        sendResponse(r);
    }
}

void RedisClient::AbstractTransporter::runCommand(const RedisClient::Command &command)
{    
    if (isSocketReconnectRequired()) {
        if (!m_reconnectEnabled) {
            qDebug() << "Connection disconnected on error. Ignoring commands.";
            return;
        }
        qDebug() << "Cannot run command. Reconnect is required.";
        m_commands.enqueue(command);
        reconnect();
        return;
    }

    qDebug() << "Run command:" << command.getRawString() << " in db "
             << m_connection->m_dbNumber << " with timeout " << m_connection->getConfig().executeTimeout();

    emit logEvent(QString("%1 > [runCommand] %2")
                  .arg(m_connection->getConfig().name())
                  .arg(printableString(command.getRawString())));

    //m_response.reset();
    auto runningCommand = QSharedPointer<RunningCommand>(new RunningCommand(command));
    m_runningCommands.enqueue(runningCommand);

    if (m_runningCommands.size() > 1) {
        qDebug() << "Multiple commands running";
    }

    sendCommand(runningCommand->cmd.getByteRepresentation());
}


RedisClient::AbstractTransporter::RunningCommand::RunningCommand(const RedisClient::Command &cmd)
    : cmd(cmd), emitter(nullptr)
{
    auto callback = cmd.getCallBack();
    auto owner = cmd.getOwner();
    if (callback && owner) {
        emitter = QSharedPointer<ResponseEmitter>(
                    new ResponseEmitter(owner, callback));
    }
}
