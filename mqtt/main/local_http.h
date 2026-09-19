#pragma once
#include "esp_err.h"

/**
  * @brief Wi-Fi连接后启动局域网状态查询及命令测试服务。
  * @retval ESP_OK表示启动成功，否则返回服务初始化错误。
  */
esp_err_t local_http_start(void);
