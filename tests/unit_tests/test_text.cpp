#include "test_text.h"
#include "qredisclient/utils/text.h"

#include <QDebug>
#include <QTest>

void TestText::testPrintableStringToBinary()
{
    //given
    QString unicodeWithEscapedBinaryData{"☂\\x00te\\st\\\\"};
    QByteArray validResult;
    validResult.append((unsigned char)226);
    validResult.append((unsigned char)152);
    validResult.append((unsigned char)130);
    validResult.append('\x00');
    validResult.append('t');
    validResult.append('e');
    validResult.append('\\');
    validResult.append('s');
    validResult.append('t');
    validResult.append('\\');
    validResult.append('\\');

    //when
    QByteArray actualResult = printableStringToBinary(unicodeWithEscapedBinaryData);

    //then
    QCOMPARE(actualResult, validResult);
}

void TestText::testPrintableString()
{
    //given
    QByteArray testData;
    testData.append('\x01');
    testData.append('\x02');
    testData.append('\x03');

    //when
    QString actualResult = printableString(testData);

    //then
    QCOMPARE(actualResult, QString("\\x01\\x02\\x03"));
}
