#include "connection.h"
#include <QThread>
#include <QDebug>
#include <QRegularExpression>
#include <QDir>
#include <QJsonDocument>

#include "command.h"
#include "scancommand.h"
#include "transporters/defaulttransporter.h"
#include "transporters/sshtransporter.h"
#include "scanresponse.h"
#include "utils/sync.h"
#include "utils/compat.h"

inline void initResources() { Q_INIT_RESOURCE(lua); }

const QString END_OF_COLLECTION = "end_of_collection";

RedisClient::Connection::Connection(const ConnectionConfig &c, bool autoConnect)
    : m_config(c),
      m_dbNumber(0),
      m_currentMode(Mode::Normal),
      m_autoConnect(autoConnect),
      m_stoppingTransporter(false)
{            
    initResources();
}

RedisClient::Connection::~Connection()
{
    if (isConnected())
        disconnect();    
}

bool RedisClient::Connection::connect(bool wait)
{
    if (isConnected())
        return true;

    if (m_config.isValid() == false)
        throw Exception("Invalid config detected");

    if (m_transporter.isNull())
        createTransporter();         

    // Create & run transporter
    m_transporterThread = QSharedPointer<QThread>(new QThread);
    m_transporterThread->setObjectName("qredisclient::transporter_thread");
    m_transporter->moveToThread(m_transporterThread.data());

    QObject::connect(m_transporterThread.data(), &QThread::started,
                     m_transporter.data(), &AbstractTransporter::init);
    QObject::connect(m_transporterThread.data(), &QThread::finished,
                     m_transporter.data(), &AbstractTransporter::disconnectFromHost);
    QObject::connect(m_transporter.data(), &AbstractTransporter::connected,
                     this, &Connection::auth);
    QObject::connect(m_transporter.data(), &AbstractTransporter::errorOccurred,
                     this, [this](const QString& err) {
        disconnect();
        emit error(QString("Disconnect on error: %1").arg(err));
    });
    QObject::connect(this, &Connection::authError, this, [this](const QString&) {
        disconnect();
    });


    SignalWaiter waiter(m_config.connectionTimeout());

    if (wait) {
        waiter.addAbortSignal(m_transporter.data(), &AbstractTransporter::errorOccurred);
        waiter.addAbortSignal(this, &Connection::authError);        
        waiter.addSuccessSignal(this, &Connection::authOk);                    
    } else {       
        waiter.addSuccessSignal(m_transporterThread.data(), &QThread::started);                       
    }

    m_transporterThread->start();
    return waiter.wait();
}

bool RedisClient::Connection::isConnected()
{
    return m_stoppingTransporter == false && isTransporterRunning();
}

void RedisClient::Connection::disconnect()
{
    if (isTransporterRunning()) {
        m_stoppingTransporter = true;
        m_transporterThread->quit();
        m_transporterThread->wait();        
        m_transporter.clear();
        m_stoppingTransporter = false;
    }
    m_dbNumber = 0;
}

void RedisClient::Connection::command(const RedisClient::Command &cmd)
{
    try {
        this->runCommand(cmd);
    } catch (RedisClient::Connection::Exception& e) {
        throw Exception("Cannot execute command." + QString(e.what()));
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

RedisClient::Response RedisClient::Connection::commandSync(const Command& command)
{
    auto cmd = command;
    CommandExecutor syncObject(cmd, m_config.executeTimeout());
    syncObject.addAbortSignal(m_transporter.data(), &AbstractTransporter::errorOccurred);

    runCommand(cmd);

    RedisClient::Response resp;

    try {
        resp = syncObject.waitResult();
    } catch (CommandExecutor::Exception& e) {
        throw Exception(e.what());
    }

    return resp;
}

void RedisClient::Connection::runCommand(const Command &cmd)
{
    if (!cmd.isValid())
        throw Exception("Command is not valid");

    if (!isConnected()) {
        if (m_autoConnect) {
            qDebug() << "Connect to Redis before running command (m_autoConnect == true)";
            if (!connect(false) || !m_transporter) {
                throw Exception("Cannot connect to redis-server. Details are available in connection log.");
            }
            qDebug() << "Continue command execution";
        } else {
            throw Exception("Try run command in not connected state");
        }
    }

    if (cmd.getOwner() && cmd.getOwner() != this)
        QObject::connect(cmd.getOwner(), SIGNAL(destroyed(QObject *)),
                m_transporter.data(), SLOT(cancelCommands(QObject *)),
                static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));

    // wait for signal from transporter
    SignalWaiter waiter(m_config.executeTimeout());
    waiter.addSuccessSignal(m_transporter.data(), &RedisClient::AbstractTransporter::commandAdded);
    waiter.addAbortSignal(m_transporter.data(), &RedisClient::AbstractTransporter::errorOccurred);   

    emit addCommandToWorker(cmd);
    waiter.wait();
}

bool RedisClient::Connection::waitForIdle(uint timeout)
{
    SignalWaiter waiter(timeout);
    waiter.addSuccessSignal(m_transporter.data(), &AbstractTransporter::queueIsEmpty);
    return waiter.wait();
}

void RedisClient::Connection::retrieveCollection(const ScanCommand &cmd, Connection::CollectionCallback callback)
{
    if (getServerVersion() < 2.8)
        throw Exception("Scan commands not supported by redis-server.");

    if (!cmd.isValidScanCommand())
        throw Exception("Invalid command");

    processScanCommand(cmd, callback);
}

void RedisClient::Connection::retrieveCollectionIncrementally(const ScanCommand& cmd,
                                                              RedisClient::Connection::IncrementalCollectionCallback callback)
{
    if (getServerVersion() < 2.8)
        throw Exception("Scan commands not supported by redis-server.");

    if (!cmd.isValidScanCommand())
        throw Exception("Invalid command");

    processScanCommand(cmd, [this, callback](QVariant c, QString err) {
        if (err == END_OF_COLLECTION) {
            callback(c, QString(), true);
        } else if (!err.isEmpty()) {
            callback(c, err, true);
        } else {
            callback(c, QString(), false);
        }
    }, QSharedPointer<QVariantList>(), true);
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

RedisClient::DatabaseList RedisClient::Connection::getKeyspaceInfo()
{
    return m_serverInfo.databases;
}

void RedisClient::Connection::refreshServerInfo()
{
    Response infoResult = internalCommandSync({"INFO", "ALL"});
    m_serverInfo = ServerInfo::fromString(infoResult.getValue().toString());
}

void RedisClient::Connection::getClusterKeys(RawKeysListCallback callback, const QString &pattern)
{
    if (mode() != Mode::Cluster) {
        throw Exception("Connection is not in cluster mode");
    }

    QSharedPointer<RawKeysList> result(new RawKeysList());
    QSharedPointer<HostList> masterNodes(new HostList(getMasterNodes()));

    m_wrapper = [this, result, masterNodes, callback, pattern](const RawKeysList &res, const QString& err){
        if (!err.isEmpty()) {
            qDebug() << "Error in cluster keys retrival:" << err;
            return callback(RawKeysList(), err);
        }

        result->append(res);

        if (masterNodes->size() > 0) {
            Host h = masterNodes->first();
            masterNodes->removeFirst();

            SignalWaiter waiter(m_config.connectionTimeout());
            waiter.addSuccessSignal(m_transporter.data(), &RedisClient::AbstractTransporter::connected);
            waiter.addAbortSignal(m_transporter.data(), &RedisClient::AbstractTransporter::errorOccurred);

            if (m_config.overrideClusterHost()) {
                reconnectTo(h.first, h.second);
            } else {
                reconnectTo(m_config.host(), h.second);
            }

            qDebug() << "Wait for reconnect...";

            if (!waiter.wait()) {
                qDebug() << "Reconnection failed";
                return callback(RawKeysList(),
                                QString("Cannot connect to cluster node %1:%2").arg(h.first).arg(h.second));
            }

            qDebug() << "Reconnected!";

            return getDatabaseKeys(m_wrapper, pattern);
        } else {
            return callback(*result, QString());
        }
    };

    getDatabaseKeys(m_wrapper, pattern);
}

void RedisClient::Connection::getDatabaseKeys(RawKeysListCallback callback, const QString &pattern, uint dbIndex)
{    
    QList<QByteArray> rawCmd {
        "scan", "0", "MATCH", pattern.toUtf8(), "COUNT", "10000"
    };
    ScanCommand keyCmd(rawCmd, dbIndex);

    retrieveCollection(keyCmd, [this, callback](QVariant r, QString err)
    {
        if (!err.isEmpty())
            return callback(RawKeysList(), QString("Cannot load keys: %1").arg(err));

        auto keysList = convertQVariantList(r.toList());

        return callback(keysList, QString());
    });
}                                              

void RedisClient::Connection::getNamespaceItems(RedisClient::Connection::NamespaceItemsCallback callback,
                                                const QString &nsSeparator, const QString &filter, uint dbIndex)
{
    QFile script("://scan.lua");
    if (!script.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open LUA resource";
        return;
    }

    QByteArray LUA_SCRIPT = script.readAll();

    QList<QByteArray> rawCmd {
        "eval", LUA_SCRIPT, "0", nsSeparator.toUtf8(), filter.toUtf8()
    };

    Command evalCmd(rawCmd, dbIndex);

    evalCmd.setCallBack(this, [this, callback] (RedisClient::Response r, QString error){

        if (!error.isEmpty()) {
            return callback(NamespaceItems(), error);
        }

        QList<QVariant> result = r.getValue().toList();

        if (result.size() != 2) {
            return callback(NamespaceItems(), "Invalid response from LUA script");
        }

        QJsonDocument rootNamespacesJson = QJsonDocument::fromJson(result[0].toByteArray());
        QJsonDocument rootKeysJson = QJsonDocument::fromJson(result[1].toByteArray());

        if (rootNamespacesJson.isEmpty()
                || rootKeysJson.isEmpty()
                || !(rootNamespacesJson.isObject() && rootKeysJson.isObject())) {
            return callback(NamespaceItems(), "Invalid response from LUA script");
        }

        QVariantMap rootNamespaces = rootNamespacesJson.toVariant().toMap();
        QList<QString> rootKeys = rootKeysJson.toVariant().toMap().keys();

        QVariantMap::const_iterator i = rootNamespaces.constBegin();
        RootNamespaces rootNs;
        rootNs.reserve(rootNamespaces.size());

        while (i != rootNamespaces.constEnd()) {
            rootNs.append(QPair<QByteArray, ulong>(
                              i.key().toUtf8(), (ulong)i.value().toDouble())
                          );
             ++i;
        }

        RootKeys keys;
        keys.reserve(rootKeys.size());

        foreach (QString key, rootKeys) {
            keys.append(key.toUtf8());
        }

        qDebug() << "ns items is loaded via lua script";

        callback(NamespaceItems(rootNs, keys), QString());
    });

    runCommand(evalCmd);
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
    return m_transporter && m_transporterThread && m_transporterThread->isRunning();
}

RedisClient::Response RedisClient::Connection::internalCommandSync(QList<QByteArray> rawCmd)
{
    Command cmd(rawCmd);
    cmd.markAsHiPriorityCommand();
    return commandSync(cmd);
}

void RedisClient::Connection::processScanCommand(const ScanCommand& cmd,
                                                 CollectionCallback callback,
                                                 QSharedPointer<QVariantList> result,
                                                 bool incrementalProcessing)
{
    if (result.isNull())
        result = QSharedPointer<QVariantList>(new QVariantList());

    auto cmdWithCallback = cmd;

    cmdWithCallback.setCallBack(this, [this, cmd, result, callback, incrementalProcessing]
                     (RedisClient::Response r, QString error){

        if (r.isErrorMessage()) {

            /*
             * aliyun cloud provides iscan command for scanning clusters
             */
            if (cmd.getPartAsString(0).toLower() == "scan" && r.isDisabledCommandErrorMessage()) {
                auto rawCmd = cmd.getSplitedRepresentattion();
                rawCmd.replace(0, "iscan");
                auto iscanCmd = ScanCommand(rawCmd);
                return processScanCommand(iscanCmd, callback, result, incrementalProcessing);
            }

            callback(r.getValue(), r.getValue().toString());
            return;
        }

        if (!error.isEmpty()) {            
            callback(QVariant(), error);
            return;
        }

        if (incrementalProcessing)
            result->clear();

        if (!ScanResponse::isValidScanResponse(r)) {            
            if (result->isEmpty())
                callback(QVariant(), incrementalProcessing ? END_OF_COLLECTION : QString());
            else
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
            callback(QVariant(*result), incrementalProcessing ? END_OF_COLLECTION : QString());
            return;
        }

        auto newCmd = cmd;
        newCmd.setCursor(scanResp->getCursor());

        processScanCommand(newCmd, callback, result);
    });

    runCommand(cmdWithCallback);
}

void RedisClient::Connection::changeCurrentDbNumber(int db)
{
    if (m_dbNumberMutex.tryLock(5000)) {
        m_dbNumber = db;
        qDebug() << "DB was selected:" << db;
        m_dbNumberMutex.unlock();
    } else {
        qWarning() << "Cannot lock db number mutex!";
    }
}

RedisClient::Connection::HostList RedisClient::Connection::getMasterNodes()
{
    HostList result;

    if (mode() != Mode::Cluster) {
        return result;
    }

    Response r;

    try {
        r = internalCommandSync({"CLUSTER", "SLOTS"});
    } catch (const Exception& e) {
        emit error(QString("Cannot retrive nodes list").arg(e.what()));
        return result;
    }

    QVariantList slotsList = r.getValue().toList();

    foreach (QVariant clusterSlot, slotsList) {
        QVariantList details = clusterSlot.toList();

        if (details.size() < 3)
            continue;

        QVariantList masterDetails = details[2].toList();

        result.append(
            {masterDetails[0].toString(), masterDetails[1].toInt()}
        );
    }

    return result;
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
            emit authError("Redis server requires password or password is not valid");
            emit error("AUTH ERROR");            
            return;
        }

        refreshServerInfo();

        // TODO(u_glide): add option to disable automatic mode switching
        if (m_serverInfo.clusterMode) {
            m_currentMode = Mode::Cluster;
            emit log("Cluster detected");
        } else if (m_serverInfo.sentinelMode) {
            m_currentMode = Mode::Sentinel;
            emit log("Sentinel detected. Requesting master node...");

            Response mastersResult = internalCommandSync({"SENTINEL", "masters"});

            if (!mastersResult.isArray()) {
                emit error(QString("Connection error: cannot retrive master node from sentinel"));
                return;
            }

            QVariantList result = mastersResult.getValue().toList();

            if (result.size() == 0) {
                emit error(QString("Connection error: invalid response from sentinel"));
                return;
            }

            QStringList masterInfo = result.at(0).toStringList();

            if (masterInfo.size() < 6) {
                emit error(QString("Connection error: invalid response from sentinel"));
                return;
            }

            QString host = masterInfo[3];

            if (!m_config.useSshTunnel() && (host == "127.0.0.1" || host == "localhost"))
                host = m_config.host();

            emit reconnectTo(host, masterInfo[5].toInt());
            return;
        }

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

RedisClient::ServerInfo::ServerInfo()
    : version(0.0),
      clusterMode(false),
      sentinelMode(false)
{

}

RedisClient::ServerInfo RedisClient::ServerInfo::fromString(const QString &info)
{
    QStringList lines = info.split("\r\n");

    ParsedServerInfo parsed;
    QString currentSection {"unknown"};
    int posOfSeparator = -1;

    foreach (QString line, lines) {
        if (line.startsWith("#")) {
            currentSection = line.mid(2).toLower();
            continue;
        }

        posOfSeparator = line.indexOf(':');

        if (posOfSeparator == -1)
            continue;

        parsed[currentSection][line.mid(0, posOfSeparator)] = line.mid(posOfSeparator + 1);
    }

    QRegExp versionRegex("redis_version:([0-9]+\\.[0-9]+)", Qt::CaseInsensitive, QRegExp::RegExp2);
    QRegExp modeRegex("redis_mode:([a-z]+)", Qt::CaseInsensitive, QRegExp::RegExp2);

    RedisClient::ServerInfo result;
    result.parsed = parsed;
    result.version = (versionRegex.indexIn(info) == -1)?
                0.0 : versionRegex.cap(1).toDouble();

    if (modeRegex.indexIn(info) != -1) {
        if (modeRegex.cap(1) == "cluster")
            result.clusterMode = true;
        if (modeRegex.cap(1) == "sentinel")
            result.sentinelMode = true;
    }

    if (result.clusterMode) {
        result.databases.insert(0, 0);
        return result;
    } else if (result.sentinelMode) {
        return result;
    }

    // Parse keyspace info
    QRegularExpression getDbAndKeysCount("^db(\\d+):keys=(\\d+).*");
    getDbAndKeysCount.setPatternOptions(QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator iter = getDbAndKeysCount.globalMatch(info);
    while (iter.hasNext()) {
        QRegularExpressionMatch match = iter.next();
        int dbIndex = match.captured(1).toInt();
        result.databases.insert(dbIndex, match.captured(2).toInt());
    }

    if (result.databases.size() == 0)
        return result;

    int lastKnownDbIndex = result.databases.lastKey();
    for (int dbIndex=0; dbIndex < lastKnownDbIndex; ++dbIndex) {
         if (!result.databases.contains(dbIndex)) {
             result.databases.insert(dbIndex, 0);
         }
    }

    return result;
}

QVariantMap RedisClient::ServerInfo::ParsedServerInfo::toVariantMap()
{
    QVariantMap categories;
    QHashIterator<QString,  QHash<QString, QString>> catIterator(*this);

    while (catIterator.hasNext())
    {
        catIterator.next();
        QHashIterator<QString, QString> propIterator(catIterator.value());
        QVariantMap properties;

        while (propIterator.hasNext())
        {
            propIterator.next();
            properties.insert(propIterator.key(), propIterator.value());
        }

        categories.insert(catIterator.key(), properties);
    }

    return categories;
}
