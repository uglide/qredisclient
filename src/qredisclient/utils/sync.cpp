#include "sync.h"
#include "qredisclient/command.h"
#include <QDebug>

RedisClient::Executor::Executor(RedisClient::Command &cmd)
{
    cmd.setCallBack(this, [this](Response r, QString error) {
        m_result = r;
        m_loop.exit();
    });

    m_timeoutTimer.setSingleShot(true);
    connect(&m_timeoutTimer, SIGNAL(timeout()), &m_loop, SLOT(quit()));
}

RedisClient::Response RedisClient::Executor::waitForResult(unsigned int timeoutInMs)
{
    if (m_result.isValid())
        return m_result; // NOTE(u_glide): No need to wait if result already fetched

    m_timeoutTimer.start(timeoutInMs);
    m_loop.exec();
    return m_result;
}


RedisClient::SignalWaiter::SignalWaiter(uint timeout)
    : m_result(false)
{
    m_timeoutTimer.setSingleShot(true);
    m_timeoutTimer.setInterval(timeout);

    connect(&m_timeoutTimer, SIGNAL(timeout()), &m_loop, SLOT(quit()));
}

bool RedisClient::SignalWaiter::wait()
{
    m_timeoutTimer.start();
    m_loop.exec();
    return m_result;
}

void RedisClient::SignalWaiter::abort()
{
    m_result=false;
    m_loop.quit();
}

void RedisClient::SignalWaiter::success()
{
    m_result=true;
    m_loop.quit();
}
