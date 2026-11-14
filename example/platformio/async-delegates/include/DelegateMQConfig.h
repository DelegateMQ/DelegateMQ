#ifndef _DELEGATEMQ_CONFIG_H
#define _DELEGATEMQ_CONFIG_H

// DelegateMQ options for this example. PlatformIO builds the library with this
// project's include/ folder on the path, so the library and the sketch see the
// same settings. Keep this file in include/, not src/.
//
// The thread port is chosen per board in platformio.ini (-DDMQ_THREAD_FREERTOS
// on ESP32). Boards with no thread port selected run bare metal
// (DMQ_THREAD_NONE): synchronous delegates and signals only. The desktop picks
// stdlib threads automatically.
//
// See src/delegate-mq/delegate/DelegateMQConfig_Template.h for every option.

// Smaller thread queues and stacks than the desktop defaults.
#define DMQ_DEFAULT_QUEUE_SIZE      10
#define DMQ_FREERTOS_STACK_WORDS    4096

#endif
