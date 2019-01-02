#include "test_transporters.h"
#include "mocks/dummyTransporter.h"

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
