# Collect all .cpp files in this subdirectory
file(GLOB SUBDIR_SOURCES CONFIGURE_DEPENDS "*.cpp")

# Collect all .h files in this subdirectory
file(GLOB SUBDIR_HEADERS CONFIGURE_DEPENDS "*.h")

# Create a library target
add_library(stress_tests_lib STATIC ${SUBDIR_SOURCES} ${SUBDIR_HEADERS})

# Include directories for the library
target_include_directories(stress_tests_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Add /bigobj flag for MSVC because stress tests are large
if (MSVC)
    target_compile_options(stress_tests_lib PRIVATE /bigobj)
endif()

target_precompile_headers(stress_tests_lib PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/delegate-mq/DelegateMQ.h"
)
