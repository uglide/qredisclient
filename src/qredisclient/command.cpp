#include "command.h"

#include <QSet>
extern "C" {
#include <crc16.h>
}
#include "qredisclient/utils/compat.h"
#include "qredisclient/utils/text.h"

RedisClient::Command::Command()
    : m_owner(nullptr),
      m_commandWithArguments(),
      m_dbIndex(-1),
      m_hiPriorityCommand(false),
      m_isPipeline(false) {}

RedisClient::Command::Command(const QList<QByteArray> &cmd, int db)
    : m_owner(nullptr),
      m_commandWithArguments(cmd),
      m_dbIndex(db),
      m_hiPriorityCommand(false),
      m_isPipeline(false) {}

RedisClient::Command::Command(const QList<QByteArray> &cmd, QObject *context,
                              Callback callback, int db)
    : m_owner(context),
      m_commandWithArguments(cmd),
      m_dbIndex(db),
      m_hiPriorityCommand(false),
      m_isPipeline(false),
      m_callback(callback) {}

RedisClient::Command::~Command() {}

RedisClient::Command &RedisClient::Command::append(const QByteArray &part) {
  if (!m_isPipeline)
    m_commandWithArguments.append(part);
  else
    m_pipelineCommands.last().append(part);
  return *this;
}

RedisClient::Command &RedisClient::Command::addToPipeline(
    const QList<QByteArray> cmd) {
  if (!m_isPipeline) {
    // Convert and use existing command arguments if there any
    if (!isEmpty()) m_pipelineCommands.append(m_commandWithArguments);
    m_isPipeline = true;
  }
  m_pipelineCommands.append(cmd);
  return *this;
}

int RedisClient::Command::length() const {
  if (!m_isPipeline)
    return m_commandWithArguments.length();
  else
    return m_pipelineCommands.length();
}

QList<QByteArray> RedisClient::Command::splitCommandString(
    const QString &rawCommand) {
  QList<QByteArray> parts;
  int i = 0;
  bool inQuote = false;
  QByteArray part;
  QSet<QChar> delimiters;
  delimiters << QChar('"') << QChar('\'');
  QChar currentDelimiter = '\0';

  QByteArray command = printableStringToBinary(rawCommand);

  while (i < command.length()) {
    if (QChar(command.at(i)).isSpace() && !inQuote) {
      if (part.length() > 0) parts.append(part);
      part = QByteArray();
    } else if (delimiters.contains(command.at(i)) &&
               (!inQuote || currentDelimiter == command.at(i))) {
      if (i > 0 && command.at(i - 1) == '\\') {
        part.remove(part.size() - 1, 1);
        part.append(command.at(i++));
        continue;
      }

      if (inQuote) {
        parts.append(part);
        currentDelimiter = '\0';
      } else {
        currentDelimiter = command.at(i);
      }

      part = QByteArray();
      inQuote = !inQuote;
    } else {
      part.append(command.at(i));
    }
    ++i;
  }
  if (parts.length() < 1 || part.length() > 0) parts.append(part);
  return parts;
}

quint16 RedisClient::Command::calcKeyHashSlot(const QByteArray &k) {
  QByteArray key(k);

  int start = key.indexOf('{');
  if (start != -1) {
    int end = key.indexOf('}', start + 1);
    if (end != -1 && end != start + 1) {
      key = key.mid(start + 1, end - start - 1);
    }
  }

  return crc16(key.constData(), key.size()) & 16383;
}

bool RedisClient::Command::hasCallback() const { return (bool)m_callback; }

AsyncFuture::Deferred<RedisClient::Response> RedisClient::Command::getDeferred()
    const {
  return m_deferred;
}

void RedisClient::Command::setCallBack(QObject *context, Callback callback) {
  m_owner = context;
  m_callback = callback;
}

RedisClient::Command::Callback RedisClient::Command::getCallBack() const {
  return m_callback;
}

bool RedisClient::Command::hasDbIndex() const { return m_dbIndex >= 0; }

bool RedisClient::Command::isSelectCommand() const {
  if (m_commandWithArguments.length() < 2) return false;

  return m_commandWithArguments.at(0).toLower() == "select";
}

bool RedisClient::Command::isSubscriptionCommand() const {
  if (m_commandWithArguments.length() < 2) return false;

  return m_commandWithArguments.at(0).toLower() == "subscribe" ||
         m_commandWithArguments.at(0).toLower() == "psubscribe";
}

bool RedisClient::Command::isUnSubscriptionCommand() const {
  if (m_commandWithArguments.length() < 2) return false;

  return m_commandWithArguments.at(0).toLower() == "unsubscribe" ||
         m_commandWithArguments.at(0).toLower() == "punsubscribe";
}

bool RedisClient::Command::isAuthCommand() const {
  if (m_commandWithArguments.length() < 2) return false;

  return m_commandWithArguments.at(0).toLower() == "auth";
}

bool RedisClient::Command::isHiPriorityCommand() const {
  return m_hiPriorityCommand;
}

bool RedisClient::Command::isPipelineCommand() const { return m_isPipeline; }

void RedisClient::Command::setPipelineCommand(const bool enable) {
  m_isPipeline = enable;
}

int RedisClient::Command::getDbIndex() const {
  if (isSelectCommand()) {
    return m_commandWithArguments.at(1).toInt();
  }
  return m_dbIndex;
}

QObject *RedisClient::Command::getOwner() const { return m_owner; }

QByteArray RedisClient::Command::getRawString(int limit) const {
  if (isAuthCommand()) return QByteArray("AUTH *******");

  return (limit > 0) ? m_commandWithArguments.join(' ').left(limit)
                     : m_commandWithArguments.join(' ');
}

QList<QByteArray> RedisClient::Command::getSplitedRepresentattion() const {
  return m_commandWithArguments;
}

QString RedisClient::Command::getPartAsString(int i) const {
  if (m_commandWithArguments.size() <= i) return QString();

  return QString::fromUtf8(m_commandWithArguments.at(i));
}

quint16 RedisClient::Command::getHashSlot() const {
  return calcKeyHashSlot(getKeyName());
}

static QHash<QByteArray, int> cmdKeyMapping = {
    {"APPEND", 0},
    {"BITCOUNT", 0},
    {"BITFIELD", 0},
    {"BITOP", 1},
    {"BITOP", 2},
    {"BITPOS", 0},
    {"BLPOP", 0},
    {"BRPOP", 0},
    {"BRPOPLPUSH", 0},
    {"BRPOPLPUSH", 1},
    {"BZPOPMIN", 0},
    {"BZPOPMAX", 0},
    {"DEBUG OBJECT", 0},
    {"DECR", 0},
    {"DECRBY", 0},
    {"DEL", 0},
    {"DUMP", 0},
    {"EVAL", 2},
    {"EVALSHA", 2},
    {"EXISTS", 0},
    {"EXPIRE", 0},
    {"EXPIREAT", 0},
    {"GEOADD", 0},
    {"GEOHASH", 0},
    {"GEOPOS", 0},
    {"GEODIST", 0},
    {"GEORADIUS", 0},
    {"GEORADIUS", 10},
    {"GEORADIUS", 11},
    {"GEORADIUSBYMEMBER", 0},
    {"GEORADIUSBYMEMBER", 9},
    {"GEORADIUSBYMEMBER", 10},
    {"GET", 0},
    {"GETBIT", 0},
    {"GETRANGE", 0},
    {"GETSET", 0},
    {"HDEL", 0},
    {"HEXISTS", 0},
    {"HGET", 0},
    {"HGETALL", 0},
    {"HINCRBY", 0},
    {"HINCRBYFLOAT", 0},
    {"HKEYS", 0},
    {"HLEN", 0},
    {"HMGET", 0},
    {"HMSET", 0},
    {"HSET", 0},
    {"HSETNX", 0},
    {"HSTRLEN", 0},
    {"HVALS", 0},
    {"INCR", 0},
    {"INCRBY", 0},
    {"INCRBYFLOAT", 0},
    {"LINDEX", 0},
    {"LINSERT", 0},
    {"LLEN", 0},
    {"LPOP", 0},
    {"LPUSH", 0},
    {"LPUSHX", 0},
    {"LRANGE", 0},
    {"LREM", 0},
    {"LSET", 0},
    {"LTRIM", 0},
    {"MEMORY USAGE", 0},
    {"MGET", 0},
    {"MIGRATE", 8},
    {"MOVE", 0},
    {"PERSIST", 0},
    {"PEXPIRE", 0},
    {"PEXPIREAT", 0},
    {"PFADD", 0},
    {"PFCOUNT", 0},
    {"PFMERGE", 0},
    {"PFMERGE", 1},
    {"PSETEX", 0},
    {"PTTL", 0},
    {"RENAME", 0},
    {"RENAME", 1},
    {"RENAMENX", 0},
    {"RENAMENX", 1},
    {"RESTORE", 0},
    {"RPOP", 0},
    {"RPOPLPUSH", 0},
    {"RPOPLPUSH", 1},
    {"RPUSH", 0},
    {"RPUSHX", 0},
    {"SADD", 0},
    {"SCARD", 0},
    {"SDIFF", 0},
    {"SDIFFSTORE", 0},
    {"SDIFFSTORE", 1},
    {"SET", 0},
    {"SETBIT", 0},
    {"SETEX", 0},
    {"SETNX", 0},
    {"SETRANGE", 0},
    {"SINTER", 0},
    {"SINTERSTORE", 0},
    {"SINTERSTORE", 1},
    {"SISMEMBER", 0},
    {"SMEMBERS", 0},
    {"SMOVE", 0},
    {"SMOVE", 1},
    {"SORT", 0},
    {"SORT", 6},
    {"SPOP", 0},
    {"SRANDMEMBER", 0},
    {"SREM", 0},
    {"STRLEN", 0},
    {"SUNION", 0},
    {"SUNIONSTORE", 0},
    {"SUNIONSTORE", 1},
    {"TOUCH", 0},
    {"TTL", 0},
    {"TYPE", 0},
    {"UNLINK", 0},
    {"WATCH", 0},
    {"ZADD", 0},
    {"ZCARD", 0},
    {"ZCOUNT", 0},
    {"ZINCRBY", 0},
    {"ZINTERSTORE", 0},
    {"ZINTERSTORE", 2},
    {"ZLEXCOUNT", 0},
    {"ZPOPMAX", 0},
    {"ZPOPMIN", 0},
    {"ZRANGE", 0},
    {"ZRANGEBYLEX", 0},
    {"ZREVRANGEBYLEX", 0},
    {"ZRANGEBYSCORE", 0},
    {"ZRANK", 0},
    {"ZREM", 0},
    {"ZREMRANGEBYLEX", 0},
    {"ZREMRANGEBYRANK", 0},
    {"ZREMRANGEBYSCORE", 0},
    {"ZREVRANGE", 0},
    {"ZREVRANGEBYSCORE", 0},
    {"ZREVRANK", 0},
    {"ZSCORE", 0},
    {"ZUNIONSTORE", 0},
    {"ZUNIONSTORE", 2},
    {"SSCAN", 0},
    {"HSCAN", 0},
    {"ZSCAN", 0},
    {"XINFO", 1},
    {"XINFO", 2},
    {"XADD", 0},
    {"XTRIM", 0},
    {"XDEL", 0},
    {"XRANGE", 0},
    {"XREVRANGE", 0},
    {"XLEN", 0},
    {"XREAD", 3},
    {"XREADGROUP", 5},
    {"XACK", 0},
    {"XCLAIM", 0},
    {"XPENDING", 0},
};

QByteArray RedisClient::Command::getKeyName() const {
  QList<QByteArray> cmd;

  if (m_isPipeline) {
    cmd = m_pipelineCommands.first();
  } else {
    cmd = m_commandWithArguments;
  }

  if (cmd.size() < 2) return QByteArray();

  QByteArray commandName{cmd.at(0).toUpper()};

  int pos = 1;

  if (!cmdKeyMapping.contains(commandName)) {
    if (cmd.size() < 3) return QByteArray();

    commandName = QString("%1 %2")
                      .arg(QString(commandName))
                      .arg(QString(cmd.at(1).toUpper()))
                      .toUtf8();

    pos += 1;

    if (!cmdKeyMapping.contains(commandName)) return QByteArray();
  }

  pos += cmdKeyMapping[commandName];

  if (pos >= cmd.size()) return QByteArray();

  return cmd.at(pos);
}

bool RedisClient::Command::isEmpty() const {
  if (!m_isPipeline)
    return m_commandWithArguments.isEmpty();
  else
    return m_pipelineCommands.isEmpty();
}

QByteArray RedisClient::Command::getByteRepresentation() const {
  if (!m_isPipeline)
    return serializeToRESP(m_commandWithArguments);
  else {
    QByteArray result = serializeToRESP({"MULTI"});
    QList<QByteArray> pipelineCmd;
    foreach (pipelineCmd, m_pipelineCommands)
      result.append(serializeToRESP(pipelineCmd));
    result.append(serializeToRESP({"EXEC"}));
    return result;
  }
}

void RedisClient::Command::markAsHiPriorityCommand() {
  m_hiPriorityCommand = true;
}

bool RedisClient::Command::isValid() const { return !isEmpty(); }

QByteArray RedisClient::Command::serializeToRESP(QList<QByteArray> args) const {
  QByteArray result;
  result.append(QString("*%1\r\n").arg(args.length()));

  for (QByteArray partArray : args) {
    result.append("$");
    result.append(QString::number(partArray.size()));
    result.append("\r\n");
    result.append(partArray);
    result.append("\r\n");
  }

  return result;
}
