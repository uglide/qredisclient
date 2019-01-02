#pragma once
#include <QDebug>
#include <QSharedPointer>
#include <QTimer>
#include "qredisclient/command.h"
#include "qredisclient/transporters/abstracttransporter.h"

class DummyTransporter : public RedisClient::AbstractTransporter {
  Q_OBJECT
 public:
  DummyTransporter(RedisClient::Connection* c)
      : RedisClient::AbstractTransporter(c),
        initCalls(0),
        disconnectCalls(0),
        addCommandCalls(0),
        cancelCommandsCalls(0),
        m_catchParsedResponses(false) {}

  int initCalls;
  int disconnectCalls;
  int addCommandCalls;
  int cancelCommandsCalls;

  void setFakeResponses(const QStringList& respList) {
    for (QString response : respList) {
      RedisClient::Response r(response.toLatin1());
      fakeResponses.push_back(r);
    }
  }

  void setFakeReadBuffer(const QByteArray& buf) {
    m_fakeBuffer = buf;
    m_catchParsedResponses = true;
  }

  QList<RedisClient::Command> executedCommands;
  QList<RedisClient::Response> fakeResponses;
  QList<RedisClient::Response> catchedResponses;

 public slots:
  void addCommand(const RedisClient::Command& cmd) override {
    addCommandCalls++;
    RedisClient::AbstractTransporter::addCommand(cmd);
  }

  void init() {
    initCalls++;

    // Init command tested after socket connection
    RedisClient::Response info("+redis_version:999.999.999\r\n");
    fakeResponses.push_front(info);

    RedisClient::Response r("+PONG\r\n");
    fakeResponses.push_front(r);

    m_loopTimer = QSharedPointer<QTimer>(new QTimer);
    m_loopTimer->setSingleShot(false);
    m_loopTimer->setInterval(1000);
    m_loopTimer->start();

    emit connected();
  }
  virtual void disconnect() { disconnectCalls++; }
  virtual void cancelCommands(QObject*) override { cancelCommandsCalls++; }

 protected:
  virtual void runCommand(const RedisClient::Command& cmd) override {
    executedCommands.push_back(cmd);

    if (fakeResponses.size() > 0) {
      m_response = fakeResponses.first();
      fakeResponses.removeFirst();
    } else {
      qDebug() << "Unexpected command: " << cmd.getRawString();
      qDebug() << "Previous commands:";
      for (auto cmd : executedCommands) {
        qDebug() << "\t" << cmd.getRawString();
      }
      m_response = RedisClient::Response();
    }

    m_runningCommands.enqueue(
        QSharedPointer<RunningCommand>(new RunningCommand(cmd)));

    sendResponse(m_response);
  }

  void sendResponse(const RedisClient::Response& response) {
    if (m_catchParsedResponses) {
      catchedResponses.append(response);
    } else {
      AbstractTransporter::sendResponse(response);
    }
  }

  void reconnect() override {}
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
