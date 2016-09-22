#include "sshtransporter.h"

#include <qsshclient/qxtsshtcpsocket.h>

#include "qredisclient/connection.h"
#include "qredisclient/connectionconfig.h"
#include "qredisclient/utils/sync.h"

#define MAX_BUFFER_SIZE 536800 //response part limit

RedisClient::SshTransporter::SshTransporter(RedisClient::Connection *c)
    :
      RedisClient::AbstractTransporter(c),
      m_socket(nullptr),
      m_isHostKeyAlreadyAdded(false)
{
}

void RedisClient::SshTransporter::initSocket()
{
    m_sshClient = QSharedPointer<QxtSshClient>(new QxtSshClient);
    connect(m_sshClient.data(), &QxtSshClient::error, this, &RedisClient::SshTransporter::OnSshConnectionError);
}

void RedisClient::SshTransporter::disconnectFromHost()
{    
    if (m_sshClient.isNull())
        return;

    m_loopTimer->stop();

    if (m_socket)
        QObject::disconnect(m_socket, 0, 0, 0);

    QObject::disconnect(m_sshClient.data(), 0, 0, 0);

    m_sshClient->disconnectFromHost();    
}

bool RedisClient::SshTransporter::isInitialized() const
{
    return !m_sshClient.isNull() && m_socket;
}

bool RedisClient::SshTransporter::isSocketReconnectRequired() const
{
    return !m_socket || !m_socket->isOpen();
}

bool RedisClient::SshTransporter::canReadFromSocket()
{
    return m_socket && m_socket->isOpen();
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
    SignalWaiter waiter(config.connectionTimeout());
    waiter.addAbortSignal(this, &RedisClient::SshTransporter::errorOccurred);
    waiter.addSuccessSignal(m_sshClient.data(), &QxtSshClient::connected);

    emit logEvent("Connecting to SSH host...");

    m_sshClient->connectToHost(config.sshUser(), config.sshHost(), config.sshPort());

    if (!waiter.wait()) {
        emit errorOccurred("Cannot connect to SSH host");
        return false;
    }

    emit logEvent("SSH tunnel established. Connecting to redis-server...");    

    return openTcpSocket();
}

void RedisClient::SshTransporter::sendCommand(const QByteArray &cmd)
{
    QByteArray command = cmd;
    char* data = command.data();
    qint64 total = 0;
    qint64 sent;

    while (total < cmd.size()) {
        sent = m_socket->write(data + total, cmd.size() - total);
        qDebug() << "Bytes written to socket" << sent;
        total += sent;
    }
}

bool RedisClient::SshTransporter::openTcpSocket()
{
    auto config = m_connection->getConfig();
    m_socket = m_sshClient->openTcpSocket(config.host(), config.port());

    if (!m_socket) {
        emit errorOccurred("SSH connection established, but socket failed");
        return false;
    }

    SignalWaiter socketWaiter(config.connectionTimeout());
    socketWaiter.addAbortSignal(m_socket, &QxtSshTcpSocket::destroyed);
    socketWaiter.addSuccessSignal(m_socket, &QxtSshTcpSocket::readyRead);

    connect(m_socket, &QxtSshTcpSocket::readyRead, this, &RedisClient::AbstractTransporter::readyRead);
    connect(m_socket, &QxtSshTcpSocket::destroyed, this, &RedisClient::SshTransporter::OnSshSocketDestroyed);

    if (!socketWaiter.wait()) {
        emit errorOccurred(QString("SSH connection established, but redis connection failed"));
        return false;
    }

    emit connected();
    emit logEvent(QString("%1 > reconnected").arg(m_connection->getConfig().name()));

    return true;
}

void RedisClient::SshTransporter::OnSshConnectionError(QxtSshClient::Error error)
{
    if (!m_isHostKeyAlreadyAdded && QxtSshClient::HostKeyUnknownError == error) {
        QxtSshKey hostKey = m_sshClient->hostKey();
        ConnectionConfig config = m_connection->getConfig();

        m_sshClient->addKnownHost(config.sshHost(), hostKey);
        m_sshClient->resetState();
        m_sshClient->connectToHost(config.sshUser(), config.sshHost(), config.sshPort());

        m_isHostKeyAlreadyAdded = true;

        return;
    }

    emit errorOccurred(QString("SSH Connection error: %1").arg(getSshErrorString(error)));
}

void RedisClient::SshTransporter::OnSshSocketDestroyed()
{
    m_socket = nullptr;
    emit logEvent("SSH socket detroyed");
}

void RedisClient::SshTransporter::reconnect()
{
    emit logEvent("Reconnect to host");
    m_loopTimer->stop();

    if (openTcpSocket()) {
        resetDbIndex();
        m_loopTimer->start();
    }
}
