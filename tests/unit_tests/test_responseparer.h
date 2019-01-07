#pragma once

#include <QObject>

class TestResponseParser : public QObject {
  Q_OBJECT

 private slots:
  void parsing();
  void parsing_data();

  void validation();
  void validation_data();

  void multipleResponsesInTheBuffer();

  void hiredisBufferCleanup();

  void source();
};
