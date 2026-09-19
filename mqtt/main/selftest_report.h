#pragma once
#include <stdbool.h>
#include "cJSON.h"
/** @brief 接收并确认合法自检行line，返回是否属于自检协议。 @retval true表示已处理。 */
bool selftest_report_accept(const char *line);
/** @brief 构造最近自检快照，返回对象由调用者释放。 @retval JSON对象或NULL。 */
cJSON *selftest_report_json(void);
