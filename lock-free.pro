TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

DESTDIR = $${PWD}/output

INCLUDEPATH += \
    $${PWD}/include

SOURCES += \
    main.cpp

HEADERS += \
    include/queue_unsafe.h \
    include/queue_locked.h \
    include/queue_spsc.h \
    include/queue_mpmc.h

unix:LIBS += -lpthread
