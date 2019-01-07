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


DEFINES += INTEGRATION_TESTS QT_NO_DEBUG_OUTPUT


isEmpty(DESTDIR) {
    DESTDIR = $$PWD
}

unix:!mac {
    #code coverage
    QMAKE_CXXFLAGS += -g -fprofile-arcs -ftest-coverage -O0
    QMAKE_LFLAGS += -g -fprofile-arcs -ftest-coverage  -O0
    LIBS += -lgcov
}

include($$PWD/redisclient-tests.pri)
include($$PROJECT_ROOT/3rdparty/3rdparty.pri)

UI_DIR = $$DESTDIR/ui
OBJECTS_DIR = $$DESTDIR/obj
MOC_DIR = $$DESTDIR/obj
RCC_DIR = $$DESTDIR/obj

RESOURCES += \
    $$PROJECT_ROOT/lua.qrc
