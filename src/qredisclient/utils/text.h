#pragma once
#include <QByteArray>
#include <QString>
#include <QTextCodec>
#include <cctype>

QString printableString(const QByteArray& raw, bool strictChecks = false);

bool isBinary(const QByteArray& raw);

QByteArray printableStringToBinary(const QString& str);
