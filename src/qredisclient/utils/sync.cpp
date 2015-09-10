#include "sync.h"

#include "qredisclient/command.h"

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
