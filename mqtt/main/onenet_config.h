#pragma once
#include "sdkconfig.h"

/**
  * @brief 从本机ESP-IDF配置读取OneNET设备身份和访问令牌。
  */
#define ONENET_PRODUCT_ID CONFIG_ONENET_PRODUCT_ID
#define ONENET_DEVICE_NAME CONFIG_ONENET_DEVICE_NAME
#define ONENET_TOKEN CONFIG_ONENET_TOKEN
