# RS485 温湿度采集

硬件使用 USART2：PA2 接 DI，PA3 接 RO，PA0 接短接后的 DE 与 /RE。
PA0 高电平发送、低电平接收；上电默认接收。MAX485 供电按模块规格，RO 与 PA3 应满足电平兼容要求；传感器另行供电，普通非隔离连接共地。

## 任务与数据接口

`main.c` 在 `osKernelInitialize()` 之后调用 `ModbusSensor_Start(&huart2)`。
任务名为 `modbusSensor`，优先级为 `osPriorityBelowNormal`，栈为 2048 字节。
启动后等待 1 秒，每秒依次查询地址 1 和 2；每次通信总超时为 200ms。
离线时记录失败并在下一轮重试，仍然继续查询另一站，不进入 Error_Handler。

初始化已同步到 CubeMX 配置：USART2 为 9600、8N1，中断优先级 5。
工程将 Modbus.c 保留为单一编译条目。

其他任务应读取快照，不再各自发起采集：

```c
#include "modbus_sensor.h"

/* 获取地址1的快照，只有valid为true时才使用本次数据。 */
ModbusSensorSample sample;
if (ModbusSensor_GetSnapshot(0U, &sample) && sample.valid) {
    /* temperature_x10为有符号0.1℃，humidity_x10为0.1%RH。 */
}
```

索引 0 对应地址 1，索引 1 对应地址 2。
快照提供 `status`、`valid`、`last_success_ms`、`success_count`、`error_count`。
通信失败保留上次数值，但 valid 为 false；成功数据超过 3 秒也返回无效。
GetSnapshot 返回 true 仅表示成功复制，不能替代 valid 检查。
本次不修改现有模拟数据、界面、上传协议和Flash缓存格式。

## 协议与实现范围

- 读取功能码 04，输入寄存器 0、1 分别为温度和湿度。
- 地址 1 请求：`01 04 00 00 00 02 71 CB`。
- 地址 2 请求：`02 04 00 00 00 02 71 F8`。
- 寄存器高字节在前，CRC16低字节在前。
- 原始温度250表示25.0℃，10250表示-25.0℃；不采用补码解释。
- 温度量程检查为-40至125℃，湿度为0至100%RH。
- 两个新传感器出厂都为地址1，先单独给第二个发送 `01 06 00 0A 00 02 28 09`，重新上电后再一起接入。本驱动不自动修改设备地址。

总线驱动使用 UART 中断收发和信号量等待，不使用DMA、不忙等应答。
TC完成回调中立即拉低PA0；接收在发送前已准备好，避免遗漏快速响应。
9600、8N1 下以至少6ms静默判帧，并用DWT检查帧内1.5字符静默上限。
DWT初始化不清零已有周期计数；运行中改变CPU时钟后需重新计算计时参数。
不把UART一个字符的IDLE事件直接视为完整RTU报文。
读取失败后中止UART事务并释放总线，下次重新清除错误并接收。
当前仅实现04读取输入寄存器（1至16个），不包含通用写寄存器协议栈。
任意时刻只允许一个事务，重复调用返回MODBUS_BUSY。

## 验证

Keil编译日志：`MDK-ARM/modbus_sensor_build.log`。
主机测试直接编译驱动源文件，模拟UART与RTOS，覆盖正常、负数、边界值、CRC、地址/功能码/长度错误、异常响应、缓冲溢出、帧内间隔、超时、硬件错误、方向恢复、总线噪声、计时回绕和快照失效。

在工作区根目录运行：

```powershell
gcc -std=c11 -Wall -Wextra -Werror -I project/wangguan/Test/modbus_host -I project/wangguan/Drivers/ModbusRTU project/wangguan/Test/modbus_host/modbus_host_test.c -o tmp/modbus_host_test.exe
.\tmp\modbus_host_test.exe
```

尚未烧录或完成实物验证；上板后先接地址1验证success_count增长，再接入已改为地址2的设备，检查两组计数及断开任一设备时另一组持续更新。
