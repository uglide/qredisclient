#pragma once
#include <QStringList>
#include <QList>
#include <QByteArray>
#include <QVariantHash>
#include <QJsonObject>

/*
 * Functions for Qt 5.4 compatibility
 */

/**
 * @brief convertStringList
 * @param list
 * @return
 */
QList<QByteArray> convertStringList(const QStringList &list);

/**
 * @brief convertQVariantList
 * @param list
 * @return
 */
QList<QByteArray> convertQVariantList(const QList<QVariant> &list);

/**
 * @brief QJsonObjectFromVariantHash
 * @param hash
 * @return
 */
QJsonObject QJsonObjectFromVariantHash(const QVariantHash &hash);

/**
 * @brief QJsonObjectToVariantHash
 * @param o
 * @return
 */
QVariantHash QJsonObjectToVariantHash(const QJsonObject &o);
