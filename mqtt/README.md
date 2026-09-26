# ESP32-C3 网关桥接固件

本工程基于 ESP-IDF，负责通过 USART3 与 STM32H743 通信，向局域网提供 HTTP 接口，并将 STM32 的采样记录转发到 OneNET MQTT。OTA 固件由电脑经 HTTP 发送给 ESP32-C3，再转发给 STM32 Bootloader；ESP32-C3 不负责安装 STM32 固件。

## 配置与构建

使用 ESP-IDF 环境，在本目录运行：

```bash
idf.py set-target esp32c3
idf.py menuconfig
idf.py build flash monitor
```

在 `Example Connection Configuration` 中设置 Wi-Fi，在 `OneNET Configuration` 中设置产品 ID、设备名和访问令牌。OneNET 令牌为空时仅跳过 MQTT 启动，局域网 HTTP 服务仍会运行。`sdkconfig` 只保存在本机，不纳入 Git；`sdkconfig.defaults` 提供不含密码的公共默认值。令牌不要写入源码或提交到仓库。

联网后串口会打印 `local_http: PC URL: http://<设备IP>`。电脑与 ESP32-C3 需要在互通的局域网中。HTTP 状态、PING/ECHO 和 STM32 OTA 操作由根目录的 [上位机](../pc_gateway/README.md)使用；首次烧录 STM32 和 A/B 分区说明见 [A/B OTA 文档](../docs/ota_ab.md)。

## 主要源码

| 文件 | 作用 |
| --- | --- |
| `main/h7_uart.c` | STM32 串口收发、心跳和 OTA 帧转发 |
| `main/local_http.c` | 局域网 HTTP 接口 |
| `main/onenet_test.c` | OneNET MQTT 上报和平台回复确认 |
| `main/selftest_report.c` | 接收并缓存 STM32 自检报告 |

仓库只保存源码和构建配置；ESP-IDF 生成的 `build/`、Bootloader 二进制及本机配置需在本地生成。
