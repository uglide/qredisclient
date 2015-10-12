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

    int allTestsResult = 0
            + QTest::qExec(new TestCommand, argc, argv)
            + QTest::qExec(new TestSsh, argc, argv)
            + QTest::qExec(new TestResponse, argc, argv)
            + QTest::qExec(new TestConnection, argc, argv)
            + QTest::qExec(new TestConfig, argc, argv)
            + QTest::qExec(new TestText, argc, argv)
            ;

    if (allTestsResult == 0)
        qDebug() << "[Tests PASS]";
    else
        qDebug() << "[Tests FAIL]";

    return (allTestsResult != 0 )? 1 : 0;
}

