# Port build options

# Start each include from empty lists: only some branches below set these, so a
# value left by an earlier include in this scope (or a parent's) would leak
# into this configuration.
foreach(_dmq_list THREAD SERIALIZE TRANSPORT OS DISPATCHER ALLOCATOR UTIL RPC FAULT DATABUS)
    set(${_dmq_list}_SOURCES "")
endforeach()

if (DMQ_THREAD STREQUAL "DMQ_THREAD_STDLIB")
    dmq_compile_definitions(DMQ_THREAD_STDLIB)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/stdlib/*.c*" 
        "${DMQ_ROOT_DIR}/port/os/stdlib/*.h" 
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_WIN32")
    dmq_compile_definitions(DMQ_THREAD_WIN32)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/win32/*.c*"
        "${DMQ_ROOT_DIR}/port/os/win32/*.h"
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_POSIX")
    dmq_compile_definitions(DMQ_THREAD_POSIX)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/posix/*.c*"
        "${DMQ_ROOT_DIR}/port/os/posix/*.h"
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_FREERTOS")
    dmq_compile_definitions(DMQ_THREAD_FREERTOS)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/freertos/*.c*" 
        "${DMQ_ROOT_DIR}/port/os/freertos/*.h" 
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_THREADX")
    dmq_compile_definitions(DMQ_THREAD_THREADX)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/threadx/*.c*" 
        "${DMQ_ROOT_DIR}/port/os/threadx/*.h" 
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_ZEPHYR")
    dmq_compile_definitions(DMQ_THREAD_ZEPHYR)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/zephyr/*.c*" 
        "${DMQ_ROOT_DIR}/port/os/zephyr/*.h" 
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_CMSIS_RTOS2")
    dmq_compile_definitions(DMQ_THREAD_CMSIS_RTOS2)
    file(GLOB THREAD_SOURCES
        "${DMQ_ROOT_DIR}/port/os/cmsis-rtos2/*.c*"
        "${DMQ_ROOT_DIR}/port/os/cmsis-rtos2/*.h"
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_NUTTX")
    dmq_compile_definitions(DMQ_THREAD_NUTTX)
    file(GLOB THREAD_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/port/os/nuttx/*.c*"
        "${DMQ_ROOT_DIR}/port/os/nuttx/*.h"
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_QT")
    dmq_compile_definitions(DMQ_THREAD_QT)
    file(GLOB THREAD_SOURCES
        "${DMQ_ROOT_DIR}/port/os/qt/*.c*"
        "${DMQ_ROOT_DIR}/port/os/qt/*.h"
    )
elseif (DMQ_THREAD STREQUAL "DMQ_THREAD_NONE")
    dmq_compile_definitions(DMQ_THREAD_NONE)
else()
    message(FATAL_ERROR "Must set DMQ_THREAD option.")
endif()

if (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_NONE")
    dmq_compile_definitions(DMQ_SERIALIZE_NONE)
elseif (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_SERIALIZE")
    dmq_compile_definitions(DMQ_SERIALIZE_SERIALIZE)
    file(GLOB SERIALIZE_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/serialize/serialize/*.h")
elseif (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_RAPIDJSON")
    dmq_compile_definitions(DMQ_SERIALIZE_RAPIDJSON)
    file(GLOB SERIALIZE_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/serialize/rapidjson/*.h")
elseif (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_MSGPACK")
    dmq_compile_definitions(DMQ_SERIALIZE_MSGPACK)
    file(GLOB SERIALIZE_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/serialize/msgpack/*.h")
elseif (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_CEREAL")
    dmq_compile_definitions(DMQ_SERIALIZE_CEREAL)
    file(GLOB SERIALIZE_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/serialize/cereal/*.h")
elseif (DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_BITSERY")
    dmq_compile_definitions(DMQ_SERIALIZE_BITSERY)
    file(GLOB SERIALIZE_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/serialize/bitsery/*.h")
else()
    message(FATAL_ERROR "Must set DMQ_SERIALIZE option.")
endif()

if (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_NONE")
    dmq_compile_definitions(DMQ_TRANSPORT_NONE)
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ZEROMQ")
    dmq_compile_definitions(DMQ_TRANSPORT_ZEROMQ)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/zeromq/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_NNG")
    dmq_compile_definitions(DMQ_TRANSPORT_NNG)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/nng/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_MQTT")
    dmq_compile_definitions(DMQ_TRANSPORT_MQTT)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/mqtt/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_WIN32_PIPE")
    dmq_compile_definitions(DMQ_TRANSPORT_WIN32_PIPE)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/win32-pipe/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_WIN32_UDP")
    dmq_compile_definitions(DMQ_TRANSPORT_WIN32_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/win32-udp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_WIN32_TCP")
    dmq_compile_definitions(DMQ_TRANSPORT_WIN32_TCP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/win32-tcp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_LINUX_UDP")
    dmq_compile_definitions(DMQ_TRANSPORT_LINUX_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/linux-udp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_LINUX_UDS")
    dmq_compile_definitions(DMQ_TRANSPORT_LINUX_UDS)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/linux-uds/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_LINUX_TCP")
    dmq_compile_definitions(DMQ_TRANSPORT_LINUX_TCP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/linux-tcp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_SERIAL_PORT")
    dmq_compile_definitions(DMQ_TRANSPORT_SERIAL_PORT)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/serial/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ARM_LWIP_UDP")
    dmq_compile_definitions(DMQ_TRANSPORT_ARM_LWIP_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/arm-lwip-udp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ARM_LWIP_NETCONN_UDP")
    dmq_compile_definitions(DMQ_TRANSPORT_ARM_LWIP_NETCONN_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/arm-lwip-netconn-udp/*.h")    
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_THREADX_UDP")
    dmq_compile_definitions(DMQ_TRANSPORT_THREADX_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/netx-udp/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_STM32_UART")
    dmq_compile_definitions(DMQ_TRANSPORT_STM32_UART)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/stm32-uart/*.h")
elseif (DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ZEPHYR_UDP")
    dmq_compile_definitions(DMQ_TRANSPORT_ZEPHYR_UDP)
    file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS
 "${DMQ_ROOT_DIR}/port/transport/zephyr-udp/*.h")
else()
    message(FATAL_ERROR "Must set DMQ_TRANSPORT option.")
endif()

file(GLOB DISPATCHER_SOURCES CONFIGURE_DEPENDS
    "${DMQ_ROOT_DIR}/extras/dispatcher/*.h" 
)

if (DMQ_ALLOCATOR STREQUAL "ON")
    dmq_compile_definitions(DMQ_ALLOCATOR)
    file(GLOB ALLOCATOR_SOURCES CONFIGURE_DEPENDS
        "${DMQ_ROOT_DIR}/extras/allocator/*.c*" 
        "${DMQ_ROOT_DIR}/extras/allocator/*.h" 
    )
endif()

if (DMQ_UTIL STREQUAL "ON")
    if (DMQ_THREAD STREQUAL "DMQ_THREAD_NONE")
        # Bare metal: only include utilities with no dmq::os::Thread/IThread
        # dependency. Fault.cpp lives in port/fault/ and is always included
        # separately. Timer.cpp is included -- it only needs dmq::Clock/
        # dmq::CriticalSection (BareMetalClock.h/BareMetalCriticalSection.h
        # exist specifically so Timer::ProcessTimers() can be driven from a
        # hardware ISR, e.g. SysTick_Handler, with no RTOS at all -- see
        # BareMetalCriticalSection.h's own doc comment). AsyncInvoke.h/
        # TimerDelegate.h/RemoteDispatcher.cpp/ThreadMonitor.cpp all genuinely
        # need a real dmq::IThread and stay excluded.
        file(GLOB UTIL_SOURCES CONFIGURE_DEPENDS
            "${DMQ_ROOT_DIR}/extras/util/Fault.h"
            "${DMQ_ROOT_DIR}/extras/util/Timer.h"
            "${DMQ_ROOT_DIR}/extras/util/Timer.cpp"
        )
    else()
        # OS/RTOS present: Include everything
        file(GLOB UTIL_SOURCES CONFIGURE_DEPENDS
            "${DMQ_ROOT_DIR}/extras/util/*.c*"
            "${DMQ_ROOT_DIR}/extras/util/*.h"
        )
        # RPC sources (RemoteDispatcher/RemoteEndpoint) need a real dmq::IThread,
        # same reasoning as the bare-metal exclusion above -- only globbed here.
        file(GLOB RPC_SOURCES CONFIGURE_DEPENDS
            "${DMQ_ROOT_DIR}/extras/rpc/*.c*"
            "${DMQ_ROOT_DIR}/extras/rpc/*.h"
        )
    endif()
endif()

# Fault handler port — always included; override by filtering Fault.cpp from DMQ_PORT_SOURCES
file(GLOB FAULT_SOURCES CONFIGURE_DEPENDS
    "${DMQ_ROOT_DIR}/port/fault/Fault.cpp"
)

if (DMQ_ASSERTS STREQUAL "ON")
    dmq_compile_definitions(DMQ_ASSERTS)
endif()

if (DMQ_LOG STREQUAL "ON")
    dmq_compile_definitions(DMQ_LOG)
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
list(APPEND DMQ_EXTRAS_SOURCES ${RPC_SOURCES})

if (DMQ_DATABUS STREQUAL "ON")
    dmq_compile_definitions(DMQ_DATABUS)
    file(GLOB DATABUS_SOURCES CONFIGURE_DEPENDS "${DMQ_ROOT_DIR}/extras/databus/*.h")
    list(APPEND DMQ_EXTRAS_SOURCES ${DATABUS_SOURCES})
endif()

# DelegateSelect.h turns DataBus and its tools on for desktop builds unless
# DMQ_DATABUS_OFF / DMQ_DATABUS_TOOLS_OFF is defined, so an explicit
# set(... "OFF") must be passed on for it to take effect there. A default
# Defaults.cmake filled in is left to DelegateSelect.h, as before.
list(FIND _DMQ_AUTO_DEFAULTS DMQ_DATABUS _dmq_databus_auto)
if (NOT DMQ_DATABUS AND _dmq_databus_auto EQUAL -1)
    dmq_compile_definitions(DMQ_DATABUS_OFF)
endif()
list(FIND _DMQ_AUTO_DEFAULTS DMQ_DATABUS_TOOLS _dmq_tools_auto)
if (NOT DMQ_DATABUS_TOOLS AND _dmq_tools_auto EQUAL -1)
    dmq_compile_definitions(DMQ_DATABUS_TOOLS_OFF)
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