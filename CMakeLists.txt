cmake_minimum_required(VERSION 3.13)
project(SheetStation VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # static libs end up in PIE executables (Sailfish) and .so (Android)

option(SHEETSTATION_APP "Build the Qt application" ON)
option(SHEETSTATION_TESTS "Build the core self-test" OFF)
option(SHEETSTATION_SAILFISH "Build the Sailfish OS flavour (Silica UI, Qt 5)" OFF)
option(SHEETSTATION_SYSTEM_LIBDATACHANNEL "Use an installed libdatachannel instead of the submodule" OFF)

# Find Qt early: the Android OpenSSL helper picks OpenSSL 3 vs 1.1 from the Qt version.
if(SHEETSTATION_APP)
    find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)
endif()

# ---------------------------------------------------------------- libdatachannel
if(SHEETSTATION_SYSTEM_LIBDATACHANNEL)
    find_package(LibDataChannel REQUIRED)
    set(SS_RTC_TARGET LibDataChannel::LibDataChannel)
else()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libdatachannel/CMakeLists.txt")
        message(FATAL_ERROR "libdatachannel submodule missing: run 'git submodule update --init --recursive'")
    endif()
    # Android has no system OpenSSL: point at KDAB's prebuilt android_openssl checkout.
    set(SHEETSTATION_ANDROID_OPENSSL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_openssl"
        CACHE PATH "Path to a KDAB android_openssl checkout (Android builds only)")
    if(ANDROID AND EXISTS "${SHEETSTATION_ANDROID_OPENSSL}/android_openssl.cmake")
        include("${SHEETSTATION_ANDROID_OPENSSL}/android_openssl.cmake")
        set(OPENSSL_INCLUDE_DIR "${OPENSSL_INCLUDE_DIR}" CACHE PATH "" FORCE)
        set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_LIB_DIR}/${libcrypto}" CACHE FILEPATH "" FORCE)
        set(OPENSSL_SSL_LIBRARY "${OPENSSL_LIB_DIR}/${libssl}" CACHE FILEPATH "" FORCE)
    elseif(ANDROID)
        message(WARNING "Android build without android_openssl: clone https://github.com/KDAB/android_openssl into third_party/")
    endif()
    # Data channels only: no media (SRTP) and no WebSocket support needed.
    set(NO_MEDIA ON CACHE BOOL "" FORCE)
    set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/libdatachannel EXCLUDE_FROM_ALL)
    set(SS_RTC_TARGET datachannel-static)
endif()

# ---------------------------------------------------------------- portable core (no Qt)
add_library(sheetstation_core STATIC
    src/core/sha256.cpp
    src/core/catalog.cpp
    src/core/peer.cpp
)
target_include_directories(sheetstation_core PUBLIC src/core)
target_link_libraries(sheetstation_core PUBLIC ${SS_RTC_TARGET})
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
    target_link_libraries(sheetstation_core PUBLIC stdc++fs) # older GCC (Sailfish SDK)
endif()

if(SHEETSTATION_TESTS)
    enable_testing()
    add_executable(sheetstation_core_test tests/core_test.cpp)
    target_link_libraries(sheetstation_core_test PRIVATE sheetstation_core)
    add_test(NAME core COMMAND sheetstation_core_test)
endif()

# ---------------------------------------------------------------- Qt application
if(SHEETSTATION_APP)
    add_subdirectory(src/app)
endif()

# ---------------------------------------------------------------- Debian package
# Desktop Linux build: `cmake --build build && cpack --config build/CPackConfig.cmake -G DEB`
# produces sheetstation_<version>_<arch>.deb. Library dependencies (Qt 6, OpenSSL) are
# filled in by dpkg-shlibdeps; the QML modules are loaded at runtime, so they are listed
# by hand.
if(SHEETSTATION_APP AND UNIX AND NOT ANDROID AND NOT APPLE AND NOT SHEETSTATION_SAILFISH)
    set(CPACK_PACKAGE_NAME "sheetstation")
    set(CPACK_PACKAGE_VENDOR "Isaak Richards")
    set(CPACK_PACKAGE_CONTACT "Isaak Richards")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Share files directly between devices, with a Google Sheet as the only server")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_STRIP_FILES ON)

    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/IsaakRichards/SheetStation")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
"Share files directly between devices, with a Google Sheet as the only server
 SheetStation shares files (e-books and the like) directly between devices over
 WebRTC. A Google Sheet, through a small Apps Script web app, is used only to
 find the other devices and swap connection details.
 .
 Not secure: do not use it for sensitive or private files.")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtquick-templates, qml6-module-qtqml-workerscript")
    set(CPACK_GENERATOR "DEB")
    include(CPack)
endif()
