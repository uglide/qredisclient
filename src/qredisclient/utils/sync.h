#pragma once
#include <QObject>
#include <QEventLoop>
#include <QTimer>

#include <qredisclient/response.h>

/**
 * Utilitary functions and classes for internal implementation.
 */
namespace RedisClient {

class Command;
class Connection;

class Executor : public QObject
{
    Q_OBJECT
    friend class Connection;
private:
    Executor(Command& cmd);
    Response waitForResult(unsigned int);
    Response m_result;
    QEventLoop m_loop;
    QTimer m_timeoutTimer;
};
}
