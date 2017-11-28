QT       += core network

TARGET = qredis-runner
TEMPLATE = app

CONFIG += debug c++11 console
CONFIG-=app_bundle

DEFINES += QT_NO_DEBUG_OUTPUT

PROJECT_ROOT = $$PWD/../../
DESTDIR = $$PWD/bin

SOURCES += \
    $$PWD/main.cpp

include($$PROJECT_ROOT/qredisclient.pri)
