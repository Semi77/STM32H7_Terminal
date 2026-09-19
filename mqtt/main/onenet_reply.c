#include "onenet_reply.h"
#include "cJSON.h"
#include <string.h>

bool OneNetReply_Push(OneNetReply *reply, const char *expected,
    const char *topic, int topic_len, const char *data, int length,
    int offset, int total, uint32_t *sequence)
{
    if (offset==0) {
        reply->used=reply->total=0;
        if (!topic || topic_len!=(int)strlen(expected) ||
            memcmp(topic, expected, strlen(expected)) || total<=0 || total>=(int)sizeof(reply->buffer)) return false;
        reply->total=(size_t)total;
    }
    if (!reply->total || length<0 || !data || offset!=(int)reply->used ||
        total!=(int)reply->total || (size_t)length>reply->total-reply->used) {
        reply->total=0;
        return false;
    }
    memcpy(reply->buffer+reply->used, data, (size_t)length);
    reply->used+=(size_t)length;
    if (reply->used!=reply->total) return false;
    reply->buffer[reply->used]='\0';
    reply->total=0;
    /* 禁止嵌入NUL导致解析器只校验有效前缀而忽略后续内容。 */
    if (memchr(reply->buffer, 0, reply->used)) return false;
    cJSON *root=cJSON_ParseWithLengthOpts(reply->buffer, reply->used+1U, NULL, true);
    if (!root) return false;
    const cJSON *id=cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *code=cJSON_GetObjectItemCaseSensitive(root, "code");
    uint32_t value=0;
    bool valid=cJSON_IsObject(root) && cJSON_IsString(id) && id->valuestring && id->valuestring[0] &&
               cJSON_IsNumber(code) && code->valuedouble==200.0;
    if (valid) {
        for (const char *p=id->valuestring; *p; ++p) {
            uint32_t digit=(unsigned char)*p-(uint32_t)'0';
            if (digit>9 || value>(UINT32_MAX-digit)/10U) { valid=false; break; }
            value=value*10U+digit;
        }
    }
    cJSON_Delete(root);
    if (!valid || !value) return false;
    *sequence=value;
    return true;
}
