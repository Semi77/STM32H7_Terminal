#include "modbus_sensor.h"
#include <stddef.h>

/**
  * @brief 读取两个输入寄存器并解析厂家定义的10000偏移负温度编码。
  * @param address 站号；temperature_x10为温度输出；humidity_x10为湿度输出。
  * @retval 通信或数据状态，输出采用十分之一单位且仅在成功时更新。
  */
ModbusStatus ModbusSensor_Read(uint8_t address, int16_t *temperature_x10,
                               uint16_t *humidity_x10)
{
    if (!temperature_x10 || !humidity_x10) return MODBUS_ARGUMENT_ERROR;
    uint16_t registers[2];
    ModbusStatus status = Modbus_ReadInputRegisters(address, 0U, 2U, registers, 200U);
    if (status != MODBUS_OK) return status;
    int32_t temperature = registers[0] < 10000U ? (int32_t)registers[0]
                                               : 10000 - (int32_t)registers[0];
    if (temperature < -400 || temperature > 1250 || registers[1] > 1000U)
        return MODBUS_FRAME_ERROR;
    *temperature_x10 = (int16_t)temperature;
    *humidity_x10 = registers[1];
    return MODBUS_OK;
}
