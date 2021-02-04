#pragma once
#include <QDebug>
#include <QSharedPointer>
#include <QTimer>
#include "qredisclient/command.h"
#include "qredisclient/response.h"
#include "qredisclient/transporters/abstracttransporter.h"
#include "qredisclient/connection.h"

class DummyTransporter : public RedisClient::AbstractTransporter {
  Q_OBJECT
 public:
  DummyTransporter(RedisClient::Connection* c)
      : RedisClient::AbstractTransporter(c),
        initCalls(0),
        disconnectCalls(0),
        addCommandCalls(0),
        cancelCommandsCalls(0),
        m_catchParsedResponses(false) {
    connect(c, &RedisClient::Connection::log,
            [](const QString& log) { qDebug() << "Connection log:" << log; });

    connect(c, &RedisClient::Connection::error,
            [](const QString& log) { qDebug() << "Connection error:" << log; });

    infoReply = QString("redis_version:999.999.999\n");
  }

  int initCalls;
  int disconnectCalls;
  int addCommandCalls;
  int cancelCommandsCalls;

  QString infoReply;

  void setFakeResponses(const QStringList& respList) {
    for (QString response : respList) {
      m_parser.feedBuffer(response.toLatin1());
      fakeResponses.push_back(m_parser.getNextResponse());
    }
  }

  void addFakeResponse(const QString& r) {
      m_parser.feedBuffer(r.toLatin1());
      fakeResponses.push_back(m_parser.getNextResponse());
  }

  void setFakeReadBuffer(const QByteArray& buf) {
    m_fakeBuffer = buf;
    m_catchParsedResponses = true;
  }

  QList<RedisClient::Command> executedCommands;
  QList<RedisClient::Response> fakeResponses;
  QList<RedisClient::Response> catchedResponses;

 public slots:
  void addCommands(const QList<RedisClient::Command>& commands) override {
    addCommandCalls += commands.size();
    RedisClient::AbstractTransporter::addCommands(commands);
  }

  void init() {
    initCalls++;

    // Init command tested after socket connection
    RedisClient::Response info(RedisClient::Response::Type::String,
                               infoReply);
    fakeResponses.push_front(info);

    RedisClient::Response r(RedisClient::Response::Type::String, "PONG");
    fakeResponses.push_front(r);

    emit connected();
  }
  virtual void disconnect() { disconnectCalls++; }
  virtual void cancelCommands(QObject*) override { cancelCommandsCalls++; }

 protected:
  virtual void runCommand(const RedisClient::Command& cmd) override {
    executedCommands.push_back(cmd);

    RedisClient::Response resp;

    if (fakeResponses.size() > 0) {
      resp = fakeResponses.first();
      fakeResponses.removeFirst();

      qDebug() << "cmd: " << cmd.getRawString();
      qDebug() << "fake resp: " << resp.value().toByteArray();

    } else {
      qDebug() << "Unexpected command: " << cmd.getRawString();
      qDebug() << "Previous commands:";
      for (auto cmd : executedCommands) {
        qDebug() << "\t" << cmd.getRawString();
      }
      resp = RedisClient::Response();
    }

    m_runningCommands.enqueue(
        QSharedPointer<RunningCommand>(new RunningCommand(cmd)));

    sendResponse(resp);
  }

  void sendResponse(const RedisClient::Response& response) {
    if (m_catchParsedResponses) {
      catchedResponses.append(response);
    } else {
      AbstractTransporter::sendResponse(response);
    }
  }

  void  reconnect() override {
      qDebug() << "Fake reconnect";      
      emit connected();
  }
  bool isInitialized() const override { return true; }
  bool isSocketReconnectRequired() const override { return false; }
  bool canReadFromSocket() override { return !m_fakeBuffer.isEmpty(); }
  QByteArray readFromSocket() override { return m_fakeBuffer; }
  void initSocket() override {}
  bool connectToHost() override { return true; }
  void sendCommand(const QByteArray&) override {}

 private:
  QByteArray m_fakeBuffer;
  bool m_catchParsedResponses;
};
