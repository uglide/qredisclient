# Core deps 
# Read API from hiredis
INCLUDEPATH += $$PWD/hiredis
DEPENDPATH += $$PWD/hiredis
HEADERS += $$PWD/hiredis/read.h \
           $$PWD/hiredis/sds.h
SOURCES += $$PWD/hiredis/read.c \
           $$PWD/hiredis/sds.c


# SSH Transporter deps
INCLUDEPATH += $$PWD/libssh2/include
DEPENDPATH += $$PWD/libssh2/include

unix:mac {
    LIBS += -L/usr/local/Cellar/libssh2/1.6.0/lib/
}

LIBS += -lssl -lz -lssh2

INCLUDEPATH += $$PWD/
HEADERS += $$PWD/qsshclient/*.h
SOURCES += $$PWD/qsshclient/*.cpp
