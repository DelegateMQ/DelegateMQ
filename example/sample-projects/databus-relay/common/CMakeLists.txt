# Header-only common library — no .cpp files, so INTERFACE avoids the
# "no source files" MSVC error that STATIC would produce.
file(GLOB SUBDIR_HEADERS "*.h")

add_library(common_lib INTERFACE)
target_include_directories(common_lib INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
