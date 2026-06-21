# Collect all .cpp files in this subdirectory
file(GLOB SUBDIR_SOURCES CONFIGURE_DEPENDS "*.cpp")

# Collect all .h files in this subdirectory
file(GLOB SUBDIR_HEADERS CONFIGURE_DEPENDS "*.h")

# Create a library target 
add_library(sample_code_lib STATIC ${SUBDIR_SOURCES} ${SUBDIR_HEADERS})

# Include directories for the library
target_include_directories(sample_code_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_precompile_headers(sample_code_lib PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/delegate-mq/DelegateMQ.h"
)