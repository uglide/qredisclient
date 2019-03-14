#include "response.h"
#include <hiredis/read.h>
#include <QDebug>
#include <QObject>
#include <QVariantList>
#include <QVector>
#include "qredisclient/utils/compat.h"
#include "qredisclient/utils/text.h"

RedisClient::Response::Response() : m_type(RedisClient::Response::Unknown) {}

RedisClient::Response::Response(Type t, const QVariant& result)
    : m_type(t), m_result(result) {}

RedisClient::Response::~Response(void) {}

bool RedisClient::Response::isEmpty() const { return m_result.isNull(); }

QVariant RedisClient::Response::value() const { return m_result; }

RedisClient::Response::Type RedisClient::Response::type() const {
  return m_type;
}

bool RedisClient::Response::isValid() { return m_type != Type::Unknown; }

bool RedisClient::Response::isMessage() const {
  if (!isArray()) return false;

  QVariantList result = m_result.toList();

  return result.size() >= 3 &&
         (result[0] == "message" || result[0] == "pmessage");
}

bool RedisClient::Response::isArray() const {
  return m_result.isValid() && m_result.canConvert(QMetaType::QVariantList);
}

bool RedisClient::Response::isValidScanResponse() const {
  if (!isArray()) return false;

  QVariantList result = m_result.toList();

  return result.size() == 2 && result.at(0).canConvert(QMetaType::QString) &&
         (result.at(1).canConvert(QMetaType::QVariantList) || result.at(1).isNull());
}

long long RedisClient::Response::getCursor() {
  if (!isArray()) return -1;

  QVariantList result = m_result.toList();

  return result.at(0).toLongLong();
}

QVariantList RedisClient::Response::getCollection() {
  if (!isArray()) return QVariantList();

  QVariantList result = m_result.toList();

  return result.at(1).toList();
}

bool RedisClient::Response::isAskRedirect() const {
  return m_type == Type::Error && m_result.toByteArray().startsWith("ASK");
}

bool RedisClient::Response::isMovedRedirect() const {
  return m_type == Type::Error && m_result.toByteArray().startsWith("MOVED");
}

QByteArray RedisClient::Response::getRedirectionHost() const {
  if (!isMovedRedirect() && !isAskRedirect()) return QByteArray();

  QByteArray hostAndPort = m_result.toByteArray().split(' ')[2];

  return hostAndPort.split(':')[0];
}

uint RedisClient::Response::getRedirectionPort() const {
  if (!isMovedRedirect() && !isAskRedirect()) return 0;

  QByteArray hostAndPort = m_result.toByteArray().split(' ')[2];

  return QString(hostAndPort.split(':')[1]).toUInt();
}

QByteArray RedisClient::Response::getChannel() const {
  if (!isMessage()) return QByteArray{};

  QVariantList result = m_result.toList();

  return result[1].toByteArray();
}

QString RedisClient::Response::valueToHumanReadString(const QVariant& value,
                                                      int indentLevel) {
  QString result;
  QString indent = QString(" ").repeated(indentLevel);

  if (value.isNull()) {
    result = QString("null");
  } else if (value.type() == QVariant::Bool) {
    result = QString(value.toBool() ? "true" : "false");
  } else if (value.type() == QVariant::StringList) {
    int index = 1;
    for (QVariant line : value.toList()) {
      result.append(
          QString("%1 %2) %3\r\n")
              .arg(indent)
              .arg(QString::number(index++))
              .arg(QString("\"%1\"").arg(printableString(line.toByteArray()))));
    }
  } else if (value.type() == QVariant::List ||
             value.canConvert(QVariant::List)) {
    QVariantList list = value.value<QVariantList>();

    int index = 1;
    for (QVariant item : list) {
      result.append(QString("%1 %2) %3\r\n")
                        .arg(indent)
                        .arg(QString::number(index++))
                        .arg(valueToHumanReadString(item, indentLevel + 1)));
    }
  } else {
    result = QString("\"%1\"").arg(printableString(value.toByteArray()));
  }

  return indent + result;
}

bool RedisClient::Response::isErrorMessage() const {
  return m_type == Type::Error && m_result.toByteArray().startsWith("ERR");
}

bool RedisClient::Response::isErrorStateMessage() const {
  return m_type == Type::Error &&
         (m_result.toByteArray().startsWith("DENIED") ||
          m_result.toByteArray().startsWith("LOADING") ||
          m_result.toByteArray().startsWith("MISCONF"));
}

bool RedisClient::Response::isDisabledCommandErrorMessage() const {
  return isErrorMessage() && m_result.toByteArray().contains("unknown command");
}

bool RedisClient::Response::isOkMessage() const {
  return m_type == Type::Status && m_result.toByteArray().startsWith("OK");
}

bool RedisClient::Response::isQueuedMessage() const {
  return m_type == Type::Status && m_result.toByteArray().startsWith("QUEUED");
}
