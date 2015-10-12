#include "compat.h"

QList<QByteArray> convertStringList(const QStringList &list)
{
    QList<QByteArray> result;

    for(QString line : list) {
        result.append(line.toUtf8());
    }

    return result;
}

QVariantHash QJsonObjectToVariantHash(const QJsonObject &o)
{
    QVariantHash hash;

    for (auto i = o.begin(); i != o.end(); ++i) {
        hash.insert(i.key(), i.value().toVariant());
    }

    return hash;
}

QJsonObject QJsonObjectFromVariantHash(const QVariantHash &hash)
{
    QJsonObject object;
    for (QVariantHash::const_iterator it = hash.constBegin(); it != hash.constEnd(); ++it)
        object.insert(it.key(), QJsonValue::fromVariant(it.value()));
    return object;
}

QList<QByteArray> convertQVariantList(const QList<QVariant> &list)
{
    QList<QByteArray> r;
    foreach (QVariant v, list) {
        r.append(v.toByteArray());
    }
    return r;
}
