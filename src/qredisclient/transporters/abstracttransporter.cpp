#include "abstracttransporter.h"
#include "qredisclient/connection.h"
#include <QDebug>

RedisClient::AbstractTransporter::AbstractTransporter(RedisClient::Connection *connection)
    : m_runningCommand(nullptr), m_connection(connection)
{
    //connect signals & slots between connection & transporter
    connect(connection, SIGNAL(addCommandToWorker(Command)), this, SLOT(addCommand(Command)));
    connect(this, SIGNAL(errorOccurred(const QString&)), connection, SIGNAL(error(const QString&)));
    connect(this, SIGNAL(logEvent(const QString&)), connection, SIGNAL(log(const QString&)));    
}

RedisClient::AbstractTransporter::~AbstractTransporter()
{
    disconnectFromHost();
}

void RedisClient::AbstractTransporter::init()
{
    if (isInitialized())
        return;

    m_executionTimer = QSharedPointer<QTimer>(new QTimer);
    m_executionTimer->setSingleShot(true);
    connect(m_executionTimer.data(), SIGNAL(timeout()), this, SLOT(executionTimeout()));

    m_loopTimer = QSharedPointer<QTimer>(new QTimer);
    m_loopTimer->setSingleShot(false);
    m_loopTimer->setInterval(0);
    connect(m_loopTimer.data(), SIGNAL(timeout()), this, SLOT(processCommandQueue()));
    connect(m_connection, &Connection::authOk, this, &AbstractTransporter::runProcessingLoop);

    initSocket();
    connectToHost();
}

void RedisClient::AbstractTransporter::addCommand(Command cmd)
{
    if (cmd.isHiPriorityCommand())
        m_commands.prepend(cmd);
    else
        m_commands.enqueue(cmd);

    processCommandQueue();

    emit commandAdded();
}

void RedisClient::AbstractTransporter::cancelCommands(QObject *owner)
{    
    if (m_runningCommand && m_runningCommand->cmd.getOwner() == owner) {
        m_runningCommand.clear();
        emit logEvent("Running command was canceled.");
    }    

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
    if (m_executionTimer)
        m_executionTimer->stop();

    logResponse(response);

    if (response.isMessage()) {
        QByteArray channel = response.getChannel();

        if (m_subscriptions.contains(channel))
            m_subscriptions[channel]->sendResponse(response, QString());

        return;
    }

    if (!m_runningCommand) {
        qDebug() << "Response recieved but command is not running";
        return;
    }

    // Reconnect to different server in cluster and reissue current
    // command if needed
    if (m_connection->mode() == Connection::Mode::Cluster
            && (response.isAskRedirect() || response.isMovedRedirect())) {
        return processClusterRedirect(response);
    }

    if (m_runningCommand->cmd.isUnSubscriptionCommand()) {
        // TODO: remove channels from m_subscriptions
        // TODO: send error to callbacks
    }

    if (m_runningCommand->emitter) {
       m_runningCommand->emitter->sendResponse(response, QString());

       if (m_runningCommand->cmd.isSubscriptionCommand())
            addSubscriptionsFromRunningCommand();
    }
    m_runningCommand.clear();
}

void RedisClient::AbstractTransporter::processCommandQueue()
{    
    if (m_runningCommand || m_commands.isEmpty()) {        
        return;
    }

    auto command = m_commands.dequeue();
    runCommand(command);
}

void RedisClient::AbstractTransporter::runProcessingLoop()
{
    m_loopTimer->start();
}

void RedisClient::AbstractTransporter::logResponse(const RedisClient::Response& response)
{
    if (!m_runningCommand)
        return;

    QString result;

    if (response.getType() == RedisClient::Response::Type::Status
            || response.getType() == RedisClient::Response::Type::Error) {
        result = response.toRawString();
    } else if (response.getType() == RedisClient::Response::Type::Bulk) {
        result = QString("Bulk");
    } else if (response.getType() == RedisClient::Response::Type::MultiBulk) {
        result = QString("Array");
    }

    emit logEvent(QString("%1 > [runCommand] %2 -> response received : %3")
                  .arg(m_connection->getConfig().name())
                  .arg(m_runningCommand->cmd.getRawString())
                  .arg(result));
}

void RedisClient::AbstractTransporter::processClusterRedirect(const RedisClient::Response &response)
{
    Q_ASSERT(m_runningCommand);
    qDebug() << "Cluster redirect";

    auto config = m_connection->getConfig();
    config.setHost(response.getRedirectionHost());
    config.setPort(response.getRedirectionPort());
    m_connection->setConnectionConfig(config);

    m_commands.prepend(m_runningCommand->cmd);
    m_runningCommand.clear();

    reconnect();
}

void RedisClient::AbstractTransporter::addSubscriptionsFromRunningCommand()
{
    Q_ASSERT(m_runningCommand);

    if (!m_runningCommand->emitter)
        return;

    QList<QByteArray> channels = m_runningCommand->cmd.getSplitedRepresentattion().mid(1);

    for (QByteArray channel : channels) {
        m_subscriptions.insert(channel, m_runningCommand->emitter);
    }
}

void RedisClient::AbstractTransporter::executionTimeout()
{
    emit errorOccurred("Execution timeout");
}

void RedisClient::AbstractTransporter::readyRead()
{
    if (!canReadFromSocket())
        return;

    m_executionTimer->stop();

    m_response.appendToSource(readFromSocket());

    if (!m_response.isValid()) {
        m_executionTimer->start(m_connection->getConfig().executeTimeout()); //restart execution timer
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
        m_commands.enqueue(command);
        reconnect();
        return;
    }

    emit logEvent(QString("%1 > [runCommand] %2")
                  .arg(m_connection->getConfig().name())
                  .arg(command.getRawString()));

    m_response.reset();
    m_runningCommand = QSharedPointer<RunningCommand>(new RunningCommand(command));
    m_executionTimer->start(m_connection->getConfig().executeTimeout());
    sendCommand(m_runningCommand->cmd.getByteRepresentation());
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
