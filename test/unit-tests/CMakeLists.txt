# Collect all .cpp files in this subdirectory and databus
file(GLOB SUBDIR_SOURCES CONFIGURE_DEPENDS "*.cpp" "databus/*.cpp" "serialize/*.cpp" "allocator/*.cpp" "dispatcher/*.cpp")

# Collect all .h files in this subdirectory and databus
file(GLOB SUBDIR_HEADERS CONFIGURE_DEPENDS "*.h" "databus/*.h" "serialize/*.h" "allocator/*.h" "dispatcher/*.h")

# Create a library target 
add_library(unit_tests_lib STATIC ${SUBDIR_SOURCES} ${SUBDIR_HEADERS})

# Include directories for the library
target_include_directories(unit_tests_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Add /bigobj flag for MSVC because unit tests are large
if (MSVC)
    target_compile_options(unit_tests_lib PRIVATE /bigobj)
endif()

target_precompile_headers(unit_tests_lib PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/delegate-mq/DelegateMQ.h"
)