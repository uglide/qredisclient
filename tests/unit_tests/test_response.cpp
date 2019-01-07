#include "test_response.h"
#include <QTest>
#include <QtCore>
#include "qredisclient/response.h"
#include "qredisclient/responseparser.h"

void TestResponse::valueToHumanReadString() {
  // given
  QVariant testSource("test");

  // when
  QString actualResult =
      RedisClient::Response::valueToHumanReadString(testSource);

  // then
  QCOMPARE(actualResult, QString("\"test\""));
}

void TestResponse::scanResponse() {
  // given
  QString testResponse =
      "*2\r\n"
      ":1\r\n"
      "*2\r\n"
      "+Foo\r\n"
      "+Bar\r\n";
  RedisClient::ResponseParser parser;
  parser.feedBuffer(testResponse.toUtf8());
  RedisClient::Response test = parser.getNextResponse();

  // when
  int cursor = test.getCursor();
  QVariantList collection = test.getCollection();

  // then
  QVERIFY(test.isValidScanResponse());
  QCOMPARE(cursor, 1);
  QCOMPARE(collection, QVariantList() << "Foo"
                                      << "Bar");
}
