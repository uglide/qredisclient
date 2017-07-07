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
        error("Your msvc version is not suppoted. qredisclient requires msvc2015")
    }
    
    WIN_DEPS_PLATFORM = Win32
    WIN_DEPS_ZLIB_VERSION = 1.2.8.7
    WIN_DEPS_SSL_VERSION = 1.1.0.3
    WIN_DEPS_LIBSSH_VERSION = 1.8.0.0
    WIN_DEPS_LIBSSH_BASE_PATH = $$PWD/windows/rmt_libssh2.$$WIN_DEPS_LIBSSH_VERSION/build/native/
    
    CONFIG(release, debug|release) {
        WIN_DEPS_TYPE = Release
    } else: CONFIG(debug, debug|release) {
        WIN_DEPS_TYPE = Debug
    }
    
    INCLUDEPATH += $$WIN_DEPS_LIBSSH_BASE_PATH/include/$$WIN_DEPS_VERSION/$$WIN_DEPS_PLATFORM/$$WIN_DEPS_TYPE/static/
    
    WIN_DEPS_PATH = $$PWD/windows/rmt_zlib.$$WIN_DEPS_ZLIB_VERSION/build/native/lib/$$WIN_DEPS_VERSION/$$WIN_DEPS_PLATFORM/$$WIN_DEPS_TYPE/static/zlibstat.lib
    WIN_DEPS_PATH2 = $$WIN_DEPS_LIBSSH_BASE_PATH/lib/$$WIN_DEPS_VERSION/Win32/$$WIN_DEPS_TYPE/static

    defined(OPENSSL_STATIC) {
      WIN_DEPS_PATH3 = $$PWD/windows/rmt_openssl.$$WIN_DEPS_SSL_VERSION/build/native/lib/$$WIN_DEPS_VERSION/$$WIN_DEPS_PLATFORM/$$WIN_DEPS_TYPE/static
    } else {
      WIN_DEPS_PATH3 = $$PWD/windows/rmt_openssl.$$WIN_DEPS_SSL_VERSION/build/native/lib/$$WIN_DEPS_VERSION/$$WIN_DEPS_PLATFORM/$$WIN_DEPS_TYPE/dynamic
    } 
    
    LIBS += $$WIN_DEPS_PATH -L$$WIN_DEPS_PATH2 -L$$WIN_DEPS_PATH3 -llibssh2 -llibssl -llibcrypto -lgdi32 -lws2_32 -lkernel32 -luser32 -lshell32 -luuid -lole32 -ladvapi32
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
