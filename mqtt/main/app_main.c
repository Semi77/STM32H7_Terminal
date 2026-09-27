#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "onenet_test.h"
#include "h7_uart.h"
#include "local_http.h"

/**
  * @brief 初始化H7串口和网络，启动局域网服务及OneNET转发。
  * @retval 无。
  */
void app_main(void)
{
    ESP_ERROR_CHECK(h7_uart_init());
    ESP_ERROR_CHECK(h7_uart_start_receive());
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(h7_uart_start_heartbeat());
    ESP_ERROR_CHECK(example_connect());
    ESP_ERROR_CHECK(h7_uart_start_time_sync());
    ESP_ERROR_CHECK(local_http_start());
    onenet_test_start();
}
