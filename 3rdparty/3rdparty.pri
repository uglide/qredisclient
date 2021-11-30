# Core deps 
# Read API from hiredis
INCLUDEPATH += $$PWD/
DEPENDPATH += $$PWD/hiredis
HEADERS += $$PWD/hiredis/read.h \
           $$PWD/hiredis/sds.h \
           $$PWD/crc16.h

SOURCES += $$PWD/hiredis/read.c \           
           $$PWD/hiredis/sds.c \
           $$PWD/crc16.c


include($$PWD/qsshclient/qsshclient.pri)
# Asyncfuture
include($$PWD/asyncfuture/asyncfuture.pri)