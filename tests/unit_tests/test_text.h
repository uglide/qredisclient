#pragma once

#include <QObject>
#include <QtCore>

class TestText : public QObject
{
    Q_OBJECT

private slots:
    void testPrintableStringToBinary();
    void testPrintableString();

};

