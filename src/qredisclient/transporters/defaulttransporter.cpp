#include "defaulttransporter.h"
#include "qredisclient/connection.h"
#include "qredisclient/connectionconfig.h"
#include "qredisclient/utils/sync.h"

#include <QSslConfiguration>
#include <QNetworkProxy>

RedisClient::DefaultTransporter::DefaultTransporter(RedisClient::Connection *c)
    : RedisClient::AbstractTransporter(c),
      m_socket(nullptr),
      m_errorOccurred(false) {}

RedisClient::DefaultTransporter::~DefaultTransporter() {
    disconnectFromHost();
}

void RedisClient::DefaultTransporter::initSocket() {
  using namespace RedisClient;

  m_socket = QSharedPointer<QSslSocket>(new QSslSocket());
  m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);  

  if (!validateSystemProxy()) {
    m_socket->setProxy(QNetworkProxy::NoProxy);
  }

  connect(
      m_socket.data(),
      (void (QSslSocket::*)(QAbstractSocket::SocketError)) & QSslSocket::error,
      this, &DefaultTransporter::error);
  connect(m_socket.data(),
          (void (QSslSocket::*)(const QList<QSslError> &errors)) &
              QSslSocket::sslErrors,
          this, &DefaultTransporter::sslError);
  connect(m_socket.data(), &QAbstractSocket::readyRead, this,
          &AbstractTransporter::readyRead);
  connect(m_socket.data(), &QSslSocket::encrypted, this,
          [this]() { emit logEvent("SSL encryption: OK"); });
  connect(m_socket.data(), &QAbstractSocket::disconnected, this, [this]() {
    if (m_runningCommands.size() > 0) {
      emit errorOccurred("Connection was interrupted");
    }
  });
}

void RedisClient::DefaultTransporter::disconnectFromHost() {
  QMutexLocker lock(&m_disconnectLock);

  RedisClient::AbstractTransporter::disconnectFromHost();

  if (m_socket.isNull()) return;

  m_socket->abort();
  m_socket.clear();
}

bool RedisClient::DefaultTransporter::isInitialized() const {
  return !m_socket.isNull();
}

bool RedisClient::DefaultTransporter::isSocketReconnectRequired() const {
  return m_socket && m_socket->state() == QAbstractSocket::UnconnectedState;
}

bool RedisClient::DefaultTransporter::canReadFromSocket() {
  return m_socket->bytesAvailable() > 0;
}

QByteArray RedisClient::DefaultTransporter::readFromSocket() {
  return m_socket->readAll();
}

bool RedisClient::DefaultTransporter::connectToHost() {
  m_errorOccurred = false;

  auto conf = m_connection->getConfig();

  bool connectionResult = false;

  if (conf.useSsl()) {
    if (!QSslSocket::supportsSsl()) {
      emit errorOccurred(
          QString("SSL Error: Openssl is missing. Please install Openssl (%1)")
              .arg(QSslSocket::sslLibraryBuildVersionString()));
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

    SignalWaiter socketWaiter(conf.connectionTimeout());
    socketWaiter.addAbortSignal(
        m_socket.data(),
        static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(
            &QAbstractSocket::error));
    socketWaiter.addAbortSignal(m_connection, &RedisClient::Connection::shutdownStart);
    socketWaiter.addAbortSignal(m_socket.data(),
                                &QAbstractSocket::disconnected);
    socketWaiter.addSuccessSignal(m_socket.data(), &QSslSocket::encrypted);

    m_socket->connectToHostEncrypted(conf.host(), conf.port());
    connectionResult = socketWaiter.wait();
  } else {
    SignalWaiter socketWaiter(conf.connectionTimeout());
    socketWaiter.addAbortSignal(
        m_socket.data(),
        static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(
            &QAbstractSocket::error));
    socketWaiter.addAbortSignal(this, &QObject::destroyed);
    socketWaiter.addAbortSignal(m_socket.data(),
                                &QAbstractSocket::disconnected);
    socketWaiter.addSuccessSignal(m_socket.data(), &QAbstractSocket::connected);

    m_socket->connectToHost(conf.host(), conf.port());
    connectionResult = socketWaiter.wait();
  }

  if (connectionResult) {
    emit connected();
    emit logEvent(QString("%1 > connected").arg(conf.name()));
    return true;
  }

  if (!m_errorOccurred) emit errorOccurred("Connection timeout");

  emit logEvent(QString("%1 > connection failed").arg(conf.name()));
  return false;
}

void RedisClient::DefaultTransporter::sendCommand(const QByteArray &cmd) {
  QByteArray command = cmd;
  char *data = command.data();
  qint64 total = 0;
  qint64 sent;

  while (total < cmd.size()) {
    sent = m_socket->write(data + total, cmd.size() - total);
    total += sent;
  }

  if (m_socket->bytesToWrite() > 1000 || m_commands.size() == 0)
    m_socket->flush();
}

void RedisClient::DefaultTransporter::error(
    QAbstractSocket::SocketError error) {
  if (error == QAbstractSocket::UnknownSocketError &&
      m_runningCommands.size() > 0) {

      if (isSocketReconnectRequired()) {
        reAddRunningCommandToQueue();
        return processCommandQueue();
      }
  }

  m_errorOccurred = true;

  emit errorOccurred(
      QString("Connection error: %1").arg(m_socket->errorString()));
}

void RedisClient::DefaultTransporter::sslError(const QList<QSslError> &errors) {
  if (errors.size() == 1 &&
      errors.first().error() == QSslError::HostNameMismatch) {
    m_socket->ignoreSslErrors();
    emit logEvent("SSL: Ignore HostName Mismatch");
    return;
  }

  QString allErrors;

  for (QSslError err : errors)
      allErrors.append(QString("SSL error: %1\n").arg(err.errorString()));

  if (m_connection->getConfig().ignoreAllSslErrors()) {
      m_socket->ignoreSslErrors();
      emit logEvent(QString("SSL: Ignoring SSL errors:\n %1").arg(allErrors));
      return;
  }

  m_errorOccurred = true;
  emit errorOccurred(QString("SSL errors:\n %1").arg(allErrors));
}

void RedisClient::DefaultTransporter::reconnect() {
  m_socket->abort();

  if (connectToHost()) {
    resetDbIndex();
  }
}
