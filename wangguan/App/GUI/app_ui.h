#ifndef APP_UI_H
#define APP_UI_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t temperature_tenths_c;
    uint16_t humidity_tenths_percent;
    uint32_t light_lux;
    uint32_t uptime_seconds;
    bool network_online;
    bool mqtt_connected;
    bool ethernet_link;
    bool flash_ready;
} app_ui_data_t;

/**
  * @brief 创建可在 PC 与 STM32 共用的环境网关主界面。
  * @retval 无。
  */
void app_ui_create(void);

/**
  * @brief 在标题栏显示RS485采集状态与成败计数，数值本身由温湿度卡片显示。
  * @param status_text 状态短标签（如"OK"）；NULL表示尚无快照。
  * @param success_count 累计成功次数。
  * @param error_count 累计失败次数。
  * @param tx_started 本次请求是否开始发送；tx_complete表示发送完成；rx_bytes为接收字节数。
  * @retval 无，必须从LVGL所在线程调用。
  */
void app_ui_update_modbus_readout(const char *status_text,
                                  uint32_t success_count, uint32_t error_count,
                                  bool tx_started, bool tx_complete, uint16_t rx_bytes);

/**
  * @brief 只刷新上传帧序号标签，温湿度由RS485采样独立驱动。
  * @param sequence 当前发送帧序号。
  * @retval 无，必须由图形任务调用。
  */
void app_ui_update_upload_sequence(uint32_t sequence);

/**
  * @brief 只刷新光照卡片和串口发送结果，不触碰温湿度卡片。
  * @param light_lux 光照，单位勒克斯。
  * @param tx_ok 当前记录的串口发送是否成功。
  * @retval 无，必须由图形任务调用。
  */
void app_ui_update_light_status(uint32_t light_lux, bool tx_ok);

/**
  * @brief 刷新模拟数据、帧序号和串口发送结果，上传状态保持未知。
  * @param sequence 当前发送帧序号。
  * @param temperature_c 模拟温度，单位摄氏度。
  * @param humidity_percent 模拟湿度，单位百分比。
  * @param light_lux 模拟光照，单位勒克斯。
  * @param has_data 是否已有发送结果。
  * @param tx_ok 本地串口发送是否成功，不代表C3接收或云端上传成功。
  * @retval 无，必须由图形任务调用。
  */
void app_ui_update_uart_test(uint32_t sequence, uint32_t temperature_c,
                             uint32_t humidity_percent, uint32_t light_lux,
                             bool has_data, bool tx_ok);

/**
  * @brief 根据上电毫秒计数更新运行时长，自动处理计数回绕且仅在秒变化时刷新。
  * @param tick_ms 从零开始的32位毫秒计数，应在图形任务中持续传入HAL_GetTick()的值。
  * @retval 无，连续调用的间隔必须小于毫秒计数的一次回绕周期。
  */
void app_ui_update_uptime(uint32_t tick_ms);

/**
  * @brief 单独更新温湿度卡片，无有效数据时显示占位符，读取失败时变色提示。
  * @param temperature_tenths_c 温度值，单位为0.1摄氏度。
  * @param humidity_tenths_percent 湿度值，单位为0.1百分比。
  * @param has_data 是否已有至少一次有效测量。
  * @param read_ok 最近一次读取是否成功。
  * @retval 无，必须从LVGL所在线程调用。
  */
void app_ui_update_temperature_humidity(int16_t temperature_tenths_c,
                                        uint16_t humidity_tenths_percent,
                                        bool has_data,
                                        bool read_ok);

/**
  * @brief 使用一份数据快照刷新界面，参数 data 包含环境数据、运行时间和设备状态。
  * @param data 待显示的数据快照，必须在创建界面后从 LVGL 所在线程调用。
  * @retval 无。
  */
void app_ui_update_data(const app_ui_data_t *data);

/**
  * @brief 更新右上角联网标志。
  * @param online 是否收到有效且未超时的在线心跳。
  * @retval 无，必须从图形任务调用。
  */
void app_ui_update_network(bool online);

/**
  * @brief 更新右下角CPU与堆占用率。
  * @param cpu_percent 所有任务占用时间占总运行时间的百分比，0~100。
  * @param heap_percent 已分配堆占configTOTAL_HEAP_SIZE的百分比，0~100。
  * @retval 无，必须从图形任务调用。
  */
void app_ui_update_resource(uint32_t cpu_percent, uint32_t heap_percent);

#endif
