cmake_minimum_required(VERSION 3.20)
project(tws LANGUAGES CXX)

# ---- Global settings ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INSTALL_RPATH "/usr/local/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# Help CMake find /usr/local-installed packages (Docker)
list(APPEND CMAKE_PREFIX_PATH "/usr/local")

# ---- Dependencies ----
find_package(Drogon CONFIG REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(twsapi CONFIG REQUIRED)

# AMQP-CPP (no official config, so keep fallback)
find_library(AMQP_CPP_LIBRARY NAMES amqpcpp REQUIRED)
find_path(AMQP_CPP_INCLUDE_DIR NAMES amqpcpp.h REQUIRED)

# Intel BID — libtwsapi.so leaves __bid64_* undefined; link libbidgcc000 into the
# executable and export symbols (-rdynamic) so the loader can satisfy the .so.
find_library(INTEL_BID_LIB NAMES bidgcc000 REQUIRED)

# ---- Source groups ----
file(GLOB_RECURSE IBKR_SRC CONFIGURE_DEPENDS src/ibkr/*.cc)
file(GLOB_RECURSE MSG_SRC CONFIGURE_DEPENDS src/messaging/*.cc)
file(GLOB_RECURSE CTRL_SRC CONFIGURE_DEPENDS src/controllers/*.cc)

# ---- Libraries ----
add_library(ibkr ${IBKR_SRC})
add_library(messaging ${MSG_SRC})
add_library(controllers ${CTRL_SRC})

# ---- Include paths ----
# controllers
target_include_directories(controllers PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    /usr/local/include
    /usr/local/include/twsapi
    /usr/local/include/twsapi/protobufUnix
)

# messaging
target_include_directories(messaging PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    /usr/include/jsoncpp
)

# ibkr
target_include_directories(ibkr PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    /usr/local/include
    /usr/local/include/twsapi
    /usr/local/include/twsapi/protobufUnix
)

# ---- Link dependencies to modules ----
target_link_libraries(messaging PUBLIC jsoncpp_lib)
target_link_libraries(messaging
    PUBLIC
        ${AMQP_CPP_LIBRARY}
        OpenSSL::SSL
        OpenSSL::Crypto
)

target_include_directories(messaging PRIVATE ${AMQP_CPP_INCLUDE_DIR})

target_link_libraries(ibkr PUBLIC jsoncpp_lib)
target_link_libraries(ibkr
    PUBLIC
        twsapi
        protobuf::libprotobuf
)

target_link_libraries(controllers
    PUBLIC
        Drogon::Drogon
)

# ---- Main executable ----
add_executable(tws src/main.cc)

# HttpController routes register via static initializers in each controller .cc.
# Without --whole-archive, the linker can omit every .o from the static
# `controllers` library (nothing in main.cc references them), so no routes exist.
target_link_libraries(tws
    PRIVATE
        ibkr
        messaging
        -Wl,--whole-archive
        controllers
        -Wl,--no-whole-archive
        -Wl,--whole-archive
        ${INTEL_BID_LIB}
        -Wl,--no-whole-archive
        Threads::Threads
        ${CMAKE_DL_LIBS}
)

# Export exe symbols so libtwsapi.so resolves Intel BID at runtime.
target_link_options(tws PRIVATE -rdynamic)

# ---- Linker workaround (IBKR quirk) ----
target_link_options(tws PRIVATE -Wl,--allow-shlib-undefined)

