#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "onenet_reply.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "onenet_config.h"
#include "onenet_test.h"
#include "h7_uart.h"

#define POST_TOPIC "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post"
#define REPLY_TOPIC POST_TOPIC "/reply"
static const char *TAG = "onenet_test";
static atomic_bool mqtt_link, report_ready;
static atomic_uint pending_id, confirmed_id;
static OneNetReply platform_reply;

/**
  * @brief 将已经获得OneNET业务成功回复的sequence回传给H7，单次写入避免拆帧。
  * @retval 无，串口失败时由H7超时重试。
  */
static void send_ack(uint32_t sequence)
{
    char line[24];
    int length=snprintf(line, sizeof(line), "ACK:%" PRIu32 "\r\n", sequence);
    if (length > 0 && (size_t)length < sizeof(line))
        (void)uart_write_bytes(H7_UART_PORT, line, length);
}

/**
  * @brief 组装平台回复，仅将当前在途消息的成功确认回传H7。
  * @param event MQTT接收分片事件。
  * @retval 无。
  */
static void receive_reply(esp_mqtt_event_handle_t event)
{
    uint32_t sequence;
    if (OneNetReply_Push(&platform_reply, REPLY_TOPIC, event->topic, event->topic_len,
            event->data, event->data_len, event->current_data_offset, event->total_data_len, &sequence) &&
        sequence==atomic_load(&pending_id)) {
        atomic_store(&confirmed_id, sequence);
        send_ack(sequence);
    }
}

/**
  * @brief 订阅就绪后转发H7单条在途记录，并定时重试失败的主题订阅。
  * @param arg MQTT客户端句柄。
  * @retval 无，任务持续运行。
  */
static void report_task(void *arg)
{
    esp_mqtt_client_handle_t client=arg;
    h7_sample_t sample;
    char payload[256];
    TickType_t last_subscribe=0;
    bool attempted=false;
    for (;;) {
        if (!atomic_load(&mqtt_link)) {
            attempted=false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!atomic_load(&report_ready)) {
            TickType_t now=xTaskGetTickCount();
            if (!attempted || (TickType_t)(now-last_subscribe)>=pdMS_TO_TICKS(5000)) {
                last_subscribe=now;
                attempted=true;
                if (esp_mqtt_client_subscribe(client, REPLY_TOPIC, 0)<0)
                    ESP_LOGW(TAG, "Reply subscription failed; retry in 5 seconds");
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        attempted=false;
        if (!h7_uart_get_sample(&sample, pdMS_TO_TICKS(100))) continue;
        if (sample.sequence==atomic_load(&confirmed_id)) {
            /* H7可能丢失ACK，重发确认即可，避免同次运行中重复上报。 */
            send_ack(sample.sequence);
            continue;
        }
        int length=snprintf(payload, sizeof(payload),
            "{\"id\":\"%" PRIu32 "\",\"version\":\"1.0\",\"params\":{"
            "\"Temperature\":{\"value\":%" PRIu32 "},\"Humidity\":{\"value\":%" PRIu32 ".%02" PRIu32 "},"
            "\"Brightness\":{\"value\":%" PRIu32 "}}}",
            sample.sequence, sample.temperature, sample.humidity/100U,
            sample.humidity%100U, sample.brightness);
        if (length<=0 || (size_t)length>=sizeof(payload)) continue;
        atomic_store(&pending_id, sample.sequence);
        /* QoS0不另建MQTT离线队列，交付确认由平台reply完成，H7负责超时重试。 */
        if (!atomic_load(&report_ready) || esp_mqtt_client_publish(client, POST_TOPIC, payload, length, 0, 0)<0)
            ESP_LOGW(TAG, "Publish failed seq=%" PRIu32 "; H7 will retry", sample.sequence);
        else ESP_LOGI(TAG, "Awaiting OneNET reply seq=%" PRIu32, sample.sequence);
    }
}

/**
  * @brief 同步连接与订阅就绪状态，并处理平台回复分片。
  * @param arg 未使用，base为事件类别，event_id为事件编号，data为MQTT事件。
  * @retval 无。
  */
static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
    (void)arg; (void)base;
    esp_mqtt_event_handle_t event=data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        atomic_store(&report_ready, false);
        h7_uart_set_mqtt_connected(false);
        atomic_store(&mqtt_link, true);
        ESP_LOGI(TAG, "Connected; waiting for reply subscription");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        if (atomic_load(&mqtt_link) && event->data && event->data_len>0 && (uint8_t)event->data[0]<0x80) {
            atomic_store(&report_ready, true);
            h7_uart_set_mqtt_connected(true);
        } else ESP_LOGW(TAG, "Subscription rejected; will retry");
        break;
    case MQTT_EVENT_DISCONNECTED:
        atomic_store(&mqtt_link, false);
        atomic_store(&report_ready, false);
        h7_uart_set_mqtt_connected(false);
        memset(&platform_reply, 0, sizeof(platform_reply));
        ESP_LOGW(TAG, "Disconnected; H7 caches data");
        break;
    case MQTT_EVENT_DATA:
        receive_reply(event);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error type=%d", event->error_handle ? event->error_handle->error_type : -1);
        break;
    default: break;
    }
}

/**
  * @brief 启动MQTT自动重连和带平台确认的H7转发任务。
  * @retval 无。
  */
void onenet_test_start(void)
{
    const esp_mqtt_client_config_t config={
        .broker.address.uri=CONFIG_EXAMPLE_MQTT_BROKER_URI,
        .credentials.client_id=ONENET_DEVICE_NAME,
        .credentials.username=ONENET_PRODUCT_ID,
        .credentials.authentication.password=ONENET_TOKEN,
        .session.protocol_ver=MQTT_PROTOCOL_V_3_1_1,
        .session.keepalive=60,
        .network.disable_auto_reconnect=false,
        .network.reconnect_timeout_ms=5000,
    };
    esp_mqtt_client_handle_t client=esp_mqtt_client_init(&config);
    ESP_ERROR_CHECK(client ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(xTaskCreate(report_task, "onenet_report", 4096, client, 5, NULL)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));
}
