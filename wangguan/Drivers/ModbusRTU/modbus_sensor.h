#ifndef MODBUS_SENSOR_H
#define MODBUS_SENSOR_H

#include "Modbus.h"

#define MODBUS_SENSOR_COUNT 2U

/** @brief 保存一个传感器的最新结果，数值在失败时保留但valid置false。 */
typedef struct {
    uint8_t address;
    bool valid;
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint32_t last_success_ms;
    uint32_t success_count;
    uint32_t error_count;
    ModbusStatus status;
} ModbusSensorSample;

/**
  * @brief 读取指定温湿度传感器，按手册解析负温度并检查量程。
  * @param address 从站地址；temperature_x10输出0.1℃；humidity_x10输出0.1%RH。
  * @retval 读取结果，失败时不修改两个输出值。
  */
ModbusStatus ModbusSensor_Read(uint8_t address, int16_t *temperature_x10,
                               uint16_t *humidity_x10);

/**
  * @brief 初始化总线并创建每秒轮询站号1和2的采集任务，在启动调度器前调用一次。
  * @param uart 已初始化的USART2句柄。
  * @retval true表示任务创建成功，false表示初始化或任务创建失败。
  */
bool ModbusSensor_Start(UART_HandleTypeDef *uart);

/**
  * @brief 线程安全地获取最近采样，通信失败或超过3秒未更新时标记无效。
  * @param index 0对应站号1，1对应站号2；sample为非空输出指针。
  * @retval true表示索引有效且快照已复制，数据有效性另看sample->valid。
  */
bool ModbusSensor_GetSnapshot(uint32_t index, ModbusSensorSample *sample);

#endif
