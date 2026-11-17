# The portable tests (see PortableTests.h) for an OS port sample:
#
#   include("${CMAKE_SOURCE_DIR}/../../../test/portable/PortableTests.cmake")
#   # then add ${PORTABLE_TESTS_SOURCES} to the app's sources and
#   # ${PORTABLE_TESTS_DIR} to its include directories
#
# Defines:
#   PORTABLE_TESTS_DIR      this directory
#   PORTABLE_TESTS_SOURCES  the test sources

set(PORTABLE_TESTS_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PORTABLE_TESTS_SOURCES
    "${PORTABLE_TESTS_DIR}/DelegateThreadsPortableTests.cpp"
    "${PORTABLE_TESTS_DIR}/TimerDelegatePortableTests.cpp"
)
