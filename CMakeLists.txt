cmake_minimum_required(VERSION 3.13)
project(SheetStation VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SHEETSTATION_APP "Build the Qt application" ON)
option(SHEETSTATION_TESTS "Build the core self-test" OFF)
option(SHEETSTATION_SAILFISH "Build the Sailfish OS flavour (Silica UI, Qt 5)" OFF)
option(SHEETSTATION_SYSTEM_LIBDATACHANNEL "Use an installed libdatachannel instead of the submodule" OFF)

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
