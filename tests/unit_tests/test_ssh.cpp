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

    config = ConnectionConfig("127.0.0.1", "test", 7000, "test");
    config.setTimeouts(10000, 10000);
}

#ifdef SSH_TESTS
void TestSsh::connectWithSshTunnelPass()
{
    //given
    config.setSshTunnelSettings("127.0.0.1", "rdm", "test", 2200, "");
    Connection connection(config, false);

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
    wait(15 * 60 * 1000);
    Response actualCmdResult = connection.commandSync("PING");
    QCOMPARE(actualCmdResult.toRawString(), validResponse);
    wait(60 * 1000);
    actualCmdResult = connection.commandSync("PING");
    QCOMPARE(actualCmdResult.toRawString(), validResponse);
    wait(60 * 1000);

    //then
    QCOMPARE(connection.isConnected(), true);    
}

void TestSsh::connectWithSshTunnelKey()
{
    //given
    Connection connection(config, false);

    //when
    bool actualResult = connection.connect();

    //then
    QCOMPARE(connection.isConnected(), true);
    QCOMPARE(actualResult, true);
}
#endif
