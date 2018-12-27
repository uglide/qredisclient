#pragma once

#include <QMutex>
#include <QSslSocket>
#include "abstracttransporter.h"

namespace RedisClient {

/**
 * @brief The DefaultTransporter class
 * Provides execution of redis commands through direct TCP socket.
 * Supports SSL.
 */
class DefaultTransporter : public AbstractTransporter {
  Q_OBJECT
 public:
  DefaultTransporter(Connection* c);
  ~DefaultTransporter() override;

 public slots:
  void disconnectFromHost() override;

 protected:
  bool isInitialized() const override;
  bool isSocketReconnectRequired() const override;
  bool canReadFromSocket() override;
  QByteArray readFromSocket() override;
  void initSocket() override;
  bool connectToHost() override;
  void sendCommand(const QByteArray& cmd) override;

 protected slots:
  void reconnect() override;

 private slots:
  void error(QAbstractSocket::SocketError error);
  void sslError(const QList<QSslError>& errors);

 protected:
  QSharedPointer<QSslSocket> m_socket;
  QMutex m_disconnectLock;
  bool m_errorOccurred;
};
}  // namespace RedisClient
