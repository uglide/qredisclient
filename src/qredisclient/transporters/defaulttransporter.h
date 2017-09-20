#pragma once

#include "abstracttransporter.h"
#include <QSslSocket>
#include <QMutex>

namespace RedisClient {

/**
 * @brief The DefaultTransporter class
 * Provides execution of redis commands through direct TCP socket.
 * Supports SSL.
 */
class DefaultTransporter : public AbstractTransporter
{
    Q_OBJECT
public:
    DefaultTransporter(Connection * c);    

public slots:    
    void disconnectFromHost();

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
    void sslError(const QList<QSslError> &errors);

private:
    QSharedPointer<QSslSocket> m_socket;
    QMutex m_disconnectLock;
    bool m_errorOccurred;
};
}
