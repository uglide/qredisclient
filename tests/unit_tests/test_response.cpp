#include "test_response.h"
#include <QTest>
#include <QtCore>
#include "qredisclient/response.h"
#include "qredisclient/scanresponse.h"

void TestResponse::getValue() {
  // given
  RedisClient::Response test;
  QFETCH(QString, testResponse);
  QFETCH(QVariant, validResult);

  test.setSource(testResponse.toUtf8());

  // when
  QVariant actualResult = test.getValue();

  // then
  QCOMPARE(actualResult, validResult);
}

void TestResponse::getValue_data() {
  QTest::addColumn<QString>("testResponse");
  QTest::addColumn<QVariant>("validResult");

  QTest::newRow("Unknown") << "=" << QVariant();
  QTest::newRow("Empty") << "" << QVariant();
  QTest::newRow("Status") << "+OK\r\n" << QVariant(QString("OK"));
  QTest::newRow("Error") << "-ERR unknown command 'foobar'\r\n"
                         << QVariant(QString("ERR unknown command 'foobar'"));
  QTest::newRow("Integer") << ":99998\r\n" << QVariant(99998);
  QTest::newRow("Bulk") << "$6\r\nfoobar\r\n" << QVariant("foobar");
  QTest::newRow("Null Bulk") << "$-1\r\n" << QVariant();

  QVariant mb1;
  mb1.setValue(QVariantList{"1", "2", "foobar"});
  QTest::newRow("Multi Bulk") << "*3\r\n:1\r\n:2\r\n$6\r\nfoobar\r\n" << mb1;

  QVariant mb2;
  mb2.setValue(QVariantList{"app_id", "0", "keyword", "", "url", "nourl"});
  QTest::newRow("Multi Bulk with empty item")
      << "*6\r\n$6\r\napp_id\r\n$1\r\n0\r\n$7\r\nkeyword\r\n$0\r\n\r\n$"
         "3\r\nurl\r\n$5\r\nnourl\r\n"
      << mb2;

  QVariant mb3;
  mb3.setValue(QVariantList{"app_id", "0", "keyword", "", "url", "n\r\nrl"});
  QTest::newRow("Multi Bulk with \\r\\n in item")
      << "*6\r\n$6\r\napp_id\r\n$1\r\n0\r\n$7\r\nkeyword\r\n$0\r\n\r\n$"
         "3\r\nurl\r\n$5\r\nn\r\nrl\r\n"
      << mb3;

  QVariant mb4;
  mb4.setValue(
      QVariantList{"app_id", "0", "keyword", QString("快樂").toUtf8()});
  QTest::newRow("Multi Bulk with unicode item")
      << "*4\r\n$6\r\napp_id\r\n$1\r\n0\r\n$7\r\nkeyword\r\n$6\r\n快樂\r\n"
      << mb4;

  QTest::newRow("Array of arrays")
      << "*2\r\n"
         "*3\r\n"
         ":1\r\n"
         ":2\r\n"
         ":3\r\n"
         "*2\r\n"
         "+Foo\r\n"
         "+Bar\r\n"
      << QVariant(QVariantList{QVariantList{"1", "2", "3"},
                               QVariantList{"Foo", "Bar"}});
}

void TestResponse::source() {
  // given
  RedisClient::Response test;
  QByteArray testSource = "test_source";

  // when
  test.appendToSource(testSource);
  QByteArray actualResult = test.source();

  // then
  QCOMPARE(actualResult, testSource);
}

void TestResponse::valueToHumanReadString() {
  // given
  QVariant testSource("test");

  // when
  QString actualResult =
      RedisClient::Response::valueToHumanReadString(testSource);

  // then
  QCOMPARE(actualResult, QString("\"test\""));
}

void TestResponse::isValid() {
  // given
  QFETCH(QString, testResponse);
  QFETCH(bool, validResult);

  RedisClient::Response test(testResponse.toUtf8());

  // when
  bool actualOnValid = test.isValid();

  // then
  QCOMPARE(actualOnValid, validResult);
}

void TestResponse::isValid_data() {
  QTest::addColumn<QString>("testResponse");
  QTest::addColumn<bool>("validResult");

  // test int
  QTest::newRow("Int valid") << ":10000\r\n" << true;
  QTest::newRow("Int invalid") << ":99\n" << false;
  QTest::newRow("Int invalid") << ":" << false;
  QTest::newRow("Int invalid") << "" << false;

  // test bulk
  QTest::newRow("Bulk valid") << "$6\r\nfoobar\r\n" << true;
  QTest::newRow("Bulk valid") << "$-1\r\n" << true;
  QTest::newRow("Bulk valid") << "$12\r\n# Keyspace\r\n\r\n" << true;
  QTest::newRow("Bulk invalid") << "$1\r\n" << false;
  QTest::newRow("Bulk invalid") << "$5\r\n\r\n" << false;
  QTest::newRow("Bulk invalid") << "$5\r\nhell\r\n" << false;
  QTest::newRow("Bulk invalid") << "$5\r\n" << false;

  // test multi bulk
  QTest::newRow("Multi Bulk valid")
      << "*5\r\n:1\r\n:2\r\n:3\r\n:4\r\n$6\r\nfoobar\r\n"
      << true;
  QTest::newRow("Multi Bulk valid")
      << "*4\r\n$3\r\nfoo\r\n$3\r\nbar\r\n$5\r\nHello\r\n$5\r\nWorld\r\n"
      << true;

  QTest::newRow("Array of Arrays valid") << "*2\r\n"
                                            "*3\r\n"
                                            ":1\r\n"
                                            ":2\r\n"
                                            ":3\r\n"
                                            "*2\r\n"
                                            "+Foo\r\n"
                                            "+Bar\r\n"
                                         << true;

  QTest::newRow("Multi Bulk invalid") << "*5\r\n" << false;
  QTest::newRow("Multi Bulk invalid") << "*5\r\n:1\r\n" << false;
  QTest::newRow("Multi Bulk invalid") << "*2\r\n:1\r\n$6\r\nHello\r\n" << false;
}

void TestResponse::scanRespGetData() {
  // given
  QString testResponse =
      "*2\r\n"
      ":1\r\n"
      "*2\r\n"
      "+Foo\r\n"
      "+Bar\r\n";
  RedisClient::ScanResponse test(testResponse.toUtf8());

  // when
  int cursor = test.getCursor();
  QVariantList collection = test.getCollection();

  // then
  QCOMPARE(cursor, 1);
  QCOMPARE(collection, QVariantList() << "Foo"
                                      << "Bar");
}

void TestResponse::scanIsValid() {
  // given
  QString testResponse =
      "*2\r\n"
      ":1\r\n"
      "*1\r\n"
      "+Bar\r\n";
  RedisClient::Response test(testResponse.toUtf8());

  // when
  // then
  QCOMPARE(RedisClient::ScanResponse::isValidScanResponse(test), true);
}

void TestResponse::multipleResponsesInTheBuffer() {
  // given
  QString testResponse =
      "*2\r\n"
      ":1\r\n"
      "*1\r\n"
      "+Bar\r\n"
      "*1\r\n"
      "+Bar\r\n";
  RedisClient::Response test;

  // when
  test.setSource(testResponse.toUtf8());
  QVariant actualResult = test.getValue();

  // then
  QVERIFY(test.hasUnusedBuffer());
}

void TestResponse::hiredisBufferCleanup() {
  // given
  QString testResponse = "+TEST123\r\n";
  testResponse =
      QString("%1+VALID_UNUSED_BUFFER").arg(testResponse.repeated(103));
  RedisClient::Response test;
  RedisClient::Response nextResp;

  // when
  test.setSource(testResponse.toUtf8());
  test.getValue();

  for (int i = 0; i < 102; i++) {
    nextResp = test.getNextResponse();

    QVERIFY(nextResp.isValid());
    QCOMPARE(nextResp.source(), QByteArray("+TEST123\r\n"));
  }

  QCOMPARE(test.getUnusedBuffer(), QByteArray("+VALID_UNUSED_BUFFER"));

  test.appendToSource("\r\n");
  nextResp = test.getNextResponse();

  // then
  QVERIFY(nextResp.isValid());
}
