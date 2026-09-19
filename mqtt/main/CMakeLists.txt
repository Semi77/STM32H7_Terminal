idf_component_register(
    SRCS "app_main.c" "onenet_test.c" "onenet_reply.c" "h7_uart.c" "local_http.c" "selftest_report.c"
    PRIV_REQUIRES esp-tls esp_wifi esp_event esp_netif esp_timer esp_http_server nvs_flash esp_driver_uart espressif__cjson
    INCLUDE_DIRS "." "../../shared"
    EMBED_TXTFILES "mosquitto.org.crt"
)
