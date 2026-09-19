#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gateway_upload.h"
#include "w25q64_qspi.h"

static uint8_t flash[0x800000];
static uint32_t now, sends, programs, erases;
static int fail_program=-1;
static bool fail_uart;
static UART_HandleTypeDef uart;

/**
  * @brief 模拟NOR读取，验证每次访问位于芯片范围内。
  */
HAL_StatusTypeDef W25Q64_ReadData(uint32_t address, uint8_t *data, uint32_t count)
{
  assert(address+count<=sizeof(flash));
  memcpy(data, flash+address, count);
  return HAL_OK;
}
/**
  * @brief 模拟NOR只能从1写成0及掉电半写，禁止跨页或越过缓存分区。
  */
HAL_StatusTypeDef W25Q64_PageProgram(uint32_t address, const uint8_t *data, uint16_t count)
{
  assert(address>=0x400000 && address+count<=sizeof(flash));
  assert(count<=256 && (address%256)+count<=256);
  ++programs;
  bool fail=fail_program==0;
  if (fail_program>=0) --fail_program;
  uint32_t length=fail ? count/2 : count;
  for (uint32_t i=0; i<length; ++i) {
    assert((flash[address+i] & data[i])==data[i]);
    flash[address+i] &= data[i];
  }
  return fail ? HAL_ERROR : HAL_OK;
}
/**
  * @brief 模拟4KB扇区擦除，确认前4MB永远不被擦写。
  */
HAL_StatusTypeDef W25Q64_SectorErase(uint32_t address)
{
  assert(address>=0x400000 && address<sizeof(flash));
  ++erases;
  memset(flash+(address & ~4095U), 0xFF, 4096);
  return HAL_OK;
}
uint32_t HAL_GetTick(void) { return now; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *port, uint8_t *data, uint16_t count, uint32_t timeout)
{
  (void)port; (void)timeout;
  assert(count>0 && data[0]=='{');
  ++sends;
  return fail_uart ? HAL_ERROR : HAL_OK;
}

/**
  * @brief 清空模拟Flash并重置硬件故障注入。
  */
static void reset(void)
{
  memset(flash, 0xFF, sizeof(flash));
  now=sends=programs=erases=0;
  fail_program=-1;
  fail_uart=false;
}
/**
  * @brief 生成带持久序号的样本，温度25度、湿度60%、光照100勒克斯。
  */
static GatewaySample sample(void)
{
  GatewaySample value={0,25,60,100,now};
  assert(OfflineCache_NextSequence(&value.sequence));
  return value;
}

/**
  * @brief 验证ACK前不删除、重启恢复、循环覆盖和写入中断。
  */
static void test_cache(void)
{
  reset();
  assert(OfflineCache_Init());
  GatewaySample a=sample(), b=sample(), out;
  assert(OfflineCache_Append(&a));
  assert(OfflineCache_Append(&b));
  assert(OfflineCache_Peek(&out) && out.sequence==a.sequence);
  assert(!OfflineCache_Ack(b.sequence));
  assert(OfflineCache_Count()==2);
  assert(OfflineCache_Ack(a.sequence));
  assert(OfflineCache_Init());
  assert(OfflineCache_Count()==1);
  assert(OfflineCache_Peek(&out) && out.sequence==b.sequence);
  GatewaySample c=sample();
  assert(c.sequence>b.sequence);
  assert(OfflineCache_Ack(b.sequence));
  assert(OfflineCache_Append(&c));
  assert(OfflineCache_Init());
  assert(OfflineCache_Peek(&out) && out.sequence==c.sequence);

  /* 数据体写一半后重启，前后完整记录都必须可继续使用。 */
  GatewaySample d=sample();
  fail_program=0;
  assert(!OfflineCache_Append(&d));
  assert(OfflineCache_Init());
  GatewaySample e=sample();
  assert(OfflineCache_Append(&e));
  assert(OfflineCache_Ack(c.sequence));
  assert(OfflineCache_Peek(&out) && out.sequence==e.sequence);
  assert(OfflineCache_Ack(e.sequence));
  assert(OfflineCache_Count()==0);

  /* 预留序号日志半写后重启，不能复用此前已经发出的序号。 */
  fail_program=0;
  assert(!OfflineCache_Init());
  assert(OfflineCache_Init());
  GatewaySample f=sample();
  assert(f.sequence>e.sequence);

  reset();
  assert(OfflineCache_Init());
  const uint32_t capacity=(0x800000U-0x402000U)/32U;
  for (uint32_t i=0; i<capacity; ++i) {
    GatewaySample value=sample();
    assert(OfflineCache_Append(&value));
  }
  assert(OfflineCache_Count()==capacity);
  GatewaySample value=sample();
  assert(OfflineCache_Append(&value));
  assert(OfflineCache_Overwritten()==128);
  assert(OfflineCache_Count()==capacity-127);
  assert(OfflineCache_Peek(&out) && out.sequence==129);
  assert(OfflineCache_Init());
  assert(OfflineCache_Count()==capacity-127);
  assert(OfflineCache_Peek(&out) && out.sequence==129);
  /* 超过5条的历史数据必须逐条补传，不能直接追平读写指针。 */
  for (uint32_t i=0; i<300; ++i) {
    assert(OfflineCache_Peek(&out));
    assert(OfflineCache_Ack(out.sequence));
  }
  assert(OfflineCache_Count()==capacity-427);
  puts("cache: restart, torn writes, ID reservation, wrap and 300 ACKs passed");
}

/**
  * @brief 验证在线RAM发送、离线存储、确认超时和恢复补传状态转换。
  */
static void test_upload(void)
{
  reset();
  assert(GatewayUpload_Init(&uart));
  GatewayUpload_Process(false,0);
  GatewaySample a=sample();
  GatewayUpload_Submit(&a);
  GatewayUpload_Process(false,0);
  assert(sends==0 && OfflineCache_Count()==1);
  GatewayUpload_Process(true,0);
  assert(sends==1 && OfflineCache_Count()==1);
  assert(GatewayUpload_GetStatus().state==GATEWAY_RECOVERING);
  GatewayUpload_Process(true,a.sequence+1);
  assert(OfflineCache_Count()==1);
  now=100;
  GatewayUpload_Process(true,a.sequence);
  assert(OfflineCache_Count()==0);
  assert(GatewayUpload_GetStatus().state==GATEWAY_ONLINE);
  uint32_t writes=programs;
  GatewaySample b=sample();
  GatewayUpload_Submit(&b);
  GatewayUpload_Process(true,0);
  assert(sends==2 && programs==writes);
  GatewayUpload_Process(false,0);
  assert(OfflineCache_Count()==1);
  assert(GatewayUpload_GetStatus().state==GATEWAY_OFFLINE);
  GatewaySample c=sample();
  GatewayUpload_Submit(&c);
  assert(OfflineCache_Count()==2);
  now=200;
  GatewayUpload_Process(true,0);
  now=300;
  GatewayUpload_Process(true,b.sequence);
  assert(OfflineCache_Count()==1);
  now=400;
  GatewayUpload_Process(true,c.sequence);
  assert(OfflineCache_Count()==0);
  GatewaySample d=sample();
  GatewayUpload_Submit(&d);
  GatewayUpload_Process(true,0);
  now+=5001;
  GatewayUpload_Process(true,0);
  assert(OfflineCache_Count()==1 && GatewayUpload_GetStatus().retries==1);
  now+=2000;
  GatewayUpload_Process(true,0);
  GatewayUpload_Process(true,d.sequence);
  assert(OfflineCache_Count()==0);
  GatewaySample e=sample();
  GatewayUpload_Submit(&e);
  fail_uart=true;
  now+=100;
  GatewayUpload_Process(true,0);
  assert(OfflineCache_Count()==1);
  assert(GatewayUpload_GetStatus().tx_status==HAL_ERROR);
  puts("upload: offline, recovery, wrong ACK, RAM fast path, timeout and UART failure passed");
}
int main(void)
{
  test_cache();
  test_upload();
  return 0;
}
