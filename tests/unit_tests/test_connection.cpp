#include "test_connection.h"
#include <QTest>
#include <chrono>
#include <thread>
#include "qredisclient/command.h"
#include "qredisclient/connection.h"

using namespace RedisClient;

void TestConnection::init() {
  qRegisterMetaType<RedisClient::Command>("Command");
  qRegisterMetaType<RedisClient::Response>("RedisClient::Response");

  config = ConnectionConfig("127.0.0.1", "test", 6379, "test");
  config.setTimeouts(10000, 100000);
}

#ifdef INTEGRATION_TESTS
void TestConnection::connectToHostAndRunCommand() {
  // given
  Connection connection(config);

  // when
  // sync execution
  QVERIFY(connection.connect());
  Response actualResult = connection.command({"PING"}).result();

  // then  
  QCOMPARE(actualResult.value().toString(), QString("PONG"));
}

void TestConnection::testScanCommand() {
  // given
  Connection connection(config);

  // when
  QVERIFY(connection.connect());
  Response result = connection.command({"SCAN", "0"}).result();
  QVariant value = result.value();

  // then
  QCOMPARE(value.isNull(), false);
}

void TestConnection::testRetriveCollection() {
  // given
  Connection connection(config);
  ScanCommand cmd({"SCAN", "0"});  // valid
  bool callbackCalled = false;
  QVERIFY(cmd.isValidScanCommand());

  // when
  QVERIFY(connection.connect());
  connection.command({"FLUSHDB"}).result();
  connection.command({"SET", "test", "1"}).result();

  connection.retrieveCollection(
      cmd, [&callbackCalled](QVariant result, QString) {
        // then - part 1
        QCOMPARE(result.isNull(), false);
        QCOMPARE(result.toList().size(), 1);
        QCOMPARE(result.canConvert(QMetaType::QVariantList), true);
        callbackCalled = true;
      });
  wait(2000);

  // then - part 2
  QCOMPARE(callbackCalled, true);
}

void TestConnection::runEmptyCommand() {
  // given
  Connection connection(config);
  Command cmd;

  // when
  QVERIFY(connection.connect());
  bool hasException = false;
  try {
    connection.runCommand(cmd);
  } catch (Connection::Exception&) {
    hasException = true;
  }

  // then
  QCOMPARE(hasException, true);
}

void TestConnection::runPipelineCommandSync() {
  Connection connection(config, true);
  QVERIFY(connection.connect());

  RedisClient::Command cmd;
  cmd.addToPipeline({"SET", "pipelines", "rock"});
  cmd.addToPipeline({"HSET", "MyHash", "Key1", "1234"});
  cmd.addToPipeline({"HSET", "MyHash", "Key2", "ABCDEFGH"});
  RedisClient::Response response = connection.command(cmd).result();

  QCOMPARE(response.isArray(), true);
  QCOMPARE(response.value().toList().length(), 3);
  auto validResult = QVariantList() << QString("OK") << 1 << 1;
  QCOMPARE(response.value().toList(), validResult);
}

void TestConnection::runPipelineCommandAsync() {
  Connection connection(config, true);
  QVERIFY(connection.connect());

  RedisClient::Command cmd({"SET", "pipelines", "async"});
  cmd.addToPipeline({"HSET", "MyHashAsync", "Key1", "1234"});
  cmd.addToPipeline({"HSET", "MyHashAsync", "Key2", "ABCDEFGH"});

  // Setup callback
  RedisClient::Response response;
  QObject* owner = new QObject();
  QEventLoop eventLoop;
  connect(this, SIGNAL(callbackReceived()), &eventLoop, SLOT(quit()));
  cmd.setCallBack(owner,
                  [this, &response](RedisClient::Response resp, QString) {
                    response = resp;
                    emit this->callbackReceived();
                  });

  connection.command(cmd);
  eventLoop.exec();  // Wait for callback to complete

  QCOMPARE(response.isArray(), true);
  QCOMPARE(response.value().toList().length(), 3);
  auto validResult = QVariantList() << QString("OK") << 1 << 1;
  QCOMPARE(response.value().toList(), validResult);
}

void TestConnection::runBinaryPipelineCommand() {
  Connection connection(config, true);
  QVERIFY(connection.connect());

  RedisClient::Command cmd;
  cmd.addToPipeline({"SET", "crlf"});
  cmd.append("\r\n");
  cmd.addToPipeline({"SET", "binary"});
  QByteArray arr;
  for (char k=31; k>=0; k--)
    arr.append(k);
  cmd.append(arr);

  RedisClient::Response response = connection.command(cmd).result();
  QCOMPARE(response.isArray(), true);
  QCOMPARE(response.value().toList().length(), 2);
  auto validResult = QVariantList() << QString("OK") << QString("OK");
  QCOMPARE(response.value().toList(), validResult);

  // Read back and check content
  response = connection.command({"GET", "crlf"}).result();
  QCOMPARE(response.value().toByteArray(), QByteArray("\r\n"));

  response = connection.command({"GET", "binary"}).result();
  QCOMPARE(response.value().toByteArray().size(), 32);
  QCOMPARE(response.value().toByteArray(), arr);
}


void TestConnection::benchmarkPipeline() {
  Connection connection(config, true);
  QVERIFY(connection.connect());
  connection.command({"flushdb"}).result();

  RedisClient::Command cmd;
  int N = 10000;
  for (int k = 1; k <= N; k++)
  {
    cmd.addToPipeline({"hset", "h"});
    cmd.append(QString("k%1").arg(k).toUtf8());
    cmd.append(QString("%1").arg(k).toUtf8());
  }

  // Measure the time it takes to complete the transaction:
  QTime t0;
  t0.start();
  RedisClient::Response response;
  try {
    response = connection.command(cmd).result();
  } catch (const RedisClient::Connection::Exception& e) {
    qDebug() << "print" << e.what();
  }
  int tf = t0.elapsed();
  QWARN(QString("Benchmark %1 HSET: %2 ms").arg(N).arg(tf).toUtf8());

  QCOMPARE(response.isArray(), true);
  QCOMPARE(response.value().toList().length(), N);

  // Read back the N'th value
  RedisClient::Response valResponse =
      connection.command({"hget", "h", QString("k%1").arg(N).toLatin1()}).result();
  QCOMPARE(valResponse.value(), QVariant(N));
}

void TestConnection::benchmarkPipelineAsync() {
  Connection connection(config, true);
  QVERIFY(connection.connect());
  connection.command({"flushdb"}).result();

  RedisClient::Command cmd;
  int N = 10000;
  for (int k = 1; k <= N; k++)
  {
    cmd.addToPipeline({"hset", "ha"});
    cmd.append(QString("k%1").arg(k).toUtf8());
    cmd.append(QString("%1").arg(k).toUtf8());
  }

  // Setup callback
  int tf;
  QTime t0;
  RedisClient::Response response;
  QObject* owner = new QObject();
  QEventLoop eventLoop;
  connect(this, SIGNAL(callbackReceived()), &eventLoop, SLOT(quit()));
  cmd.setCallBack(owner, [this, &response, &t0, &tf](
                             RedisClient::Response _response, QString) {
    tf = t0.elapsed();
    response = _response;
    emit this->callbackReceived();
  });

  // Wait for callback:
  t0.start();
  connection.command(cmd);
  eventLoop.exec();
  QWARN(QString("Benchmark %1 HSET: %2 ms").arg(N).arg(tf).toUtf8());

  QCOMPARE(response.isArray(), true);
  QCOMPARE(response.value().toList().length(), N);

  // Read back the N'th value
  RedisClient::Response valResponse =
      connection.command({"hget", "ha", QString("k%1").arg(N).toLatin1()}).result();
  QCOMPARE(valResponse.value(), QVariant(N));
}

void TestConnection::autoConnect() {
  // given
  Connection connection(config);

  // when
  bool hasException = false;
  try {
    connection.command({"PING"});  // valid
  } catch (Connection::Exception&) {
    hasException = true;
  }

  // then
  QCOMPARE(hasException, false);
}

void TestConnection::runCommandAndDelete() {
  // given
  Connection connection(config);
  Command cmd(QList<QByteArray>{"ping"});
  QObject* owner = new QObject();
  cmd.setCallBack(owner, [](RedisClient::Response, QString) {});

  // when
  QVERIFY(connection.connect());
  connection.runCommand(cmd);
  connection.runCommand(cmd);
  owner->deleteLater();

  // then
  // no errors
}

void TestConnection::subscribeAndUnsubscribe() {
  // given
  Connection connection(config);
  Connection connectionPublisher(config);
  int messagesRecieved = 0;

  // when
  QVERIFY(connection.connect());
  QVERIFY(connectionPublisher.connect());

  connection.command(QList<QByteArray>{"SUBSCRIBE", "ch1", "ch2", "ch3"}, this,
                     [&messagesRecieved](RedisClient::Response r, QString) {
                       qDebug() << "recieved msg:" << r.value().toList();
                       messagesRecieved++;
                     });
  connectionPublisher.command({"PUBLISH", "ch1", "MSG1"});
  connectionPublisher.command({"PUBLISH", "ch2", "MSG2"});
  connectionPublisher.command({"PUBLISH", "ch3", "MSG3"});

  wait(5000);

  // then
  QCOMPARE(4, messagesRecieved);  // Subscribe resp + 3 messages from publisher
}

void TestConnection::checkTimeout() {
  // given
  Connection connection(config);

  // when
  QVERIFY(connection.connect());

  int totalRuns = 20;

  for (int c = 0; c < totalRuns; c++) {
    qWarning() << "Wait " << (c + 1) << "/" << totalRuns;
    wait(1000 * 10);
  }

  Response actualCommandResult = connection.command({"ping"}).result();

  // then
  QCOMPARE(actualCommandResult.value().toString(), QString("PONG"));
}

void TestConnection::checkQueueProcessing() {
  // given
  Connection connection(config);

  // when
  QVERIFY(connection.connect());
  connection.command({"SET", "test_incr_key", "0"});

  for (int i = 0; i < 1000; ++i) {
    connection.command({"INCR", "test_incr_key"});
  }

  // then
  QCOMPARE(connection.waitForIdle(10000), true);
  QCOMPARE(connection.command({"GET", "test_incr_key"}).result().value(),
           QVariant(1000));
}

void TestConnection::connectWithAuth() {
  // given
  Connection connection(config);

  // when
  QVERIFY(connection.connect());
  connection.command({"config", "set", "requirepass", "test"}).result();
  connection.disconnect();

  bool actualConnectResult = connection.connect();
  Response actualCommandResult = connection.command({"ping"}).result();

  // then
  QCOMPARE(actualConnectResult, true);
  QCOMPARE(actualCommandResult.value().toString(), QString("PONG"));
}

void TestConnection::connectWithInvalidAuth() {
  // given
  ConnectionConfig invalidAuth("127.0.0.1", "fake_value");
  Connection connection(invalidAuth);

  // when
  bool connectResult = connection.connect();

  // then
  QCOMPARE(connectResult, false);
  QCOMPARE(connection.isConnected(), false);
}

void TestConnection::connectAndDisconnect() {
  // given
  Connection connection(config);

  // when
  bool connectResult = connection.connect();
  connection.disconnect();

  // then
  QCOMPARE(connectResult, true);
  QCOMPARE(connection.isConnected(), false);
}

#endif

void TestConnection::connectWithInvalidConfig() {
  // given
  ConnectionConfig invalidConfig;
  Connection connection(invalidConfig);
  bool exceptionRaised = false;

  // when
  try {
    connection.connect();
  } catch (Connection::Exception&) {
    exceptionRaised = true;
  }

  // then
  QCOMPARE(exceptionRaised, true);
  QCOMPARE(connection.isConnected(), false);
}

void TestConnection::testWithDummyTransporter() {
  // given
  // connection with dummy transporter
  QSharedPointer<Connection> connection = getRealConnectionWithDummyTransporter(
      QStringList() << QString("+PONG\r\n"));

  // when
  QVERIFY(connection->connect());
  Response actualResult = connection->command({"PING"}).result();

  // then
  QCOMPARE(connection->isConnected(), true);
  QCOMPARE(actualResult.value().toString(), QString("PONG"));
}

void TestConnection::testParseServerInfo() {
  // given
  QString testInfo(
      "# Server\n"
      "redis_version:999.999.999\n"
      "redis_git_sha1:3bf72d0d\n"
      "redis_git_dirty:0\n"
      "redis_build_id:69b45658ca5a9e2d\n"
      "redis_mode:cluster\n"
      "os:Linux 3.13.7-x86_64-linode38 x86_64\n"
      "arch_bits:32\n"
      "multiplexing_api:epoll\n"
      "gcc_version:4.4.1\n"
      "process_id:14029\n"
      "run_id:63bccba63aa231ac84b459af7a6ae34cb89caecd\n"
      "tcp_port:6379\n"
      "uptime_in_seconds:18354826\n"
      "uptime_in_days:212\n"
      "hz:10\n"
      "lru_clock:14100747\n"
      "config_file:/etc/redis/6379.conf\n");

  // when
  ServerInfo actualResult = ServerInfo::fromString(testInfo);

  // then
  QCOMPARE(actualResult.version, 999.999);
  QCOMPARE(actualResult.clusterMode, true);
}

void TestConnection::testConfig() {
  // given
  Connection connection(config);
  ConnectionConfig empty;

  // when
  connection.setConnectionConfig(empty);
  ConnectionConfig actualResult = connection.getConfig();

  // then
  QCOMPARE(actualResult.isNull(), empty.isNull());
}
