# DHT11 接入说明

模块 DATA 接 PE5，VCC 接 3.3 V，GND 与开发板共地。资料原理图包含 DATA 到 VCC 的 4.7 kΩ 上拉电阻；请以实际模块为准。

CubeMX 保持 PE5 为 `DHT`、开漏输出、初始高电平、无内部上下拉。驱动写低发送起始信号，写高释放数据线，通过输入电平接收应答和数据。

`DHT11_Init()` 在屏幕初始化后启用 DWT 计数器，不重置已有计数值。`DHT11_Read()` 仅允许一个 FreeRTOS 任务调用，不可从中断或已关闭中断的临界区调用；系统核心时钟改变后需要重新初始化。驱动限制上电稳定时间至少 1 秒、相邻读取启动间隔至少 2 秒。

接收数组 `frame[5]` 依次保存湿度整数、湿度小数、温度整数、温度小数和校验和。每个字节按高位优先接收，只有前四字节求和的低 8 位等于第五字节才更新调用者的数据。按所附 DHT11 手册，小数字节为零；这里不是 DHT22 的16位数值协议。

屏幕图形任务每 2 秒尝试读取一次：

- 成功：更新 TEMP / HUMID，显示一位小数。
- 尚无成功数据：显示 `--.-`，不把未测量值显示为零。
- 超时或校验失败：保持上次有效值，文字变橙色；下一次成功恢复正常颜色。
- 光照和其他设备状态不会被本驱动覆盖。

一次调用约占用图形任务 20 ms 起始低电平和约 4 ms 接收时间。起始阶段开启中断和调度；接收阶段暂停任务切换，只在应答或单个位采样时屏蔽中断，位间恢复中断。等待单个电平的超时为 100 μs。位间中断总耗时应明显小于约 50 μs 的低电平窗口；增加耗时中断或对中断延迟有严格要求时，应改为定时器捕获/DMA接收。软件测试不能替代真实波形测量。

## 构建与 CubeMX 再生成

Keil 工程已加入 `Drivers/DHT11` 头文件路径及 `dht11.c`。主函数没有增加驱动逻辑。

本次完整编译发现工程使用 ARM Compiler 6.22，但引用了仅适用于旧编译器语法的 RVDS FreeRTOS 端口。现改用同版本 FreeRTOS V10.6.2 的官方 GCC/ARM_CM4F 端口；原 RVDS 文件保留，未编译进目标。下载来源：

- [port.c](https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/V10.6.2/portable/GCC/ARM_CM4F/port.c)
- [portmacro.h](https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/V10.6.2/portable/GCC/ARM_CM4F/portmacro.h)

CubeMX 再生成后，需要确认 Keil 的端口源文件与头文件路径仍同时指向 `portable/GCC/ARM_CM4F`，不能混用 RVDS 头文件，并确认自定义 DHT11/GUI 文件仍在工程内。

## 软件验证

在工作区根目录运行以下命令可编译真实驱动的主机波形测试，测试文件不加入 Keil 工程：

```powershell
gcc -std=c11 -Wall -Wextra -Werror -I project/wangguan/Test/dht11_host -I project/wangguan/Drivers/DHT11 project/wangguan/Test/dht11_host/dht11_host_test.c project/wangguan/Drivers/DHT11/dht11.c -o tmp/dht11_host_test.exe
.\tmp\dht11_host_test.exe
```

测试覆盖正常帧、全零帧、校验和进位、错误帧丢弃及恢复、读取间隔、断线、卡低、截断帧、26～28 μs 零脉宽、DWT及毫秒时钟回绕、DWT初始化失败、错误出口恢复中断和调度。

`Test/dht11_host/ui_host_test.c` 另使用现有 PC 模拟器的真实 LVGL 库验证占位符、温湿度格式、失败变色和恢复，以及光照卡片保持不变。尚未进行开发板烧录和实物采样验证。
