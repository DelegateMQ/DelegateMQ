# MqttBridge.cmake
# Adds tools/bridge/mqtt/MqttBridge (an MQTT output for the JsonTopics layer)
# and the shared bridge layer to a target, building Eclipse Paho MQTT C from the
# workspace checkout (../mqtt, fetched by 01_fetch_repos.py) as a static
# library. Building from source keeps Paho's architecture matching the
# target's (e.g. the Win32 Pumptron build) instead of relying on a prebuilt
# library.
#
# Usage:
#   include(<DelegateMQ>/tools/bridge/mqtt/MqttBridge.cmake)
#   dmq_add_mqtt_bridge(<target>)
#
# The target must already include DelegateMQ (DMQ_DATABUS=ON).

include("${CMAKE_CURRENT_LIST_DIR}/../common/BridgeCommon.cmake")

set(DMQ_MQTT_BRIDGE_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(DMQ_PAHO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../mqtt" ABSOLUTE)

function(dmq_add_mqtt_bridge target)
    if(NOT TARGET paho-mqtt3c-static)
        if(NOT EXISTS "${DMQ_PAHO_ROOT}/src/MQTTClient.h")
            message(FATAL_ERROR "Paho MQTT C not found at ${DMQ_PAHO_ROOT}. Run 01_fetch_repos.py first.")
        endif()
        # Static library only: no shared libs, tests, samples, docs or TLS.
        set(PAHO_BUILD_STATIC TRUE CACHE BOOL "MqttBridge: static Paho" FORCE)
        set(PAHO_BUILD_SHARED FALSE CACHE BOOL "MqttBridge: static Paho" FORCE)
        set(PAHO_ENABLE_TESTING FALSE CACHE BOOL "MqttBridge: no Paho tests" FORCE)
        set(PAHO_BUILD_SAMPLES FALSE CACHE BOOL "MqttBridge: no Paho samples" FORCE)
        set(PAHO_BUILD_DOCUMENTATION FALSE CACHE BOOL "MqttBridge: no Paho docs" FORCE)
        set(PAHO_WITH_SSL FALSE CACHE BOOL "MqttBridge: no TLS" FORCE)
        # Paho 1.3.x declares cmake_minimum_required(2.8.12), which CMake 4 rejects.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        add_subdirectory("${DMQ_PAHO_ROOT}" "${CMAKE_BINARY_DIR}/paho-mqtt" EXCLUDE_FROM_ALL)
    endif()

    dmq_add_bridge_common(${target})

    set(bridge_sources
        "${DMQ_MQTT_BRIDGE_DIR}/MqttBridge.cpp"
        "${DMQ_MQTT_BRIDGE_DIR}/MqttBridge.h"
    )
    target_sources(${target} PRIVATE ${bridge_sources})
    source_group("Tools Files/MQTT Bridge" FILES ${bridge_sources})
    target_include_directories(${target} PRIVATE "${DMQ_MQTT_BRIDGE_DIR}" "${DMQ_PAHO_ROOT}/src")
    target_link_libraries(${target} PRIVATE paho-mqtt3c-static)
endfunction()
