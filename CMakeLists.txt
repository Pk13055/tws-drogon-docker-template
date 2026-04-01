cmake_minimum_required(VERSION 3.16)
project(tws LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(twsapi CONFIG REQUIRED)
find_library(AMQP_CPP_LIBRARY NAMES amqpcpp REQUIRED)
find_path(AMQP_CPP_INCLUDE_DIR NAMES amqpcpp.h REQUIRED)

add_executable(
    tws
    main.cc
    ibkr/ibkr_probe.cc
    controllers/orders/orders_controller.cc
    controllers/pricing/pricing_controller.cc
    controllers/messaging/messaging_controller.cc
    messaging/message_broker.cc
    messaging/broker_config.cc
    messaging/amqp_trantor_handler.cc
    messaging/rabbitmq_broker.cc
    messaging/messaging_registry.cc
)

target_include_directories(
    tws
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${AMQP_CPP_INCLUDE_DIR}
        /usr/local/include/twsapi
        /usr/local/include/twsapi/protobufUnix
)
target_link_libraries(
    tws
    PRIVATE
        Drogon::Drogon
        twsapi
        ${AMQP_CPP_LIBRARY}
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        ${CMAKE_DL_LIBS}
)

# The packaged IBKR shared library carries optional BID decimal symbols that are
# not needed for the current connectivity probe path.
target_link_options(tws PRIVATE -Wl,--allow-shlib-undefined)
