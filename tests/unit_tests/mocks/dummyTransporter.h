#pragma once
#include <QDebug>
#include "qredisclient/command.h"
#include "qredisclient/transporters/abstracttransporter.h"

class DummyTransporter : public RedisClient::AbstractTransporter
{
    Q_OBJECT
public:
    DummyTransporter(RedisClient::Connection* c)
        : RedisClient::AbstractTransporter(c),
          initCalls(0),
          disconnectCalls(0),
          addCommandCalls(0),
          cancelCommandsCalls(0)
    {

    }

    int initCalls;
    int disconnectCalls;
    int addCommandCalls;
    int cancelCommandsCalls;

    void setFakeResponses(const QStringList& respList)
    {
        for (QString response : respList) {
            RedisClient::Response r(response.toLatin1());
            fakeResponses.push_back(r);
        }
    }

    QList<RedisClient::Command> executedCommands;
    QList<RedisClient::Response> fakeResponses;

public slots:
    void addCommand(RedisClient::Command cmd) override
    {
        addCommandCalls++;
        RedisClient::AbstractTransporter::addCommand(cmd);
    }

    void init()
    {
        initCalls++;

        // Init command tested after socket connection
        RedisClient::Response info("+FAKE_SERVER_INFO\r\n");
        fakeResponses.push_front(info);

        RedisClient::Response r("+PONG\r\n");
        fakeResponses.push_front(r);

        qDebug() << "Transporter connect: OK";
        emit connected();
    }
    virtual void disconnect() { disconnectCalls++; }
    virtual void cancelCommands(QObject *) override { cancelCommandsCalls++; }

protected:
    virtual void runCommand(const RedisClient::Command &cmd) override
    {
        executedCommands.push_back(cmd);        
        m_runningCommand = cmd;

        if (fakeResponses.size() > 0) {            
            m_response = fakeResponses.first();
            fakeResponses.removeFirst();                        
        } else {
            qDebug() << "Unexpected command: "<< cmd.getRawString();
            qDebug() << "Previous commands:";
            for (auto cmd : executedCommands) {
                qDebug() << "\t" << cmd.getRawString();
            }
            m_response = RedisClient::Response();
        }

        auto callback = cmd.getCallBack();
        auto owner = cmd.getOwner();
        if (callback && owner) {
            m_emitter = QSharedPointer<RedisClient::ResponseEmitter>(
                        new RedisClient::ResponseEmitter(owner, callback));
        }

        sendResponse(m_response);
    }

    void reconnect() override {}
    bool isInitialized() const override { return true; }
    bool isSocketReconnectRequired() const override { return false; }
    bool canReadFromSocket() override { return false; }
    QByteArray readFromSocket() override { return QByteArray(); }
    void initSocket() override {}
    bool connectToHost() override { return true; }
    void sendCommand(const QByteArray&) override {}
};
