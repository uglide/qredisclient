
REDISCLIENT_SRC_DIR = $$PWD/../../src/qredisclient/

HEADERS  += \
    $$PWD/*.h \
    $$PWD/mocks/*.h \
    $$REDISCLIENT_SRC_DIR/*.h \
    $$REDISCLIENT_SRC_DIR/transporters/*.h \
    $$REDISCLIENT_SRC_DIR/utils/*.h \

SOURCES += \
    $$PWD/*.cpp \
    $$REDISCLIENT_SRC_DIR/*.cpp \
    $$REDISCLIENT_SRC_DIR/transporters/*.cpp \
    $$REDISCLIENT_SRC_DIR/utils/*.cpp \

OTHER_FILES += \
    connections.xml
