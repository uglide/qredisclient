#include "connectionconfig.h"
#include "qredisclient/utils/compat.h"
#include <QFile>
#include <QJsonDocument>
#include <QCryptographicHash>

RedisClient::ConnectionConfig::ConnectionConfig(const QString &host, const QString &auth, const uint port, const QString &name)
{
    m_parameters.insert("name", name);
    m_parameters.insert("auth", auth);
    m_parameters.insert("host", host);
    m_parameters.insert("port", port);
    m_parameters.insert("timeout_connect", DEFAULT_TIMEOUT_IN_MS);
    m_parameters.insert("timeout_execute", DEFAULT_TIMEOUT_IN_MS);
}

RedisClient::ConnectionConfig &RedisClient::ConnectionConfig::operator =(const ConnectionConfig &other)
{
    if (this != &other) {
        m_parameters = other.m_parameters;
    }

    return *this;
}

RedisClient::ConnectionConfig::ConnectionConfig(const QVariantHash &options)
    : m_parameters(options)
{
}

QByteArray RedisClient::ConnectionConfig::id() const
{        
    QByteArray storedId = param<QByteArray>("id");

    if (!storedId.isEmpty())
        return storedId;

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QJsonDocument(toJsonObject({"id"})).toJson(QJsonDocument::Compact));
    return hash.result();
}

void RedisClient::ConnectionConfig::setId(QByteArray id)
{
    setParam<QByteArray>("id", id);
}

QString RedisClient::ConnectionConfig::name() const
{
    return param<QString>("name");
}

QString RedisClient::ConnectionConfig::host() const
{
    return param<QString>("host");
}

QString RedisClient::ConnectionConfig::auth() const
{
    return param<QString>("auth");
}

QString RedisClient::ConnectionConfig::username() const
{
    return param<QString>("username");
}

uint RedisClient::ConnectionConfig::port() const
{
    return param<uint>("port");
}

void RedisClient::ConnectionConfig::setName(QString name)
{
    setParam<QString>("name", name);
}

void RedisClient::ConnectionConfig::setAuth(QString auth)
{
    setParam<QString>("auth", auth);
}

void RedisClient::ConnectionConfig::setUsername(QString username)
{
    setParam<QString>("username", username);
}

void RedisClient::ConnectionConfig::setHost(QString host)
{
    setParam<QString>("host", host);
}

void RedisClient::ConnectionConfig::setPort(uint port)
{
    setParam<uint>("port", port);
}

uint RedisClient::ConnectionConfig::executeTimeout() const
{
    return param<uint>("timeout_execute");
}

uint RedisClient::ConnectionConfig::connectionTimeout() const
{
    return param<uint>("timeout_connect");
}

void RedisClient::ConnectionConfig::setExecutionTimeout(uint timeout)
{
    setParam<uint>("timeout_execute", timeout);
}

void RedisClient::ConnectionConfig::setConnectionTimeout(uint timeout)
{
    setParam<uint>("timeout_connect", timeout);
}

void RedisClient::ConnectionConfig::setTimeouts(uint connectionTimeout, uint commandExecutionTimeout)
{
    setParam<uint>("timeout_connect", connectionTimeout);
    setParam<uint>("timeout_execute", commandExecutionTimeout);
}

QList<QSslCertificate> RedisClient::ConnectionConfig::sslCaCertificates() const
{
    QString path = param<QString>("ssl_ca_cert_path");
    if (!path.isEmpty() && QFile::exists(path))
        return QSslCertificate::fromPath(path);

    return QList<QSslCertificate>();
}

QString RedisClient::ConnectionConfig::sslCaCertPath() const
{
    return param<QString>("ssl_ca_cert_path");
}

QString RedisClient::ConnectionConfig::sslPrivateKeyPath() const
{
    return param<QString>("ssl_private_key_path");
}

QString RedisClient::ConnectionConfig::sslLocalCertPath() const
{
    return param<QString>("ssl_local_cert_path");
}

bool RedisClient::ConnectionConfig::ignoreAllSslErrors() const
{
    return param<bool>("ssl_ignore_all_errors", false);
}

void RedisClient::ConnectionConfig::setSslCaCertPath(QString path)
{
    setParam<QString>("ssl_ca_cert_path", path);
}

void RedisClient::ConnectionConfig::setSslPrivateKeyPath(QString path)
{
    setParam<QString>("ssl_private_key_path", path);
}

void RedisClient::ConnectionConfig::setSslLocalCertPath(QString path)
{
    setParam<QString>("ssl_local_cert_path", path);
}

void RedisClient::ConnectionConfig::setIgnoreAllSslErrors(bool v)
{
    m_parameters.insert("ssl_ignore_all_errors", v);
}

bool RedisClient::ConnectionConfig::isSshPasswordUsed() const
{
    return !param<QString>("ssh_password").isEmpty();
}

QString RedisClient::ConnectionConfig::sshPassword() const
{
    return param<QString>("ssh_password");
}

QString RedisClient::ConnectionConfig::sshUser() const
{
    return param<QString>("ssh_user");
}

QString RedisClient::ConnectionConfig::sshHost() const
{
    return param<QString>("ssh_host");
}

uint RedisClient::ConnectionConfig::sshPort() const
{
    return param<uint>("ssh_port", DEFAULT_SSH_PORT);
}

bool RedisClient::ConnectionConfig::sshAgent() const
{
    return param<bool>("ssh_agent", false);
}

QString RedisClient::ConnectionConfig::sshAgentPath() const
{
    return param<QString>("ssh_agent_path", "");
}

QVariantHash RedisClient::ConnectionConfig::getInternalParameters() const
{
    return m_parameters;
}

bool RedisClient::ConnectionConfig::overrideClusterHost() const
{
    return param<bool>("cluster_host_override", true);
}

void RedisClient::ConnectionConfig::setClusterHostOverride(bool v)
{
    m_parameters.insert("cluster_host_override", v);
}

bool RedisClient::ConnectionConfig::isNull() const
{
    return param<QString>("host").isEmpty()
            || param<uint>("port") <= 0;
}

bool RedisClient::ConnectionConfig::useSshTunnel() const
{
    bool hasAuthMethod = !param<QString>("ssh_password").isEmpty()
            || !param<QString>("ssh_private_key_path").isEmpty()
            || param<bool>("ask_ssh_password", false)
            || sshAgent();

    return !param<QString>("ssh_host").isEmpty()
            && sshPort() > 0
            && !param<QString>("ssh_user").isEmpty()
            && hasAuthMethod;
}

bool RedisClient::ConnectionConfig::useAuth() const
{
    return !param<QString>("auth").isEmpty();
}

bool RedisClient::ConnectionConfig::useAcl() const
{
    return !param<QString>("username").isEmpty();
}

bool RedisClient::ConnectionConfig::useSsl() const
{
    return param<bool>("ssl");
}

void RedisClient::ConnectionConfig::setSsl(bool enabled)
{
    setParam<bool>("ssl", enabled);
}

bool RedisClient::ConnectionConfig::isValid() const
{
    return isNull() == false
            && param<uint>("timeout_connect") > 1000
            && param<uint>("timeout_execute") > 1000;
}

QString RedisClient::ConnectionConfig::getSshPrivateKeyPath() const
{
    return param<QString>("ssh_private_key_path");
}

QString RedisClient::ConnectionConfig::getSshPublicKeyPath() const
{
    return param<QString>("ssh_public_key_path");
}

void RedisClient::ConnectionConfig::setSshPassword(QString pass)
{
    m_parameters.insert("ssh_password", pass);
}

void RedisClient::ConnectionConfig::setSshHost(QString host)
{
    m_parameters.insert("ssh_host", host);
}

void RedisClient::ConnectionConfig::setSshPrivateKeyPath(QString path)
{
    m_parameters.insert("ssh_private_key_path", path);
}

void RedisClient::ConnectionConfig::setSshUser(QString user)
{
    m_parameters.insert("ssh_user", user);
}

void RedisClient::ConnectionConfig::setSshPort(uint port)
{
    m_parameters.insert("ssh_port", port);
}

void RedisClient::ConnectionConfig::setSshAgent(bool v)
{
    m_parameters.insert("ssh_agent", v);
}

void RedisClient::ConnectionConfig::setSshAgentPath(const QString &v)
{
    m_parameters.insert("ssh_agent_path", v);
}

RedisClient::ConnectionConfig RedisClient::ConnectionConfig::fromJsonObject(const QJsonObject &config)
{
    QVariantHash options = QJsonObjectToVariantHash(config);
    ConnectionConfig c(options);
    return c;
}

QJsonObject RedisClient::ConnectionConfig::toJsonObject(QSet<QString> ignoreFields) const
{
    auto params = m_parameters;    

    for (auto ignoredParam : ignoreFields) {
        params.remove(ignoredParam);
    }

    return QJsonObjectFromVariantHash(params);
}
