#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>

typedef enum {
    MODBUS_OK = 0,
    MODBUS_TIMEOUT,
    MODBUS_UART_ERROR,
    MODBUS_FRAME_ERROR,
    MODBUS_CRC_ERROR,
    MODBUS_EXCEPTION,
    MODBUS_BUSY,
    MODBUS_ARGUMENT_ERROR
} ModbusStatus;

typedef struct {
    bool tx_started;
    bool tx_complete;
    uint16_t rx_bytes;
} ModbusDiagnostics;

/**
  * @brief 读取指定站号最近一次事务的收发诊断信息。
  * @param address 站号1或2；diagnostics为诊断信息输出地址。
  * @retval true表示已有该站号的事务记录。
  */
bool Modbus_GetDiagnostics(uint8_t address, ModbusDiagnostics *diagnostics);

/**
  * @brief 绑定9600、8N1的USART2及PA0-DE、PA1-RE控制，在内核初始化后调用一次。
  * @param uart 已完成硬件初始化的USART2句柄。
  * @retval true表示初始化成功，false表示参数或资源错误。
  */
bool Modbus_Init(UART_HandleTypeDef *uart);

/**
  * @brief 中断收发并等待输入寄存器响应，仅允许任务上下文调用。
  * @param address 从站地址1至247；start为协议起始地址；count为寄存器数1至16。
  * @param values 接收count个寄存器的数组，仅成功时写入。
  * @param timeout_ms 包含总线空闲等待和收发的总超时，范围20至1000毫秒。
  * @retval 通信状态，同一时刻只允许一个调用者使用总线。
  */
ModbusStatus Modbus_ReadInputRegisters(uint8_t address, uint16_t start,
                                      uint16_t count, uint16_t *values,
                                      uint32_t timeout_ms);

/**
  * @brief 将USART2单字节接收事件交给Modbus驱动，其他串口忽略。
  * @param uart 产生接收完成事件的串口句柄。
  * @retval 无。
  */
void Modbus_RxComplete(UART_HandleTypeDef *uart);

/**
  * @brief 记录USART2硬件错误并唤醒等待任务。
  * @param uart 产生错误事件的串口句柄。
  * @retval 无。
  */
void Modbus_UartError(UART_HandleTypeDef *uart);

#endif
