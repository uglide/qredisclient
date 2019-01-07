#pragma once
#include <QMetaType>
#include <QVariant>
#include <QVector>
#include "qredisclient/response.h"

namespace RedisClient {
struct ParsingResult;

struct ParsingResult {
  ParsingResult(int t, QVariant v) : type(t), val(v) {}

  ParsingResult(int s) : type(2) { array.resize(s); }

  Response toResponse();

  int type;
  QVariant val;
  QVector<ParsingResult *> array;
};

}  // namespace RedisClient

Q_DECLARE_METATYPE(QVector<RedisClient::ParsingResult *>)
