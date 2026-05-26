# Port build options

if (DMQ_THREAD STREQUAL "DMQ_THREAD_STDLIB")
    add_compile_definitions(DMQ_THREAD_STDLIB)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/stdlib/*.c*" 
        "${DMQ_ROOT_DIR}/port/os/stdlib/*.h" 
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_WIN32")
    add_compile_definitions(DMQ_THREAD_WIN32)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/win32/*.c*" 
        "${DMQ_ROOT_DIR}/port/os/win32/*.h" 
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_FREERTOS")
    add_compile_definitions(DMQ_THREAD_FREERTOS)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/freertos/*.c*" 
        "${DMQ_ROOT_DIR}/port/os/freertos/*.h" 
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_THREADX")
    add_compile_definitions(DMQ_THREAD_THREADX)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/threadx/*.c*" 
        "${DMQ_ROOT_DIR}/port/os/threadx/*.h" 
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_ZEPHYR")
    add_compile_definitions(DMQ_THREAD_ZEPHYR)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/zephyr/*.c*" 
        "${DMQ_ROOT_DIR}/port/os/zephyr/*.h" 
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_CMSIS_RTOS2")
    add_compile_definitions(DMQ_THREAD_CMSIS_RTOS2)
    file(GLOB THREAD_SOURCES
        "${DMQ_ROOT_DIR}/port/os/cmsis-rtos2/*.c*"
        "${DMQ_ROOT_DIR}/port/os/cmsis-rtos2/*.h"
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_QT")
    add_compile_definitions(DMQ_THREAD_QT)
    file(GLOB THREAD_SOURCES
        "${DMQ_ROOT_DIR}/port/os/qt/*.c*"
        "${DMQ_ROOT_DIR}/port/os/qt/*.h"
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_NONE")
    add_compile_definitions(DMQ_THREAD_NONE)
else()
    message(FATAL_ERROR "Must set DMQ_THREAD option.")
endif()

if (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_NONE")
    add_compile_definitions(DMQ_SERIALIZE_NONE)
elseif (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_SERIALIZE")
    add_compile_definitions(DMQ_SERIALIZE_SERIALIZE)
    file(GLOB SERIALIZE_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/serialize/serialize/*.h")
elseif (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_RAPIDJSON")
    add_compile_definitions(DMQ_SERIALIZE_RAPIDJSON)
    file(GLOB SERIALIZE_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/serialize/rapidjson/*.h")
elseif (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_MSGPACK")
    add_compile_definitions(DMQ_SERIALIZE_MSGPACK)
    file(GLOB SERIALIZE_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/serialize/msgpack/*.h")
elseif (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_CEREAL")
    add_compile_definitions(DMQ_SERIALIZE_CEREAL)
    file(GLOB SERIALIZE_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/serialize/cereal/*.h")
elseif (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_BITSERY")
    add_compile_definitions(DMQ_SERIALIZE_BITSERY)
    file(GLOB SERIALIZE_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/serialize/bitsery/*.h")
else()
    message(FATAL_ERROR "Must set DMQ_SERIALIZE option.")
endif()

if (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_NONE")
    add_compile_definitions(DMQ_TRANSPORT_NONE)
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ZEROMQ")
    add_compile_definitions(DMQ_TRANSPORT_ZEROMQ)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/zeromq/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_NNG")
    add_compile_definitions(DMQ_TRANSPORT_NNG)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/nng/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_MQTT")
    add_compile_definitions(DMQ_TRANSPORT_MQTT)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/mqtt/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_WIN32_PIPE")
    add_compile_definitions(DMQ_TRANSPORT_WIN32_PIPE)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/win32-pipe/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_WIN32_UDP")
    add_compile_definitions(DMQ_TRANSPORT_WIN32_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/win32-udp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_WIN32_TCP")
    add_compile_definitions(DMQ_TRANSPORT_WIN32_TCP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/win32-tcp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_LINUX_UDP")
    add_compile_definitions(DMQ_TRANSPORT_LINUX_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/linux-udp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_LINUX_TCP")
    add_compile_definitions(DMQ_TRANSPORT_LINUX_TCP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/linux-tcp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_SERIAL_PORT")
    add_compile_definitions(DMQ_TRANSPORT_SERIAL_PORT)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/serial/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ARM_LWIP_UDP")
    add_compile_definitions(DMQ_TRANSPORT_ARM_LWIP_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/arm-lwip-udp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ARM_LWIP_NETCONN_UDP")
    add_compile_definitions(DMQ_TRANSPORT_ARM_LWIP_NETCONN_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/arm-lwip-netconn-udp/*.h")    
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_THREADX_UDP")
    add_compile_definitions(DMQ_TRANSPORT_THREADX_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/netx-udp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_STM32_UART")
    add_compile_definitions(DMQ_TRANSPORT_STM32_UART)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/stm32-uart/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ZEPHYR_UDP")
    add_compile_definitions(DMQ_TRANSPORT_ZEPHYR_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/zephyr-udp/*.h")
else()
    message(FATAL_ERROR "Must set DMQ_TRANSPORT option.")
endif()

file(GLOB DISPATCHER_SOURCES CONFIGURE_DEPENDS
    "${DMQ_ROOT_DIR}/extras/dispatcher/*.h" 
)

if (DMQ_ALLOCATOR STREQUAL "ON")
    add_compile_definitions(DMQ_ALLOCATOR)
    file(GLOB ALLOCATOR_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/extras/allocator/*.c*" 
        "${DMQ_ROOT_DIR}/extras/allocator/*.h" 
    )
endif()

if (DMQ_UTIL STREQUAL "ON")
    if (DMQ_THREAD STREQUAL "DMQ_THREAD_NONE")
        # Bare metal: Only include utilities that DON'T need mutexes
        # Fault.cpp lives in port/fault/ and is always included separately
        file(GLOB UTIL_SOURCES CONFIGURE_DEPENDS
            "${DMQ_ROOT_DIR}/extras/util/Fault.h"
            # Explicitly exclude Timer.cpp and AsyncInvoke.cpp
        )
    else()
        # OS/RTOS present: Include everything 
        file(GLOB UTIL_SOURCES CONFIGURE_DEPENDS
            "${DMQ_ROOT_DIR}/extras/util/*.c*"
            "${DMQ_ROOT_DIR}/extras/util/*.h"
        )
    endif()
endif()

# Fault handler port — always included; override by filtering Fault.cpp from DMQ_PORT_SOURCES
file(GLOB FAULT_SOURCES CONFIGURE_DEPENDS
    "${DMQ_ROOT_DIR}/port/fault/Fault.cpp"
)

if (DMQ_ASSERTS STREQUAL "ON")
    add_compile_definitions(DMQ_ASSERTS)
endif()

if (DMQ_LOG STREQUAL "ON")
    add_compile_definitions(DMQ_LOG)
endif()

# Port sources: OS/transport/serialization adapters (port/ directory)
set(DMQ_PORT_ONLY_SOURCES "")
list(APPEND DMQ_PORT_ONLY_SOURCES ${TRANSPORT_SOURCES})
list(APPEND DMQ_PORT_ONLY_SOURCES ${SERIALIZE_SOURCES})
list(APPEND DMQ_PORT_ONLY_SOURCES ${THREAD_SOURCES})
list(APPEND DMQ_PORT_ONLY_SOURCES ${OS_SOURCES})
list(APPEND DMQ_PORT_ONLY_SOURCES ${FAULT_SOURCES})

# Extras sources: optional higher-level infrastructure (extras/ directory)
set(DMQ_EXTRAS_SOURCES "")
list(APPEND DMQ_EXTRAS_SOURCES ${DISPATCHER_SOURCES})
list(APPEND DMQ_EXTRAS_SOURCES ${ALLOCATOR_SOURCES})
list(APPEND DMQ_EXTRAS_SOURCES ${UTIL_SOURCES})

if (DMQ_DATABUS STREQUAL "ON")
    add_compile_definitions(DMQ_DATABUS)
    file(GLOB DATABUS_SOURCES CONFIGURE_DEPENDS "${DMQ_ROOT_DIR}/extras/databus/*.h")
    list(APPEND DMQ_EXTRAS_SOURCES ${DATABUS_SOURCES})
endif()

# Combined list for compilation
set(DMQ_PORT_SOURCES "")
list(APPEND DMQ_PORT_SOURCES ${DMQ_PORT_ONLY_SOURCES})
list(APPEND DMQ_PORT_SOURCES ${DMQ_EXTRAS_SOURCES})

# Collect all DelegateMQ files
file(GLOB DMQ_LIB_SOURCES CONFIGURE_DEPENDS
    "${DMQ_ROOT_DIR}/*.h"
    "${DMQ_ROOT_DIR}/delegate/*.h"
)