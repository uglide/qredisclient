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
    : m_config(c), m_connected(false), m_dbNumber(0), m_currentMode(Mode::Normal)
{            
    m_timeoutTimer.setSingleShot(true);
    QObject::connect(&m_timeoutTimer, SIGNAL(timeout()), &m_connectionLoop, SLOT(quit()));
}

RedisClient::Connection::~Connection()
{
    if (isTransporterRunning())
        disconnect();
}

bool RedisClient::Connection::connect() // todo: add block/unblock parameter
{
    if (isConnected())
        return true;    

    if (isTransporterRunning())
        return false;

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
    QObject::connect(m_transporter.data(), &AbstractTransporter::commandAdded,
                     this, &Connection::commandAddedToTransporter);

    //wait for data
    QEventLoop loop;
    QTimer timeoutTimer;

    //configure sync objects
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, SIGNAL(timeout()), &loop, SLOT(quit()));
    QObject::connect(m_transporter.data(), SIGNAL(errorOccurred(const QString&)), &loop, SLOT(quit()));
    QObject::connect(this, SIGNAL(authError(const QString&)), &loop, SLOT(quit()));
    QObject::connect(this, SIGNAL(authOk()), &loop, SLOT(quit()));

    m_transporterThread->start();    
    timeoutTimer.start(m_config.connectionTimeout());
    loop.exec();

    if (!m_connected)
        disconnect();

    return m_connected;
}

bool RedisClient::Connection::isConnected()
{
    return m_connected;
}

void RedisClient::Connection::disconnect()
{
    if (isTransporterRunning()) {
        m_transporterThread->quit();
        m_transporterThread->wait();        
        m_transporter.clear();
    }

    m_connected = false;
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

void RedisClient::Connection::runCommand(const Command &cmd)
{
    if (!cmd.isValid())
        throw Exception("Command is not valid");

    if (!isTransporterRunning() || !m_connected)
        throw Exception("Try run command in not connected state");

    if (cmd.hasDbIndex() && m_dbNumber != cmd.getDbIndex()) {
        commandSync("SELECT", QString::number(cmd.getDbIndex()));
        m_dbNumber = cmd.getDbIndex();
    }

    if (cmd.getOwner())
        QObject::connect(cmd.getOwner(), SIGNAL(destroyed(QObject *)),
                m_transporter.data(), SLOT(cancelCommands(QObject *)));

    emit addCommandToWorker(cmd);
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

bool RedisClient::Connection::waitConnectedState(unsigned int timeoutInMs)
{
    if (isConnected())
        return true;

    if (!isTransporterRunning())
        return false;

    m_timeoutTimer.start(timeoutInMs);
    m_connectionLoop.exec();

    return isConnected();
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

void RedisClient::Connection::setConnectedState()
{
    m_connected = true;

    if (m_connectionLoop.isRunning())
        m_connectionLoop.exit();

    emit connected();
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
    if (!this->waitConnectedState(m_config.executeTimeout()))
        throw Exception("Cannot execute command. Connection not established.");

    auto cmd = command;
    Executor syncObject(cmd);

    try {
        this->runCommand(cmd);
    } catch (RedisClient::Connection::Exception& e) {
        throw Exception("Cannot execute command." + QString(e.what()));
    }

    return syncObject.waitForResult(m_config.executeTimeout());
}

void RedisClient::Connection::connectionReady()
{
    // todo: create signal in operations::auth() method and connect to this signal
    m_connected = true;
    // todo: do another ready staff
}

void RedisClient::Connection::commandAddedToTransporter()
{   
}

void RedisClient::Connection::auth()
{
    emit log("AUTH");
    m_connected = true;    

    try {
        if (m_config.useAuth()) {
            internalCommandSync({"AUTH", m_config.auth().toUtf8()});
        }

        Response testResult = internalCommandSync({"PING"});

        if (testResult.toRawString() == "+PONG\r\n") {
            Response infoResult = internalCommandSync({"INFO"});
            m_serverInfo = ServerInfo::fromString(infoResult.getValue().toString());

            setConnectedState();
            emit log("AUTH OK");
            emit authOk();
        } else {
            emit error("AUTH ERROR");
            emit authError("Redis server require password or password invalid");
            m_connected = false;
        }
    } catch (const Exception&) {
        emit error("Connection error on AUTH");
        emit authError("Connection error on AUTH");
        m_connected = false;
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

    int pos = versionRegex.indexIn(info);

    RedisClient::ServerInfo result;
    if (pos == -1) {
        result.version = 0.0;
    } else {
        result.version = versionRegex.cap(1).toDouble();
    }

    return result;
}
