#pragma once
#include <functional>
#include <QObject>
#include <QByteArray>
#include <QQueue>
#include <QTimer>
#include <QSharedPointer>

#include "qredisclient/command.h"
#include "qredisclient/response.h"

namespace RedisClient {

class Response;
class ResponseEmitter;
class Connection;

/**
 * @brief The AbstractTransporter class
 * Provides basic abstraction for transporting commands to redis-server.
 * THIS IS IMPLEMENTATION CLASS AND SHOULDN'T BE USED DIRECTLY.
 */
class AbstractTransporter : public QObject
{
    Q_OBJECT
public:
    AbstractTransporter(Connection * c); //TODO: replace raw pointer by WeakPtr
    virtual ~AbstractTransporter();

signals:
    void errorOccurred(const QString&);
    void logEvent(const QString&);
    void connected();
    void commandAdded();

public slots:
    virtual void init();
    virtual void disconnectFromHost() {}
    virtual void addCommand(Command);
    virtual void cancelCommands(QObject *);
    virtual void readyRead();

protected slots:
    virtual void executionTimeout();
    virtual void reconnect() = 0;
    virtual void processCommandQueue();
    virtual void runProcessingLoop();

protected:
    virtual bool isInitialized() const = 0;
    virtual bool isSocketReconnectRequired() const = 0;
    virtual bool canReadFromSocket() = 0;
    virtual QByteArray readFromSocket() = 0;
    virtual void initSocket() = 0;
    virtual bool connectToHost() = 0;
    virtual void runCommand(const Command &command);
    virtual void sendCommand(const QByteArray& cmd) = 0;
    virtual void sendResponse(const Response &response);

private:
    void logResponse(const Response &response);
    void processClusterRedirect(const Response& r);
    void addSubscriptionsFromRunningCommand();

protected:
    class RunningCommand {
    public:
        RunningCommand(const Command& cmd);
        Command cmd;
        QSharedPointer<ResponseEmitter> emitter;
    };

protected:
    Connection * m_connection;
    QSharedPointer<RunningCommand> m_runningCommand;
    Response m_response;    
    QQueue<Command> m_commands;
    QSharedPointer<QTimer> m_executionTimer;
    QSharedPointer<QTimer> m_loopTimer;
    typedef QHash<QByteArray, QSharedPointer<ResponseEmitter>> Subscriptions;
    Subscriptions m_subscriptions;
};


/**
 * @brief The ResponseEmitter class
 * Class used to send responses to callers.
 * THIS IS IMPLEMENTATION CLASS AND SHOULDN'T BE USED DIRECTLY.
 */
class ResponseEmitter : public QObject {
    Q_OBJECT
public:
    ResponseEmitter(QObject* owner, Command::Callback callback)
        : owner(owner)
    {
        QObject::connect(this, &ResponseEmitter::response,
                         owner, callback, Qt::AutoConnection);
    }

    void sendResponse(const Response& r, const QString& err)
    {        
        emit response(r, err);
    }
    QObject* owner;
signals:
    void response(Response, QString);
};
}
