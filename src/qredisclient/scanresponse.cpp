#include "scanresponse.h"

long long RedisClient::ScanResponse::getCursor()
{    
    if (m_result.isNull()) parse();

    if (!isArray())
        return -1;

    QVariantList result = m_result->toList();

    return result.at(0).toLongLong();
}

QVariantList RedisClient::ScanResponse::getCollection()
{    
    if (m_result.isNull()) parse();

    if (!isArray())
        return QVariantList();

    QVariantList result = m_result->toList();

    return result.at(1).toList();
}

bool RedisClient::ScanResponse::isValidScanResponse(Response& r)
{
    QVariant value = r.getValue();

    if (!r.isArray())
        return false;

    QVariantList result = value.toList();

    return result.size() == 2
            && result.at(0).canConvert(QMetaType::QString)
            && result.at(1).canConvert(QMetaType::QVariantList);
}
