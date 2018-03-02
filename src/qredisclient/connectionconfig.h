#pragma once
#include <QString>
#include <QList>
#include <QVariantHash>
#include <QJsonObject>
#include <QSslCertificate>

namespace RedisClient {

class Connection;

/**
 * @brief The ConnectionConfig class
 * Supports loading settigns from JSON objects
 */
class ConnectionConfig
{
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
    ConnectionConfig(const QString & host = "", const QString & auth = "",
                     const uint port = DEFAULT_REDIS_PORT, const QString & name = "");
    ConnectionConfig & operator = (const ConnectionConfig & other);
    ConnectionConfig(const QVariantHash& options);

    QString name() const;
    QString host() const;
    QString auth() const;
    uint port() const;

    void setName(QString name);
    void setAuth(QString auth);
    void setHost(QString host);
    void setPort(uint port);

    bool isNull() const;
    bool useAuth() const;
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
     * NOTE: SSL over SSH tunnel is not supported!
     */
    bool useSsl() const;
    void setSsl(bool enabled);
    QList<QSslCertificate> sslCaCertificates() const;
    QString sslCaCertPath() const;
    QString sslPrivateKeyPath() const;
    QString sslLocalCertPath() const;

    void setSslCaCertPath(QString path);
    void setSslPrivateKeyPath(QString path);
    void setSslLocalCertPath(QString path);
    void setSslSettigns(QString sslCaCertPath,
                        QString sslPrivateKeyPath = "",
                        QString sslLocalCertPath = "");

    /*
     * SSH Tunnel settings
     */
    bool useSshTunnel() const;
    bool isSshPasswordUsed() const;
    QString sshPassword() const;
    QString sshUser() const;
    QString sshHost() const;
    uint sshPort() const;

    /**
     * @brief getSshPrivateKey from specified path
     * @return QString with ssh key
     */
    QString getSshPrivateKey() const;
    QString getSshPrivateKeyPath() const;

    /**
     * @brief getSshPublicKey from specified path
     * @return QString with ssh key
     */
    QString getSshPublicKey() const;
    QString getSshPublicKeyPath() const;

    void setSshPassword(QString pass);
    void setSshHost(QString host);
    void setSshPrivateKeyPath(QString path);
    void setSshUser(QString user);
    void setSshPort(uint port);

    /**
     * @brief setSshTunnelSettings - Set SSH settings
     * @param host
     * @param user
     * @param pass
     * @param port
     * @param sshPrivatekeyPath
     */
    void setSshTunnelSettings(QString host, QString user, QString pass,
                              uint port = DEFAULT_SSH_PORT,
                              QString sshPrivatekeyPath = "",
                              QString sshPublickeyPath = "");

    /*
     * Cluster settings
     */
    bool overrideClusterHost() const;
    void setClusterHostOverride(bool v);

    /*
     * Convert config to JSON
     */
    QJsonObject toJsonObject();
    static ConnectionConfig fromJsonObject(const QJsonObject& config);

    /*
     * Following methods used internally in Connection class
     */
    QWeakPointer<Connection> getOwner() const;
    void setOwner(QWeakPointer<Connection>);

    QVariantHash getInternalParameters() const;

protected:
    /*
      * Extension API
      * Use following methods to implement custom wrappers
      * around ConnectionConfig class
      */
    template <class T> inline T param(const QString& p, T default_value=T()) const
    {
        if (m_parameters.contains(p)) return m_parameters[p].value<T>();
        return default_value;
    }

    template <class T> inline void setParam(const QString& key, T p)
    {
        m_parameters.insert(key, p);
    }

    QString getValidPathFromParameter(const QString& param) const;

protected:
    QWeakPointer<Connection> m_owner;
    QVariantHash m_parameters;
};
}
