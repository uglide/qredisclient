#include "responseparser.h"
#include <hiredis/read.h>
#include <QDebug>
#include "private/parsedresponse.h"
#include "response.h"

RedisClient::ResponseParser::ResponseParser()
    : m_redisReader(
          QSharedPointer<redisReader>(redisReaderCreate(), redisReaderFree)) {}

QByteArray RedisClient::ResponseParser::buffer() const {
  return QByteArray(m_redisReader.data()->buf, m_redisReader.data()->len);
}

bool RedisClient::ResponseParser::feedBuffer(const QByteArray& b) {
  if (redisReaderFeed(m_redisReader.data(), b.constData(), b.size()) ==
      REDIS_ERR) {
    qDebug() << "hiredis cannot feed buffer, error:"
             << m_redisReader.data()->err;
    qDebug() << "current buffer:"
             << QByteArray::fromRawData(m_redisReader.data()->buf,
                                        m_redisReader.data()->len);
    return false;
  }

  return true;
}

RedisClient::Response RedisClient::ResponseParser::getNextResponse() {
  if (!hasUnusedBuffer()) return Response();

  ParsingResult* replyPtr = nullptr;

  if (redisReaderGetReply(m_redisReader.data(), (void**)&replyPtr) ==
      REDIS_ERR) {
    qDebug() << "hiredis cannot parse buffer" << m_redisReader.data()->errstr;
    //    qDebug() << "current buffer:"
    //             << QByteArray::fromRawData(m_redisReader.data()->buf,
    //                                        m_redisReader.data()->len);
    //    qDebug() << "all buffer:" << m_responseSource;

    if (replyPtr) delete replyPtr;

    return RedisClient::Response();
  }

  if (!replyPtr) return RedisClient::Response();

  QScopedPointer<ParsingResult> reply(replyPtr);

  return reply->toResponse();
}

void RedisClient::ResponseParser::reset() {
  m_buffer.clear();
  m_redisReader =
      QSharedPointer<redisReader>(redisReaderCreate(), redisReaderFree);
}

bool RedisClient::ResponseParser::hasUnusedBuffer() const {
  return m_redisReader->pos != m_redisReader->len;
}

QByteArray RedisClient::ResponseParser::unusedBuffer() {
  if (!hasUnusedBuffer()) return QByteArray();

  return QByteArray(m_redisReader->buf + m_redisReader->pos,
                    m_redisReader->len - m_redisReader->pos);
}

/***
 * Parsing
 **/

const redisReplyObjectFunctions RedisClient::ResponseParser::defaultFunctions =
    {RedisClient::ResponseParser::createStringObject,
     RedisClient::ResponseParser::createArrayObject,
     RedisClient::ResponseParser::createIntegerObject,
     RedisClient::ResponseParser::createDoubleObject,
     RedisClient::ResponseParser::createNilObject,
     RedisClient::ResponseParser::createBoolObject,
     RedisClient::ResponseParser::freeObject};

redisReader* RedisClient::ResponseParser::redisReaderCreate() {
  return redisReaderCreateWithFunctions(
      const_cast<redisReplyObjectFunctions*>(&defaultFunctions));
}

void setParent(const redisReadTask* task, RedisClient::ParsingResult* r) {
  auto parent = (RedisClient::ParsingResult*)task->parent->obj;

  Q_ASSERT(parent);
  Q_ASSERT(task->idx < parent->array.size());

  parent->array[task->idx] = r;
}

void* RedisClient::ResponseParser::createStringObject(const redisReadTask* task,
                                                      char* str, size_t len) {
  ParsingResult* s =
      new ParsingResult(task->type, QVariant(QByteArray(str, len)));

  if (task->parent) setParent(task, s);

  return s;
}

void* RedisClient::ResponseParser::createArrayObject(const redisReadTask* task,
                                                     size_t elements) {
  ParsingResult* arr = new ParsingResult(elements);

  if (task->parent) setParent(task, arr);

  return arr;
}

void* RedisClient::ResponseParser::createIntegerObject(
    const redisReadTask* task, long long value) {
  ParsingResult* val = new ParsingResult(task->type, QVariant(value));

  if (task->parent) setParent(task, val);

  return val;
}

void *RedisClient::ResponseParser::createDoubleObject(const redisReadTask *task, double value, char *, size_t)
{
    ParsingResult* val = new ParsingResult(task->type, QVariant(value));

    if (task->parent) setParent(task, val);

    return val;
}

void* RedisClient::ResponseParser::createNilObject(const redisReadTask* task) {
  ParsingResult* nil = new ParsingResult(task->type, QVariant());

  if (task->parent) setParent(task, nil);

  return nil;
}

void *RedisClient::ResponseParser::createBoolObject(const redisReadTask *task, int value)
{
    ParsingResult* val = new ParsingResult(task->type, QVariant(static_cast<bool>(value)));

    if (task->parent) setParent(task, val);

    return val;
}

void RedisClient::ResponseParser::freeObject(void* obj) {
  if (obj == nullptr) return;

  ParsingResult* o = (ParsingResult*)obj;

  for (int index = 0; index < o->array.size(); ++index) {
    freeObject((void*)o->array[index]);
  }

  delete o;
}
