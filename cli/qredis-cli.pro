QT       += core network

TARGET = qredis-cli
TEMPLATE = app

CONFIG += debug c++11
CONFIG-=app_bundle

PROJECT_ROOT = $$PWD/..//

SOURCES += \
    $$PWD/main.cpp

include($$PROJECT_ROOT/qredisclient.pri)
