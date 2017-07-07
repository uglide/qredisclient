# Core deps 
# Read API from hiredis
INCLUDEPATH += $$PWD/hiredis
DEPENDPATH += $$PWD/hiredis
HEADERS += $$PWD/hiredis/read.h \
           $$PWD/hiredis/sds.h
SOURCES += $$PWD/hiredis/read.c \
           $$PWD/hiredis/sds.c


unix:mac {
    INCLUDEPATH += /usr/local/opt/libssh2/include
    LIBS += -L/usr/local/opt/libssh2/lib
    
    INCLUDEPATH += /usr/local/opt/openssl/include
    LIBS += -L/usr/local/opt/openssl/lib
}

win32-msvc* {
    win32-msvc2015 {
        message("msvc2015 detected")
        WIN_DEPS_VERSION = v140
    } else {
        error("Your msvc version is not suppoted. qredisclient requires msvc2015+")
    }

    INCLUDEPATH += $$PWD/windows/rmt_libssh2.1.8.0/build/native/include/

    CONFIG(release, debug|release) {
        WIN_DEPS_PATH = $$PWD/windows/rmt_zlib.1.2.8.7/build/native/lib/$$WIN_DEPS_VERSION/Win32/Release/static/zlibstat.lib
        WIN_DEPS_PATH2 = $$PWD/windows/rmt_libssh2.1.8.0/build/native/lib/$$WIN_DEPS_VERSION/Win32/Release/static

        defined(OPENSSL_STATIC) {
            WIN_DEPS_PATH3 = $$PWD/windows/rmt_openssl.1.1.0.3/build/native/lib/$$WIN_DEPS_VERSION/Win32/Release/static
        } else {
            WIN_DEPS_PATH3 = $$PWD/windows/rmt_openssl.1.1.0.3/build/native/lib/$$WIN_DEPS_VERSION/Win32/Release/dynamic
        }
    } else: CONFIG(debug, debug|release) {
        WIN_DEPS_PATH = $$PWD/windows/rmt_zlib.1.2.8.7/build/native/lib/$$WIN_DEPS_VERSION/Win32/Debug/static/zlibstat.lib
        WIN_DEPS_PATH2 = $$PWD/windows/rmt_libssh2.1.8.0/build/native/lib/$$WIN_DEPS_VERSION/Win32/Debug/static

        defined(OPENSSL_STATIC) {
            WIN_DEPS_PATH3 = $$PWD/windows/rmt_openssl.1.1.0.3/build/native/lib/$$WIN_DEPS_VERSION/Win32/Debug/static
        } else {
            WIN_DEPS_PATH3 = $$PWD/windows/rmt_openssl.1.1.0.3/build/native/lib/$$WIN_DEPS_VERSION/Win32/Debug/dynamic
        }
    }

    LIBS += $$WIN_DEPS_PATH -L$$WIN_DEPS_PATH2 -L$$WIN_DEPS_PATH3 -llibssh2 -llibeay32 -lssleay32 -lgdi32 -lws2_32 -lkernel32 -luser32 -lshell32 -luuid -lole32 -ladvapi32
} else {    
   exists( /usr/local/lib/libssh2.a ) {
      LIBS += /usr/local/lib/libssh2.a -lz -lssl -lcrypto
      INCLUDEPATH += /usr/local/include/
   } else {
      LIBS += -lssl -lz -lssh2
   }
}


INCLUDEPATH += $$PWD/
HEADERS += $$PWD/qsshclient/*.h
SOURCES += $$PWD/qsshclient/*.cpp
