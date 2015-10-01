#include "connection.h"
#include <QThread>
#include <QDebug>
#include "command.h"
#include "scancommand.h"
#include "transporters/defaulttransporter.h"
#include "transporters/sshtransporter.h"
#include "scanresponse.h"
#include "utils/sync.h"

RedisClient::Connection::Connection(const ConnectionConfig &c)
    : m_config(c), m_dbNumber(0), m_currentMode(Mode::Normal)
{            
}

RedisClient::Connection::~Connection()
{
    if (isConnected())
        disconnect();
}

bool RedisClient::Connection::connect() // todo: add block/unblock parameter
{
    if (isConnected())
        return true;

    if (m_config.isValid() == false)
        throw Exception("Invalid config detected");

    if (m_transporter.isNull())
        createTransporter();

    // Create & run transporter    
    m_transporterThread = QSharedPointer<QThread>(new QThread);
    m_transporter->moveToThread(m_transporterThread.data());
    QObject::connect(m_transporterThread.data(), &QThread::started,
                     m_transporter.data(), &AbstractTransporter::init);
    QObject::connect(m_transporterThread.data(), &QThread::finished,
                     m_transporter.data(), &AbstractTransporter::disconnectFromHost);
    QObject::connect(m_transporter.data(), &AbstractTransporter::connected,
                     this, &Connection::auth);

    //wait for data
    SignalWaiter waiter(m_config.connectionTimeout());
    waiter.addAbortSignal(m_transporter.data(), &AbstractTransporter::errorOccurred);
    waiter.addAbortSignal(this, &Connection::authError);
    waiter.addSuccessSignal(this, &Connection::authOk);
    m_transporterThread->start();
    bool result = waiter.wait();

    if (!result)
        disconnect();

    return result;
}

bool RedisClient::Connection::isConnected()
{
    return m_transporter && isTransporterRunning();
}

void RedisClient::Connection::disconnect()
{
    if (isTransporterRunning()) {
        m_transporterThread->quit();
        m_transporterThread->wait();        
        m_transporter.clear();
    }
}

void RedisClient::Connection::command(QList<QByteArray> rawCmd, int db)
{
    Command cmd(rawCmd, db);

    try {
        this->runCommand(cmd);
    } catch (RedisClient::Connection::Exception& e) {
        throw Exception("Cannot execute command." + QString(e.what()));
    }
}

void RedisClient::Connection::command(QList<QByteArray> rawCmd, QObject *owner,
                                      RedisClient::Command::Callback callback, int db)
{
    Command cmd(rawCmd, owner, callback, db);

    try {
        this->runCommand(cmd);
    } catch (RedisClient::Connection::Exception& e) {
        throw Exception("Cannot execute command." + QString(e.what()));
    }
}

RedisClient::Response RedisClient::Connection::commandSync(QList<QByteArray> rawCmd, int db)
{
    Command cmd(rawCmd, db);
    return commandSync(cmd);
}

RedisClient::Response RedisClient::Connection::commandSync(QString cmd, int db)
{
    QList<QByteArray> rawCmd{cmd.toUtf8()};
    return commandSync(rawCmd, db);
}

RedisClient::Response RedisClient::Connection::commandSync(QString cmd, QString arg1, int db)
{
    QList<QByteArray> rawCmd{cmd.toUtf8(), arg1.toUtf8()};
    return commandSync(rawCmd, db);
}

RedisClient::Response RedisClient::Connection::commandSync(QString cmd, QString arg1, QString arg2, int db)
{
    QList<QByteArray> rawCmd{cmd.toUtf8(), arg1.toUtf8(), arg2.toUtf8()};
    return commandSync(rawCmd, db);
}

RedisClient::Response RedisClient::Connection::commandSync(QString cmd, QString arg1, QString arg2, QString arg3, int db)
{
    QList<QByteArray> rawCmd{cmd.toUtf8(), arg1.toUtf8(), arg2.toUtf8(), arg3.toUtf8()};
    return commandSync(rawCmd, db);
}

void RedisClient::Connection::runCommand(Command &cmd)
{
    if (!cmd.isValid())
        throw Exception("Command is not valid");

    if (!isConnected())
        throw Exception("Try run command in not connected state");

    if (cmd.hasDbIndex() && m_dbNumber != cmd.getDbIndex())
        commandSync("SELECT", QString::number(cmd.getDbIndex()));

    if (cmd.isSelectCommand()) {
        auto originalCallback = cmd.getCallBack();
        int dbIndex = cmd.getPartAsString(1).toInt();

        cmd.setCallBack(cmd.getOwner(), [originalCallback, dbIndex, this](Response r, QString e) {
            if (r.isOkMessage()) {
                m_dbNumber = dbIndex;
                qDebug() << "DB was selected:" << dbIndex;
            }
            return originalCallback(r, e);
        });
    }

    if (cmd.getOwner())
        QObject::connect(cmd.getOwner(), SIGNAL(destroyed(QObject *)),
                m_transporter.data(), SLOT(cancelCommands(QObject *)));

    // wait for signal from transporter
    SignalWaiter waiter(m_config.executeTimeout());
    waiter.addSuccessSignal(m_transporter.data(), &RedisClient::AbstractTransporter::commandAdded);
    waiter.addAbortSignal(m_transporter.data(), &RedisClient::AbstractTransporter::errorOccurred);

    emit addCommandToWorker(cmd);
    waiter.wait();
}

void RedisClient::Connection::retrieveCollection(QSharedPointer<RedisClient::ScanCommand> cmd,
                                                 Connection::CollectionCallback callback)
{
    if (getServerVersion() < 2.8)
        throw Exception("Scan commands not supported by redis-server.");

    if (!cmd->isValidScanCommand())
        throw Exception("Invalid command");

    processScanCommand(cmd, callback);
}

RedisClient::ConnectionConfig RedisClient::Connection::getConfig() const
{
    return m_config;
}

void RedisClient::Connection::setConnectionConfig(const RedisClient::ConnectionConfig &c)
{
    m_config = c;
}

RedisClient::Connection::Mode RedisClient::Connection::mode() const
{
    return m_currentMode;
}

double RedisClient::Connection::getServerVersion()
{
    return m_serverInfo.version;
}

void RedisClient::Connection::createTransporter()
{
    //todo : implement unix socket transporter
    if (m_config.useSshTunnel()) {
       m_transporter = QSharedPointer<AbstractTransporter>(new SshTransporter(this));
    } else {
       m_transporter = QSharedPointer<AbstractTransporter>(new DefaultTransporter(this));
    }
}

bool RedisClient::Connection::isTransporterRunning()
{
    return m_transporter.isNull() == false
            && m_transporterThread.isNull() == false
            && m_transporterThread->isRunning();
}

RedisClient::Response RedisClient::Connection::internalCommandSync(QList<QByteArray> rawCmd)
{
    Command cmd(rawCmd);
    cmd.markAsHiPriorityCommand();
    return commandSync(cmd);
}

void RedisClient::Connection::processScanCommand(QSharedPointer<ScanCommand> cmd,
                                                 CollectionCallback callback,
                                                 QSharedPointer<QVariantList> result)
{
    if (result.isNull())
        result = QSharedPointer<QVariantList>(new QVariantList());

    cmd->setCallBack(this, [this, cmd, result, callback](RedisClient::Response r, QString error){
        if (r.isErrorMessage()) {
            callback(r.getValue(), r.getValue().toString());
            return;
        }

        if (!ScanResponse::isValidScanResponse(r) && !result->isEmpty()) {
            callback(QVariant(*result), QString());
            return;
        }

        RedisClient::ScanResponse* scanResp = (RedisClient::ScanResponse*)(&r);

        if (!scanResp) {
            callback(QVariant(),
                     "Error occured on cast ScanResponse from Response.");
            return;
        }

        result->append(scanResp->getCollection());

        if (scanResp->getCursor() <= 0) {            
            callback(QVariant(*result), QString());
            return;
        }

        cmd->setCursor(scanResp->getCursor());

        processScanCommand(cmd, callback, result);
    });

    runCommand(*cmd);
}

RedisClient::Response RedisClient::Connection::commandSync(const Command& command)
{
    auto cmd = command;
    Executor syncObject(cmd);

    try {
        this->runCommand(cmd);
    } catch (RedisClient::Connection::Exception& e) {
        throw Exception("Cannot execute command." + QString(e.what()));
    }

    return syncObject.waitForResult(m_config.executeTimeout());
}

void RedisClient::Connection::auth()
{
    emit log("AUTH");

    try {
        if (m_config.useAuth()) {
            internalCommandSync({"AUTH", m_config.auth().toUtf8()});
        }

        Response testResult = internalCommandSync({"PING"});

        if (testResult.toRawString() != "+PONG\r\n") {
            emit error("AUTH ERROR");
            emit authError("Redis server require password or password invalid");
            return;
        }

        Response infoResult = internalCommandSync({"INFO"});
        m_serverInfo = ServerInfo::fromString(infoResult.getValue().toString());

        // TODO(u_glide): add option to disable automatic mode switching
        if (m_serverInfo.clusterMode)
            m_currentMode = Mode::Cluster;

        emit log("AUTH OK");
        emit authOk();
        emit connected();
    } catch (const Exception& e) {
        emit error(QString("Connection error on AUTH: %1").arg(e.what()));
        emit authError("Connection error on AUTH");
    }
}

void RedisClient::Connection::setTransporter(QSharedPointer<RedisClient::AbstractTransporter> transporter)
{
    if (transporter.isNull())
        return;

    m_transporter = transporter;
}

QSharedPointer<RedisClient::AbstractTransporter> RedisClient::Connection::getTransporter() const
{
    return m_transporter;
}

RedisClient::ServerInfo RedisClient::ServerInfo::fromString(const QString &info)
{
    QRegExp versionRegex("redis_version:([0-9]\\.[0-9]+)", Qt::CaseInsensitive, QRegExp::RegExp2);
    QRegExp modeRegex("redis_mode:([a-z]+)", Qt::CaseInsensitive, QRegExp::RegExp2);

    RedisClient::ServerInfo result;
    result.version = (versionRegex.indexIn(info) == -1)?
                0.0 : versionRegex.cap(1).toDouble();
    result.clusterMode = (modeRegex.indexIn(info) != -1
            && modeRegex.cap(1) == "cluster");
    return result;
}
