cmake_minimum_required(VERSION 3.16)

# --- ARM CROSS-COMPILATION SETUP ---
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Flags
set(ARM_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 --specs=nano.specs -lc -lgcc -lnosys")
set(CMAKE_C_FLAGS "${ARM_FLAGS}" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${ARM_FLAGS}" CACHE STRING "")
# -----------------------------------------------------------

set(DELEGATE_APP "delegate_app")
project(Delegate VERSION 1.0 LANGUAGES C CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# Options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_NONE")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")

include("${CMAKE_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

include_directories(${DMQ_INCLUDE_DIR})

# Create executable
add_executable(${DELEGATE_APP} main.cpp retarget.c startup.c ${DMQ_LIB_SOURCES})

target_compile_features(${DELEGATE_APP} PRIVATE cxx_std_20)

# [MISSING PART] - YOU MUST ADD THIS!
# This tells the linker to use flash.ld for memory layout
target_link_options(${DELEGATE_APP} PRIVATE 
    "-T${CMAKE_SOURCE_DIR}/flash.ld" 
    "-Wl,-Map=${CMAKE_BINARY_DIR}/output.map"
)