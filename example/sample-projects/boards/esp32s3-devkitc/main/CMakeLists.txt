# The app component: main.cpp and DelegateMQ (src/DelegateMQ.cpp, the single
# file that compiles whatever the DMQ_* options select).

set(DMQ_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../..")

idf_component_register(
    SRCS
        "main.cpp"
        "${DMQ_ROOT}/src/DelegateMQ.cpp"
    INCLUDE_DIRS
        "."
        "${DMQ_ROOT}/src/delegate-mq"
    REQUIRES
        esp_driver_gpio
        esp_psram
        led_strip
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    DMQ_THREAD_FREERTOS
    DMQ_SERIALIZE_NONE
    DMQ_TRANSPORT_NONE
)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
