#include <chrono>
#include <thread>
#include <QTest>
#include "test_connection.h"
#include "qredisclient/connection.h"
#include "qredisclient/command.h"

using namespace RedisClient;

void TestConnection::init()
{
    qRegisterMetaType<RedisClient::Command>("Command");
    qRegisterMetaType<RedisClient::Response>("RedisClient::Response");

    config = ConnectionConfig("127.0.0.1", "test", 6379, "test");
    config.setTimeouts(10000, 10000);
}

#ifdef INTEGRATION_TESTS
void TestConnection::connectToHostAndRunCommand()
{
    //given
    Connection connection(config);

    //when
    //sync execution
    connection.connect();
    Response actualResult = connection.commandSync("PING");

    //then
    QCOMPARE(connection.isConnected(), true);
    QCOMPARE(actualResult.toRawString(), QString("+PONG\r\n"));
}

void TestConnection::testScanCommand()
{
    //given
    Connection connection(config);

    //when
    connection.connect();
    Response result = connection.commandSync("SCAN", "0");
    QVariant value = result.getValue();


    //then
    QCOMPARE(value.isNull(), false);
}

void TestConnection::testRetriveCollection()
{
    //given
    Connection connection(config);
    ScanCommand cmd({"SCAN", "0"}); //valid
    bool callbackCalled = false;
    QVERIFY(cmd.isValidScanCommand());

    //when
    connection.connect();
    connection.commandSync("FLUSHDB");
    connection.commandSync("SET", "test", "1");

    connection.retrieveCollection(cmd, [&callbackCalled](QVariant result, QString) {
        //then - part 1
        QCOMPARE(result.isNull(), false);
        QCOMPARE(result.toList().size(), 1);
        QCOMPARE(result.canConvert(QMetaType::QVariantList), true);
        callbackCalled = true;
    });
    wait(2000);

    //then - part 2
    QCOMPARE(callbackCalled, true);
}

void TestConnection::runEmptyCommand()
{
    //given
    Connection connection(config);
    Command cmd;

    //when
    connection.connect();
    bool hasException = false;
    try {        
        connection.runCommand(cmd);
    } catch (Connection::Exception&) {
        hasException = true;
    }

    //then
    QCOMPARE(hasException, true);
}

void TestConnection::autoConnect()
{
    //given
    Connection connection(config);

    //when    
    bool hasException = false;
    try {
        connection.commandSync("PING"); //valid
    } catch (Connection::Exception&) {
        hasException = true;
    }

    //then
    QCOMPARE(hasException, false);
}

void TestConnection::runCommandAndDelete()
{
    //given
    Connection connection(config);
    Command cmd(QList<QByteArray>{"ping"});
    QObject * owner = new QObject();
    cmd.setCallBack(owner, [](RedisClient::Response, QString){});

    //when
    connection.connect();
    connection.runCommand(cmd);
    connection.runCommand(cmd);
    delete owner;

    //then
    //no errors
}

void TestConnection::subscribeAndUnsubscribe()
{
    //given
    Connection connection(config);
    Connection connectionPublisher(config);
    int messagesRecieved = 0;

    //when
    QVERIFY(connection.connect());
    QVERIFY(connectionPublisher.connect());

    connection.command(QList<QByteArray>{"SUBSCRIBE", "ch1", "ch2", "ch3"}, this,
                       [&messagesRecieved](RedisClient::Response r, QString){
        qDebug() << "recieved msg:" << r.getValue().toList();
        messagesRecieved++;
    });
    connectionPublisher.commandSync("PUBLISH", "ch1", "MSG1");
    connectionPublisher.commandSync("PUBLISH", "ch2", "MSG2");
    connectionPublisher.commandSync("PUBLISH", "ch3", "MSG3");

    wait(5000);

    //then
    QCOMPARE(4, messagesRecieved); // Subscribe resp + 3 messages from publisher
}

void TestConnection::checkTimeout()
{
    //given
    Connection connection(config);

    //when
    QVERIFY(connection.connect());

    int totalRuns = 20;

    for (int c=0; c < totalRuns; c++) {
        qDebug() << "Wait " << (c + 1) << "/" << totalRuns; 
        wait(1000 * 10);
    }

    Response actualCommandResult = connection.commandSync("ping");

    //then
    QCOMPARE(actualCommandResult.toRawString(), QString("+PONG\r\n"));
}

void TestConnection::checkQueueProcessing()
{
    //given
    Connection connection(config);

    //when
    QVERIFY(connection.connect());
    connection.commandSync("SET", "test_incr_key", "0");

    for (int i=0; i < 1000; ++i) {
        connection.command({"INCR", "test_incr_key"});
    }

    //then
    QCOMPARE(connection.waitForIdle(10000), true);
    QCOMPARE(connection.commandSync("GET", "test_incr_key").getValue(), QVariant(1000));
}

void TestConnection::connectWithAuth()
{   
    //given
    Connection connection(config);

    //when
    connection.connect();
    connection.commandSync(QList<QByteArray>{"config", "set", "requirepass", "test"});
    connection.disconnect();

    bool actualConnectResult = connection.connect();    
    Response actualCommandResult = connection.commandSync("ping");

    //then
    QCOMPARE(actualConnectResult, true);
    QCOMPARE(actualCommandResult.toRawString(), QString("+PONG\r\n"));
}

void TestConnection::connectWithInvalidAuth()
{    
    //given
    ConnectionConfig invalidAuth("127.0.0.1", "fake_value");
    Connection connection(invalidAuth);

    //when
    bool connectResult = connection.connect();

    //then
    QCOMPARE(connectResult, false);
    QCOMPARE(connection.isConnected(), false);
}

void TestConnection::connectAndDisconnect()
{
    //given
    Connection connection(config);

    //when
    bool connectResult = connection.connect();
    connection.disconnect();

    //then
    QCOMPARE(connectResult, true);
    QCOMPARE(connection.isConnected(), false);
}


#endif

void TestConnection::connectWithInvalidConfig()
{
    //given
    ConnectionConfig invalidConfig;
    Connection connection(invalidConfig);
    bool exceptionRaised = false;

    //when
    try {
        connection.connect();
    } catch (Connection::Exception&) {
        exceptionRaised = true;
    }

    //then
    QCOMPARE(exceptionRaised, true);
    QCOMPARE(connection.isConnected(), false);
}


void TestConnection::testWithDummyTransporter()
{
    //given            
    // connection with dummy transporter    
    QString validResponse("+PONG\r\n");
    QSharedPointer<Connection> connection = getRealConnectionWithDummyTransporter(QStringList() << validResponse);    

    //when    
    QVERIFY(connection->connect());
    Response actualResult = connection->commandSync("PING");

    //then
    QCOMPARE(connection->isConnected(), true);
    QCOMPARE(actualResult.toRawString(), validResponse);
}

void TestConnection::testParseServerInfo()
{
    //given
    QString testInfo("# Server\n"
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

    //when
    ServerInfo actualResult = ServerInfo::fromString(testInfo);

    //then
    QCOMPARE(actualResult.version, 999.999);
    QCOMPARE(actualResult.clusterMode, true);
}

void TestConnection::testConfig()
{
    //given
    Connection connection(config);
    ConnectionConfig empty;

    //when
    connection.setConnectionConfig(empty);
    ConnectionConfig actualResult = connection.getConfig();

    //then
    QCOMPARE(actualResult.isNull(), empty.isNull());
}
