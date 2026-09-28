# DataBus <-> MQTT gateway demo (tools/bridge/MqttBridge).
cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "databus_mqtt_gateway_app")
project(DataBusMqttGateway VERSION 1.0 LANGUAGES C CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

set(DMQ_THREAD "DMQ_THREAD_STDLIB")
set(DMQ_DATABUS "ON")
include("${CMAKE_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")
include("${CMAKE_SOURCE_DIR}/../../../tools/bridge/mqtt/MqttBridge.cmake")

add_executable(${DELEGATE_APP}
    main.cpp
    ${DMQ_LIB_SOURCES}
    ${DMQ_PORT_SOURCES}
)
target_compile_definitions(${DELEGATE_APP} PRIVATE ${DMQ_COMPILE_DEFINITIONS})
target_include_directories(${DELEGATE_APP} PRIVATE ${DMQ_INCLUDE_DIR})
dmq_add_mqtt_bridge(${DELEGATE_APP})

source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})
