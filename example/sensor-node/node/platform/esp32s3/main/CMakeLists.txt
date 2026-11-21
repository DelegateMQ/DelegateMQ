# The node: the portable app (node/app: SensorNode, RunNode()), this platform
# (main.cpp, Esp32Hal.h, Wifi.cpp) and DelegateMQ (src/DelegateMQ.cpp, the
# single file that compiles whatever the DMQ_* options select).

set(SENSOR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(DMQ_ROOT "${SENSOR_ROOT}/../..")

idf_component_register(
    SRCS
        "main.cpp"
        "Wifi.cpp"
        "${SENSOR_ROOT}/node/app/SensorNode.cpp"
        "${SENSOR_ROOT}/node/app/NodeMain.cpp"
        "${DMQ_ROOT}/src/DelegateMQ.cpp"
    INCLUDE_DIRS
        "."
        "${SENSOR_ROOT}/node/app"
        "${SENSOR_ROOT}/common"
        "${DMQ_ROOT}/src/delegate-mq"
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        esp_driver_gpio
        esp_driver_tsens
        led_strip
)

# FreeRTOS threads, the built-in serializer, UDP over lwIP's sockets, DataBus.
# DMQ_FREERTOS_STACK_WORDS: on ESP-IDF a stack is sized in bytes (StackType_t
# is one byte), and NetThread serializes and sends from its stack.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    DMQ_THREAD_FREERTOS
    DMQ_SERIALIZE_SERIALIZE
    DMQ_TRANSPORT_ARM_LWIP_UDP
    DMQ_DATABUS
    DMQ_FREERTOS_STACK_WORDS=8192
)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
