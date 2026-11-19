# The app component: main.cpp, DelegateMQ (src/DelegateMQ.cpp, the single
# file that compiles whatever the DMQ_* options select) and the portable tests.

set(DMQ_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../..")

idf_component_register(
    SRCS
        "main.cpp"
        "${DMQ_ROOT}/src/DelegateMQ.cpp"
        "${DMQ_ROOT}/test/portable/DelegateThreadsPortableTests.cpp"
        "${DMQ_ROOT}/test/portable/TimerDelegatePortableTests.cpp"
    INCLUDE_DIRS
        "."
        "${DMQ_ROOT}/src/delegate-mq"
        "${DMQ_ROOT}/test/portable"
    REQUIRES
        esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    DMQ_THREAD_FREERTOS
    DMQ_SERIALIZE_NONE
    DMQ_TRANSPORT_NONE
)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
