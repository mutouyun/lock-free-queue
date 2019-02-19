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
    include/queue_s2s.h \
    include/queue_m2m.h
