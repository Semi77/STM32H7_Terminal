#include "local_http.h"
#include "h7_uart.h"
#include "selftest_report.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include "ota_wire.h"

/**
  * @brief 发送并释放JSON对象，req为请求句柄，body为响应对象。
  * @retval HTTP发送结果。
  */
static esp_err_t send_json(httpd_req_t *req, cJSON *body)
{
    char *text = body ? cJSON_PrintUnformatted(body) : NULL;
    cJSON_Delete(body);
    if (!text) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return result;
}

/**
  * @brief 返回设备与最近H7数据状态，req为HTTP请求。
  * @retval HTTP发送结果。
  */
static esp_err_t status_get(httpd_req_t *req)
{
    h7_status_t status;
    h7_uart_get_status(&status);
    wifi_ap_record_t ap;
    bool wifi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    cJSON *body = cJSON_CreateObject();
    if (!body) return send_json(req, NULL);
    cJSON *report=selftest_report_json();
    if(!report) {cJSON_Delete(body);return send_json(req,NULL);}
    cJSON_AddItemToObject(body,"selftest",report);
    cJSON_AddStringToObject(body, "device", "h7-gateway");
    cJSON_AddNumberToObject(body, "api_version", 1);
    cJSON_AddStringToObject(body, "mode", status.bootloader_page ? "bootloader" : "unknown_or_app");
    cJSON_AddBoolToObject(body, "bootloader_supported", true);
    cJSON_AddBoolToObject(body, "ota_supported", true);
    cJSON_AddNumberToObject(body, "ota_install_version", 1);
    cJSON_AddNumberToObject(body, "ota_download_version", 1);
    cJSON_AddBoolToObject(body, "wifi_connected", wifi);
    if (wifi) cJSON_AddNumberToObject(body, "rssi", ap.rssi);
    cJSON_AddNumberToObject(body, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(body, "uart_baud", H7_UART_BAUD_RATE);
    cJSON_AddBoolToObject(body, "mqtt_ready", status.mqtt_ready);
    cJSON_AddBoolToObject(body, "has_sample", status.has_sample);
    if (status.has_sample) {
        cJSON_AddNumberToObject(body, "sample_age_ms", status.sample_age_ms);
        cJSON *sample = cJSON_AddObjectToObject(body, "sample");
        if (!sample) { cJSON_Delete(body); return send_json(req, NULL); }
        cJSON_AddNumberToObject(sample, "sequence", status.sample.sequence);
        cJSON_AddNumberToObject(sample, "temperature", status.sample.temperature);
        cJSON_AddNumberToObject(sample, "humidity", status.sample.humidity);
        cJSON_AddNumberToObject(sample, "brightness", status.sample.brightness);
    }
    return send_json(req, body);
}

/**
  * @brief 接收有长度限制的PING或ECHO命令并回复，req为HTTP请求。
  * @retval HTTP处理结果，未知命令不会转发给STM32。
  */
static esp_err_t command_post(httpd_req_t *req)
{
    char buffer[384];
    if (req->content_len == 0 || req->content_len >= sizeof(buffer))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body must be 1..383 bytes");
    size_t received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, buffer + received, req->content_len - received);
        if (count <= 0) {
            httpd_resp_send_err(req, count == HTTPD_SOCK_ERR_TIMEOUT ?
                HTTPD_408_REQ_TIMEOUT : HTTPD_400_BAD_REQUEST, "Incomplete request");
            return ESP_FAIL;
        }
        received += (size_t)count;
    }
    buffer[received] = '\0';
    cJSON *input = cJSON_ParseWithLengthOpts(buffer, received + 1, NULL, true);
    const cJSON *command = cJSON_GetObjectItemCaseSensitive(input, "command");
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(input, "message");
    bool ping = cJSON_IsString(command) && strcmp(command->valuestring, "PING") == 0;
    bool echo = cJSON_IsString(command) && strcmp(command->valuestring, "ECHO") == 0 &&
                cJSON_IsString(message) && strlen(message->valuestring) <= 128;
    bool boot = cJSON_IsString(command) && strcmp(command->valuestring, "Bootloader") == 0;
    /* App是Bootloader的逆操作：让停留在引导页的STM32复位回Bank2应用。 */
    bool app = cJSON_IsString(command) && strcmp(command->valuestring, "App") == 0;
    if (!ping && !echo && !boot && !app) {
        cJSON_Delete(input);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected PING, ECHO, Bootloader or App");
    }
    char page_reply[H7_REPLY_MAX] = {0};
    if (boot || app) {
        esp_err_t result = h7_uart_enter_page(app ? "App" : "Bootloader", page_reply);
        if (result != ESP_OK) {
            cJSON_Delete(input);
            httpd_resp_set_status(req, result == ESP_ERR_TIMEOUT ? "504 Gateway Timeout" : "502 Bad Gateway");
            return httpd_resp_send(req, result == ESP_ERR_TIMEOUT ?
                "H7 expected command reply timeout; check firmware and UART" : "H7 rejected request; check OTA recovery or display status", HTTPD_RESP_USE_STRLEN);
        }
    }
    cJSON *body = cJSON_CreateObject();
    if (body) {
        cJSON_AddBoolToObject(body, "ok", true);
        cJSON_AddStringToObject(body, "target", (boot || app) ? "stm32" : "esp32");
        cJSON_AddStringToObject(body, "command", command->valuestring);
        /* 回填STM32的真实确认，使上位机能区分进入引导页与启动应用。 */
        cJSON_AddStringToObject(body, "reply",
                                (boot || app) ? page_reply : (ping ? "PONG" : message->valuestring));
    }
    cJSON_Delete(input);
    return send_json(req, body);
}

/**
  * @brief 获得IP时在串口打印上位机连接地址，其他参数为事件框架输入。
  * @retval 无。
  */
static void log_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;
    const ip_event_got_ip_t *event = data;
    ESP_LOGI("local_http", "PC URL: http://" IPSTR, IP2STR(&event->ip_info.ip));
}

/**
  * @brief 接收一帧有界二进制OTA请求并返回H7原始确认行，req为HTTP请求。
  * @retval HTTP处理结果，超时不代表Flash一定未写入，上位机使用同一帧重试。
  */
static esp_err_t ota_post(httpd_req_t *req)
{
    uint8_t frame[OTA_FRAME_MAX];
    char reply[100];
    if (req->content_len<24 || req->content_len>sizeof(frame))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid OTA frame length");
    size_t used=0;
    while (used<req->content_len) {
        int n=httpd_req_recv(req,(char*)frame+used,req->content_len-used);
        if (n<=0) { httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Incomplete OTA frame"); return ESP_FAIL; }
        used+=(size_t)n;
    }
    esp_err_t result=h7_uart_ota(frame,used,reply);
    if (result!=ESP_OK) {
        httpd_resp_set_status(req,result==ESP_ERR_TIMEOUT?"504 Gateway Timeout":"400 Bad Request");
        return httpd_resp_send(req,"H7 OTA frame rejected or timeout",HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_type(req,"text/plain");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,reply,HTTPD_RESP_USE_STRLEN);
}

esp_err_t local_http_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.lru_purge_enable = true;
    esp_err_t result = httpd_start(&server, &config);
    if (result != ESP_OK) return result;
    const httpd_uri_t routes[] = {
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/command", .method = HTTP_POST, .handler = command_post},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_post},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        result = httpd_register_uri_handler(server, &routes[i]);
        if (result != ESP_OK) { httpd_stop(server); return result; }
    }
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, log_ip, NULL);
    if (result != ESP_OK) { httpd_stop(server); return result; }
    /* 首次联网已完成，后续重新获取地址由事件回调打印。 */
    esp_netif_t *netif = esp_netif_get_default_netif();
    esp_netif_ip_info_t info;
    if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK)
        ESP_LOGI("local_http", "PC URL: http://" IPSTR, IP2STR(&info.ip));
    return ESP_OK;
}
