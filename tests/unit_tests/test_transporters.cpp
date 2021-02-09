#include "test_transporters.h"
#include "mocks/dummyTransporter.h"

#include <QSignalSpy>

void TestTransporters::readPartialResponses() {
  // given
  RedisClient::ConnectionConfig dummyConf = getDummyConfig();

  QSharedPointer<RedisClient::Connection> connection(
      new RedisClient::Connection(dummyConf));

  QSharedPointer<DummyTransporter> transporter(
      new DummyTransporter(connection.data()));

  // when
  transporter->setFakeReadBuffer("+QUEUED");
  transporter->readyRead();
  transporter->setFakeReadBuffer("\r\n+QUEUED\r\n");
  transporter->readyRead();

  // then
  QCOMPARE(transporter->catchedResponses.size(), 2);
}

void TestTransporters::handleClusterRedirects() {
  // given
  RedisClient::ConnectionConfig dummyConf = getDummyConfig();

  QSharedPointer<RedisClient::Connection> connection(
      new RedisClient::Connection(dummyConf));

  QSharedPointer<DummyTransporter> transporter(
      new DummyTransporter(connection.data()));

  QString infoReply(
      "redis_version:999.999.999\n"
      "redis_mode:cluster");

  QString clusterSlotsReply(
      "*3\r\n*4\r\n:5461\r\n:10922\r\n*3\r\n$9\r\n127.0.0.1\r\n:7001\r\n$"
      "40\r\n02b7c6390276511bf15bd79f713f1c2eefd03972\r\n*3\r\n$9\r\n127.0.0."
      "1\r\n:7005\r\n$40\r\n3c1ee6a71ffdea142c1851ec715c738fc70255ab\r\n*4\r\n:"
      "10923\r\n:16383\r\n*3\r\n$9\r\n127.0.0.1\r\n:7002\r\n$"
      "40\r\nb4e674914ccd289ee027faeb1e198be2b8118c5e\r\n*3\r\n$9\r\n127.0.0."
      "1\r\n:7003\r\n$40\r\n1811490667e63ea7f4773eb2218c697e1c1fe185\r\n*4\r\n:"
      "0\r\n:5460\r\n*3\r\n$9\r\n127.0.0.1\r\n:7000\r\n$"
      "40\r\n952e7b229300ac0023451b367b1058ce5676b031\r\n*3\r\n$9\r\n127.0.0."
      "1\r\n:7004\r\n$40\r\n9bce4881666b0bc2e51bfc3aba63d8e50c2114a2\r\n");

  QString movedReply("-MOVED 3999 127.0.0.1:7005\r\n");

  transporter->infoReply = infoReply;

  // Slots map "test" key to 7001 port
  transporter->addFakeResponse(clusterSlotsReply);
  transporter->addFakeResponse(QString("+PONG\r\n"));

  // But node reply with redirect to 7005 port
  transporter->addFakeResponse(movedReply);

  // Responses for reconnectons
  for (int i = 0; i < 6; i++) {
    transporter->addFakeResponse(QString("+PONG\r\n"));
    transporter->fakeResponses.push_back(
        RedisClient::Response(RedisClient::Response::Type::String, infoReply));
    transporter->addFakeResponse(clusterSlotsReply);    
    transporter->addFakeResponse(QString("+PONG\r\n"));
    transporter->addFakeResponse(movedReply);
  }
  QSignalSpy spy(connection.data(), &RedisClient::Connection::error);
  bool commandReturnedResult = false;

  // when
  connection->setTransporter(transporter);
  connection->connect();

  connection->cmd(
      {"type", "test"}, this, -1,
      [this, transporter, &commandReturnedResult](const RedisClient::Response& r) {
        qDebug() << "fake response received";
        qDebug() << "commands:" << transporter->executedCommands.size();
        commandReturnedResult = true;
      },
      [this](const QString& err) { qDebug() << "fake err received" << err; });

  wait(5000);
  QCOMPARE(transporter->executedCommands.size(), 30);
  QCOMPARE(commandReturnedResult, false);
  QCOMPARE(spy.count(), 1);
}
