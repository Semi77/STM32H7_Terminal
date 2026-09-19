#ifndef MODBUS_HOST_OS_H
#define MODBUS_HOST_OS_H
#include <stdint.h>
#include <stddef.h>
typedef void *osSemaphoreId_t;
typedef void *osThreadId_t;
typedef int osStatus_t;
typedef struct { const char *name; uint32_t stack_size; int priority; } osThreadAttr_t;
#define osOK 0
#define osKernelRunning 2
#define osPriorityBelowNormal 16
int osKernelGetState(void);
uint32_t osKernelGetTickFreq(void);
uint32_t osKernelGetTickCount(void);
osSemaphoreId_t osSemaphoreNew(uint32_t max, uint32_t initial, const void *attr);
osStatus_t osSemaphoreAcquire(osSemaphoreId_t id, uint32_t ticks);
osStatus_t osSemaphoreRelease(osSemaphoreId_t id);
osThreadId_t osThreadNew(void (*entry)(void *), void *arg, const osThreadAttr_t *attr);
osStatus_t osDelay(uint32_t ticks);
osStatus_t osDelayUntil(uint32_t ticks);
#endif
