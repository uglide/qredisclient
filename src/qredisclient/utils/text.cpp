#include "text.h"

QString printableString(const QByteArray &raw)
{
    QTextCodec::ConverterState state;
    QTextCodec *codec = QTextCodec::codecForName("UTF-8");
    const QString text = codec->toUnicode(raw.constData(), raw.size(), &state);

    if (state.invalidChars == 0)
        return text;

    QByteArray escapedBinaryString;
    char const hex[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                           'A', 'B', 'C', 'D', 'E', 'F'};

    foreach (char i, raw) {
        if (isprint(i)) {
            escapedBinaryString.append(i);
        } else {
            escapedBinaryString.append("\\x");
            escapedBinaryString.append(&hex[(i  & 0xF0) >> 4], 1);
            escapedBinaryString.append(&hex[i & 0xF], 1);
        }
    }
    return escapedBinaryString;
}
