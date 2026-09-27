# RS485 温湿度采集

硬件使用 USART2：PA2 接 DI，PA3 接 RO，PA0 单独接 DE，PA1 单独接低电平有效的 RE；DE 与 RE 不再短接。
发送时 PA1、PA0 依次拉高，发送完成后 PA0、PA1 依次拉低；上电默认接收。当前 MAX485 模块使用 5 V 供电，RO 接 PA3；传感器另行供电，普通非隔离连接共地。A 接传感器 A（RS485+），B 接传感器 B（RS485−）。

## 任务与数据接口

`main.c` 在 `osKernelInitialize()` 之后调用 `ModbusSensor_Start(&huart2)`。
任务名为 `modbusSensor`，FreeRTOS 优先级为 16，栈为 2048 字节。
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
屏幕温湿度卡片读取站号 1 的快照；USART3 上传到 ESP32-C3 的温湿度目前仍由模拟采样任务产生，没有使用这里的快照。

## 协议与实现范围

- 读取功能码 04，输入寄存器 0、1 分别为温度和湿度。
- 地址 1 请求：`01 04 00 00 00 02 71 CB`。
- 地址 2 请求：`02 04 00 00 00 02 71 F8`。
- 寄存器高字节在前，CRC16低字节在前。
- 原始温度250表示25.0℃，10250表示-25.0℃；不采用补码解释。
- 温度量程检查为-40至125℃，湿度为0至100%RH。
- 两个新传感器出厂都为地址1，先单独给第二个发送 `01 06 00 0A 00 02 28 09`，重新上电后再一起接入。本驱动不自动修改设备地址。

总线驱动使用 UART 中断收发和信号量等待，不使用DMA、不忙等应答。
TC 完成回调中依次拉低 PA0、PA1；接收中断在发送前已准备好，避免遗漏快速响应。
9600、8N1 下以至少6ms静默判帧，并用DWT检查帧内1.5字符静默上限。
DWT初始化不清零已有周期计数；运行中改变CPU时钟后需重新计算计时参数。
不把UART一个字符的IDLE事件直接视为完整RTU报文。
读取失败后中止UART事务并释放总线，下次重新清除错误并接收。
当前仅实现04读取输入寄存器（1至16个），不包含通用写寄存器协议栈。
任意时刻只允许一个事务，重复调用返回MODBUS_BUSY。

## 状态与验证

屏幕状态形如 `485 OK T1C1R9`。`T` 表示请求已开始发送，`C` 表示 UART 发送完成，`R` 为本次收到的字节数。`TMO T1C1R0` 表示请求发完后没有收到应答字节；若 `R` 大于零而仍失败，再检查帧长度、CRC 和状态码。正常读取两个寄存器的应答为 9 字节。

Keil 工程已编译通过；站号 1 的 RS485 温湿度采集及屏幕显示已在开发板上验证。站号 2、总线断线恢复和 OTA 安装后的完整系统仍需分别实测。

主机测试直接编译驱动源文件，模拟UART与RTOS，覆盖正常、负数、边界值、CRC、地址/功能码/长度错误、异常响应、缓冲溢出、帧内间隔、超时、硬件错误、方向恢复、总线噪声、计时回绕和快照失效。

在本仓库根目录运行：

```powershell
gcc -std=c11 -Wall -Wextra -Werror -I wangguan/Test/modbus_host -I wangguan/Drivers/ModbusRTU wangguan/Test/modbus_host/modbus_host_test.c -o wangguan/Test/modbus_host/modbus_host_test.exe
.\wangguan\Test\modbus_host\modbus_host_test.exe
```

接入第二个传感器前，应先单独修改其站号为 2 并重新上电；两台都保持出厂站号 1 会发生地址冲突。
