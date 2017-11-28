#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QJsonDocument>
#include <QFile>
#include <QString>
#include <QJsonArray>
#include <QElapsedTimer>

#include <iostream>
#include "qredisclient/redisclient.h"

int main(int argc, char *argv[])
{
    QCoreApplication app( argc, argv );
    initRedisClient();

    QCoreApplication::setApplicationName("qredis-runner");
    QCoreApplication::setApplicationVersion("0.0.1");

    QCommandLineParser parser;
    parser.setApplicationDescription("qredis-runner powered by qredisclient");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    QStringList positionals = parser.positionalArguments();

    if (positionals.size() == 0) {
        parser.showHelp();
        return 0;
    }

    if (!QFile::exists(positionals[0])) {
        std::cerr << "Invalid commands file" << positionals[0].toStdString() << std::endl;
        return 1;
    }

    QFile f(positionals[0]);
    f.open(QIODevice::ReadOnly);
    QJsonParseError err;
    QJsonDocument commands = QJsonDocument::fromJson(f.readAll(), &err);

    if (!commands.isArray()) {
        std::cerr << "Cannot parse commands file" << positionals[0].toStdString() << std::endl;
        std::cerr << "JSON error: " << err.errorString().toStdString() << std::endl;
        return 1;
    }

    QJsonArray allCommands = commands.array();

    RedisClient::ConnectionConfig config("127.0.0.1", "test");
    config.setPort(6379);
    RedisClient::Connection connection(config);   

    int processedCommandsCount = 0;
    int errorsCount = 0;

    QObject::connect(&connection, &RedisClient::Connection::error, [&errorsCount](QString msg) {
        qDebug() << "Connection error:" << msg;
        errorsCount++;
    });

    QTimer::singleShot(0, [&connection, &allCommands, &app, &errorsCount, &processedCommandsCount]() {

        QElapsedTimer pTimer;
        pTimer.start();

        try {
            connection.connect();
        } catch (const RedisClient::Connection::Exception& e) {
            std::cerr << "Cannot connect to local redis-server:" << e.what()  << std::endl;
            app.exit(2);
            return;
        }

        qint64 connectedIn = pTimer.elapsed();
        pTimer.restart();

        for (QJsonValue cmd : allCommands) {
            if (!cmd.isArray())
                continue;

            QVariantList cmdParts = cmd.toArray().toVariantList();
            QList<QByteArray> cmdArray;

            for (QVariant v : cmdParts) {
                cmdArray.append(v.toByteArray());
            }

            try {
                connection.command(cmdArray, (QObject*)&app, [&errorsCount, &processedCommandsCount](RedisClient::Response r, QString err){
                    if (!err.isEmpty()) {
                        errorsCount++;
                        qDebug() << "Command error:" << err;
                        return;
                    }

                    processedCommandsCount++;
                });
            } catch (const RedisClient::Connection::Exception& e) {
                std::cerr << "Cannot run command:" << e.what()  << std::endl;
                errorsCount++;
            }
        }

        bool result = connection.waitForIdle(600);
        double processedIn = (long double)pTimer.elapsed() / 1000;

        qDebug() << "Processing result:" << result;

        uint totalCommands = allCommands.size();


        std::cout << "======================================" << std::endl;
        std::cout << "Test finished:"  << std::endl;
        std::cout << "======================================"  << std::endl;
        std::cout << "Total commands:" << totalCommands  << std::endl;
        std::cout << "Processed commands:" << processedCommandsCount  << std::endl;
        std::cout << "Errors:" << errorsCount  << std::endl;
        std::cout << "======================================"  << std::endl;
        std::cout << "Connected in: " << connectedIn << " ms"  << std::endl;
        std::cout << "Processed in: " << processedIn << " sec"  << std::endl;
        std::cout << "Speed: " << totalCommands / processedIn << " cmd/sec"  << std::endl;
        std::cout << "======================================"  << std::endl;
        app.exit();
    });

    return app.exec();
}
