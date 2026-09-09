# DelegateMQ Defaults
# 
# This module auto-selects default DelegateMQ build options based on the target platform.
# Options can be overridden by setting the variables before including DelegateMQ.cmake.

# --- Threading Defaults ---
if(NOT DEFINED DMQ_THREAD)
    if(WIN32 OR UNIX)
        set(DMQ_THREAD "DMQ_THREAD_STDLIB")
    elseif(FREERTOS OR FREE_RTOS)
        set(DMQ_THREAD "DMQ_THREAD_FREERTOS")
    elseif(THREADX)
        set(DMQ_THREAD "DMQ_THREAD_THREADX")
    elseif(ZEPHYR)
        set(DMQ_THREAD "DMQ_THREAD_ZEPHYR")
    elseif(CMSIS_RTOS2)
        set(DMQ_THREAD "DMQ_THREAD_CMSIS_RTOS2")
    else()
        set(DMQ_THREAD "DMQ_THREAD_NONE")
    endif()
endif()

# --- Transport Defaults ---
if(NOT DEFINED DMQ_TRANSPORT)
    if(WIN32)
        set(DMQ_TRANSPORT "DMQ_TRANSPORT_WIN32_UDP")
    elseif(UNIX AND NOT APPLE)
        set(DMQ_TRANSPORT "DMQ_TRANSPORT_LINUX_UDP")
    else()
        set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
    endif()
endif()

# --- Serialization Defaults ---
if(NOT DEFINED DMQ_SERIALIZE)
    if(WIN32 OR UNIX)
        set(DMQ_SERIALIZE "DMQ_SERIALIZE_SERIALIZE")
    else()
        set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
    endif()
endif()

# --- Utility Class Defaults ---
if(NOT DEFINED DMQ_UTIL)
    set(DMQ_UTIL "ON")
endif()

# --- DataBus Defaults ---
# Keyed off DMQ_THREAD (the actual target), not WIN32/UNIX (the host doing
# the compiling) -- DMQ_THREAD is already resolved above, and every RTOS
# simulator sample (freertos-linux, threadx-linux, zephyr-linux) compiles
# with a host GCC on a UNIX box despite targeting an embedded thread port,
# which used to default DMQ_DATABUS on for them too. DataBus's
# NetworkConnect.h reaches for the host's own BSD socket headers
# unconditionally on Linux/macOS/Windows, which both wastes flash on a real
# embedded build and can conflict outright with a target's own native
# network stack headers (e.g. Zephyr's <zephyr/net/socket.h> redefining
# sockaddr_in et al.) if a sample also pulls those in directly. RTOS/bare-
# metal targets that genuinely want DataBus (e.g. databus-freertos/server)
# already opt in explicitly by setting DMQ_DATABUS "ON" themselves.
if(NOT DEFINED DMQ_DATABUS)
    if(DMQ_THREAD STREQUAL "DMQ_THREAD_STDLIB" OR DMQ_THREAD STREQUAL "DMQ_THREAD_WIN32" OR DMQ_THREAD STREQUAL "DMQ_THREAD_QT")
        set(DMQ_DATABUS "ON")
    else()
        set(DMQ_DATABUS "OFF")
    endif()
endif()

# --- DataBus Tools Defaults ---
if(NOT DEFINED DMQ_DATABUS_TOOLS)
    if(DMQ_DATABUS STREQUAL "ON" AND (WIN32 OR UNIX))
        set(DMQ_DATABUS_TOOLS "ON")
    else()
        set(DMQ_DATABUS_TOOLS "OFF")
    endif()
endif()

# --- Allocator Defaults ---
if(NOT DEFINED DMQ_ALLOCATOR)
    set(DMQ_ALLOCATOR "OFF")
endif()

# --- Assert Defaults ---
if(NOT DEFINED DMQ_ASSERTS)
    set(DMQ_ASSERTS "OFF")
endif()

# --- Logging Defaults ---
if(NOT DEFINED DMQ_LOG)
    set(DMQ_LOG "OFF")
endif()
