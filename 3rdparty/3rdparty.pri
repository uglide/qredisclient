# Core deps 
# Read API from hiredis
INCLUDEPATH += $$PWD/hiredis
DEPENDPATH += $$PWD/hiredis
HEADERS += $$PWD/hiredis/read.h \
           $$PWD/hiredis/sds.h
SOURCES += $$PWD/hiredis/read.c \
           $$PWD/hiredis/sds.c


include($$PWD/qsshclient/qsshclient.pri)
