# Function to copy .dll files from vcpkg bin directory to the build output directory
function(copy_files _src_files _dest_dir)
    # Copy each DLL file to the build output directory
    foreach(FILE ${_src_files})
        add_custom_command(TARGET ${DELEGATE_APP} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FILE}      # Copy files
            ${_dest_dir}  # Destination directory
            COMMENT "Copying ${FILE} to build output"
        )
    endforeach()
endfunction()

# Record DelegateMQ compile definitions in DMQ_COMPILE_DEFINITIONS (used by
# dmq_add_library) and, unless DMQ_NO_GLOBAL_DEFINITIONS is set, also apply
# them directory-wide the way add_compile_definitions() always has.
macro(dmq_compile_definitions)
    list(APPEND DMQ_COMPILE_DEFINITIONS ${ARGN})
    if(NOT DMQ_NO_GLOBAL_DEFINITIONS)
        add_compile_definitions(${ARGN})
    endif()
endmacro()

# dmq_add_library(<name>)
#
# Creates a static library target from the port/extras sources selected by the
# current DMQ_* options. Compile definitions, include directories (DelegateMQ,
# DMQ_USER_CONFIG_DIR if set, and enabled third-party headers as SYSTEM) and
# link dependencies are PUBLIC, so a consumer only needs
# target_link_libraries(app PRIVATE <name>).
#
# Call in the same directory, after including DelegateMQ.cmake. Each call
# captures that include's options, so a build tree with several configurations
# (e.g. a FreeRTOS controller and a stdlib GUI) creates one target per config.
#
# RTOS ports (FreeRTOS, ThreadX, Zephyr, NuttX, CMSIS-RTOS2) compile against the
# application's own RTOS config headers; add those to the target, e.g.
#   target_include_directories(<name> PUBLIC path/to/FreeRTOSConfig_dir)
function(dmq_add_library _name)
    add_library(${_name} STATIC ${DMQ_PORT_SOURCES} ${DMQ_LIB_SOURCES})
    target_compile_features(${_name} PUBLIC cxx_std_17)
    target_compile_definitions(${_name} PUBLIC ${DMQ_COMPILE_DEFINITIONS})
    target_compile_options(${_name} PUBLIC ${DMQ_COMPILE_OPTIONS})
    target_include_directories(${_name} PUBLIC ${DMQ_INCLUDE_DIR} ${DMQ_USER_CONFIG_DIR})
    # SYSTEM: keep third-party header warnings out of DMQ_STRICT and the app's own warning level
    target_include_directories(${_name} SYSTEM PUBLIC ${DMQ_EXTERNAL_INCLUDE_DIRS})
    target_link_libraries(${_name} PUBLIC ${DMQ_LINK_LIBRARIES})

    source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
    source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
    source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})
endfunction()
