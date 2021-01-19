#pragma once
#include <QByteArray>
#include <QSharedPointer>
#include <QString>
#include <QVariant>
#include <QVector>

#include "exception.h"

struct redisReader;
struct redisReadTask;
struct redisReplyObjectFunctions;

namespace RedisClient {
class Response {
  ADD_EXCEPTION

 public:
  enum Type { String = 1, Array, Integer, Nil, Status, Error, Unknown };

 public:
  Response();
  Response(Response::Type, const QVariant &);

  virtual ~Response(void);

  QVariant value() const;
  Type type() const;

  bool isEmpty() const;
  bool isErrorMessage() const;
  bool isErrorStateMessage() const;
  bool isProtocolErrorMessage() const;
  bool isDisabledCommandErrorMessage() const;
  bool isPermissionError() const;
  bool isWrongPasswordError() const;
  bool isOkMessage() const;
  bool isQueuedMessage() const;
  bool isValid();
  bool isMessage() const;
  bool isArray() const;

  // Scan response
  bool isValidScanResponse() const;
  long long getCursor();
  QVariantList getCollection();

  // Pub/Sub support
  QByteArray getChannel() const;

  // Cluster support
  bool isAskRedirect() const;
  bool isMovedRedirect() const;
  QByteArray getRedirectionHost() const;
  uint getRedirectionPort() const;

  static QString valueToHumanReadString(const QVariant &, int indentLevel = 0);

 protected:
  Type m_type;
  QVariant m_result;
};
}  // namespace RedisClient
