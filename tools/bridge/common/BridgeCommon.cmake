# BridgeCommon.cmake
# Adds the shared JSON bridge layer (JsonTopics, BridgeJson.h) to a target.
# Included by each JSON bridge's .cmake (e.g. ../mqtt/MqttBridge.cmake);
# applications normally don't include this directly.
#
# Usage: dmq_add_bridge_common(<target>)   (safe to call more than once)

set(DMQ_BRIDGE_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(dmq_add_bridge_common target)
    get_target_property(already ${target} DMQ_BRIDGE_COMMON_ADDED)
    if(already)
        return()
    endif()
    set_target_properties(${target} PROPERTIES DMQ_BRIDGE_COMMON_ADDED TRUE)

    set(common_sources
        "${DMQ_BRIDGE_COMMON_DIR}/JsonTopics.cpp"
        "${DMQ_BRIDGE_COMMON_DIR}/JsonTopics.h"
        "${DMQ_BRIDGE_COMMON_DIR}/BridgeJson.h"
    )
    target_sources(${target} PRIVATE ${common_sources})
    source_group("Tools Files/Bridge Common" FILES ${common_sources})
    target_include_directories(${target} PRIVATE "${DMQ_BRIDGE_COMMON_DIR}")
endfunction()
