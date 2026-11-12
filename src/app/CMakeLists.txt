# Qt 6 for Windows / Linux / Android, Qt 5 (>= 5.6) for Sailfish OS.
find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Network Qml Quick REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(APP_SOURCES
    main.cpp
    station.cpp station.h
    sheetclient.cpp sheetclient.h
)

if(SHEETSTATION_SAILFISH)
    set(APP_QRC ${PROJECT_SOURCE_DIR}/qml/sailfish/sailfish.qrc)
else()
    set(APP_QRC ${PROJECT_SOURCE_DIR}/qml/generic/generic.qrc)
endif()

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(sheetstation MANUAL_FINALIZATION ${APP_SOURCES} ${APP_QRC})
else()
    add_executable(sheetstation ${APP_SOURCES} ${APP_QRC})
endif()

target_compile_definitions(sheetstation PRIVATE SHEETSTATION_VERSION="${PROJECT_VERSION}")
target_link_libraries(sheetstation PRIVATE
    sheetstation_core
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Qml
    Qt${QT_VERSION_MAJOR}::Quick
)

if(SHEETSTATION_SAILFISH)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SAILFISHAPP REQUIRED IMPORTED_TARGET sailfishapp)
    target_compile_definitions(sheetstation PRIVATE SHEETSTATION_SAILFISH)
    target_link_libraries(sheetstation PRIVATE PkgConfig::SAILFISHAPP)
endif()

set_target_properties(sheetstation PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(ANDROID)
    set_target_properties(sheetstation PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${PROJECT_SOURCE_DIR}/android
        QT_ANDROID_PACKAGE_NAME io.github.isaakrichards.sheetstation
        QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
        QT_ANDROID_VERSION_CODE 1)
    if(COMMAND add_android_openssl_libraries)
        add_android_openssl_libraries(sheetstation) # Qt Network needs it for https on Android
    endif()
endif()

# ------------------------------------------------------------------ install
include(GNUInstallDirs)
install(TARGETS sheetstation
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .)

if(UNIX AND NOT ANDROID AND NOT APPLE)
    if(SHEETSTATION_SAILFISH)
        set(SS_DESKTOP ${PROJECT_SOURCE_DIR}/packaging/sailfish/sheetstation.desktop)
    else()
        set(SS_DESKTOP ${PROJECT_SOURCE_DIR}/packaging/linux/sheetstation.desktop)
    endif()
    install(FILES ${SS_DESKTOP} DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/icons/sheetstation.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
    if(SHEETSTATION_SAILFISH)
        foreach(size 86 108 128 172)
            install(FILES ${PROJECT_SOURCE_DIR}/packaging/icons/${size}x${size}/sheetstation.png
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}x${size}/apps)
        endforeach()
    endif()
endif()

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_finalize_executable(sheetstation)
endif()
