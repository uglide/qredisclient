#include <chrono>
#include <thread>
#include <QTest>
#include "test_ssh.h"
#include "qredisclient/connection.h"
#include "qredisclient/command.h"


using namespace RedisClient;

void TestSsh::init()
{
    qRegisterMetaType<RedisClient::Command>("Command");
    qRegisterMetaType<RedisClient::Response>("RedisClient::Response");

    config = ConnectionConfig("127.0.0.1", "test", 6379, "test");
    config.setTimeouts(20000, 20000);
}

#ifdef SSH_TESTS
void TestSsh::connectWithSshTunnelPass()
{
    //given
    config.setSshTunnelSettings("127.0.0.1", "rdm", "test", 2200, "");
    Connection connection(config, false);
    QObject::connect(&connection, &Connection::log, this, [](const QString& e) {
        qDebug() << "Connection event:" << e;
    });

    //when
    bool actualResult = connection.connect();

    //then
    QCOMPARE(connection.isConnected(), true);
    QCOMPARE(actualResult, true);
}

void TestSsh::connectAndCheckTimeout()
{
    //given
    QString validResponse("+PONG\r\n");
    config.setSshTunnelSettings("127.0.0.1", "rdm", "test", 2200, "");
    Connection connection(config, false);

    //when
    QVERIFY(connection.connect());
    wait(60 * 1000);
    Response actualCmdResult = connection.commandSync("PING");
    QCOMPARE(actualCmdResult.toRawString(), validResponse);

    //then
    QCOMPARE(connection.isConnected(), true);
}

void TestSsh::connectWithSshTunnelKey()
{
    //given
    QFETCH(QString, password);
    QFETCH(QString, keyPath);
    config.setSshTunnelSettings("127.0.0.1", "rdm", password, 2201,
                                keyPath, QString("%1.pub").arg(keyPath));
    Connection connection(config, false);
    QObject::connect(&connection, &RedisClient::Connection::error, this, [](const QString& err){
        qDebug() << "Connection error:" << err;
    });

    //when
    bool actualResult = connection.connect();

    //then
    QCOMPARE(connection.isConnected(), true);
    QCOMPARE(actualResult, true);
}

void TestSsh::connectWithSshTunnelKey_data()
{
    QTest::addColumn<QString>("password");
    QTest::addColumn<QString>("keyPath");

    QTest::newRow("Private key without password") << "" << "without_pass.key";
    QTest::newRow("Private key with password") << "SSH_KEY_PASS" << "without_pass.key";

}
#endif
