#pragma once
#include <QByteArray>
#include <QEventLoop>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSharedPointer>
#include <QTimer>
#include <QVariantList>
#include <functional>

#include "command.h"
#include "connectionconfig.h"
#include "exception.h"
#include "response.h"
#include "scancommand.h"

namespace RedisClient {

class AbstractTransporter;

typedef QMap<int, int> DatabaseList;

#define DEFAULT_SCAN_LIMIT 10000

/**
 * @brief The ServerInfo struct
 * Represents redis-server information parsed from INFO command.
 */
struct ServerInfo {
  ServerInfo();

  ServerInfo(double version, bool clusterMode);

  double version;
  bool clusterMode;
  bool sentinelMode;
  DatabaseList databases;

  class ParsedServerInfo : public QHash<QString, QHash<QString, QString>> {
   public:
    QVariantMap toVariantMap();
  };

  ParsedServerInfo parsed;

  static ServerInfo fromString(const QString &info);
};

/**
 * @brief The Connection class
 * Main client class.
 */
class Connection : public QObject {
  Q_OBJECT
  ADD_EXCEPTION

  friend class AbstractTransporter;

 public:
  enum class Mode { Normal, PubSub, Cluster, Sentinel, Monitor };
  class InvalidModeException : public Connection::Exception {};

  class SSHSupportException : public Connection::Exception {
   public:
    SSHSupportException(const QString &e);
  };

 public:
  /**
   * @brief Constructs connection class
   * @param c - connection config
   * @param autoconnect - Auto connect if disconnected
   * NOTE: different config options are required for different transporters.
   */
  Connection(const ConnectionConfig &c, bool autoConnect = true);

  /**
   * @brief ~Connection
   * If connection established internally call disconnect()
   */
  virtual ~Connection();

  /**
   * @brief connects to redis-server
   * @param wait -  true = sync mode, false = async mode
   * @return true - on success
   * @throws Connection::Exception if config is invalid or something went wrong.
   */
  virtual bool connect(bool wait = true);

  /**
   * @brief isConnected
   * @return
   */
  virtual bool isConnected();

  /**
   * @brief disconnect from redis-server
   */
  virtual void disconnect();

  /**
   * @brief disableAutoConnect
   */
  virtual void disableAutoConnect();

  /**
   * @brief getConfig
   * @return
   */
  ConnectionConfig getConfig() const;

  /**
   * @brief setConnectionConfig
   */
  void setConnectionConfig(const ConnectionConfig &);

  /**
   * @brief Get current mode
   * @return
   */
  Mode mode() const;

  /**
   * @brief Get current db index
   * @return int
   */
  int dbIndex() const;

  /**
   * @brief Get redis-server version
   * @return
   */
  virtual double getServerVersion();

  /**
   * @brief Get keyspace info parsed from INFO command
   * @return
   */
  virtual DatabaseList getKeyspaceInfo();

  /**
   * @brief getEnabledModules from INFO command
   * @return
   */
  virtual QHash<QString, QString> getEnabledModules();

  /**
   * @brief update internal structure for methods getServerVersion() and
   * getKeyspaceInfo()
   */
  virtual void refreshServerInfo(std::function<void()> callback);

  /*
   * Hi-Level Operations API
   */
  /**
   * @brief RawKeysList
   */
  typedef QList<QByteArray> RawKeysList;
  typedef std::function<void(const RawKeysList &, const QString &)>
      RawKeysListCallback;

  /**
   * @brief getDatabaseKeys - async keys loading
   * @param callback
   * @param pattern
   * @param dbIndex
   */
  virtual void getDatabaseKeys(RawKeysListCallback callback,
                               const QString &pattern = QString("*"),
                               int dbIndex = 0, long scanLimit = DEFAULT_SCAN_LIMIT);

  typedef QList<QPair<QByteArray, ulong>> RootNamespaces;
  typedef QList<QByteArray> RootKeys;
  typedef QPair<RootNamespaces, RootKeys> NamespaceItems;
  typedef std::function<void(const NamespaceItems &, const QString &)>
      NamespaceItemsCallback;

  virtual void getNamespaceItems(NamespaceItemsCallback callback,
                                 const QString &nsSeparator,
                                 const QString &pattern = QString("*"),
                                 int dbIndex = 0);

  /**
   * @brief getClusterKeys - async keys loading from all cluster nodes
   * @param callback
   * @param pattern
   */
  virtual void getClusterKeys(RawKeysListCallback callback,
                              const QString &pattern, long scanLimit = DEFAULT_SCAN_LIMIT);

  /**
   * @brief flushDbKeys - Remove keys on all master nodes
   */
  virtual void flushDbKeys(int dbIndex,
                           std::function<void(const QString &)> callback);

  typedef QPair<QString, int> Host;
  typedef QList<Host> HostList;

  /**
   * @brief getMasterNodes - Get master nodes of cluster
   * @return
   */
  void getMasterNodes(std::function<void(HostList, const QString& err)> callback);

  typedef QPair<int, int> Range;
  typedef QMap<Range, Host> ClusterSlots;

  /**
   * @brief getClusterSlots
   * @return
   */
  void getClusterSlots(std::function<void(ClusterSlots, const QString& err)> callback);

  /**
   * @brief getClisterHost
   * @param cmd
   * @return
   */
  Host getClusterHost(const Command &cmd);

  /**
   * @brief isCommandSupported
   * @param rawCmd
   * @return
   */
  virtual QFuture<bool> isCommandSupported(QList<QByteArray> rawCmd);

  /*
   * Command execution API
   */
  /**
   * @brief command
   * @param cmd
   */
  QFuture<Response> command(const Command &cmd);

  /**
   * @brief Execute command without callback in async mode.
   * @param rawCmd
   * @param db
   */
  QFuture<Response> command(QList<QByteArray> rawCmd, int db = -1);

  /**
   * @brief Execute command with callback in async mode.
   * @param rawCmd
   * @param owner
   * @param callback
   * @param db
   */
  QFuture<Response> command(QList<QByteArray> rawCmd, QObject *owner,
                            RedisClient::Command::Callback callback,
                            int db = -1, bool priorityCmd=false);

  /**
   * @brief Hi-level wrapper with basic error handling
   * @param rawCmd
   * @param owner
   * @param db
   * @param callback
   * @param errback
   */
  inline QFuture<Response> cmd(
      QList<QByteArray> rawCmd, QObject *owner, int db,
      std::function<void(const RedisClient::Response &)> callback,
      std::function<void(const QString &)> errback,
      bool hiPriorityCmd=false,
      bool ignoreErrorResponses=false) {
    try {
      return this->command(
          rawCmd, owner,
          [callback, errback, ignoreErrorResponses](RedisClient::Response r, QString err) {
            if (err.size() > 0) return errback(err);
            if (!ignoreErrorResponses && r.isErrorMessage()) return errback(r.value().toString());

            return callback(r);
          },
          db, hiPriorityCmd);
    } catch (const RedisClient::Connection::Exception &e) {
      errback(QString(e.what()));
      return QFuture<Response>();
    }
  }

  /**
   * @brief pipelinedCmd
   * @param rawCmds
   * @param owner
   * @param db
   * @param callback
   */
  void pipelinedCmd(const QList<QList<QByteArray> > &rawCmds, QObject *owner, int db,
      std::function<void(const RedisClient::Response &, QString err)> callback, bool transaction);


  int pipelineCommandsLimit() const;

  /**
   * @brief CollectionCallback
   */
  typedef std::function<void(QVariant, QString err)> CollectionCallback;

  /**
   * @brief IncrementalCollectionCallback
   */
  typedef std::function<void(QVariant, QString err, bool final)>
      IncrementalCollectionCallback;

  /**
   * @brief retrieveCollection
   * @param cmd
   * @param callback
   */
  virtual void retrieveCollection(const ScanCommand &cmd,
                                  CollectionCallback callback);

  /**
   * @brief retrieveCollection
   * @param cmd
   * @param callback
   */
  virtual void retrieveCollectionIncrementally(
      const ScanCommand &cmd, IncrementalCollectionCallback callback);

  /**
   * @brief runCommand
   * @param cmd
   * @return QFuture<Response>
   */
  virtual QFuture<Response> runCommand(const Command &cmd);

  /**
   * @brief runCommands
   * @param commands
   */
  virtual void runCommands(const QList<Command> &cmd);

  /**
   * @brief waitForIdle - Wait until all commands in queue will be processed
   * @param timeout - in milliseconds
   */
  bool waitForIdle(uint timeout);

  /**
   * @brief create new connection object with same settings
   * @return
   */
  virtual QSharedPointer<Connection> clone(bool copyServerInfo = true) const;

  /*
   * Low level functions for modification
   * commands execution.
   */
  void setTransporter(QSharedPointer<AbstractTransporter>);
  QSharedPointer<AbstractTransporter> getTransporter() const;

  void callAfterConnect(std::function<void(const QString &err)> callback);

 signals:
  void addCommandsToWorker(const QList<Command> &);
  void error(const QString &);
  void log(const QString &);
  void connected();
  void shutdownStart();
  void disconnected();
  void authOk();
  void authError(const QString &);

  // Cluster & Sentinel
  void reconnectTo(const QString &host, int port);

 protected:
  void createTransporter();
  bool isTransporterRunning();

  void processScanCommand(
      const ScanCommand &cmd, CollectionCallback callback,
      QSharedPointer<QVariantList> result = QSharedPointer<QVariantList>(),
      bool incrementalProcessing = false);

  void changeCurrentDbNumber(int db);

  void clusterConnectToNextMasterNode(
      std::function<void(const QString &err)> callback);

  bool hasNotVisitedClusterNodes() const;  

  void sentinelConnectToMaster();

  void rawClusterSlots(std::function<void(QVariantList, const QString&)> callback);

 protected slots:
  void auth();

 protected:
  ConnectionConfig m_config;
  QSharedPointer<QThread> m_transporterThread;
  QSharedPointer<AbstractTransporter> m_transporter;

  int m_dbNumber;
  ServerInfo m_serverInfo;
  Mode m_currentMode;
  QMutex m_dbNumberMutex;
  QMutex m_blockingOp;
  bool m_autoConnect;
  bool m_stoppingTransporter;
  RawKeysListCallback m_collectClusterNodeKeys;
  RedisClient::Command::Callback m_cmdCallback;
  QSharedPointer<HostList> m_notVisitedMasterNodes;
  ClusterSlots m_clusterSlots;
};
}  // namespace RedisClient
