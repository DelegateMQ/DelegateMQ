include(CMakeFindDependencyMacro)

# Previously, this checked if "${_var}" (the variable name) existed as a file.
# Now it correctly checks "${_file}" (the actual path).
macro(set_and_check _var _file)
    set(${_var} "${_file}")
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist!")
    endif()
endmacro()

# ---------------------------------------------------------------------------
# ZeroMQ library package
# ---------------------------------------------------------------------------
if(DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ZEROMQ")
    set(ZMQ_ROOT "${DMQ_ROOT_DIR}/../../../zeromq/install")
    list(APPEND CMAKE_PREFIX_PATH "${ZMQ_ROOT}")

    find_package(ZeroMQ CONFIG REQUIRED)

    if (ZeroMQ_FOUND)
        message(STATUS "ZeroMQ found: ${ZeroMQ_VERSION}")
        if(TARGET libzmq-static AND NOT TARGET libzmq)
            add_library(libzmq ALIAS libzmq-static)
        endif()
        add_compile_definitions(ZMQ_STATIC)
    else()
        message(FATAL_ERROR "ZeroMQ not found in ${ZMQ_ROOT}")
    endif()
endif()

# ---------------------------------------------------------------------------
# NNG library
# ---------------------------------------------------------------------------
if(DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_NNG")
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # Fixed: Point to the 'install' folder where headers actually live after build
        set_and_check(NNG_INCLUDE_DIR "${DMQ_ROOT_DIR}/../../../nng/install/include")
        set_and_check(NNG_LIBRARY_DIR "${DMQ_ROOT_DIR}/../../../nng/install/lib")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_and_check(NNG_INCLUDE_DIR "/usr/local/include/nng")
        set_and_check(NNG_LIBRARY_DIR "/usr/local/include/nng")
    endif()
endif()

# ---------------------------------------------------------------------------
# MQTT C library
# ---------------------------------------------------------------------------
if(DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_MQTT")
    # Define root relative to DelegateMQ source
    set(MQTT_ROOT "${DMQ_ROOT_DIR}/../../../mqtt")

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # 1. Headers
        # Note: If 'install' step wasn't run, headers might be in "${MQTT_ROOT}/src"
        set_and_check(MQTT_INCLUDE_DIR "${MQTT_ROOT}/install/include")
        
        # 2. Library (Lib) Directory (for linking)
        # Point to build/src where the .lib files are generated
        set(MQTT_LIBRARY_DIR "${MQTT_ROOT}/build/src")
        
        # 3. Binary (DLL) Directory (for runtime copy)
        # Point to build/src where the .dll files are generated
        set(MQTT_BINARY_DIR "${MQTT_ROOT}/build/src")

        # Fallback for manual definition if needed
        set(MQTT_LIBRARY 
            "${MQTT_LIBRARY_DIR}/paho-mqtt3c.lib"
            "ws2_32" "Rpcrt4" "Crypt32"
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_and_check(MQTT_INCLUDE_DIR "/usr/local/include")
        set_and_check(MQTT_LIBRARY "/usr/local/lib/libpaho-mqtt3c.so")
        set_and_check(MQTT_BINARY_DIR "/usr/local/bin")
    endif()
endif()

# ---------------------------------------------------------------------------
# libserialport library
# ---------------------------------------------------------------------------
if(DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_SERIAL_PORT")
    # Define the root of libserialport relative to the DelegateMQ source directory
    # Structure: Workspace/DelegateMQ/src/delegate-mq/External.cmake -> Workspace/libserialport
    set(SERIALPORT_ROOT "${DMQ_ROOT_DIR}/../../../libserialport")

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # 1. Header: Found in the root of the source folder (libserialport.h)
        set_and_check(SERIALPORT_INCLUDE_DIR "${SERIALPORT_ROOT}") 
        
        # 2. Library: Found in the Visual Studio build output folder
        # Note: Change 'Debug' to 'Release' if building in Release mode
        set_and_check(SERIALPORT_LIBRARY_DIR "${SERIALPORT_ROOT}/x64/Debug")

    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # 1. Header: Found in the root of the source folder
        set_and_check(SERIALPORT_INCLUDE_DIR "${SERIALPORT_ROOT}") 
        
        # 2. Library: Found in the local build directory
        # Standard Autotools builds (./configure && make) place .so files in the hidden .libs/ directory
        if(EXISTS "${SERIALPORT_ROOT}/.libs")
            set_and_check(SERIALPORT_LIBRARY_DIR "${SERIALPORT_ROOT}/.libs")
        else()
            # Fallback: Check root if built differently
            set_and_check(SERIALPORT_LIBRARY_DIR "${SERIALPORT_ROOT}")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# lwIP library (For ARM/Embedded)
# ---------------------------------------------------------------------------
if(DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ARM_LWIP_UDP" OR DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ARM_LWIP_NETCONN_UDP")
    
    # Adjust this path if your lwIP root is different
    set(LWIP_ROOT "${DMQ_ROOT_DIR}/../../../lwip")

    # lwIP requires 3 main include directories for the socket API
    set_and_check(LWIP_INCLUDE_DIR_1 "${LWIP_ROOT}/src/include")
    set_and_check(LWIP_INCLUDE_DIR_2 "${LWIP_ROOT}/src/include/ipv4")
    
    # Often required for lwipopts.h or arch/cc.h.
    # NOTE: You may need to customize this depending on where your project keeps 'lwipopts.h'
    set(LWIP_INCLUDE_DIR_3 "${LWIP_ROOT}/contrib/ports/unix/port/include")

    set(LWIP_INCLUDE_DIRS 
        "${LWIP_INCLUDE_DIR_1}"
        "${LWIP_INCLUDE_DIR_2}"
        "${LWIP_INCLUDE_DIR_3}"
    )
    
    # Collect core lwIP sources
    file(GLOB_RECURSE LWIP_SOURCES 
        "${LWIP_ROOT}/src/core/*.c"
        "${LWIP_ROOT}/src/api/*.c"
        "${LWIP_ROOT}/src/netif/*.c"
    )
endif()

# ---------------------------------------------------------------------------
# NetX / NetX Duo library (For ThreadX)
# ---------------------------------------------------------------------------
if(DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_THREADX_UDP")
    # Adjust this path to your NetX / NetX Duo root
    set(NETX_ROOT "${DMQ_ROOT_DIR}/../../../netxduo")
    
    # Verify root
    set_and_check(NETX_INCLUDE_DIR "${NETX_ROOT}/common/inc")
    
    # Collect common sources
    file(GLOB NETX_SOURCES 
        "${NETX_ROOT}/common/src/*.c"
        "${NETX_ROOT}/common/inc/*.h"
    )
    
    # Note: Architecture-specific ports usually handled by your main ThreadX build system
endif()
    
# ---------------------------------------------------------------------------
# MessagePack
# ---------------------------------------------------------------------------
if(DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_MSGPACK")
    add_compile_definitions(MSGPACK_NO_BOOST)
    set_and_check(MSGPACK_INCLUDE_DIR "${DMQ_ROOT_DIR}/../../../msgpack-c/include")
endif()

# ---------------------------------------------------------------------------
# Cereal
# ---------------------------------------------------------------------------
if(DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_CEREAL")
    set_and_check(CEREAL_INCLUDE_DIR "${DMQ_ROOT_DIR}/../../../cereal/include")
endif()

# ---------------------------------------------------------------------------
# Bitsery
# ---------------------------------------------------------------------------
if(DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_BITSERY")
    set_and_check(BITSERY_INCLUDE_DIR "${DMQ_ROOT_DIR}/../../../bitsery/include")
endif()

# ---------------------------------------------------------------------------
# RapidJSON
# ---------------------------------------------------------------------------
if(DMQ_SERIALIZE STREQUAL "DMQ_SERIALIZE_RAPIDJSON")
    set_and_check(RAPIDJSON_INCLUDE_DIR "${DMQ_ROOT_DIR}/../../../rapidjson/include")

    # Globally silence C++17 std::iterator warnings on RapidJSON library
    if(MSVC)
        add_compile_definitions(_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
    endif()
endif()

# ---------------------------------------------------------------------------
# FreeRTOS
# ---------------------------------------------------------------------------
if(DMQ_THREAD STREQUAL "DMQ_THREAD_FREERTOS")
    set_and_check(FREERTOS_ROOT_DIR "${DMQ_ROOT_DIR}/../../../FreeRTOS")
    
    file(GLOB FREERTOS_SOURCES 
        "${FREERTOS_ROOT_DIR}/*.c"
        "${FREERTOS_ROOT_DIR}/include/*.h"
    )
    
    if(WIN32)
        set(FREERTOS_PORT_DIR "MSVC-MingW")
    elseif(APPLE)
        set(FREERTOS_PORT_DIR "ThirdParty/GCC/Posix")
    elseif(UNIX)
        set(FREERTOS_PORT_DIR "ThirdParty/GCC/Posix")
    else()
        message(FATAL_ERROR "DelegateMQ: Unsupported OS for FreeRTOS simulation.")
    endif()


    list(APPEND FREERTOS_SOURCES
        "${FREERTOS_ROOT_DIR}/portable/${FREERTOS_PORT_DIR}/port.c"
        "${FREERTOS_ROOT_DIR}/portable/MemMang/heap_5.c"
    )
    
    if(UNIX AND NOT APPLE)
        file(GLOB POSIX_UTILS "${FREERTOS_ROOT_DIR}/portable/${FREERTOS_PORT_DIR}/utils/*.c")
        list(APPEND FREERTOS_SOURCES ${POSIX_UTILS})
        set(FREERTOS_LIBRARIES pthread rt)
    endif()
endif()

# ---------------------------------------------------------------------------
# ThreadX
# ---------------------------------------------------------------------------
if(DMQ_THREAD STREQUAL "DMQ_THREAD_THREADX")
    set_and_check(THREADX_ROOT_DIR "${DMQ_ROOT_DIR}/../../../threadx")
    
    # Collect Common Core Sources
    file(GLOB THREADX_SOURCES 
        "${THREADX_ROOT_DIR}/common/src/*.c"
        "${THREADX_ROOT_DIR}/common/inc/*.h"
    )

    # Note: You generally need to append architecture-specific port sources here
    # Example for Windows Simulation (MSVC):
    if(MSVC)
        # file(GLOB PORT_SOURCES "${THREADX_ROOT_DIR}/ports/win32/vs_2019/src/*.c")
        # list(APPEND THREADX_SOURCES ${PORT_SOURCES})
    endif()
endif()

# ---------------------------------------------------------------------------
# Zephyr
# ---------------------------------------------------------------------------
if(DMQ_THREAD STREQUAL "DMQ_THREAD_ZEPHYR")
    # Zephyr is a build system, not just a library.
    # We typically do NOT manually glob source files here.
    # The application's main CMakeLists.txt must call `find_package(Zephyr)` 
    # which sets up the include paths and kernel linking automatically.
    # Optional: Just verify the root directory exists if you want to be safe
    set_and_check(ZEPHYR_ROOT_DIR "${DMQ_ROOT_DIR}/../../../zephyr")
    
    # Do NOT glob sources. Zephyr builds itself.
endif()

# ---------------------------------------------------------------------------
# CMSIS-RTOS2
# ---------------------------------------------------------------------------
if(DMQ_THREAD STREQUAL "DMQ_THREAD_CMSIS_RTOS2")
    # CMSIS is usually provided by the IDE (Keil/IAR) or a silicon vendor pack (STM32Cube).
    # We do NOT glob sources here by default.
    # Users must ensure the 'cmsis_os2.h' path is in their include path.
endif()

# ---------------------------------------------------------------------------
# Qt Framework
# ---------------------------------------------------------------------------
if(DMQ_THREAD STREQUAL "DMQ_THREAD_QT")
    add_compile_definitions(DMQ_THREAD_QT)
    
    # Find the Qt packages (Core is usually sufficient for QThread)
    find_package(Qt6 COMPONENTS Core REQUIRED)
    # Or Qt5: find_package(Qt5 COMPONENTS Core REQUIRED)

    # Collect the source files
    file(GLOB THREAD_SOURCES 
        "${DMQ_ROOT_DIR}/port/os/qt/*.cpp" 
        "${DMQ_ROOT_DIR}/port/os/qt/*.h" 
    )
    
    # Important: Enable CMAKE_AUTOMOC for the Qt Meta-Object system 
    # (needed for signals/slots in Thread.h)
    set(CMAKE_AUTOMOC ON)
    
    # You might need to link Qt6::Core to your main target later:
    # target_link_libraries(YourApp PRIVATE Qt6::Core)
endif()

# ---------------------------------------------------------------------------
# spdlog
# ---------------------------------------------------------------------------
if (DMQ_LOG STREQUAL "ON")
    set_and_check(SPDLOG_INCLUDE_DIR "${DMQ_ROOT_DIR}/../../../spdlog/include")
endif()