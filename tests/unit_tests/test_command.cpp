#include "test_command.h"
#include "qredisclient/command.h"
#include "qredisclient/scancommand.h"

#include <QDebug>
#include <QTest>

void TestCommand::prepareCommand()
{
	//given 
    RedisClient::Command cmd({"EXISTS", "testkey:test"});

	//when
    QByteArray actualResult = cmd.getByteRepresentation();

	//then
    QCOMPARE(actualResult, QByteArray("*2\r\n$6\r\nEXISTS\r\n$12\r\ntestkey:test\r\n"));
}

void TestCommand::parseCommandString()
{
    //given
    QFETCH(QString, data);

    //when
    QString actualResult = RedisClient::Command::splitCommandString(data).join("::");

    //then
    QFETCH(QString, validResult);
    QCOMPARE(actualResult, validResult);
}

void TestCommand::parseCommandString_data()
{
    QTest::addColumn<QString>("data");
    QTest::addColumn<QString>("validResult");
    QTest::newRow("Valid one delimited") << "test \"123\"" << "test::123";
    QTest::newRow("Valid one delimited") << "test '123'" << "test::123";
    QTest::newRow("Valid one raw") << "test 123" << "test::123";
    QTest::newRow("Valid two delimited") << "test \"123\" \"234\"" << "test::123::234";
    QTest::newRow("Valid mixed") << "test \"123\" 234" << "test::123::234";
    QTest::newRow("Valid delimited & escaped") << "test \"10\" \"car\\" "\"s\"" << "test::10::car\"s";
}

void TestCommand::isSelectCommand()
{
    //given
    RedisClient::Command cmd({"SELECT","0"});

    //when
    bool actualResult = cmd.isSelectCommand();

    //then
    QCOMPARE(actualResult, true);
}

void TestCommand::scanCommandSetCursor()
{
    //given
    QFETCH(QList<QByteArray>, rawCommandString);
    QFETCH(int, cursor);
    QFETCH(int, index);
    RedisClient::ScanCommand cmd(rawCommandString);

    //when
    cmd.setCursor(cursor);
    QString actualResult = cmd.getPartAsString(index);

    //then
    QCOMPARE(actualResult, QString::number(cursor));
}

void TestCommand::scanCommandSetCursor_data()
{
    QTest::addColumn<QList<QByteArray>>("rawCommandString");
    QTest::addColumn<int>("cursor");
    QTest::addColumn<int>("index");
    QTest::newRow("Valid scan") << QList<QByteArray>{"scan", "0"} << 1 << 1;
    QTest::newRow("Valid sscan") << QList<QByteArray>{"sscan", "set", "0"} << 1 << 2;
    QTest::newRow("Valid hscan") << QList<QByteArray>{"hscan", "set", "0"} << 1 << 2;
    QTest::newRow("Valid zscan") << QList<QByteArray>{"zscan", "set", "0"} << 1 << 2;
}

void TestCommand::scanCommandIsValid()
{
    //given
    QFETCH(QList<QByteArray>, rawCommandString);
    QFETCH(bool, expected);
    RedisClient::ScanCommand cmd(rawCommandString);

    bool actualResult = cmd.isValidScanCommand();

    QCOMPARE(actualResult, expected);
}

void TestCommand::scanCommandIsValid_data()
{
    QTest::addColumn<QList<QByteArray>>("rawCommandString");
    QTest::addColumn<bool>("expected");

    QTest::newRow("Valid scan") << QList<QByteArray>{"scan", "0"} << true;
    QTest::newRow("Invalid scan") << QList<QByteArray>{"set", "0"} << false;
    QTest::newRow("Valid value scan") << QList<QByteArray>{"sscan", "set", "0"} << true;
    QTest::newRow("Invalid value scan") << QList<QByteArray>{"set", "test", "0"} << false;
}

void TestCommand::pipelineCommand()
{
    RedisClient::Command cmd;
    cmd.addToPipeline({"PING"});
    cmd.addToPipeline({"SET", "foo"});
    cmd.append("bar");  // Append part to previous command

    QCOMPARE(cmd.isEmpty(), false);
    QCOMPARE(cmd.isValid(), true);
    QCOMPARE(cmd.isAuthCommand(), false);
    QCOMPARE(cmd.isSelectCommand(), false);
    QCOMPARE(cmd.isSubscriptionCommand(), false);
    QCOMPARE(cmd.isUnSubscriptionCommand(), false);

    QByteArray actualResult = cmd.getByteRepresentation();
    QCOMPARE(actualResult, QByteArray("*1\r\n$5\r\nMULTI\r\n*1\r\n$4\r\nPING\r\n*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n*1\r\n$4\r\nEXEC\r\n"));
}

void TestCommand::calcKeyHashSlot()
{
    //given
    QFETCH(QList<QByteArray>, rawCommandString);
    QFETCH(quint16, expected);
    RedisClient::Command cmd(rawCommandString);

    quint16 actualResult = cmd.getHashSlot();

    QCOMPARE(actualResult, expected);
}

void TestCommand::calcKeyHashSlot_data()
{
    QTest::addColumn<QList<QByteArray>>("rawCommandString");
    QTest::addColumn<quint16>("expected");

    QTest::newRow("End == end of key")
            << QList<QByteArray>{"type", "site:{752ef10e-81a9-4d7a-9d39-ef58ee6174db}"}
            << (quint16)12605;

    QTest::newRow("End in the middle of the key")
            << QList<QByteArray>{"type", "site:{752ef10e-81a9-4d7a-9d39-ef58ee6174db}:more_data"}
            << (quint16)12605;

    QTest::newRow("Start == start of the key")
            << QList<QByteArray>{"type", "{752ef10e-81a9-4d7a-9d39-ef58ee6174db}:more_data"}
            << (quint16)12605;
}
