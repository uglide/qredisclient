#include <cctype>
#include <QTextCodec>
#include <QString>
#include <QByteArray>

QString printableString(const QByteArray& raw);

bool isBinary(const QByteArray& raw);

QByteArray printableStringToBinary(const QString& str);
