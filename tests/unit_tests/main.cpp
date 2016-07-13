#include <QTest>
#include <QCoreApplication>

//tests
#include <iostream>
#include "test_command.h"
#include "test_response.h"
#include "test_connection.h"
#include "test_ssh.h"
#include "test_config.h"
#include "test_text.h"
#include "qredisclient/redisclient.h"

int main(int argc, char *argv[])
{
    QCoreApplication app( argc, argv );

    initRedisClient();

    QScopedPointer<QObject> testCommand(new TestCommand);
    QScopedPointer<QObject> testSsh(new TestSsh);
    QScopedPointer<QObject> testResponse(new TestResponse);
    QScopedPointer<QObject> testConfig(new TestConfig);
    QScopedPointer<QObject> testText(new TestText);
    QScopedPointer<QObject> testConnection(new TestConnection);

    int allTestsResult = 0
            + QTest::qExec(testCommand.data(), argc, argv)
            + QTest::qExec(testSsh.data(), argc, argv)
            + QTest::qExec(testResponse.data(), argc, argv)
            + QTest::qExec(testConfig.data(), argc, argv)
            + QTest::qExec(testText.data(), argc, argv)
            + QTest::qExec(testConnection.data(), argc, argv)
            ;

    if (allTestsResult == 0)
        qDebug() << "[Tests PASS]";
    else
        qDebug() << "[Tests FAIL]";

    return (allTestsResult != 0 )? 1 : 0;
}

