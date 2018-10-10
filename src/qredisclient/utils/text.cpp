#include "text.h"
#include <functional>

bool byteArrayToValidUnicode(const QByteArray &raw, QString *result = nullptr,
                             bool strict = false) {
  QTextCodec::ConverterState state;
  QTextCodec *codec = QTextCodec::codecForName("UTF-8");
  const QString text = codec->toUnicode(raw.constData(), raw.size(), &state);

  if (state.invalidChars == 0) {
    foreach (QChar c, text) {
      if (strict) {
        if (!c.isPrint() || (c.isSpace() && c != ' ')) return false;
      } else {
        if (c.isSpace()) continue;
        if (!c.isPrint()) return false;
      }
    }

    if (result) *result = text;
    return true;
  }
  return false;
}

QString printableString(const QByteArray &raw, bool strictChecks) {
  QString text;

  if (byteArrayToValidUnicode(raw, &text, strictChecks)) return text;

  QByteArray escapedBinaryString;
  char const hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

  bool printableChar = false;

  foreach (char i, raw) {
    if (strictChecks) {
      printableChar =
          isprint((unsigned char)i) && (!isspace((unsigned char)i) || i == ' ');
    } else {
      printableChar = isprint((unsigned char)i);
    }

    if (printableChar) {
      escapedBinaryString.append(i);
    } else {
      escapedBinaryString.append("\\x");
      escapedBinaryString.append(&hex[(i & 0xF0) >> 4], 1);
      escapedBinaryString.append(&hex[i & 0xF], 1);
    }
  }
  return escapedBinaryString;
}

bool isBinary(const QByteArray &raw) {
  return !byteArrayToValidUnicode(raw, nullptr, false);
}

QByteArray printableStringToBinary(const QString &str) {
  QByteArray utfData = str.toUtf8();
  QByteArray processedString;

  bool quoteStarted = false;
  bool xFound = false;
  QByteArray hexBuff;

  std::function<void()> clear = [&quoteStarted, &xFound, &hexBuff]() {
    quoteStarted = xFound = false;
    hexBuff.clear();
  };

  for (QByteArray::const_iterator i = utfData.constBegin();
       i != utfData.constEnd(); ++i) {
    if (quoteStarted) {
      if (xFound) {
        if (('0' <= *i && *i <= '9') ||
            ('a' <= QChar(*i).toLower() && QChar(*i).toLower() <= 'f')) {
          hexBuff.append(*i);

          if (hexBuff.size() == 2) {
            processedString.append(QByteArray::fromHex(hexBuff));
            clear();
          }

        } else {
          processedString.append('\\');
          processedString.append('x');
          processedString.append(hexBuff);
          clear();
        }
      } else {
        if (*i == 'x') {
          xFound = true;
          continue;
        } else {
          processedString.append('\\');
          processedString.append(*i);
          clear();
        }
      }

    } else if (*i == '\\') {
      quoteStarted = true;
    } else {
      processedString.append(*i);
    }
  }

  // Remove unicode headers
  processedString.replace("\xEF\xBE", QByteArray());

  return processedString;
}
