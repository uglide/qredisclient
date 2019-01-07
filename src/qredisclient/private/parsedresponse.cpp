#include "parsedresponse.h"

QVariant convertUnsafeArray(RedisClient::ParsingResult* res) {
  QVariantList result;

  for (int index = 0; index < res->array.size(); ++index) {
    if (res->array[index] && res->array[index]->array.size() > 0) {
      result.append(convertUnsafeArray(res->array[index]));
    } else {
      result.append(res->array[index]->val);
    }
    delete res->array[index];
  }
  return QVariant(result);
}

RedisClient::Response RedisClient::ParsingResult::toResponse() {
  auto responseType = static_cast<RedisClient::Response::Type>(type);

  if (responseType == RedisClient::Response::Array) {
    return RedisClient::Response(responseType, convertUnsafeArray(this));
  } else {
    return RedisClient::Response(responseType, val);
  }
}
