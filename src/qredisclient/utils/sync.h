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
protected:
    QEventLoop m_loop;
    QTimer m_timeoutTimer;   
    bool m_resultReceived;
private:
    bool m_result;
};

class CommandExecutor : public SignalWaiter
{
    Q_OBJECT
    ADD_EXCEPTION
public:
    CommandExecutor(Command& cmd, uint timeout);

    Response waitResult();

private:
    bool wait() { return false; }
private:
    Response m_result;
    QString m_error;
};

}
