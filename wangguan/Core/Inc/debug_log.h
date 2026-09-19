#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include "Modbus.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * 诊断日志：默认关闭，不产生任何代码。
 * 置1后通过SWO/ITM输出，使用ITM端口0及“Display”时间戳通道，不占用任何串口。
 * 观察方式：调试器需启用SWO跟踪（Trace），并在printf窗口勾选ITM端口0。
 * 注意USART1已被上位机PING/App命令占用，USART2为Modbus总线，USART3为ESP32链路，
 * 因此这里统一走ITM，避免干扰既有串口协议。
 */
#ifndef DEBUG_LOG_ENABLE
#define DEBUG_LOG_ENABLE 0
#endif

/**
  * @brief 输出一次Modbus采集结果，包含原始0.1单位数值、状态和成败计数。
  * @param have_snapshot false表示尚无快照，此时数值字段无意义。
  * @param status 最近一次采集状态。
  * @param temperature_x10 温度原始值，单位0.1℃。
  * @param humidity_x10 湿度原始值，单位0.1%RH。
  * @param valid 本次采样是否有效。
  * @param success_count 累计成功次数。
  * @param error_count 累计失败次数。
  * @retval 无，日志关闭时不生成代码。
  */
void DebugLog_Modbus(bool have_snapshot, ModbusStatus status,
                     int16_t temperature_x10, uint16_t humidity_x10, bool valid,
                     uint32_t success_count, uint32_t error_count);

/**
  * @brief 输出一行纯ASCII文本，供初始化阶段定位卡点。
  * @param text 以零结尾的ASCII字符串。
  * @retval 无，日志关闭时不生成代码。
  */
void DebugLog_Line(const char *text);

#endif
