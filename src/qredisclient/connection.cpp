#include "connection.h"

#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QThread>

#include "command.h"
#include "scancommand.h"
#include "transporters/defaulttransporter.h"
#include "utils/compat.h"
#include "utils/sync.h"

#ifdef SSH_SUPPORT
#include "sshtransporter.h"
#endif

inline void initResources() { Q_INIT_RESOURCE(lua); }

const QString END_OF_COLLECTION = "end_of_collection";

RedisClient::Connection::Connection(const ConnectionConfig &c, bool autoConnect)
    : m_config(c),
      m_dbNumber(0),
      m_currentMode(Mode::Normal),
      m_autoConnect(autoConnect),
      m_stoppingTransporter(false) {
  initResources();
}

RedisClient::Connection::~Connection() {
  if (isConnected()) disconnect();
}

bool RedisClient::Connection::connect(bool wait) {
  if (isConnected()) return true;

  if (m_config.isValid() == false) throw Exception("Invalid config detected");

  if (m_transporter.isNull()) createTransporter();

  // Create & run transporter
  m_transporterThread = QSharedPointer<QThread>(new QThread);
  m_transporterThread->setObjectName("qredisclient::transporter_thread");
  m_transporter->moveToThread(m_transporterThread.data());

  QObject::connect(m_transporterThread.data(), &QThread::started,
                   m_transporter.data(), &AbstractTransporter::init);
  QObject::connect(m_transporterThread.data(), &QThread::finished,
                   m_transporter.data(),
                   &AbstractTransporter::disconnectFromHost);
  QObject::connect(this, &Connection::shutdownStart, m_transporter.data(),
                   &AbstractTransporter::disconnectFromHost);
  QObject::connect(m_transporter.data(), &AbstractTransporter::connected, this,
                   &Connection::auth);
  QObject::connect(m_transporter.data(), &AbstractTransporter::errorOccurred,
                   this, [this](const QString &err) {
                     disconnect();
                     emit error(QString("Disconnect on error: %1").arg(err));
                   });
  QObject::connect(this, &Connection::authError, this,
                   [this](const QString &) { disconnect(); });

  if (wait) {
    SignalWaiter waiter(m_config.connectionTimeout());
    waiter.addAbortSignal(this, &Connection::shutdownStart);
    waiter.addAbortSignal(m_transporter.data(),
                          &AbstractTransporter::errorOccurred);
    waiter.addAbortSignal(this, &Connection::authError);
    waiter.addSuccessSignal(this, &Connection::authOk);
    m_transporterThread->start();
    return waiter.wait();
  } else {
    m_transporterThread->start();
    return true;
  }
}

bool RedisClient::Connection::isConnected() {
  return m_stoppingTransporter == false && isTransporterRunning();
}

void RedisClient::Connection::disconnect() {
  emit shutdownStart();
  if (isTransporterRunning()) {
    m_stoppingTransporter = true;

    if (m_blockingOp.tryLock(10000)) {
      m_blockingOp.unlock();
    } else {
      qWarning() << "Blocking operation is still in progress";
    }

    m_transporterThread->quit();
    m_transporterThread->wait();
    m_transporter.clear();
    m_transporterThread.clear();
    m_stoppingTransporter = false;
  }
  m_dbNumber = 0;
}

void RedisClient::Connection::disableAutoConnect() { m_autoConnect = false; }

QFuture<RedisClient::Response> RedisClient::Connection::command(
    const RedisClient::Command &cmd) {
  try {
    return this->runCommand(cmd);
  } catch (RedisClient::Connection::Exception &e) {
    throw Exception("Cannot execute command." + QString(e.what()));
  }
}

QFuture<RedisClient::Response> RedisClient::Connection::command(
    QList<QByteArray> rawCmd, int db) {
  Command cmd(rawCmd, db);

  try {
    return this->runCommand(cmd);
  } catch (RedisClient::Connection::Exception &e) {
    throw Exception("Cannot execute command." + QString(e.what()));
  }
}

QFuture<RedisClient::Response> RedisClient::Connection::command(
    QList<QByteArray> rawCmd, QObject *owner,
    RedisClient::Command::Callback callback, int db, bool priorityCmd) {
  Command cmd(rawCmd, owner, callback, db);

  if (priorityCmd) {
      cmd.markAsHiPriorityCommand();
  }

  try {
    return this->runCommand(cmd);
  } catch (RedisClient::Connection::Exception &e) {
    throw Exception("Cannot execute command." + QString(e.what()));
  }
}

void RedisClient::Connection::pipelinedCmd(const QList<QList<QByteArray>> &rawCmds, QObject *owner, int db,
    std::function<void(const RedisClient::Response &, QString)> callback,
    bool transaction) {
  QMutexLocker l(&m_blockingOp);
  QList<Command> pendingCommands;

  if (mode() == Mode::Cluster) {
    for (const QList<QByteArray> &rawCmd : rawCmds) {
      if (m_stoppingTransporter) return;

      Command cmd(rawCmd);
      cmd.setCallBack(owner, callback);      
      pendingCommands.append(cmd);
    }
    runCommands(pendingCommands);
  } else {
    RedisClient::Command cmd({}, db);
    cmd.setCallBack(owner, callback);
    cmd.setPipelineCommand(true, transaction);

    int limit = pipelineCommandsLimit();

    for (const QList<QByteArray> &rawCmd : rawCmds) {
      if (m_stoppingTransporter) return;

      if (cmd.length() >= limit) {
          pendingCommands.append(cmd);

          cmd = RedisClient::Command({}, db);
          cmd.setCallBack(owner, callback);
          cmd.setPipelineCommand(true, transaction);
      }

      cmd.addToPipeline(rawCmd);
    }
    runCommands(pendingCommands);
    runCommand(cmd);
  }
}

int RedisClient::Connection::pipelineCommandsLimit() const
{
    if (m_transporter) {
      return m_transporter->pipelineCommandsLimit();
    }

    if (mode() == Mode::Cluster) {
        return 1;
    }

    return 100;
}

QFuture<RedisClient::Response> RedisClient::Connection::runCommand(
    const Command &cmd) {
  if (!cmd.isValid()) throw Exception("Command is not valid");

  if (!isConnected()) {
    if (m_autoConnect) {
      auto d = QSharedPointer<AsyncFuture::Deferred<RedisClient::Response>>(
          new AsyncFuture::Deferred<RedisClient::Response>());

      callAfterConnect([this, cmd, d](const QString &err) {
        if (err.isEmpty()) {
          d->complete(runCommand(cmd));
        } else {
          d->cancel();
        }
      });

      connect(false);

      return d->future();
    } else {
      throw Exception("Cannot run command in not connected state");
    }
  }

  if (cmd.getOwner() && cmd.getOwner() != this)
    QObject::connect(cmd.getOwner(), SIGNAL(destroyed(QObject *)),
                     m_transporter.data(), SLOT(cancelCommands(QObject *)),
                     static_cast<Qt::ConnectionType>(Qt::QueuedConnection |
                                                     Qt::UniqueConnection));

  auto deferred = cmd.getDeferred();

  emit addCommandsToWorker({cmd});

  return deferred.future();
}

void RedisClient::Connection::runCommands(const QList<Command> &commands) {
  if (!isConnected()) {
    if (m_autoConnect) {
      callAfterConnect([this, commands](const QString &err) {
        if (err.isEmpty()) {
          runCommands(commands);
        }
      });
      connect(false);
      return;
    } else {
      throw Exception("Cannot run command in not connected state");
    }
  }

  for (auto cmd : commands) {
    if (cmd.getOwner() && cmd.getOwner() != this)
      QObject::connect(cmd.getOwner(), SIGNAL(destroyed(QObject *)),
                       m_transporter.data(), SLOT(cancelCommands(QObject *)),
                       static_cast<Qt::ConnectionType>(Qt::QueuedConnection |
                                                       Qt::UniqueConnection));
  }
  emit addCommandsToWorker(commands);
}

bool RedisClient::Connection::waitForIdle(uint timeout) {
  SignalWaiter waiter(timeout);
  waiter.addSuccessSignal(m_transporter.data(),
                          &AbstractTransporter::queueIsEmpty);
  return waiter.wait();
}

QSharedPointer<RedisClient::Connection> RedisClient::Connection::clone(
    bool copyServerInfo) const {

  auto config = getConfig();
  config.setId(config.id());

  auto newConnection = QSharedPointer<RedisClient::Connection>(
      new RedisClient::Connection(config), &QObject::deleteLater);

  if (copyServerInfo) newConnection->m_serverInfo = m_serverInfo;

  newConnection->m_currentMode = m_currentMode;
  newConnection->m_clusterSlots = m_clusterSlots;

  return newConnection;
}

void RedisClient::Connection::retrieveCollection(
    const ScanCommand &cmd, Connection::CollectionCallback callback) {
  if (!cmd.isValidScanCommand()) throw Exception("Invalid command");

  processScanCommand(cmd, callback);
}

void RedisClient::Connection::retrieveCollectionIncrementally(
    const ScanCommand &cmd,
    RedisClient::Connection::IncrementalCollectionCallback callback) {
  if (!cmd.isValidScanCommand()) throw Exception("Invalid command");

  processScanCommand(
      cmd,
      [callback](QVariant c, QString err) {
        if (err == END_OF_COLLECTION) {
          callback(c, QString(), true);
        } else if (!err.isEmpty()) {
          callback(c, err, true);
        } else {
          callback(c, QString(), false);
        }
      },
      QSharedPointer<QVariantList>(), true);
}

RedisClient::ConnectionConfig RedisClient::Connection::getConfig() const {
  return m_config;
}

void RedisClient::Connection::setConnectionConfig(
    const RedisClient::ConnectionConfig &c) {
  m_config = c;
}

RedisClient::Connection::Mode RedisClient::Connection::mode() const {
  return m_currentMode;
}

int RedisClient::Connection::dbIndex() const { return m_dbNumber; }

double RedisClient::Connection::getServerVersion() {
  return m_serverInfo.version;
}

RedisClient::DatabaseList RedisClient::Connection::getKeyspaceInfo() {
    return m_serverInfo.databases;
}

QHash<QString, QString> RedisClient::Connection::getEnabledModules()
{
    if (!m_serverInfo.parsed.contains("modules")) {
        return QHash<QString, QString>();
    }

    return m_serverInfo.parsed["modules"];
}

void RedisClient::Connection::refreshServerInfo(std::function<void()> callback) {
  QString errMsg("Cannot refresh server info: %1");

  cmd(
      {"INFO"}, this, -1,
      [this, errMsg, callback](const Response &infoResult) {
        if (infoResult.isPermissionError()) {
          cmd(
              {"CLUSTER INFO"}, this, -1,
              [this, callback](const Response &infoResult) {
                bool isCluster = !infoResult.isErrorMessage();

                m_serverInfo = ServerInfo(6.0, isCluster);
                callback();
              },
              [this, errMsg](const QString &err) {
                emit error(errMsg.arg(err));
              },
              true, true);

        } else {
            m_serverInfo = ServerInfo::fromString(infoResult.value().toString());
            callback();
        }
      },
      [this, errMsg](const QString &err) { emit error(errMsg.arg(err)); },
      true, true);
}

void RedisClient::Connection::getClusterKeys(RawKeysListCallback callback,
                                             const QString &pattern, long scanLimit) {
  if (mode() != Mode::Cluster) {
    throw Exception("Connection is not in cluster mode");
  }

  QSharedPointer<RawKeysList> result(new RawKeysList());

  auto onConnect = [this, callback, pattern, result, scanLimit](const QString &err) {
    if (!err.isEmpty()) {
      return callback(*result,
                      QObject::tr("Cannot connect to cluster node %1:%2")
                          .arg(m_config.host())
                          .arg(m_config.port()));
    }

    getDatabaseKeys(m_collectClusterNodeKeys, pattern, -1, scanLimit);
  };

  m_collectClusterNodeKeys = [this, result, callback, onConnect](
                                 const RawKeysList &res, const QString &err) {
    if (!err.isEmpty()) {
      return callback(RawKeysList(), err);
    }

    result->append(res);

    if (!hasNotVisitedClusterNodes()) return callback(*result, QString());

    clusterConnectToNextMasterNode(onConnect);
  };

  getMasterNodes([this, callback, onConnect](const HostList& hosts, const QString& err) {
     if (err.size() > 0)
         return callback(RawKeysList(), err);

     m_notVisitedMasterNodes =
         QSharedPointer<HostList>(new HostList(hosts));

     clusterConnectToNextMasterNode(onConnect);
  });
}

void RedisClient::Connection::flushDbKeys(
    int dbIndex, std::function<void(const QString &)> callback) {
  if (mode() == Mode::Cluster) {
    auto onConnect = [this, callback](const QString &err) {
      if (!err.isEmpty()) {
        return callback(QObject::tr("Cannot connect to cluster node %1:%2")
                            .arg(m_config.host())
                            .arg(m_config.port()));
      }

      command({"FLUSHDB"}, this, m_cmdCallback);
    };

    m_cmdCallback = [this, callback, dbIndex, onConnect](
                        const RedisClient::Response &, const QString &error) {
      if (!error.isEmpty()) {
        callback(QString(QObject::tr("Cannot flush db (%1): %2"))
                     .arg(dbIndex)
                     .arg(error));
        return;
      }

      if (!hasNotVisitedClusterNodes()) return callback(QString());

      clusterConnectToNextMasterNode(onConnect);
    };    

    getMasterNodes([this, callback, onConnect](const HostList& hosts, const QString& err) {
       if (err.size() > 0)
           return callback(err);

       m_notVisitedMasterNodes =
           QSharedPointer<HostList>(new HostList(hosts));

       clusterConnectToNextMasterNode(onConnect);
    });

  } else {
    command(
        {"FLUSHDB"}, this,
        [dbIndex, callback](const RedisClient::Response &,
                            const QString &error) {
          if (!error.isEmpty()) {
            callback(QString(QObject::tr("Cannot flush db (%1): %2"))
                         .arg(dbIndex)
                         .arg(error));
          } else {
            callback(QString());
          }
        },
        dbIndex);
  }
}

void RedisClient::Connection::getDatabaseKeys(RawKeysListCallback callback,
                                              const QString &pattern,
                                              int dbIndex, long scanLimit) {
  QList<QByteArray> rawCmd{"scan",  "0",
                           "MATCH", pattern.toUtf8(),
                           "COUNT", QString::number(scanLimit).toLatin1()};
  ScanCommand keyCmd(rawCmd, dbIndex);

  retrieveCollection(keyCmd, [callback](QVariant r, QString err) {
    if (!err.isEmpty())
      return callback(RawKeysList(), QString("Cannot load keys: %1").arg(err));

    auto keysList = convertQVariantList(r.toList());

    return callback(keysList, QString());
  });
}

void RedisClient::Connection::getNamespaceItems(
    RedisClient::Connection::NamespaceItemsCallback callback,
    const QString &nsSeparator, const QString &filter, int dbIndex) {
  QFile script("://scan.lua");
  if (!script.open(QIODevice::ReadOnly)) {
    qWarning() << "Cannot open LUA resource";
    return;
  }

  QByteArray LUA_SCRIPT = script.readAll();

  QList<QByteArray> rawCmd{"eval", LUA_SCRIPT, "0", nsSeparator.toUtf8(),
                           filter.toUtf8()};

  Command evalCmd(rawCmd, dbIndex);

  evalCmd.setCallBack(this, [callback](RedisClient::Response r, QString error) {
    if (!error.isEmpty()) {
      return callback(NamespaceItems(), error);
    }

    QList<QVariant> result = r.value().toList();

    if (result.size() != 2) {
      return callback(NamespaceItems(), "Invalid response from LUA script");
    }

    QJsonDocument rootNamespacesJson =
        QJsonDocument::fromJson(result[0].toByteArray());
    QJsonDocument rootKeysJson =
        QJsonDocument::fromJson(result[1].toByteArray());

    if (rootNamespacesJson.isEmpty() || rootKeysJson.isEmpty() ||
        !(rootNamespacesJson.isObject() && rootKeysJson.isObject())) {
      return callback(NamespaceItems(), "Invalid response from LUA script");
    }

    QVariantMap rootNamespaces = rootNamespacesJson.toVariant().toMap();
    QList<QString> rootKeys = rootKeysJson.toVariant().toMap().keys();

    QVariantMap::const_iterator i = rootNamespaces.constBegin();
    RootNamespaces rootNs;
    rootNs.reserve(rootNamespaces.size());

    while (i != rootNamespaces.constEnd()) {
      rootNs.append(QPair<QByteArray, ulong>(i.key().toUtf8(),
                                             (ulong)i.value().toDouble()));
      ++i;
    }

    RootKeys keys;
    keys.reserve(rootKeys.size());

    foreach (QString key, rootKeys) { keys.append(key.toUtf8()); }

    callback(NamespaceItems(rootNs, keys), QString());
  });

  runCommand(evalCmd);
}

void RedisClient::Connection::createTransporter() {
  // todo : implement unix socket transporter
  if (m_config.useSshTunnel()) {
#ifdef SSH_SUPPORT
    m_transporter =
        QSharedPointer<AbstractTransporter>(new SshTransporter(this));
#else
    throw SSHSupportException("QRedisClient compiled without ssh support.");
#endif
  } else {
    m_transporter =
        QSharedPointer<AbstractTransporter>(new DefaultTransporter(this));
  }
}

bool RedisClient::Connection::isTransporterRunning() {
  return m_transporter && m_transporterThread &&
         m_transporterThread->isRunning();
}

void RedisClient::Connection::processScanCommand(
    const ScanCommand &cmd, CollectionCallback callback,
    QSharedPointer<QVariantList> result, bool incrementalProcessing) {
  if (result.isNull())
    result = QSharedPointer<QVariantList>(new QVariantList());

  auto cmdWithCallback = cmd;

  cmdWithCallback.setCallBack(
      this, [this, cmd, result, callback, incrementalProcessing](
                RedisClient::Response r, QString error) {
        if (r.isErrorMessage()) {
          /*
           * aliyun cloud provides iscan command for scanning clusters
           */
          if (cmd.getPartAsString(0).toLower() == "scan" &&
              r.isDisabledCommandErrorMessage()) {
            auto rawCmd = cmd.getSplitedRepresentattion();
            rawCmd.replace(0, "iscan");
            auto iscanCmd = ScanCommand(rawCmd);
            return processScanCommand(iscanCmd, callback, result,
                                      incrementalProcessing);
          }

          callback(r.value(), r.value().toString());
          return;
        }

        if (!error.isEmpty()) {
          callback(QVariant(), error);
          return;
        }

        if (incrementalProcessing) result->clear();

        if (!r.isValidScanResponse()) {
          if (result->isEmpty())
            callback(QVariant(),
                     incrementalProcessing ? END_OF_COLLECTION : QString());
          else
            callback(QVariant(*result), QString());

          return;
        }

        result->append(r.getCollection());

        if (r.getCursor() <= 0) {
          callback(QVariant(*result),
                   incrementalProcessing ? END_OF_COLLECTION : QString());
          return;
        }

        auto newCmd = cmd;
        newCmd.setCursor(r.getCursor());

        processScanCommand(newCmd, callback, result);
      });

  runCommand(cmdWithCallback);
}

void RedisClient::Connection::changeCurrentDbNumber(int db) {
  if (m_dbNumberMutex.tryLock(5000)) {
    m_dbNumber = db;
    m_dbNumberMutex.unlock();
  } else {
    qWarning() << "Cannot lock db number mutex!";
  }
}

void RedisClient::Connection::clusterConnectToNextMasterNode(
    std::function<void(const QString &err)> callback) {
  if (!hasNotVisitedClusterNodes()) {
    return;
  }

  Host h = m_notVisitedMasterNodes->first();
  m_notVisitedMasterNodes->removeFirst();

  callAfterConnect(callback);

  if (m_config.overrideClusterHost()) {
    reconnectTo(h.first, h.second);
  } else {
    reconnectTo(m_config.host(), h.second);
  }
}

bool RedisClient::Connection::hasNotVisitedClusterNodes() const {
  return m_notVisitedMasterNodes && m_notVisitedMasterNodes->size() > 0;
}

void RedisClient::Connection::callAfterConnect(
    std::function<void(const QString &err)> callback) {
  auto context = new QObject();

  QObject::connect(this, &Connection::authOk, context, [callback, context]() {
    callback(QString());
    context->deleteLater();
  });
  QObject::connect(this, &Connection::error, context,
                   [callback, context](const QString &err) {
                     callback(err);
                     context->deleteLater();
                   });
}

void RedisClient::Connection::sentinelConnectToMaster() {
  cmd(
      {"SENTINEL", "masters"}, this, -1,
      [this](const Response &mastersResult) {
        if (!mastersResult.isArray()) {
          emit error(QString(
              "Connection error: cannot retrive master node from sentinel"));
          return;
        }

        QVariantList result = mastersResult.value().toList();

        if (result.size() == 0) {
          emit error(
              QString("Connection error: invalid response from sentinel"));
          return;
        }

        QStringList masterInfo = result.at(0).toStringList();

        if (masterInfo.size() < 6) {
          emit error(
              QString("Connection error: invalid response from sentinel"));
          return;
        }

        QString host = masterInfo[3];

        if (!m_config.useSshTunnel() &&
            (host == "127.0.0.1" || host == "localhost"))
          host = m_config.host();

        emit reconnectTo(host, masterInfo[5].toInt());
      },
      [this](const QString &err) {
        emit error(QString("Connection error: cannot retrive master node from "
                           "sentinel: %1")
                       .arg(err));
      }, true);
}

void RedisClient::Connection::rawClusterSlots(
    std::function<void(QVariantList, const QString &)> callback) {
  if (mode() != Mode::Cluster) {
    return callback(QVariantList(), QString("Invalid connection mode"));
  }

  cmd(
      {"CLUSTER", "SLOTS"}, this, -1,
      [callback](const Response &r) {
        callback(r.value().toList(), QString());
      },
      [callback](const QString &err) {
        return callback(QVariantList(),
                        QString("Cannot retrive nodes list: %1").arg(err));
      }, true);
}

void RedisClient::Connection::getMasterNodes(
    std::function<void(RedisClient::Connection::HostList, const QString &)>
        callback) {
  rawClusterSlots([callback](QVariantList slotsList, const QString &err) {
    if (err.size() > 0 || slotsList.size() == 0) {
      return callback(HostList(), err);
    }

    QSet<Host> masterNodes;

    foreach (QVariant clusterSlot, slotsList) {
      QVariantList details = clusterSlot.toList();

      if (details.size() < 3) continue;

      QVariantList masterDetails = details[2].toList();

      masterNodes.insert(
          {masterDetails[0].toString(), masterDetails[1].toInt()});
    }

    callback(masterNodes.values(), err);
  });
}

void RedisClient::Connection::getClusterSlots(
    std::function<void(RedisClient::Connection::ClusterSlots, const QString &)>
        callback) {
  rawClusterSlots([callback](QVariantList slotsList, const QString &err) {
    if (err.size() > 0 || slotsList.size() == 0) {
      return callback(ClusterSlots(), err);
    }

    ClusterSlots hashSlots;

    foreach (QVariant clusterSlot, slotsList) {
      QVariantList details = clusterSlot.toList();

      if (details.size() < 3) continue;

      QVariantList masterDetails = details[2].toList();

      Range r{details[0].toInt(), details[1].toInt()};

      hashSlots.insert(r,
                       {masterDetails[0].toString(), masterDetails[1].toInt()});
    }

    callback(hashSlots, err);
  });
}

RedisClient::Connection::Host RedisClient::Connection::getClusterHost(
    const Command &cmd) {
  if (m_clusterSlots.size() == 0) {
    qWarning() << "cluster slots should be loaded first";
    return Host(m_config.host(), m_config.port());
  }

  quint16 slot = cmd.getHashSlot();

  for (auto slotRange : m_clusterSlots.keys()) {
    if (slotRange.first <= slot && slot <= slotRange.second) {
      return m_clusterSlots[slotRange];
    }
  }

  qWarning() << "cannot find cluster node for slot:" << slot;

  return Host(m_config.host(), m_config.port());
}

QFuture<bool> RedisClient::Connection::isCommandSupported(
    QList<QByteArray> rawCmd) {
  auto d = QSharedPointer<AsyncFuture::Deferred<bool>>(
      new AsyncFuture::Deferred<bool>());

  cmd(
      rawCmd, this, -1,
      [d](RedisClient::Response r) {
        d->complete(!r.isDisabledCommandErrorMessage());
      },
      [d](const QString &err) {
        d->complete(!err.contains("unknown command"));
      });

  return d->future();
}

void RedisClient::Connection::auth() {
  auto handleConnectionError = [this](const QString &err) {
    emit error(QString("Connection error on AUTH: %1").arg(err));
    emit authError("Connection error on AUTH");
  };

  auto testConnection = [this, handleConnectionError]() {
    cmd(
        {"PING"}, this, -1,
        [this](const Response &resp) {
          if (resp.value().toByteArray() != QByteArray("PONG")) {
            emit authError(
                "Redis server requires password or password is not valid");
            emit error(QString("AUTH ERROR. Redis server requires password or "
                               "password is not valid: %1")
                           .arg(resp.value().toString()));
            return;
          }

          bool connectionWithPopulatedServerInfo =
              (m_serverInfo.parsed.size() > 0 &&
               (m_currentMode == Mode::Cluster ||
                m_currentMode == Mode::Normal));

          if (connectionWithPopulatedServerInfo) {
            emit authOk();
            emit connected();
            return;
          }

          refreshServerInfo([this]() {
            if (m_serverInfo.clusterMode) {
              m_currentMode = Mode::Cluster;
              getClusterSlots([this](const ClusterSlots &cs,
                                     const QString &err) {
                if (err.size() > 0) {
                  emit error(
                      QString("Cannot retrieve cluster slots: %1").arg(err));
                  return;
                }

                m_clusterSlots = cs;

                emit authOk();
                emit connected();
              });
              emit log("Cluster detected");
            } else if (m_serverInfo.sentinelMode) {
              m_currentMode = Mode::Sentinel;
              emit log("Sentinel detected. Requesting master node...");
              return sentinelConnectToMaster();
            } else {
                emit authOk();
                emit connected();
            }
          });
        },
        handleConnectionError, true);
  };

  if (m_config.useAuth() || m_config.useAcl()) {
    QList<QByteArray> authCmd;

    if (m_config.useAcl()) {
      authCmd = {"AUTH", m_config.username().toUtf8(),
                 m_config.auth().toUtf8()};
    } else {
      authCmd = {"AUTH", m_config.auth().toUtf8()};
    }

    auto handleAuthResp = [this, testConnection](const Response &authResult) {
      if (authResult.isWrongPasswordError()) {
        emit authError("Invalid credentials");
        emit error(QString("AUTH ERROR. Invalid credentials: %1")
                       .arg(authResult.value().toString()));
        return;
      } else if (!authResult.isOkMessage()) {
        // NOTE(u_glide): Workaround for redis-sentinel < 5.0 and for
        // redis-sentinels >= 5.0.1 without configured password
        emit log(QString("redis-server doesn't support AUTH command or is"
                         "misconfigured. Trying "
                         "to proceed without password. (Error: %1)")
                     .arg(authResult.value().toString()));        
      }

      testConnection();
    };

    command(
        authCmd, this,
        [handleAuthResp, handleConnectionError](RedisClient::Response r,
                                                QString err) {
          if (err.size() > 0) return handleConnectionError(err);

          return handleAuthResp(r);
        }, -1, true);
  } else {
    testConnection();
  }
}

void RedisClient::Connection::setTransporter(
    QSharedPointer<RedisClient::AbstractTransporter> transporter) {
  if (transporter.isNull()) return;

  m_transporter = transporter;
}

QSharedPointer<RedisClient::AbstractTransporter>
RedisClient::Connection::getTransporter() const {
  return m_transporter;
}

RedisClient::ServerInfo::ServerInfo()
    : version(0.0), clusterMode(false), sentinelMode(false) {}

RedisClient::ServerInfo::ServerInfo(double version, bool clusterMode)
    : version(version), clusterMode(clusterMode), sentinelMode(false)
{
    parsed.insert("", {{"", ""}});
}

RedisClient::ServerInfo RedisClient::ServerInfo::fromString(
    const QString &info) {
  QStringList lines = info.split("\r\n");

  ParsedServerInfo parsed;
  QString currentSection{"unknown"};
  int posOfSeparator = -1;
  int lineSectionStart = 0;

  foreach (QString line, lines) {
    if (line.startsWith("#")) {
      currentSection = line.mid(2).toLower();
      continue;
    }

    if (line.startsWith("module:")) {
        lineSectionStart = line.indexOf('=') + 1;
        posOfSeparator = line.indexOf(',', lineSectionStart);
    } else {
        posOfSeparator = line.indexOf(':');
        lineSectionStart = 0;
    }

    if (posOfSeparator == -1) continue;

    parsed[currentSection][line.mid(lineSectionStart, posOfSeparator - lineSectionStart)] =
        line.mid(posOfSeparator + 1);
  }

  QRegExp versionRegex("redis_version:([0-9]+\\.[0-9]+)", Qt::CaseInsensitive,
                       QRegExp::RegExp2);
  QRegExp modeRegex("redis_mode:([a-z]+)", Qt::CaseInsensitive,
                    QRegExp::RegExp2);

  RedisClient::ServerInfo result;
  result.parsed = parsed;
  result.version =
      (versionRegex.indexIn(info) == -1) ? 0.0 : versionRegex.cap(1).toDouble();

  if (modeRegex.indexIn(info) != -1) {
    if (modeRegex.cap(1) == "cluster") result.clusterMode = true;
    if (modeRegex.cap(1) == "sentinel") result.sentinelMode = true;
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

  if (result.databases.size() == 0) return result;

  int lastKnownDbIndex = result.databases.lastKey();
  for (int dbIndex = 0; dbIndex < lastKnownDbIndex; ++dbIndex) {
    if (!result.databases.contains(dbIndex)) {
      result.databases.insert(dbIndex, 0);
    }
  }

  return result;
}

QVariantMap RedisClient::ServerInfo::ParsedServerInfo::toVariantMap() {
  QVariantMap categories;
  QHashIterator<QString, QHash<QString, QString>> catIterator(*this);

  while (catIterator.hasNext()) {
    catIterator.next();
    QHashIterator<QString, QString> propIterator(catIterator.value());
    QVariantMap properties;

    while (propIterator.hasNext()) {
      propIterator.next();
      properties.insert(propIterator.key(), propIterator.value());
    }

    categories.insert(catIterator.key(), properties);
  }

  return categories;
}

RedisClient::Connection::SSHSupportException::SSHSupportException(
    const QString &e)
    : Connection::Exception(e) {}
