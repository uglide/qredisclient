#include <QTest>
#include <QtCore>
#include "qredisclient/response.h"
#include "qredisclient/responseparser.h"
#include "test_responseparer.h"

void TestResponseParser::parsing() {
  // given
  RedisClient::ResponseParser parser;
  QFETCH(QString, testResponse);
  QFETCH(QVariant, validResult);

  parser.feedBuffer(testResponse.toUtf8());

  // when
  QVariant actualResult = parser.getNextResponse().value();

  // then
  QCOMPARE(actualResult, validResult);
}

void TestResponseParser::parsing_data() {
  QTest::addColumn<QString>("testResponse");
  QTest::addColumn<QVariant>("validResult");

  //  QTest::newRow("Unknown") << "=" << QVariant();
  //  QTest::newRow("Empty") << "" << QVariant();
  //  QTest::newRow("Status") << "+OK\r\n" << QVariant(QString("OK"));
  //  QTest::newRow("Error") << "-ERR unknown command 'foobar'\r\n"
  //                         << QVariant(QString("ERR unknown command
  //                         'foobar'"));
  //  QTest::newRow("Integer") << ":99998\r\n" << QVariant(99998);
  //  QTest::newRow("Bulk") << "$6\r\nfoobar\r\n" << QVariant("foobar");
  //  QTest::newRow("Null Bulk") << "$-1\r\n" << QVariant();

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

void TestResponseParser::source() {
  // given
  RedisClient::ResponseParser parser;
  QByteArray testSource = "test_source";

  // when
  parser.feedBuffer(testSource);
  QByteArray actualResult = parser.buffer();

  // then
  QCOMPARE(actualResult, testSource);
}

void TestResponseParser::validation() {
  // given
  QFETCH(QString, testResponse);
  QFETCH(bool, validResult);

  RedisClient::ResponseParser parser;

  // when
  parser.feedBuffer(testResponse.toUtf8());
  bool actualOnValid = parser.getNextResponse().isValid();

  // then
  QCOMPARE(actualOnValid, validResult);
}

void TestResponseParser::validation_data() {
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

void TestResponseParser::multipleResponsesInTheBuffer() {
  // given
  QString testResponse =
      "*2\r\n"
      ":1\r\n"
      "*1\r\n"
      "+Bar\r\n"
      "*1\r\n"
      "+Bar\r\n";
  RedisClient::ResponseParser parser;

  // when
  parser.feedBuffer(testResponse.toUtf8());
  parser.getNextResponse();

  // then
  QVERIFY(parser.hasUnusedBuffer());
  QVERIFY(parser.getNextResponse().value().isValid());
}

void TestResponseParser::hiredisBufferCleanup() {
  // given
  QString testResponse = "+TEST123\r\n";
  testResponse =
      QString("%1+VALID_UNUSED_BUFFER").arg(testResponse.repeated(103));
  RedisClient::ResponseParser parser;
  RedisClient::Response resp;

  // when
  parser.feedBuffer(testResponse.toUtf8());

  for (int i = 0; i < 103; i++) {
    resp = parser.getNextResponse();

    QVERIFY(resp.isValid());
    QCOMPARE(resp.value().toByteArray(), QByteArray("TEST123"));
  }

  QCOMPARE(parser.unusedBuffer(), QByteArray("+VALID_UNUSED_BUFFER"));

  parser.feedBuffer("\r\n");
  resp = parser.getNextResponse();

  // then
  QVERIFY(resp.isValid());
}
