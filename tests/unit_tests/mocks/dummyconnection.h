#pragma once
#include <functional>
#include <QVariant>
#include "qredisclient/connection.h"
#include "qredisclient/scancommand.h"

class DummyConnection : public RedisClient::Connection
{
public:
    DummyConnection(double version=2.6, bool raise_error=false)
        : RedisClient::Connection(RedisClient::ConnectionConfig()),
          runCommandCalled(0), retrieveCollectionCalled(0),
          getServerVersionCalled(0), m_version(version), 
          m_raiseExceptionOnConnect(raise_error)
    {
    }

    bool isConnected() override
    {
        return !m_raiseExceptionOnConnect;
    }

    bool connect(bool wait=true) override
    {
        Q_UNUSED(wait);
        if (m_raiseExceptionOnConnect)
            throw RedisClient::Connection::Exception("fake error");

        return true;
    }

    void disconnect() override {}

    double getServerVersion() override
    {
        getServerVersionCalled++;
        return m_version;
    }

    void retrieveCollection(QSharedPointer<RedisClient::ScanCommand>,
                            Connection::CollectionCallback callback) override
    {
        QVariant resp;

        if (fakeScanCollections.size()) {
           resp = fakeScanCollections.first();
           fakeScanCollections.removeFirst();
        }

        retrieveCollectionCalled++;
        callback(resp, QString());
    }

    void runCommand(RedisClient::Command &cmd) override
    {
        RedisClient::Response resp;

        if (fakeResponses.size()) {
           resp = fakeResponses.first();
           fakeResponses.removeFirst();
        } else {
           qDebug() << "Unexpected command: "<< cmd.getRawString();
        }

        auto callback = cmd.getCallBack();
        callback(resp, QString());

        runCommandCalled++;        
    }

    uint runCommandCalled;
    uint retrieveCollectionCalled;
    uint getServerVersionCalled;

    QList<QVariant> fakeScanCollections;
    QList<RedisClient::Response> fakeResponses;

    void setFakeResponses(const QStringList& respList)
    {
        for (QString response : respList) {
            RedisClient::Response r(response.toLatin1());
            fakeResponses.push_back(r);
        }
    }

private:
    double m_version;
    bool m_raiseExceptionOnConnect;
};

