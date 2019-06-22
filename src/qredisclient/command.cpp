#include "command.h"
#include <QSet>
#include "qredisclient/utils/compat.h"
#include "qredisclient/utils/text.h"


RedisClient::Command::Command()
    : m_owner(nullptr), m_commandWithArguments(), m_dbIndex(-1),
      m_hiPriorityCommand(false), m_isPipeline(false)
{
}

RedisClient::Command::Command(const QList<QByteArray> &cmd, int db)
    : m_owner(nullptr), m_commandWithArguments(cmd), m_dbIndex(db),
      m_hiPriorityCommand(false), m_isPipeline(false)
{
}

RedisClient::Command::Command(const QList<QByteArray> &cmd, QObject *context,
                              Callback callback, int db)
    : m_owner(context), m_commandWithArguments(cmd), m_dbIndex(db),
      m_hiPriorityCommand(false), m_isPipeline(false), m_callback(callback)
{
}

RedisClient::Command::~Command() {}

RedisClient::Command &RedisClient::Command::append(const QByteArray &part) {
  if (!m_isPipeline)
    m_commandWithArguments.append(part);
  else
    m_pipelineCommands.last().append(part);
  return *this;
}

RedisClient::Command &RedisClient::Command::addToPipeline(const QList<QByteArray> cmd) {
  if(!m_isPipeline) {
    // Convert and use existing command arguments if there any
    if (!isEmpty())
      m_pipelineCommands.append(m_commandWithArguments);
    m_isPipeline = true;
  }
  m_pipelineCommands.append(cmd);
  return *this;
}

int RedisClient::Command::length() const
{
  if (!m_isPipeline)
    return m_commandWithArguments.length();
  else
    return m_pipelineCommands.length();
}

QList<QByteArray> RedisClient::Command::splitCommandString(const QString &rawCommand)
{
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

bool RedisClient::Command::isPipelineCommand() const
{
    return m_isPipeline;
}

void RedisClient::Command::setPipelineCommand(const bool enable)
{
    m_isPipeline = enable;
}

int RedisClient::Command::getDbIndex() const
{
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

bool RedisClient::Command::isEmpty() const {
  if (!m_isPipeline)
    return m_commandWithArguments.isEmpty();
  else
    return m_pipelineCommands.isEmpty();
}

QByteArray RedisClient::Command::getByteRepresentation() const
{
    if (!m_isPipeline)
        return serializeToRESP(m_commandWithArguments);
    else
    {
        QByteArray result = serializeToRESP({"MULTI"});
        QList<QByteArray> pipelineCmd;
        foreach (pipelineCmd, m_pipelineCommands)
            result.append(serializeToRESP(pipelineCmd));
        result.append(serializeToRESP({"EXEC"}));
        return result;
    }
}

void RedisClient::Command::markAsHiPriorityCommand()
{
    m_hiPriorityCommand = true;
}

bool RedisClient::Command::isValid() const
{
    return !isEmpty();
}

QByteArray RedisClient::Command::serializeToRESP(QList<QByteArray> args) const
{
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
