#include "sshtransporter.h"

#include <qsshclient/qxtsshtcpsocket.h>

#include "qredisclient/connection.h"
#include "qredisclient/connectionconfig.h"

#define MAX_BUFFER_SIZE 536800 //response part limit

RedisClient::SshTransporter::SshTransporter(RedisClient::Connection *c)
    :
      RedisClient::AbstractTransporter(c),
      m_socket(nullptr),
      m_isHostKeyAlreadyAdded(false),
      m_lastConnectionOk(false)
{
}

void RedisClient::SshTransporter::initSocket()
{
    m_sshClient = QSharedPointer<QxtSshClient>(new QxtSshClient);
    connect(m_sshClient.data(), &QxtSshClient::error, this, &RedisClient::SshTransporter::OnSshConnectionError);
    connect(m_sshClient.data(), &QxtSshClient::connected, this, &RedisClient::SshTransporter::OnSshConnected);

    m_syncLoop = QSharedPointer<QEventLoop>(new QEventLoop);
    m_syncTimer = QSharedPointer<QTimer>(new QTimer);
    m_syncTimer->setSingleShot(true);
    connect(m_syncTimer.data(), SIGNAL(timeout()), m_syncLoop.data(), SLOT(quit()));
}

void RedisClient::SshTransporter::disconnectFromHost()
{    
    if (m_sshClient.isNull())
        return;

    if (m_socket)
        QObject::disconnect(m_socket, 0, 0, 0);

    QObject::disconnect(m_sshClient.data(), 0, 0, 0);

    m_sshClient->resetState();
}

bool RedisClient::SshTransporter::isInitialized() const
{
    return !m_sshClient.isNull();
}

bool RedisClient::SshTransporter::isSocketReconnectRequired() const
{
    return !m_socket;
}

bool RedisClient::SshTransporter::canReadFromSocket()
{
    if (!m_lastConnectionOk) //on first emit
        m_lastConnectionOk = true;

    if (m_syncLoop->isRunning())
        m_syncLoop->exit();

    return m_isCommandRunning && m_socket;
}

QByteArray RedisClient::SshTransporter::readFromSocket()
{
    return m_socket->read(MAX_BUFFER_SIZE);
}

bool RedisClient::SshTransporter::connectToHost()
{
    ConnectionConfig config = m_connection->getConfig();

    if (config.isSshPasswordUsed())
        m_sshClient->setPassphrase(config.sshPassword());

    QString privateKey = config.getSshPrivateKey();

    if (!privateKey.isEmpty()) {
        m_sshClient->setKeyFiles("", privateKey);
    }

    //connect to ssh server
    m_lastConnectionOk = false;
    m_syncTimer->start(config.connectionTimeout());
    m_sshClient->connectToHost(config.sshUser(), config.sshHost(), config.sshPort());
    m_syncLoop->exec();

    if (!m_lastConnectionOk) {
        if (!m_syncTimer->isActive())
            emit errorOccurred("SSH connection timeout, check connection settings");
        return false;
    }

    //connect to redis
    m_socket = m_sshClient->openTcpSocket(config.host(), config.port());

    if (!m_socket) {
        emit errorOccurred("SSH connection established, but socket failed");
        return false;
    }

    connect(m_socket, &QxtSshTcpSocket::readyRead, this, &RedisClient::AbstractTransporter::readyRead);
    connect(m_socket, SIGNAL(destroyed()), this, SLOT(OnSshSocketDestroyed()));

    m_lastConnectionOk = false;
    m_syncTimer->start(config.connectionTimeout());
    m_syncLoop->exec();

    if (!m_lastConnectionOk) {
        emit errorOccurred(QString("SSH connection established, but redis connection failed"));
        return false;
    }

    emit connected();
    emit logEvent(QString("%1 > connected").arg(m_connection->getConfig().name()));

    return true;
}

void RedisClient::SshTransporter::sendCommand(const QByteArray &cmd)
{
    const char* cString = cmd.constData();
    m_socket->write(cString, cmd.size());
}

void RedisClient::SshTransporter::OnSshConnectionError(QxtSshClient::Error error)
{
    if (!m_isHostKeyAlreadyAdded && QxtSshClient::HostKeyUnknownError == error) {
        QxtSshKey hostKey = m_sshClient->hostKey();
        m_sshClient->addKnownHost(m_connection->getConfig().sshHost(), hostKey);
        m_sshClient->resetState();
        m_sshClient->connectToHost(m_connection->getConfig().sshUser(),
                                   m_connection->getConfig().sshHost(),
                                   m_connection->getConfig().sshPort());

        m_isHostKeyAlreadyAdded = true;
        return;
    }

    if (m_syncLoop->isRunning()) {
        m_syncLoop->exit();
    }

    emit errorOccurred(QString("SSH Connection error: %1").arg(getSshErrorString(error)));
}

void RedisClient::SshTransporter::OnSshConnected()
{
    m_lastConnectionOk = true;

    if (m_syncLoop->isRunning()) {
        m_syncLoop->exit();
    }
}

void RedisClient::SshTransporter::OnSshConnectionClose()
{
    if (m_syncLoop->isRunning())
        m_syncLoop->exit();

    emit logEvent("SSH connection closed");
}

void RedisClient::SshTransporter::OnSshSocketDestroyed()
{
    m_socket = nullptr;
    emit logEvent("SSH socket detroyed");
}

void RedisClient::SshTransporter::reconnect()
{
    emit logEvent("Reconnect to host");
    if (m_socket) m_socket->close();
    m_sshClient->resetState();
    connectToHost();
}
