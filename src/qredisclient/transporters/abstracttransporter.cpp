#include "abstracttransporter.h"
#include "qredisclient/connection.h"
#include <QDebug>

RedisClient::AbstractTransporter::AbstractTransporter(RedisClient::Connection *connection)
    : m_isCommandRunning(false), m_connection(connection)
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

    initSocket();
    connectToHost();
}

void RedisClient::AbstractTransporter::addCommand(Command cmd)
{
    if (cmd.isHiPriorityCommand())
        m_commands.prepend(cmd);
    else
        m_commands.enqueue(cmd);

    emit commandAdded();
    processCommandQueue();
}

void RedisClient::AbstractTransporter::cancelCommands(QObject *owner)
{    
    if (m_isCommandRunning && m_runningCommand.getOwner() == owner) {
        m_runningCommand.cancel();
        cleanRunningCommand();
        emit logEvent("Running command was canceled.");
    }

    // TODO: remove subscriptions

    QListIterator<Command> cmd(m_commands);

    while (cmd.hasNext())
    {
        auto currentCommand = cmd.next();

        if (currentCommand.getOwner() == owner) {
            currentCommand.cancel();
            emit logEvent("Command was canceled.");
        }
    }    
}

void RedisClient::AbstractTransporter::sendResponse(const RedisClient::Response& response)
{
    if (m_executionTimer)
        m_executionTimer->stop();

    if (m_runningCommand.isCanceled())
        return;

    logResponse(response);

    if (response.isMessage()) {
        QByteArray channel = response.getChannel();

        if (m_subscriptions.contains(channel))
            m_subscriptions[channel]->sendResponse(response, QString());

        return;
    }

    if (m_runningCommand.isUnSubscriptionCommand()) {
        // TODO: remove channels from m_subscriptions
        // TODO: send error to callbacks
    }

    if (m_emitter) { // NOTE(u_glide): Command has callback
       m_emitter->sendResponse(response, QString());

       if (m_runningCommand.isSubscriptionCommand()) {
           QList<QByteArray> channels = m_runningCommand.getSplitedRepresentattion().mid(1);

           for (QByteArray channel : channels) {
               m_subscriptions.insert(channel, m_emitter);
           }
       }
    }

    cleanRunningCommand();
}

void RedisClient::AbstractTransporter::processCommandQueue()
{
    if (m_isCommandRunning || m_commands.isEmpty()) {
        return;
    }

    auto command = m_commands.dequeue();
    runCommand(command);
}

void RedisClient::AbstractTransporter::cleanRunningCommand()
{        
    m_isCommandRunning = false;
    m_emitter.clear();
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

    emit logEvent(QString("%1 > [runCommand] %2 -> response received : %3")
                  .arg(m_connection->getConfig().name())
                  .arg(m_runningCommand.getRawString())
                  .arg(result));
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
        m_commands.enqueue(m_runningCommand);
        m_isCommandRunning = false;
        reconnect();
        return;
    }

    auto callback = command.getCallBack();
    auto owner = command.getOwner();
    if (callback && owner) {
        m_emitter = QSharedPointer<ResponseEmitter>(
                    new ResponseEmitter(owner, callback));
    }

    emit logEvent(QString("%1 > [runCommand] %2")
                  .arg(m_connection->getConfig().name())
                  .arg(command.getRawString()));

    m_response.reset();
    m_isCommandRunning = true;
    m_runningCommand = command;
    m_executionTimer->start(m_connection->getConfig().executeTimeout());
    sendCommand(m_runningCommand.getByteRepresentation());
}
