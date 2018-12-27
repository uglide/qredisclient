#pragma once
#include <QEventLoop>
#include <QObject>
#include <QTimer>

#include <asyncfuture.h>
#include <qredisclient/response.h>

/**
 * Utilitary functions and classes for internal implementation.
 */
namespace RedisClient {

class Command;

class SignalWaiter : public QObject {
  Q_OBJECT
 public:
  SignalWaiter(uint timeout);

  bool wait();

  template <typename Func1>
  void addAbortSignal(
      const typename QtPrivate::FunctionPointer<Func1>::Object *sender,
      Func1 signal) {
    connect(sender, signal, this, &SignalWaiter::abort);
  }

  template <typename Func1>
  void addSuccessSignal(
      const typename QtPrivate::FunctionPointer<Func1>::Object *sender,
      Func1 signal) {
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

}  // namespace RedisClient
