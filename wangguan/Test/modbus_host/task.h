#ifndef MODBUS_HOST_TASK_H
#define MODBUS_HOST_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
int xTaskGetSchedulerState(void);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskCreate(void (*entry)(void *), const char *name, uint32_t stack,
                       void *argument, uint32_t priority, TaskHandle_t *handle);
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t *last_wake, TickType_t period);
#endif
