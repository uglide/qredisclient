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
    Response waitForResult(unsigned int, QString &err);
    Response m_result;
    QEventLoop m_loop;
    QTimer m_timeoutTimer;
    QString m_error;
};

class SignalWaiter : public QObject
{
    Q_OBJECT
public:
    SignalWaiter(uint timeout);
    bool wait();

    template <typename Func1>
    void addAbortSignal(const typename QtPrivate::FunctionPointer<Func1>::Object *sender, Func1 signal)
    {
        connect(sender, signal, this, &SignalWaiter::abort);
    }

    template <typename Func1>
    void addSuccessSignal(const typename QtPrivate::FunctionPointer<Func1>::Object *sender, Func1 signal)
    {
        connect(sender, signal, this, &SignalWaiter::success);
    }

signals:
    void succeed();
    void aborted();

protected slots:
    void abort();
    void success();
private:
    QEventLoop m_loop;
    QTimer m_timeoutTimer;
    bool m_result;
    bool m_resultReceived;
};
}
