QT       += core network testlib

TARGET = tests
TEMPLATE = app

CONFIG += debug c++11
CONFIG-=app_bundle   

PROJECT_ROOT = $$PWD/../..//
SRC_DIR = $$PROJECT_ROOT/src//

INCLUDEPATH += \
    $$SRC_DIR/ \
    $$PWD/

DEFINES += INTEGRATION_TESTS

unix:!mac {
    #code coverage
    QMAKE_CXXFLAGS += -g -fprofile-arcs -ftest-coverage -O0
    QMAKE_LFLAGS += -g -fprofile-arcs -ftest-coverage  -O0
    LIBS += -lgcov
}

include($$PWD/redisclient-tests.pri)
include($$PROJECT_ROOT/3rdparty/3rdparty.pri)
