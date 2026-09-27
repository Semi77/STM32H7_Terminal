#pragma once

#include "driver/uart.h"
#include "esp_err.h"
#include <stdbool.h>

/* H7通信使用UART1，GPIO4发送、GPIO5接收，双方采用460800、8N1。 */
#define H7_UART_PORT UART_NUM_1
#define H7_UART_TX_PIN 4
#define H7_UART_RX_PIN 5
#define H7_UART_BAUD_RATE 460800
/* STM32页面确认行的最大长度，含结尾空字符。 */
#define H7_REPLY_MAX 16

/**
  * @brief 初始化连接STM32H7的UART1及收发缓冲区，上电时调用一次。
  * @retval ESP_OK表示成功，其他值为串口驱动错误码。
  */
esp_err_t h7_uart_init(void);

/* 模拟数据单位与H7一致：摄氏度、百分比、勒克斯。 */
typedef struct {
    uint32_t sequence;
    uint32_t temperature;
    uint32_t humidity;
    uint32_t brightness;
} h7_sample_t;

/**
  * @brief 启动按行接收任务及单条转发缓冲，持久缓存由H7负责。
  * @retval ESP_OK表示成功，ESP_ERR_NO_MEM表示资源不足。
  */
esp_err_t h7_uart_start_receive(void);

/**
  * @brief 从缓存取出最旧的一条模拟数据。
  * @param sample 数据输出指针。
  * @param wait_ticks 等待数据的RTOS节拍数。
  * @retval true表示取得数据，false表示超时或参数无效。
  */
bool h7_uart_get_sample(h7_sample_t *sample, TickType_t wait_ticks);

/**
  * @brief 更新MQTT上传就绪标志，供每秒联网心跳使用。
  * @param connected MQTT是否连接且已订阅回复主题。
  * @retval 无。
  */
void h7_uart_set_mqtt_connected(bool connected);
/**
  * @brief 注册Wi-Fi状态事件并启动心跳，事件循环创建后、连接Wi-Fi前调用。
  * @retval ESP_OK表示成功，否则为初始化错误。
  */
esp_err_t h7_uart_start_heartbeat(void);

/**
  * @brief 启动网络校时，并定期将UTC秒数发送给STM32。
  * @retval ESP_OK表示任务已启动，其他值表示初始化失败。
  */
esp_err_t h7_uart_start_time_sync(void);

typedef struct {
    bool has_sample;
    bool mqtt_ready;
    h7_sample_t sample;
    uint32_t sample_age_ms;
    bool bootloader_page;
} h7_status_t;

/**
  * @brief 获取不消耗上传队列的状态快照，status为非空输出指针。
  * @retval 无。
  */
void h7_uart_get_status(h7_status_t *status);

/**
  * @brief 让H7进入引导页或启动应用，并等待屏幕初始化后的真实确认。
  * @param cmd 发送给H7的页面命令，取"Bootloader"或"App"。
  * @param reply 非空时写入STM32返回的确认文本，供上位机校验实际动作。
  * @retval ESP_OK表示收到确认，超时或页面初始化失败返回错误。
  */
esp_err_t h7_uart_enter_page(const char *cmd, char reply[H7_REPLY_MAX]);

/**
  * @brief 转发一帧OTA请求并等待匹配回复；frame/size为二进制帧，reply为至少100字节输出区。
  * @retval ESP_OK表示取得匹配回复，其业务状态及CRC仍由上位机检查。
  */
esp_err_t h7_uart_ota(const uint8_t *frame, size_t size, char reply[100]);
