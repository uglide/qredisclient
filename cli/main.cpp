#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>

#include <iostream>
#include "qredisclient/redisclient.h"

int main(int argc, char *argv[])
{
    QCoreApplication app( argc, argv );
    initRedisClient();

    QCoreApplication::setApplicationName("qredis-cli");
    QCoreApplication::setApplicationVersion("0.0.1");

    QCommandLineParser parser;
    parser.setApplicationDescription("redis-cli powered by qredisclient");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    QStringList positionals = parser.positionalArguments();

    if (positionals.size() == 0) {
        parser.showHelp();
        return 0;
    }

    RedisClient::ConnectionConfig config("127.0.0.1");
    config.setPort(7000);
    RedisClient::Connection connection(config);

    // convert QStringList to QList<QByteArray>
    QList<QByteArray> cmd;
    for (QString part: positionals) {
        cmd.append(part.toUtf8());
    }

    QObject::connect(&connection, &RedisClient::Connection::log, [](QString msg) {
        qDebug() << "Connection:" << msg;
    });

    QObject::connect(&connection, &RedisClient::Connection::error, [](QString msg) {
        qDebug() << "Connection error:" << msg;
    });

    try {
        connection.connect();
        auto result = connection.commandSync(cmd);
        QVariant val = result.getValue();
        std::cout << RedisClient::Response::valueToHumanReadString(val).toStdString();
    } catch (const RedisClient::Connection::Exception& e) {
        std::cerr << "Cannot run command:" << e.what();
    }
}
