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
  enum Type { Status, Error, Integer, Bulk, MultiBulk, Unknown };

 public:
  Response();
  Response(const QByteArray &);
  // Response(const Response &);
  virtual ~Response(void);

  QVariant getValue();
  Type getType() const;
  QByteArray source() const;
  QString toRawString(long limit = 1500) const;

  bool isEmpty() const;
  bool isErrorMessage() const;
  bool isErrorStateMessage() const;
  bool isDisabledCommandErrorMessage() const;
  bool isOkMessage() const;
  bool isQueuedMessage() const;
  bool isValid();
  bool isMessage() const;
  bool isArray() const;
  bool hasUnusedBuffer() const;

  // Pub/Sub support
  QByteArray getChannel() const;

  // Cluster support
  bool isAskRedirect() const;
  bool isMovedRedirect() const;
  QByteArray getRedirectionHost() const;
  uint getRedirectionPort() const;

 public:
  void setSource(const QByteArray &);
  void appendToSource(const QByteArray &);
  QByteArray getUnusedBuffer();
  Response getNextResponse();
  void clearBuffers();
  void reset();

  static QString valueToHumanReadString(const QVariant &, int indentLevel = 0);

 protected:
  Response(const QByteArray &, QSharedPointer<QVariant>);
  Type getResponseType(const QByteArray &) const;
  Type getResponseType(const char) const;

  bool parse();
  QSharedPointer<QVariant> getNextReplyFromBuffer();
  void feed(const QByteArray &buffer);

  long getReaderAbsolutePosition();

 protected:
  QByteArray m_responseSource;
  QSharedPointer<redisReader> m_redisReader;
  QSharedPointer<QVariant> m_result;
  long m_endOfValidResponseInBuffer;

 private:
  /*
   * hiredis custom functions
   */
  static void *createStringObject(const redisReadTask *task, char *str,
                                  size_t len);
  static void *createArrayObject(const redisReadTask *t, int elements);
  static void *createIntegerObject(const redisReadTask *task, long long value);
  static void *createNilObject(const redisReadTask *task);
  static void freeObject(void *obj);

  static const redisReplyObjectFunctions defaultFunctions;

  static redisReader *redisReaderCreate(void);
};
}  // namespace RedisClient

Q_DECLARE_METATYPE(QVector<QVariant *>)
