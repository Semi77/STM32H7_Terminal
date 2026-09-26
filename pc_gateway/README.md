# H7 网关 Wi-Fi 上位机

实现电脑与 ESP32-C3 的局域网 HTTP 通信：状态查询、2秒自动刷新、PING/PONG、UTF-8 ECHO、进入STM32 Bootloader、上传固件到W25Q64及点击“安装固件”写入非活动Bank。保留现有OneNET MQTT转发。安装后完整读回校验，试启动成功后确认；失败时切回旧版，详见 [A/B升级与首次烧录](../docs/ota_ab.md)。

请选择Keil构建生成的 `wangguan.ota.bin`，上位机校验构建期长度和CRC后才开始上传；原始 `.bin` 仅供工厂烧录。入口为 `h7_wifi_tool.py`，无需第三方Python库。

## 使用

1. 编译并烧录 `project/mqtt` 的 ESP32 固件，并通过调试器更新STM32 Bootloader。Wi-Fi 使用该工程原有配置；需要修改时在 ESP-IDF menuconfig 的 Example Connection Configuration 中设置。电脑与 ESP32 接入可互通的同一路由器网络，访客网络或客户端隔离可能阻止访问。
2. ESP32 获得 IP 后，串口日志会打印 `local_http: PC URL: http://设备IP`。重新联网地址可能变化。
3. 在本目录运行 `powershell -ExecutionPolicy Bypass -File .\start.ps1`，或使用带 Tk 的 Python 运行 `python main.py`。启动脚本优先使用本机现有 ESP-IDF Python，不需要 pip 安装库。
4. 在窗口输入日志中的地址，点击“连接 / 刷新”，然后测试 PING 和中文回显。也可用浏览器访问 `http://设备IP/api/status`。

## 接口

- `GET /api/status`：返回 `device=h7-gateway`、`api_version=1`、ESP32 运行时间、Wi-Fi RSSI、UART 波特率、MQTT 上传就绪状态和最近有效串口记录。
- `POST /api/command`：请求 `{"command":"PING"}`，回复 `{"ok":true,"target":"esp32","command":"PING","reply":"PONG"}`。
- 回显请求 `{"command":"ECHO","message":"你好"}`，message 限制为 128 个 UTF-8 字节；整个请求体为 1～383 字节。
- 非法命令、损坏的 JSON 返回 HTTP 400；接收超时返回 408；未知接口返回 404。

## 状态含义与范围

PING/ECHO在ESP32本地执行；Bootloader指令和OTA数据转发给STM32。点击“选择升级文件”和“发送固件”，上位机自动进入引导页并上传，应用最大384 KiB；完成后只保存到外部Flash，再点击“安装固件”。未完成上传停止传输60秒后可以重新上传，完整下载等待安装不会过期。本版没有OneNET云端查询接口，界面记录来自ESP32最近收到的H7串口数据。

`mqtt_ready` 表示 MQTT 已连接并订阅回复主题，不是最近一条数据已被云端确认。`sample_age_ms` 是距离最近有效串口记录的时间，不是传感器采样年龄；缓存补传可能带来旧记录。目前 H7 只在上传就绪时发送记录，所以没有记录不能直接判断 H7 离线。超过约 49.7 天无新记录时，32 位数据年龄会回绕。

HTTP服务不含认证，供可信局域网使用。

## 验证

`python -m unittest test_ota test_client -v` 使用本机HTTP模拟器测试状态、中文回显、上传、重试、取消和CRC错误。它不替代实机测试。

实机验收：PING 获得 PONG；中文 ECHO 原样返回；断开设备后窗口可操作并显示失败；重新联网后可恢复查询；原有 MQTT 上报继续工作。
