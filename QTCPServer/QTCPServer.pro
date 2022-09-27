#-------------------------------------------------
#
# Project created by QtCreator 2018-11-14T20:44:02
#
#-------------------------------------------------

QT       += core gui network serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = QTCPServer
TEMPLATE = app

CONFIG+=sdk_no_version_check

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++11

SOURCES += \
        main.cpp \
    mainwindow.cpp \
    qsbusthreadworker.cpp \
    CrsfSerial/CrsfSerial.cpp \
    crc8/crc8.cpp

HEADERS += \
    mainwindow.h \
    qsbusthreadworker.h \
    CrsfSerial/crsf_protocol.h \
    CrsfSerial/CrsfSerial.h \
    crc8/crc8.h

FORMS += \
    mainwindow.ui

INCLUDEPATH += $$PWD/raspberry-sbus/src/driver/include
INCLUDEPATH += $$PWD/raspberry-sbus/src/common/include
INCLUDEPATH += $$PWD/raspberry-sbus/src/decoder/include
INCLUDEPATH += $$PWD/raspberry-sbus/src/tty/include
INCLUDEPATH += $$PWD/CrsfSerial
INCLUDEPATH += $$PWD/crc8

LIBS += -L$$PWD/raspberry-sbus/build/debug/src -llibsbus

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
