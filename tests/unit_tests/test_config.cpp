#include "test_config.h"
#include "qredisclient/connectionconfig.h"
#include "qredisclient/connection.h"

#include <QDebug>
#include <QTest>
#include <QJsonObject>

using namespace RedisClient;

void TestConfig::testGetParam()
{
    //given
    QString host = "fake_host";
    QString name = "fake_name";
    QString auth = "fake_auth";
    uint port = 1111;
    ConnectionConfig config(host, auth, port, name);

    //when
    QString actualHost = config.host();
    QString actualName = config.name();
    QString actualAuth = config.auth();
    uint actualPort = config.port();

    //then
    QCOMPARE(actualHost, host);
    QCOMPARE(actualName, name);
    QCOMPARE(actualPort, port);
    QCOMPARE(actualAuth, auth);
}

void TestConfig::testOwner()
{
    //given
    ConnectionConfig config;
    QSharedPointer<Connection> testObj(new Connection(config));

    //when
    config.setOwner(testObj.toWeakRef());


    //then
    QCOMPARE(config.getOwner().toStrongRef(), testObj);
}

void TestConfig::testSerialization()
{
    //given
    QJsonObject test {
        {"host", "fake"},
        {"name", "fake"},
        {"port", 1111},
        {"timeout_connect", 60000},
        {"timeout_execute", 60000},
    };

    //when
    ConnectionConfig config = ConnectionConfig::fromJsonObject(test);
    QJsonObject actualResult = config.toJsonObject();

    //then
    QCOMPARE(config.name(), QString("fake"));
    QCOMPARE(config.host(), QString("fake"));
    QCOMPARE(config.port(), 1111u);
    QCOMPARE(config.executeTimeout(), 60000u);
    QCOMPARE(config.connectionTimeout(), 60000u);
    QCOMPARE(actualResult.contains("auth"), false);
    QCOMPARE(actualResult.contains("namespaceSeparator"), false);
    QCOMPARE(actualResult.size(), test.size());
}
