#include <QObject>
#include "qredisclient/command.h"
#include "qredisclient/response.h"

namespace RedisClient {

/**
 * @brief The ResponseEmitter class
 * Class used to send responses to callers.
 * THIS IS IMPLEMENTATION CLASS AND SHOULDN'T BE USED DIRECTLY.
 */
class ResponseEmitter : public QObject {
  Q_OBJECT
 public:
  ResponseEmitter(QObject *owner, Command::Callback callback) : owner(owner) {
    QObject::connect(this, &ResponseEmitter::response, owner, callback,
                     Qt::AutoConnection);
  }

  void sendResponse(const Response &r, const QString &err) {
    emit response(r, err);
  }
  QObject *owner;
 signals:
  void response(Response, QString);
};

}  // namespace RedisClient
