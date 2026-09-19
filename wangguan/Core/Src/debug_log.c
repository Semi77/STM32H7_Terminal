#include "debug_log.h"

#if DEBUG_LOG_ENABLE

/* ITM需要TRCENA；未连接调试器时端口会长期为忙，因此所有写操作都有次数上限。 */
#define DBG_ITM_PORT   0U
#define DBG_WRITE_LIMIT 2000000U

static bool dbg_ready;

/**
  * @brief 首次使用时打开跟踪与ITM，之后直接复用已有使能状态。
  * @retval 无。
  */
static void dbg_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    ITM->LAR = 0xC5ACCE55U;
    ITM->TER |= (1UL << DBG_ITM_PORT);
    ITM->TCR = ITM_TCR_ITMENA_Msk | ITM_TCR_SYNCENA_Msk |
               (1UL << ITM_TCR_TraceBusID_Pos);
    dbg_ready = true;
}

/**
  * @brief 读取ITM端口0状态。CMSIS把PORT声明为只写，硬件实际可读，故用独立指针访问。
  * @retval 非零表示FIFO可写入一个字节。
  */
static uint32_t dbg_port_ready(void)
{
    volatile uint32_t *port = (volatile uint32_t *)&ITM->PORT[DBG_ITM_PORT];
    return *port;
}

/**
  * @brief 输出一个字符，等待FIFO就绪期间带超时避免阻塞业务任务。
  * @param character 待发送字节。
  * @retval 无。
  */
static void dbg_char(char character)
{
    uint32_t guard = DBG_WRITE_LIMIT;
    if (!dbg_ready) dbg_init();
    while (dbg_port_ready() == 0U && guard--) {}
    if (guard == 0U) return;
    ITM->PORT[DBG_ITM_PORT].u8 = (uint8_t)character;
}

/**
  * @brief 输出以零结尾字符串。
  * @param text 待发送字符串。
  * @retval 无。
  */
static void dbg_text(const char *text)
{
    if (!text) return;
    while (*text) dbg_char(*text++);
}

/**
  * @brief 以十进制输出无符号数，不支持负数。
  * @param value 待输出数值。
  * @retval 无。
  */
static void dbg_u32(uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value && count < sizeof(digits));
    while (count) dbg_char(digits[--count]);
}

/**
  * @brief 以十进制输出有符号数。
  * @param value 待输出数值。
  * @retval 无。
  */
static void dbg_i32(int32_t value)
{
    if (value < 0) {
        dbg_char('-');
        dbg_u32((uint32_t)(-value));
    } else dbg_u32((uint32_t)value);
}

/**
  * @brief 输出一个两位小数位的定点数，用于0.1单位原始值。
  * @param value 定点数值。
  * @retval 无。
  */
static void dbg_fixed1(int32_t value)
{
    bool negative = value < 0;
    uint32_t magnitude = (uint32_t)(negative ? -value : value);
    if (negative) dbg_char('-');
    dbg_u32(magnitude / 10U);
    dbg_char('.');
    dbg_char((char)('0' + (magnitude % 10U)));
}

void DebugLog_Modbus(bool have_snapshot, ModbusStatus status,
                     int16_t temperature_x10, uint16_t humidity_x10, bool valid,
                     uint32_t success_count, uint32_t error_count)
{
    dbg_text("RS485 ");
    if (!have_snapshot) {
        dbg_text("no-snapshot");
    } else {
        dbg_text("T=");
        dbg_fixed1(temperature_x10);
        dbg_text("C H=");
        dbg_fixed1((int32_t)humidity_x10);
        dbg_char('%');
        dbg_text(" raw=");
        dbg_i32(temperature_x10);
        dbg_char('/');
        dbg_u32(humidity_x10);
        dbg_text(" status=");
        dbg_u32((uint32_t)status);
        dbg_text(valid ? " valid" : " invalid");
    }
    dbg_text(" ok=");
    dbg_u32(success_count);
    dbg_text(" err=");
    dbg_u32(error_count);
    dbg_text("\r\n");
}

void DebugLog_Line(const char *text)
{
    dbg_text(text);
    dbg_text("\r\n");
}

#else

/* 关闭日志时保留空的强符号定义，避免调用方产生未定义引用。 */
void DebugLog_Modbus(bool have_snapshot, ModbusStatus status,
                     int16_t temperature_x10, uint16_t humidity_x10, bool valid,
                     uint32_t success_count, uint32_t error_count)
{
    (void)have_snapshot; (void)status; (void)temperature_x10;
    (void)humidity_x10; (void)valid; (void)success_count; (void)error_count;
}

void DebugLog_Line(const char *text)
{
    (void)text;
}

#endif
