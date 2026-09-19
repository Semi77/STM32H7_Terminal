#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "onenet_reply.h"

/**
  * @brief 将text作为完整平台回复输入解析器，返回是否获得业务成功确认。
  */
static bool parse(const char *text)
{
    OneNetReply reply={0};
    uint32_t sequence=0;
    return OneNetReply_Push(&reply, "reply", "reply", 5, text, (int)strlen(text),
                           0, (int)strlen(text), &sequence);
}

/**
  * @brief 验证平台成功、失败、错序分片和非法消息都按协议处理。
  */
int main(void)
{
    const char *message="{\"id\":\"123\",\"code\":200,\"msg\":\"success\"}";
    int length=(int)strlen(message);
    for (int split=1; split<length; ++split) {
        OneNetReply reply={0};
        uint32_t sequence=0;
        assert(!OneNetReply_Push(&reply,"reply","reply",5,message,split,0,length,&sequence));
        assert(OneNetReply_Push(&reply,"reply",NULL,0,message+split,length-split,split,length,&sequence));
        assert(sequence==123);
    }
    assert(parse(message));
    assert(parse("{\"code\":200, \"id\":\"4294967295\"}"));
    assert(!parse("{\"id\":\"123\",\"code\":400}"));
    assert(!parse("{\"id\":\"123\",\"code\":\"200\"}"));
    assert(!parse("{\"id\":\"4294967296\",\"code\":200}"));
    assert(!parse("{\"id\":\"-1\",\"code\":200}"));
    assert(!parse("{\"id\":\"0\",\"code\":200}"));
    assert(!parse("{\"id\":123,\"code\":200}"));
    assert(!parse("{\"id\":\"123\",\"code\":200}junk"));
    assert(!parse("{\"id\":\"123\""));
    OneNetReply reply={0};
    uint32_t sequence=0;
    assert(!OneNetReply_Push(&reply,"reply","other",5,message,length,0,length,&sequence));
    assert(!OneNetReply_Push(&reply,"reply","reply",5,message,10,0,length,&sequence));
    assert(!OneNetReply_Push(&reply,"reply",NULL,0,message+11,length-11,11,length,&sequence));
    assert(!OneNetReply_Push(&reply,"reply","reply",5,message,length,0,1024,&sequence));
    char nul_message[100];
    memcpy(nul_message,message,(size_t)length+1U);
    nul_message[length+1]='x';
    assert(!OneNetReply_Push(&reply,"reply","reply",5,nul_message,length+2,0,length+2,&sequence));
    puts("OneNET reply: all split positions, codes, IDs, topic, offsets and bounds passed");
    return 0;
}
