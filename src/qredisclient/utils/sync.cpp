#include "sync.h"
#include "qredisclient/command.h"

RedisClient::SignalWaiter::SignalWaiter(uint timeout)
    : m_result(false), m_resultReceived(false)
{
    m_timeoutTimer.setSingleShot(true);
    m_timeoutTimer.setInterval(timeout);

    connect(&m_timeoutTimer, SIGNAL(timeout()), &m_loop, SLOT(quit()));
}

bool RedisClient::SignalWaiter::wait()
{
    if (m_resultReceived) { // result recieved before wait() call
        return m_result;
    }

    m_timeoutTimer.start();
    m_loop.exec();
    return m_result;
}

void RedisClient::SignalWaiter::abort()
{
    m_result=false;
    m_resultReceived = true;
    m_loop.quit();
    emit aborted();
}

void RedisClient::SignalWaiter::success()
{
    m_result=true;
    m_resultReceived = true;
    m_loop.quit();
    emit succeed();
}

RedisClient::CommandExecutor::CommandExecutor(RedisClient::Command &cmd, uint timeout)
    : RedisClient::SignalWaiter(timeout)
{
    cmd.setCallBack(this, [this](Response r, QString error) {
        if (error.isEmpty()) {
            m_result = r;
        } else {
            m_error = error;
        }
        m_resultReceived = true;

        if (m_loop.isRunning()) {
            m_loop.exit();
        }
    });
}

RedisClient::Response RedisClient::CommandExecutor::waitResult()
{
    if (m_resultReceived) { // result recieved before wait() call
        return m_result;
    }

    m_timeoutTimer.start();
    m_loop.exec();

    if (!m_error.isEmpty())
        throw Exception(m_error);
    else if (m_result.isEmpty())
        throw Exception(tr("Command execution timeout"));

    return m_result;
}
