#pragma once
#include <QObject>
#include <QtCore>
#include "basetestcase.h"
#include "qredisclient/connectionconfig.h"


class TestSsh : public BaseTestCase
{
    Q_OBJECT

private slots:
    void init();

#ifdef SSH_TESTS
    void connectWithSshTunnelPass();

    void connectWithSshTunnelKey();
    void connectWithSshTunnelKey_data();

    void connectAndCheckTimeout();
#endif
private:
    RedisClient::ConnectionConfig config;
};

