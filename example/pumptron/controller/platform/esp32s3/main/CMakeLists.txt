# The Pumptron controller on the ESP32-S3: the portable pump application
# (controller/pump), the shared messages and topology (common), this platform
# (main.cpp, Esp32Board.h, Wifi.cpp) and DelegateMQ (src/DelegateMQ.cpp, the
# single file that compiles whatever the DMQ_* options select).

set(CONTROLLER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(PUMPTRON_DIR "${CONTROLLER_DIR}/..")
set(DMQ_ROOT "${PUMPTRON_DIR}/../..")

idf_component_register(
    SRCS
        "main.cpp"
        "Wifi.cpp"
        "${CONTROLLER_DIR}/pump/PumpController.cpp"
        "${CONTROLLER_DIR}/pump/PumpModel.cpp"
        "${PUMPTRON_DIR}/common/util/Serializers.cpp"
        "${PUMPTRON_DIR}/common/util/Stringifiers.cpp"
        "${DMQ_ROOT}/src/DelegateMQ.cpp"
    INCLUDE_DIRS
        "."
        "${CONTROLLER_DIR}"
        "${PUMPTRON_DIR}/common"
        "${DMQ_ROOT}/src/delegate-mq"
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        esp_driver_gpio
        esp_driver_tsens
        esp_system
        led_strip
)

# The F4 build's options (FreeRTOS, the built-in serializer, DataBus, the
# fixed-block allocator, Pumptron's DelegateMQConfig.h), with UDP over lwIP's
# sockets instead of the STM32 UART. DMQ_FREERTOS_STACK_WORDS: on ESP-IDF a
# stack is sized in bytes (StackType_t is one byte).
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    DMQ_THREAD_FREERTOS
    DMQ_SERIALIZE_SERIALIZE
    DMQ_TRANSPORT_ARM_LWIP_UDP
    DMQ_DATABUS
    DMQ_ALLOCATOR
    DMQ_USER_CONFIG="DelegateMQConfig.h"
    DMQ_FREERTOS_STACK_WORDS=8192
)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
