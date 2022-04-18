#pragma once
#include <QJsonObject>
#include <QList>
#include <QSslCertificate>
#include <QString>
#include <QVariantHash>

namespace RedisClient {

class Connection;

/**
 * @brief The ConnectionConfig class
 * Supports loading settigns from JSON objects
 */
class ConnectionConfig {
 public:
  static const uint DEFAULT_REDIS_PORT = 6379;
  static const uint DEFAULT_SSH_PORT = 22;
  static const uint DEFAULT_TIMEOUT_IN_MS = 60000;

 public:
  /**
   * @brief Default constructor for local connections
   * @param host
   * @param name
   * @param port
   */
  ConnectionConfig(const QString& host = "", const QString& auth = "",
                   const uint port = DEFAULT_REDIS_PORT,
                   const QString& name = "");
  ConnectionConfig& operator=(const ConnectionConfig& other);
  ConnectionConfig(const QVariantHash& options);

  virtual ~ConnectionConfig() = default;

  QByteArray id() const;
  void setId(QByteArray id);

  QString name() const;
  QString host() const;
  QString auth() const;
  QString username() const;
  uint port() const;

  void setName(QString name);
  void setAuth(QString auth);
  void setUsername(QString username);
  void setHost(QString host);
  void setPort(uint port);

  bool isNull() const;
  bool useAuth() const;
  bool useAcl() const;
  bool isValid() const;

  /*
   * Timeouts in ms
   */
  uint executeTimeout() const;
  uint connectionTimeout() const;

  void setExecutionTimeout(uint timeout);
  void setConnectionTimeout(uint timeout);
  void setTimeouts(uint connectionTimeout, uint commandExecutionTimeout);

  /*
   * SSL settings
   */
  virtual bool useSsl() const;
  virtual void setSsl(bool enabled);
  virtual QList<QSslCertificate> sslCaCertificates() const;
  virtual QString sslCaCertPath() const;
  virtual QString sslPrivateKeyPath() const;
  virtual QString sslLocalCertPath() const;
  bool ignoreAllSslErrors() const;

  virtual void setSslCaCertPath(QString path);
  virtual void setSslPrivateKeyPath(QString path);
  virtual void setSslLocalCertPath(QString path);
  void setIgnoreAllSslErrors(bool v);

  /*
   * SSH Tunnel settings
   */
  virtual bool useSshTunnel() const;
  virtual bool isSshPasswordUsed() const;
  virtual QString sshPassword() const;
  virtual QString sshUser() const;
  virtual QString sshHost() const;
  virtual uint sshPort() const;
  virtual bool sshAgent() const;
  virtual QString sshAgentPath() const;

  /**
   * @brief getSshPrivateKeyPath from specified path
   * @return QString with ssh key
   */
  virtual QString getSshPrivateKeyPath() const;

  /**
   * @brief getSshPublicKeyPath from specified path
   * @return QString with ssh key
   */
  virtual QString getSshPublicKeyPath() const;

  virtual void setSshPassword(QString pass);
  virtual void setSshHost(QString host);
  virtual void setSshPrivateKeyPath(QString path);
  virtual void setSshUser(QString user);
  virtual void setSshPort(uint port);
  virtual void setSshAgent(bool v);
  virtual void setSshAgentPath(const QString& v);

  /*
   * Cluster settings
   */
  bool overrideClusterHost() const;
  void setClusterHostOverride(bool v);

  /*
   * Convert config to JSON
   */
  QJsonObject toJsonObject(QSet<QString> ignoreFields = QSet<QString>()) const;
  static ConnectionConfig fromJsonObject(const QJsonObject& config);  

  QVariantHash getInternalParameters() const;

 protected:
  /*
   * Extension API
   * Use following methods to implement custom wrappers
   * around ConnectionConfig class
   */
  template <class T>
  inline T param(const QString& p, T default_value = T()) const {
    if (m_parameters.contains(p)) return m_parameters[p].value<T>();
    return default_value;
  }

  template <class T>
  inline void setParam(const QString& key, T p) {
    m_parameters.insert(key, p);
  }

 protected:
  QVariantHash m_parameters;  
};
}  // namespace RedisClient
