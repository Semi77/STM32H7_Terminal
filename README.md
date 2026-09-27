# STM32H7 Terminal

基于 STM32H743VGT6 和 ESP32-C3 的带屏幕终端 / 网关系统。STM32 运行 FreeRTOS 与 LVGL，驱动 128×160 ST7735S 屏幕；ESP32-C3 提供局域网 HTTP 接口和 OneNET MQTT 桥接。应用固件可通过 HTTP 上传到外部 Flash，再由 STM32 Bootloader 安装到非活动 Bank；候选应用确认失败时回滚。

> 当前为开发中的源码工程。站号 1 的 RS485 温湿度采集已在开发板上验证，屏幕能够显示温湿度；A/B 升级、断电恢复和回滚仍需按 [实机验收清单](docs/ota_ab.md)验证。

屏幕温湿度读取 RS485 传感器快照，光照读取 I²C BH1750。USART3 上传到 ESP32-C3 的温湿度仍是模拟值，光照取 BH1750 最近一次有效读数；因此 OneNET 上的温湿度不能当作传感器实测值。

## 系统组成

| 目录 | 目标平台 | 作用 |
| --- | --- | --- |
| `wangguan/` | STM32H743（Keil MDK-ARM） | 主应用：LVGL/ST7735S 屏幕、FreeRTOS、RS485 温湿度与 BH1750 光照采集、W25Q64 外部 Flash、FATFS、OTA 下载端；DHT11 驱动保留但当前未用于显示或上传 |
| `bootloader/` | STM32H743（Keil MDK-ARM） | 引导程序：A/B 分区跳转、外部镜像恢复与安装保护 |
| `Clean/` | STM32H743（Keil MDK-ARM） | 首次迁移前清理整片 W25Q64 的辅助工程；运行会清除外部 Flash 中的数据 |
| `mqtt/` | ESP32-C3（ESP-IDF） | 桥接固件：USART 与 H7 通信、局域网 HTTP 服务、OneNET MQTT 转发 |
| `pc_gateway/` | Windows / Python | 上位机：状态查询、PING / UTF-8 ECHO、进入 Bootloader、上传 `.ota.bin` 并安装 |
| `shared/` | 跨平台共用 | 协议头文件（`ota_wire.h`、`boot_request.h`、`selftest_wire.h`、`memory_layout.h`）与固件打包脚本 |
| `docs/` | — | A/B 首次烧录、OTA 和 CPU 统计说明 |

## 数据流

```
PC 上位机 ──HTTP──> ESP32-C3 ──USART3──> STM32 App / Bootloader
                        │                    │
                        └──MQTT──> OneNET    └──> W25Q64 下载槽 ──> 非活动 Bank
```

OTA 下载槽位于外部 Flash `0x00080000`。下载完成后需在上位机点击“安装固件”。内部 Flash 的两个 Bank 各有一份 Bootloader，应用区各为 384 KiB。完整分区、首次烧录顺序及回滚流程见 [A/B OTA 说明](docs/ota_ab.md)。

## RS485 温湿度接线

使用 5 V MAX485 TTL 转 RS485 模块，`RE` 与 `DE` 分别接线，不要短接：

| MAX485 模块 | STM32 / 传感器 |
| --- | --- |
| DI / RO | PA2（USART2_TX）/ PA3（USART2_RX） |
| DE / RE | PA0 / PA1（RE 低电平使能接收） |
| VCC / GND | 5 V / STM32 GND；非隔离传感器电源负极共地 |
| A / B | 传感器 A（RS485+）/ B（RS485−） |

USART2 为 9600、8N1。固件每秒查询站号 1 和 2；目前屏幕使用站号 1 的结果。请求帧及状态诊断说明见 [RS485 驱动说明](wangguan/Drivers/ModbusRTU/README.md)。

## 构建

### STM32（Keil MDK-ARM）

分别打开 `wangguan/MDK-ARM/wangguan.uvprojx`、`bootloader/bootloader/MDK-ARM/bootloader.uvprojx` 构建。首次部署还需按 [首次烧录说明](docs/ota_ab.md)生成并烧录两个 Bank 的引导程序和应用，不能只烧录一个 Bootloader。

应用工程链接成功后会自动运行打包脚本，生成调试器用 BIN 和 HTTP OTA 用 `.ota.bin`。如果需要手动运行（需 Python 3 和 Keil 的 `fromelf`）：

```powershell
python shared/package_firmware.py `
  --fromelf "C:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe" `
  --axf wangguan/MDK-ARM/wangguan/wangguan.axf `
  --bin wangguan/MDK-ARM/wangguan/wangguan.bin
```

脚本会校验 AXF 是否链接到应用分区（基址 `0x08020000`，上限 384 KiB），失败时删除旧的 OTA 产物以免误用。生成两个文件：

- `wangguan/MDK-ARM/wangguan/wangguan.bin`：原始应用，**仅供首次烧录或调试器烧录**。
- `wangguan/MDK-ARM/wangguan/wangguan.ota.bin`：HTTP OTA 专用，原始 BIN 后追加 32 字节描述（`H7FW` 魔数、格式版本、目标地址、原始长度、原始 CRC32、保留字段、描述 CRC32，均为小端 32 位）。上位机校验描述后才发送原始应用；截断、篡改、错误目标或缺描述的 BIN 一律拒绝。

### ESP32-C3（ESP-IDF）

```bash
cd mqtt
idf.py set-target esp32c3
idf.py build flash monitor
```

Wi-Fi 参数在 `idf.py menuconfig` 的 Example Connection Configuration 中设置，OneNET 令牌在 OneNET Configuration 中设置；本机 `mqtt/sdkconfig` 已被 Git 忽略。设备联网后串口会打印 `local_http: PC URL: http://<设备IP>`。ESP32-C3 的具体构建说明见 [mqtt/README.md](mqtt/README.md)。

### 上位机

```powershell
cd pc_gateway
powershell -ExecutionPolicy Bypass -File .\start.ps1
```

启动脚本优先使用本机已有的 ESP-IDF Python，无需 pip 安装第三方库；也可用带 Tk 的 Python 直接运行 `python main.py`。

## 使用流程

1. 首次使用时，按 [A/B 首次烧录步骤](docs/ota_ab.md)准备并烧录 STM32；另行烧录 ESP32-C3 桥接固件。
2. 电脑与 ESP32 接入同一可互通局域网（访客网络或客户端隔离会阻断访问）。
3. 运行上位机，输入串口日志中的地址并连接。
4. 选择 `wangguan.ota.bin` 上传；上传完成后点击“安装固件”，写入非活动 Bank。
5. Bootloader 读回校验并试启动；候选应用确认成功后成为正式版本，失败则回滚。

## 验证

- 上位机与协议测试：`cd pc_gateway && python -m unittest test_ota test_client -v`（本机 HTTP 模拟器，不替代实机测试）。
- C 主机测试：`pc_gateway/tests` 下的 OTA 存储 / 安装测试，覆盖分块、读回、CRC 和安装断电点；应用区上限为 384 KiB。
- RS485 主机测试：见 [驱动说明](wangguan/Drivers/ModbusRTU/README.md)；站号 1 的实际收发和屏幕显示已验证，站号 2 仍需接入已改地址的设备验证。
- A/B 与实机待验证项见 [docs/ota_ab.md](docs/ota_ab.md)；CPU 统计和旧单区 OTA 修复记录见 [docs/ota_cpu_fix.md](docs/ota_cpu_fix.md)。

主机测试不能替代开发板测试。优先验证正常升级、安装期间断电、候选应用两次启动失败后的回滚，以及再次升级切换回另一 Bank。

## 说明

- HTTP 服务不含认证，仅供可信局域网使用。
- 本仓库为源码仓库：Keil 编译输出（`.o/.axf/.hex/.map` 等）、ESP-IDF `build/`、Python `__pycache__` 与临时目录均已通过 `.gitignore` 排除，克隆后需自行构建。
- OneNET 访问令牌从本机 `mqtt/sdkconfig` 读取，不应提交到仓库。旧提交曾包含令牌，使用前应在 OneNET 端轮换。
- 调试器烧录应用可能与 OTA 的外部状态记录不一致；首次迁移后建议统一使用 OTA 安装。
