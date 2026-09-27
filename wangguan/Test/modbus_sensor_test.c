#include "modbus_sensor.h"
#include "FreeRTOS.h"
#include "task.h"

static ModbusSensorSample samples[MODBUS_SENSOR_COUNT];
static TaskHandle_t sensor_task;

/**
  * @brief 轮询两个站号，设备离线时记录错误并继续采集另一站。
  * @param argument 未使用，传入NULL。
  * @retval 无，任务持续运行。
  */
static void ModbusSensor_Task(void *argument)
{
    (void)argument;
    const TickType_t period = configTICK_RATE_HZ;
    /* 上电先留一秒稳定时间，后续失败仍按周期重试，不阻塞网关启动。 */
    vTaskDelay(period);
    TickType_t next_wake = xTaskGetTickCount();
    for (;;) {
        for (uint32_t i = 0U; i < MODBUS_SENSOR_COUNT; ++i) {
            int16_t temperature;
            uint16_t humidity;
            ModbusStatus status = ModbusSensor_Read((uint8_t)(i + 1U), &temperature, &humidity);
            /* 在临界区内更新快照，避免其他任务读到未完成的数据。 */
            taskENTER_CRITICAL();
            samples[i].status = status;
            samples[i].valid = status == MODBUS_OK;
            if (status == MODBUS_OK) {
                samples[i].temperature_x10 = temperature;
                samples[i].humidity_x10 = humidity;
                samples[i].last_success_ms = HAL_GetTick();
                ++samples[i].success_count;
            } else {
                ++samples[i].error_count;
            }
            taskEXIT_CRITICAL();
        }
        if ((int32_t)(next_wake + period - xTaskGetTickCount()) <= 0)
            next_wake = xTaskGetTickCount();
        vTaskDelayUntil(&next_wake, period);
    }
}

/**
  * @brief 创建唯一采集任务并初始化两个快照的站号和无效状态。
  * @param uart 已配置为9600、8N1的USART2句柄。
  * @retval true表示任务已经创建，false表示初始化或创建失败。
  */
bool ModbusSensor_Start(UART_HandleTypeDef *uart)
{
    if (sensor_task) return true;
    if (!Modbus_Init(uart)) return false;
    for (uint32_t i = 0U; i < MODBUS_SENSOR_COUNT; ++i) {
        samples[i].address = (uint8_t)(i + 1U);
        samples[i].status = MODBUS_TIMEOUT;
    }
    return xTaskCreate(ModbusSensor_Task, "modbusSensor", 2048U / sizeof(StackType_t),
                       NULL, 16U, &sensor_task) == pdPASS;
}

/**
  * @brief 复制指定站号的快照，超过三秒未成功采样的旧数据返回无效标志。
  * @param index 0对应站号1、1对应站号2；sample为非空输出指针。
  * @retval true表示复制成功，具体采样有效性由valid字段表示。
  */
bool ModbusSensor_GetSnapshot(uint32_t index, ModbusSensorSample *sample)
{
    if (index >= MODBUS_SENSOR_COUNT || !sample) return false;
    taskENTER_CRITICAL();
    *sample = samples[index];
    taskEXIT_CRITICAL();
    if ((uint32_t)(HAL_GetTick() - sample->last_success_ms) >= 3000U)
        sample->valid = false;
    return true;
}
