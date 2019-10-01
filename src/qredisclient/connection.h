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

/**
 * @brief The ServerInfo struct
 * Represents redis-server information parsed from INFO command.
 */
struct ServerInfo {
  ServerInfo();

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
  enum class Mode { Normal, PubSub, Cluster, Sentinel };
  class InvalidModeException : public Connection::Exception {};

  class SSHSupportException : public Connection::Exception {
  public:
      SSHSupportException(const QString& e);
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
   * @brief update internal structure for methods getServerVersion() and
   * getKeyspaceInfo()
   */
  virtual void refreshServerInfo();

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
                               int dbIndex = 0, long scanLimit = 10000);

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
                              const QString &pattern);

  /**
   * @brief flushDbKeys - Remove keys on all master nodes
   */
  virtual void flushDbKeys(int dbIndex,
                           std::function<void(const QString &)> callback);

  typedef QPair<QString, int> Host;
  typedef QList<Host> HostList;

  /**
   * @brief getMasterNodes - Get master nodes of cluster
   * @return HostList
   */
  HostList getMasterNodes();

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
                            int db = -1);

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
      std::function<void(const QString &)> errback) {
    try {
      return this->command(
          rawCmd, owner,
          [callback, errback](RedisClient::Response r, QString err) {
            if (err.size() > 0) return errback(err);
            if (r.isErrorMessage()) return errback(r.value().toString());

            return callback(r);
          },
          db);
    } catch (const RedisClient::Connection::Exception &e) {
      errback(QString(e.what()));
      return QFuture<Response>();
    }
  }

  /**
   * @brief commandSync
   * @param cmd
   * @return
   */
  Response commandSync(const Command &cmd);

  /**
   * @brief Execute command without callback and wait for response.
   * @param rawCmd
   * @param db
   * @return
   */
  Response commandSync(QList<QByteArray> rawCmd, int db = -1);

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
   * @brief waitForIdle - Wait until all commands in queue will be processed
   * @param timeout - in milliseconds
   */
  bool waitForIdle(uint timeout);

  /**
   * @brief create new connection object with same settings
   * @return
   */
  virtual QSharedPointer<Connection> clone() const;

  /*
   * Low level functions for modification
   * commands execution.
   */
  void setTransporter(QSharedPointer<AbstractTransporter>);
  QSharedPointer<AbstractTransporter> getTransporter() const;

 signals:
  void addCommandToWorker(const Command &);
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

  Response internalCommandSync(QList<QByteArray> rawCmd);

  void processScanCommand(
      const ScanCommand &cmd, CollectionCallback callback,
      QSharedPointer<QVariantList> result = QSharedPointer<QVariantList>(),
      bool incrementalProcessing = false);

  void changeCurrentDbNumber(int db);

  void clusterConnectToNextMasterNode(std::function<void(const QString& err)> callback);

  bool hasNotVisitedClusterNodes() const;

  void callAfterConnect(std::function<void(const QString& err)> callback);

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
  bool m_autoConnect;
  bool m_stoppingTransporter;
  RawKeysListCallback m_collectClusterNodeKeys;
  RedisClient::Command::Callback m_cmdCallback;
  QSharedPointer<HostList> m_notVisitedMasterNodes;
};
}  // namespace RedisClient
