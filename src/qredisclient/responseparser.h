#pragma once
#include <QByteArray>
#include <QSharedPointer>
#include <QVariant>
#include <QVector>

struct redisReader;
struct redisReadTask;
struct redisReplyObjectFunctions;

namespace RedisClient {

class Response;

class ResponseParser {
 public:
  ResponseParser();

  QByteArray buffer() const;
  bool feedBuffer(const QByteArray &);
  bool hasUnusedBuffer() const;
  QByteArray unusedBuffer();
  Response getNextResponse();
  void reset();

 protected:
  QSharedPointer<redisReader> m_redisReader;
  QByteArray m_buffer;

 private:
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
