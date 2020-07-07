#pragma once
#include <asyncfuture.h>
#include <QVariant>
#include <functional>
#include "qredisclient/connection.h"
#include "qredisclient/responseparser.h"
#include "qredisclient/scancommand.h"

class DummyConnection : public RedisClient::Connection {
 public:
  DummyConnection(double version = 2.6, bool raise_error = false)
      : RedisClient::Connection(RedisClient::ConnectionConfig()),
        runCommandCalled(0),
        retrieveCollectionCalled(0),
        getServerVersionCalled(0),
        returnErrorOnCmdRun(false),
        m_version(version),
        m_raiseExceptionOnConnect(raise_error) {}

  QSharedPointer<RedisClient::Connection> clone(bool copyServerInfo=true) const override {
    if (m_clone) {
      return m_clone;
    }
    return RedisClient::Connection::clone();
  }

  bool isConnected() override { return !m_raiseExceptionOnConnect; }

  bool connect(bool wait = true) override {
    Q_UNUSED(wait);
    if (m_raiseExceptionOnConnect)
      throw RedisClient::Connection::Exception("fake error");

    return true;
  }

  void disconnect() override {}

  double getServerVersion() override {
    getServerVersionCalled++;
    return m_version;
  }

  void retrieveCollection(const RedisClient::ScanCommand&,
                          Connection::CollectionCallback callback) override {
    QVariant resp;

    if (fakeScanCollections.size()) {
      resp = fakeScanCollections.first();
      fakeScanCollections.removeFirst();
    }

    retrieveCollectionCalled++;
    callback(resp, QString());
  }

  QFuture<RedisClient::Response> runCommand(
      const RedisClient::Command& cmd) override {
    RedisClient::Response resp;
    AsyncFuture::Deferred<RedisClient::Response> d;

    if (returnErrorOnCmdRun) {
      auto callback = cmd.getCallBack();
      callback(resp, QString("fake error"));
      return d.future();
    }

    if (fakeResponses.size()) {
      resp = fakeResponses.first();
      fakeResponses.removeFirst();
    } else {
      qDebug() << "Unexpected command: " << cmd.getRawString();
    }

    auto callback = cmd.getCallBack();
    callback(resp, QString());

    runCommandCalled++;
    executedCommands.push_back(cmd);
    return d.future();
  }

  uint runCommandCalled;
  uint retrieveCollectionCalled;
  uint getServerVersionCalled;

  QList<QVariant> fakeScanCollections;
  QList<RedisClient::Response> fakeResponses;

  void setFakeResponses(const QStringList& respList) {
    RedisClient::ResponseParser p;

    for (QString response : respList) {
      p.feedBuffer(response.toLatin1());

      if (response.contains("# Keyspace") && respList.size() > 0) {
        m_serverInfo = RedisClient::ServerInfo::fromString(
            p.getNextResponse().value().toString());
        continue;
      }

      fakeResponses.push_back(p.getNextResponse());
    }
  }

  void setClone(QSharedPointer<RedisClient::Connection> clone) {
    m_clone = clone;
  }

  QList<RedisClient::Command> executedCommands;
  bool returnErrorOnCmdRun;

 private:
  double m_version;
  bool m_raiseExceptionOnConnect;
  QSharedPointer<RedisClient::Connection> m_clone;
};
