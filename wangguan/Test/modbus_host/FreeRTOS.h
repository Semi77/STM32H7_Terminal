#ifndef MODBUS_HOST_FREERTOS_H
#define MODBUS_HOST_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define configTICK_RATE_HZ 1000U
#define taskSCHEDULER_RUNNING 2
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define portYIELD_FROM_ISR(wake) ((void)(wake))
#endif
