#pragma once

#include <QObject>

class TestResponse : public QObject {
  Q_OBJECT

 private slots:
  void valueToHumanReadString();
  void scanResponse();
};
