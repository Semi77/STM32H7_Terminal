#include <stdatomic.h>
#include <string.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "h7_uart.h"
#include <stdio.h>
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ota_wire.h"
#include "selftest_report.h"
#include "selftest_wire.h"

#define H7_UART_BUFFER_SIZE 1024

/**
  * @brief 配置UART1为460800、8N1、无流控，并安装收发缓冲驱动。
  * @retval ESP_OK表示成功，其他值为串口驱动错误码。
  */
esp_err_t h7_uart_init(void)
{
    const uart_config_t config = {
        .baud_rate = H7_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t result = uart_param_config(H7_UART_PORT, &config);
    if (result != ESP_OK) {
        return result;
    }
    result = uart_set_pin(H7_UART_PORT, H7_UART_TX_PIN, H7_UART_RX_PIN,
                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (result != ESP_OK) {
        return result;
    }
    return uart_driver_install(H7_UART_PORT, H7_UART_BUFFER_SIZE,
                               H7_UART_BUFFER_SIZE, 0, NULL, 0);
}


#define H7_SAMPLE_QUEUE_LENGTH 1
#define H7_LINE_SIZE SELFTEST_LINE_SIZE

static QueueHandle_t sample_queue;
static QueueHandle_t ota_replies;
static QueueHandle_t page_replies;
static atomic_int page_expected;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static h7_sample_t latest_sample;
static TickType_t latest_tick;
static bool has_sample;
static atomic_uint boot_reply_tick;
static atomic_int boot_reply;


/**
  * @brief 解析H7整数JSON并暂存一条待转发数据，持久缓存由H7负责。
  * @param line 以空字符结束的一行串口数据。
  * @retval 无。
  */
static void accept_line(const char *line)
{
    if(selftest_report_accept(line)) return;
    if (strncmp(line,"OTA:",4)==0) {
        char reply[100]={0};
        if (strlen(line)<sizeof(reply)) {
            strcpy(reply,line);
            if (ota_replies) (void)xQueueOverwrite(ota_replies,reply);
        }
        return;
    }
    if (strcmp(line, "BOOT_READY") == 0 || strcmp(line, "BOOT_DISPLAY_ERROR") == 0 ||
        strcmp(line, "BOOT_REJECTED") == 0 || strcmp(line, "APP_STARTING") == 0) {
        int state=strcmp(line,"BOOT_READY")==0?1:strcmp(line,"APP_STARTING")==0?2:-1;
        atomic_store(&boot_reply_tick, xTaskGetTickCount());
        atomic_store(&boot_reply, state);
        /* 按命令过滤确认，周期BOOT_READY不能覆盖App的接受或拒绝结果。 */
        int expected=atomic_load(&page_expected);
        if (page_replies && expected && (state==expected || state<0))
            (void)xQueueOverwrite(page_replies,&state);
        return;
    }
    if (strcmp(line, "BOOT_ACCEPTED") == 0) return;
    unsigned long seq, temperature, humidity, brightness;
    int end = 0;
    int fields = sscanf(line,
        "{\"seq\":%lu,\"Temperature\":%lu,\"Humidity\":%lu,\"Brightness\":%lu}%n",
        &seq, &temperature, &humidity, &brightness, &end);
    if (fields != 4 || end == 0 || line[end] != '\0' || seq == 0 ||
        temperature > 125 || humidity > 100 || brightness > 200000) {
        ESP_LOGW("h7_uart", "Invalid H7 data line");
        return;
    }
    h7_sample_t sample = {seq, temperature, humidity, brightness};
    atomic_store(&boot_reply, 0);
    portENTER_CRITICAL(&status_lock);
    latest_sample = sample;
    latest_tick = xTaskGetTickCount();
    has_sample = true;
    portEXIT_CRITICAL(&status_lock);
    /* H7只允许一条在途记录，重复重试覆盖同一槽位，防止双端离线队列积压。 */
    (void)xQueueOverwrite(sample_queue, &sample);
    ESP_LOGI("h7_uart", "RX seq=%lu temp=%lu humidity=%lu light=%lu",
             seq, temperature, humidity, brightness);
}

/**
  * @brief 持续拼接换行分隔的数据，支持分段接收、多行接收和超长行丢弃。
  * @param arg 未使用的任务参数。
  * @retval 无，任务持续运行。
  */
static void receive_task(void *arg)
{
    (void)arg;
    char line[H7_LINE_SIZE];
    uint8_t bytes[128];
    size_t used = 0;
    bool overflow = false;
    for (;;) {
        int count = uart_read_bytes(H7_UART_PORT, bytes, sizeof(bytes), pdMS_TO_TICKS(100));
        for (int i = 0; i < count; ++i) {
            if (bytes[i] == '\n') {
                if (!overflow && used > 0) {
                    if (line[used - 1] == '\r') --used;
                    line[used] = '\0';
                    accept_line(line);
                }
                used = 0;
                overflow = false;
            } else if (!overflow) {
                if (bytes[i] == 0 || used >= sizeof(line) - 1) {
                    overflow = true;
                    ESP_LOGW("h7_uart", "Discarding invalid or oversized line");
                } else {
                    line[used++] = (char)bytes[i];
                }
            }
        }
    }
}

/**
  * @brief 创建有限缓存和串口接收任务，联网等待期间也持续接收数据。
  * @retval ESP_OK表示成功，ESP_ERR_NO_MEM表示创建失败。
  */
esp_err_t h7_uart_start_receive(void)
{
    sample_queue = xQueueCreate(H7_SAMPLE_QUEUE_LENGTH, sizeof(h7_sample_t));
    if (sample_queue == NULL) return ESP_ERR_NO_MEM;
    ota_replies = xQueueCreate(1, 100);
    if (!ota_replies) { vQueueDelete(sample_queue); sample_queue=NULL; return ESP_ERR_NO_MEM; }
    page_replies=xQueueCreate(1,sizeof(int));
    if (!page_replies) {
        vQueueDelete(sample_queue); sample_queue=NULL;
        vQueueDelete(ota_replies); ota_replies=NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(receive_task, "h7_receive", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(sample_queue);
        vQueueDelete(ota_replies); ota_replies=NULL;
        vQueueDelete(page_replies); page_replies=NULL;
        sample_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/**
  * @brief 取出最旧数据供OneNET发送任务使用。
  * @param sample 数据输出指针。
  * @param wait_ticks 最大等待节拍数。
  * @retval true表示成功取得数据，否则返回false。
  */
bool h7_uart_get_sample(h7_sample_t *sample, TickType_t wait_ticks)
{
    return sample != NULL && sample_queue != NULL &&
           xQueueReceive(sample_queue, sample, wait_ticks) == pdTRUE;
}


static atomic_bool wifi_has_ip;
static atomic_bool mqtt_connected;

/**
  * @brief 复制最近有效串口数据及其年龄，status为非空输出指针。
  * @retval 无，数据年龄不代表H7独立心跳状态。
  */
void h7_uart_get_status(h7_status_t *status)
{
    portENTER_CRITICAL(&status_lock);
    status->has_sample = has_sample;
    status->sample = latest_sample;
    status->sample_age_ms = (uint32_t)(xTaskGetTickCount() - latest_tick) * portTICK_PERIOD_MS;
    portEXIT_CRITICAL(&status_lock);
    status->mqtt_ready = atomic_load(&mqtt_connected);
    status->bootloader_page = atomic_load(&boot_reply) == 1 &&
        (TickType_t)(xTaskGetTickCount() - atomic_load(&boot_reply_tick)) < pdMS_TO_TICKS(3000);
}

esp_err_t h7_uart_enter_page(const char *cmd, char reply[H7_REPLY_MAX])
{
    /* HTTP任务等待期间，独立UART任务继续接收；不以串口发送成功代替H7确认。 */
    if (cmd == NULL || (strcmp(cmd, "Bootloader") != 0 && strcmp(cmd, "App") != 0))
        return ESP_ERR_INVALID_ARG;
    if (!page_replies) return ESP_ERR_INVALID_STATE;
    int expected=strcmp(cmd,"App")==0?2:1;
    xQueueReset(page_replies);
    atomic_store(&page_expected,expected);
    if (reply) reply[0]='\0';
    char command[16];
    int length=snprintf(command,sizeof(command),"%s\r\n",cmd);
    esp_err_t result=ESP_FAIL;
    if (uart_write_bytes(H7_UART_PORT,command,(size_t)length)==length) {
        int state;
        if (xQueueReceive(page_replies,&state,pdMS_TO_TICKS(8000))==pdTRUE) {
            result=state==expected?ESP_OK:ESP_FAIL;
            if (result==ESP_OK && reply)
                strlcpy(reply,expected==2?"APP_STARTING":"BOOT_READY",H7_REPLY_MAX);
        } else result=ESP_ERR_TIMEOUT;
    }
    atomic_store(&page_expected,0);
    return result;
}

esp_err_t h7_uart_enter_bootloader(void)
{
    return h7_uart_enter_page("Bootloader", NULL);
}

esp_err_t h7_uart_ota(const uint8_t *frame, size_t size, char reply[100])
{
    if (!ota_replies || size<24 || size>OTA_FRAME_MAX || ota_u32(frame)!=OTA_MAGIC ||
        ota_u32(frame+16)!=size-24 ||
        (ota_crc_update(0xFFFFFFFFU,frame,size-4)^0xFFFFFFFFU)!=ota_u32(frame+size-4))
        return ESP_ERR_INVALID_ARG;
    xQueueReset(ota_replies);
    /* 整帧一次写入，UART驱动的发送锁避免NET/ACK行插入二进制帧内部。 */
    if (uart_write_bytes(H7_UART_PORT,frame,size)!=(int)size) return ESP_FAIL;
    /* 安装包含四个内部扇区擦除及整包校验，等待实际完成确认。 */
    TickType_t start=xTaskGetTickCount(), limit=pdMS_TO_TICKS(ota_u32(frame+4)==OTA_INSTALL?120000:15000);
    while ((TickType_t)(xTaskGetTickCount()-start)<limit) {
        TickType_t remaining=limit-(TickType_t)(xTaskGetTickCount()-start);
        if (xQueueReceive(ota_replies,reply,remaining)!=pdTRUE) break;
        unsigned long session,cmd,offset;
        if (sscanf(reply,"OTA:%lx:%lu:%lu",&session,&cmd,&offset)==3 &&
            session==ota_u32(frame+8) && cmd==ota_u32(frame+4) && offset==ota_u32(frame+12)) return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

/**
  * @brief 保存MQTT上传就绪状态供心跳任务读取。
  * @param connected 是否已连接并成功订阅平台回复主题。
  * @retval 无。
  */
void h7_uart_set_mqtt_connected(bool connected)
{
    atomic_store(&mqtt_connected, connected);
}

/**
  * @brief 跟踪Wi-Fi断开及IPv4获取丢失事件，断网时同时清除MQTT状态。
  * @param arg 未使用。
  * @param base 事件类别。
  * @param id 事件编号。
  * @param data 未使用的事件数据。
  * @retval 无。
  */
static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&wifi_has_ip, true);
    } else if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) ||
               (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) {
        atomic_store(&wifi_has_ip, false);
        atomic_store(&mqtt_connected, false);
    }
}

/**
  * @brief 每秒发送NET状态行，Wi-Fi取得IP且平台回复订阅成功时发送NET:1。
  * @param arg 未使用的任务参数。
  * @retval 无，任务持续运行。
  */
static void heartbeat_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        bool online = atomic_load(&wifi_has_ip) && atomic_load(&mqtt_connected);
        uart_write_bytes(H7_UART_PORT, online ? "NET:1\r\n" : "NET:0\r\n", 7);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
    }
}

/**
  * @brief 注册网络状态事件并启动独立心跳任务，保证等待联网期间仍返回离线状态。
  * @retval ESP_OK表示成功，否则返回事件注册或任务创建错误。
  */
esp_err_t h7_uart_start_heartbeat(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, network_event, NULL));
    return xTaskCreate(heartbeat_task, "h7_heartbeat", 2048, NULL, 5, NULL)
           == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
