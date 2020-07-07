#pragma once

#include "command.h"
#include "connection.h"
#include "connectionconfig.h"
#include "response.h"
#include <QObject>
#include <QVector>
#include <QByteArray>

inline void initRedisClient()
{
    qRegisterMetaType<RedisClient::Command>("Command");
    qRegisterMetaType<RedisClient::Command>("RedisClient::Command");
    qRegisterMetaType<QList<RedisClient::Command>>("QList<Command>");
    qRegisterMetaType<QList<RedisClient::Command>>("QList<RedisClient::Command>");
    qRegisterMetaType<RedisClient::Response>("Response");
    qRegisterMetaType<RedisClient::Response>("RedisClient::Response");
    qRegisterMetaType<QVector<QVariant*>>("QVector<QVariant*>");
    qRegisterMetaType<QVariant*>("QVariant*");    
}
