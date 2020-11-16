#pragma once

#include <QObject>
#include "basetestcase.h"

class TestTransporters : public BaseTestCase {
  Q_OBJECT

 private slots:
  void readPartialResponses();
  void handleClusterRedirects();
};
