# STM32H7 Terminal

基于 STM32H743 + ESP32-C3 的带屏幕终端 / 网关系统，支持通过 HTTP 从电脑上传固件并由 STM32 Bootloader 完成外部 Flash 暂存、校验与内部 Bank2 安装（OTA）。

## 系统组成

| 目录 | 目标平台 | 作用 |
| --- | --- | --- |
| `wangguan/` | STM32H743（Keil MDK-ARM） | 主应用：LVGL 屏幕界面、FreeRTOS、传感器（DHT11 / Modbus）、W25Q64 外部 Flash、FATFS、OTA 安装端 |
| `bootloader/` | STM32H743（Keil MDK-ARM） | 引导程序：A/B 分区跳转、外部镜像恢复与安装保护 |
| `Clean/` | STM32H743（Keil MDK-ARM） | 最小化工程（LCD 驱动验证 / 移植基线） |
| `mqtt/` | ESP32-C3（ESP-IDF） | 桥接固件：USART 与 H7 通信、局域网 HTTP 服务、OneNET MQTT 转发 |
| `pc_gateway/` | Windows / Python | 上位机：状态查询、PING / UTF-8 ECHO、进入 Bootloader、上传 `.ota.bin` 并安装 |
| `shared/` | 跨平台共用 | 协议头文件（`ota_wire.h`、`boot_request.h`、`selftest_wire.h`、`memory_layout.h`）与固件打包脚本 |
| `docs/` | — | 架构图与 OTA / CPU 统计修复说明 |

## 数据流

```
PC 上位机 ──HTTP──> ESP32-C3 ──USART3──> STM32 App
                        │                    │
                        └──MQTT──> OneNET    └──> W25Q64 下载槽 ──> Bootloader 安装到 Bank2
```

OTA 下载槽位于外部 Flash `0x00080000`，参数扇区 `0x00100000` / `0x00101000`，另有独立恢复保护扇区 `0x00102000`（不得挪用）。下载完成不会自动安装，需要在界面点击“安装固件”；安装意图落盘后复位才会自动恢复安装。

## 构建

### STM32（Keil MDK-ARM）

分别打开 `wangguan/MDK-ARM/wangguan.uvprojx`、`bootloader/bootloader/MDK-ARM/bootloader.uvprojx`、`Clean/MDK-ARM/Clean.uvprojx` 构建。

应用工程链接成功后运行打包脚本（需 Python 3，并用 Keil 自带的 `fromelf` 从 AXF 生成 BIN）：

```powershell
python shared/package_firmware.py `
  --fromelf "C:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe" `
  --axf wangguan/MDK-ARM/wangguan/wangguan.axf `
  --bin wangguan/MDK-ARM/wangguan/wangguan.bin
```

脚本会校验 AXF 是否链接到应用分区（基址 `0x08020000`，上限 384 KiB），失败时删除旧的 OTA 产物以免误用。生成两个文件：
- `wangguan/MDK-ARM/wangguan/wangguan.bin`：原始应用，**仅供调试器烧录**。
- `wangguan/MDK-ARM/wangguan/wangguan.ota.bin`：HTTP OTA 专用，原始 BIN 后追加 32 字节描述（`H7FW` 魔数、格式版本、目标地址、原始长度、原始 CRC32、保留字段、描述 CRC32，均为小端 32 位）。上位机校验描述后才发送原始应用；截断、篡改、错误目标或缺描述的 BIN 一律拒绝。

### ESP32-C3（ESP-IDF）

```bash
cd mqtt
idf.py set-target esp32c3
idf.py build flash monitor
```

Wi-Fi 等参数在 `idf.py menuconfig` 的 Example Connection Configuration 中配置。设备联网后串口会打印 `local_http: PC URL: http://<设备IP>`。

### 上位机

```powershell
cd pc_gateway
powershell -ExecutionPolicy Bypass -File .\start.ps1
```

启动脚本优先使用本机已有的 ESP-IDF Python，无需 pip 安装第三方库；也可用带 Tk 的 Python 直接运行 `python main.py`。

## 使用流程

1. 用调试器烧录 Bootloader（`bootloader`）与 ESP32 桥接固件（`mqtt`）。
2. 电脑与 ESP32 接入同一可互通局域网（访客网络或客户端隔离会阻断访问）。
3. 运行上位机，输入串口日志中的地址并连接。
4. 选择 `wangguan.ota.bin` 上传；上传完成后点击“安装固件”写入 Bank2。
5. 安装后上位机会完整读回校验并自动启动。

## 验证

- 上位机与协议测试：`cd pc_gateway && python -m unittest test_ota test_client -v`（本机 HTTP 模拟器，不替代实机测试）。
- C 主机测试：`pc_gateway/tests` 下的 OTA 存储 / 安装测试，覆盖分块、重复确认、读回、CRC、最大 512 KiB 镜像，以及安装期间逐步断电恢复。
- 详细结果与实机待验证项见 [docs/ota_cpu_fix.md](docs/ota_cpu_fix.md)。

主机测试不能替代开发板测试。实机需验证：CPU 负载数值随空闲/忙碌变化；上传中断 60 秒后可重试；完整下载等待超过 60 秒仍可安装；安装中断后能恢复；故障镜像无法通过 App 命令强制启动。

## 说明

- HTTP 服务不含认证，仅供可信局域网使用。
- 本仓库为源码仓库：Keil 编译输出（`.o/.axf/.hex/.map` 等）、ESP-IDF `build/`、Python `__pycache__` 与临时目录均已通过 `.gitignore` 排除，克隆后需自行构建。
- 调试器烧录应用会与 OTA 的外部 DONE 记录不一致；建议统一使用 OTA 安装以保持镜像与记录同步。
