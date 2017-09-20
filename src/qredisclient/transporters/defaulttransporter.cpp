#include "defaulttransporter.h"
#include "qredisclient/connection.h"
#include "qredisclient/connectionconfig.h"
#include "qredisclient/utils/sync.h"

#include <QSslConfiguration>

RedisClient::DefaultTransporter::DefaultTransporter(RedisClient::Connection *c)
    : RedisClient::AbstractTransporter(c), m_socket(nullptr), m_errorOccurred(false)
{
}

void RedisClient::DefaultTransporter::initSocket()
{
    using namespace RedisClient;

    m_socket = QSharedPointer<QSslSocket>(new QSslSocket());
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

    connect(m_socket.data(), (void (QSslSocket::*)(QAbstractSocket::SocketError))&QSslSocket::error,
            this, &DefaultTransporter::error);
    connect(m_socket.data(), (void (QSslSocket::*)(const QList<QSslError> &errors))&QSslSocket::sslErrors,
            this, &DefaultTransporter::sslError);
    connect(m_socket.data(), &QAbstractSocket::readyRead, this, &AbstractTransporter::readyRead);
    connect(m_socket.data(), &QSslSocket::encrypted, this, [this]() { emit logEvent("SSL encryption: OK"); });
}

void RedisClient::DefaultTransporter::disconnectFromHost()
{
    QMutexLocker lock(&m_disconnectLock);

    if (m_socket.isNull())
        return;

    m_loopTimer->stop();

    m_socket->abort();
    m_socket.clear();
}

bool RedisClient::DefaultTransporter::isInitialized() const
{
    return !m_socket.isNull();
}

bool RedisClient::DefaultTransporter::isSocketReconnectRequired() const
{
    return m_socket && m_socket->state() == QAbstractSocket::UnconnectedState;
}

bool RedisClient::DefaultTransporter::canReadFromSocket()
{
    return m_socket->bytesAvailable() > 0;
}

QByteArray RedisClient::DefaultTransporter::readFromSocket()
{
    return m_socket->readAll();
}

bool RedisClient::DefaultTransporter::connectToHost()
{
    m_errorOccurred = false;

    auto conf = m_connection->getConfig();

    bool connectionResult = false;

    if (conf.useSsl()) {

        if (!QSslSocket::supportsSsl()) {
            emit errorOccurred("SSL Error: Openssl is missing. Please install Openssl.");
            return false;
        }

        m_socket->setSslConfiguration(QSslConfiguration::defaultConfiguration());

        QList<QSslCertificate> trustedCas = conf.sslCaCertificates();

        if (!trustedCas.empty()) {
            m_socket->addCaCertificates(trustedCas);            
        }                

        QString privateKey = conf.sslPrivateKeyPath();
        if (!privateKey.isEmpty()) {
            m_socket->setPrivateKey(privateKey);
        }

        QString localCert = conf.sslLocalCertPath();
        if (!localCert.isEmpty()) {
            m_socket->setLocalCertificate(localCert);
        }

        m_socket->connectToHostEncrypted(conf.host(), conf.port());
        connectionResult = m_socket->waitForEncrypted(conf.connectionTimeout());

    } else {
        m_socket->connectToHost(conf.host(), conf.port());
        connectionResult = m_socket->waitForConnected(conf.connectionTimeout());
    }

    if (connectionResult)
    {
        emit connected();
        emit logEvent(QString("%1 > connected").arg(conf.name()));
        return true;
    }

    if (!m_errorOccurred)
        emit errorOccurred("Connection timeout");

    emit logEvent(QString("%1 > connection failed").arg(conf.name()));
    return false;
}

void RedisClient::DefaultTransporter::sendCommand(const QByteArray& cmd)
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
    m_socket->flush();
}

void RedisClient::DefaultTransporter::error(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::UnknownSocketError
            && connectToHost()
            && m_runningCommands.size() > 0) {
        reAddRunningCommandToQueue();
        return processCommandQueue();
    }

    m_errorOccurred = true;

    emit errorOccurred(
        QString("Connection error: %1").arg(m_socket->errorString())
        );

    if (m_response.isValid())
        return sendResponse(m_response);
}

void RedisClient::DefaultTransporter::sslError(const QList<QSslError> &errors)
{
    if (errors.size() == 1 && errors.first().error() == QSslError::HostNameMismatch) {
        m_socket->ignoreSslErrors();
        emit logEvent("SSL: Ignore HostName Mismatch");
        return;
    }

    m_errorOccurred = true;
    for (QSslError err : errors)
        emit errorOccurred(QString("SSL error: %1").arg(err.errorString()));
}

void RedisClient::DefaultTransporter::reconnect()
{
    emit logEvent("Reconnect to host");

    if (m_loopTimer->isActive())
        m_loopTimer->stop();

    m_socket->abort();    

    if (connectToHost()) {
        resetDbIndex();
        m_loopTimer->start();
    }
}
