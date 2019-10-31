#include "sync.h"
#include "qredisclient/command.h"

RedisClient::SignalWaiter::SignalWaiter(uint timeout)
    : m_result(false), m_resultReceived(false) {
  m_timeoutTimer.setSingleShot(true);
  m_timeoutTimer.setInterval(timeout);

  connect(&m_timeoutTimer, SIGNAL(timeout()), &m_loop, SLOT(quit()));
  if (QCoreApplication::instance()) {
    connect(QCoreApplication::instance(),
            SIGNAL(aboutToQuit()), this, SLOT(abort()));
  }
}

bool RedisClient::SignalWaiter::wait() {
  if (m_resultReceived) {  // result recieved before wait() call
    return m_result;
  }

  m_timeoutTimer.start();
  m_loop.exec();
  return m_result;
}

void RedisClient::SignalWaiter::abort() {
  m_result = false;
  m_resultReceived = true;
  m_loop.quit();
  emit aborted();
}

void RedisClient::SignalWaiter::success() {
  m_result = true;
  m_resultReceived = true;
  m_loop.quit();
  emit succeed();
}
