#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {
    char buffer[1024];
    size_t used, total;
} OneNetReply;
/**
  * @brief 按主题和偏移组装平台JSON回复，完整且code=200时输出sequence。
  * @param reply 组帧状态；expected为目标主题；topic和topic_len为当前主题及长度。
  * @param data和length为分片；offset和total为消息偏移及总长；sequence为成功消息ID输出。
  * @retval true表示完整业务成功回复，false表示待续、失败或非法数据。
  */
bool OneNetReply_Push(OneNetReply *reply, const char *expected,
    const char *topic, int topic_len, const char *data, int length,
    int offset, int total, uint32_t *sequence);
