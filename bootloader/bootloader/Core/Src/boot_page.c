#include "boot_page.h"
#include "boot_jump.h"
#include "boot_request.h"
#include "boot_uart.h"
#include "boot_ota.h"
#include "ota_install.h"
#include "ota_store.h"
#include "st7735s.h"
#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef boot_uart3;
static SPI_HandleTypeDef boot_spi1;
#define BOOT_RX_SIZE 4096U
static uint8_t rx_buffer[BOOT_RX_SIZE];
static volatile uint32_t rx_head, rx_tail;
static volatile bool rx_overflow;

/** @brief USART3中断持续收字节，Flash擦写及显示期间也保留接收数据。 @retval 无。 */
void USART3_IRQHandler(void)
{
    uint32_t errors=USART3->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE);
    if (errors) {
        USART3->ICR=USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
        rx_overflow=true;
    }
    while (USART3->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t byte=(uint8_t)USART3->RDR;
        uint32_t next=(rx_head+1U)%BOOT_RX_SIZE;
        if (next==rx_tail) rx_overflow=true;
        else { rx_buffer[rx_head]=byte; __DMB(); rx_head=next; }
    }
}

/** @brief 从单生产者环形缓冲读取字节到byte。 @retval true表示取得字节。 */
static bool receive_byte(uint8_t *byte)
{
    if (rx_head==rx_tail) return false;
    *byte=rx_buffer[rx_tail];
    __DMB();
    rx_tail=(rx_tail+1U)%BOOT_RX_SIZE;
    return true;
}

/**
  * @brief 配置与应用一致的屏幕GPIO及SPI1，并初始化裸机显示。
  * @retval true表示屏幕初始化成功。
  */
static bool display_init(void)
{
		// 初始化GPIOA B C和SPI1实例
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, Screen_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, Screen_BLK_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, Screen_RES_Pin | Screen_DC_Pin, GPIO_PIN_RESET);
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = Screen_CS_Pin | Screen_BLK_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = Screen_RES_Pin | Screen_DC_Pin;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);
    RCC_PeriphCLKInitTypeDef clocks = {0};
    clocks.PeriphClockSelection = RCC_PERIPHCLK_CKPER | RCC_PERIPHCLK_SPI1;
    clocks.CkperClockSelection = RCC_CLKPSOURCE_HSI;
    clocks.Spi123ClockSelection = RCC_SPI123CLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&clocks) != HAL_OK) return false;
    __HAL_RCC_SPI1_CLK_ENABLE();
    boot_spi1.Instance = SPI1;
    boot_spi1.Init.Mode = SPI_MODE_MASTER;
    boot_spi1.Init.Direction = SPI_DIRECTION_2LINES_TXONLY;
    boot_spi1.Init.DataSize = SPI_DATASIZE_8BIT;
    boot_spi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    boot_spi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    boot_spi1.Init.NSS = SPI_NSS_SOFT;
    boot_spi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
    boot_spi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    boot_spi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    boot_spi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    return HAL_SPI_Init(&boot_spi1) == HAL_OK && ST7735S_Init(&boot_spi1) == HAL_OK;
}

/**
  * @brief 配置PB10/PB11上的USART3为460800波特率中断接收。
  * @retval true表示串口可用。
  */
static bool uart_init(void)
{
    RCC_PeriphCLKInitTypeDef clocks = {0};
    clocks.PeriphClockSelection = RCC_PERIPHCLK_USART3;
    clocks.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&clocks) != HAL_OK) return false;
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOB, &gpio);
    boot_uart3.Instance = USART3;
    boot_uart3.Init.BaudRate = 460800;
    boot_uart3.Init.WordLength = UART_WORDLENGTH_8B;
    boot_uart3.Init.StopBits = UART_STOPBITS_1;
    boot_uart3.Init.Mode = UART_MODE_TX_RX;
    boot_uart3.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&boot_uart3)!=HAL_OK) return false;
    HAL_NVIC_SetPriority(USART3_IRQn,5,0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    __HAL_UART_ENABLE_IT(&boot_uart3,UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&boot_uart3,UART_IT_ERR);
    return true;
}

/**
  * @brief 通过少量5乘7字模绘制引导标题，避免引入GUI库或整屏缓冲。
  * @retval true表示标题与背景发送成功。
  */
static bool display_page(void)
{
    static const uint8_t letters[10][5] = {
        {0x7f,0x49,0x49,0x49,0x36}, /* B */
        {0x3e,0x41,0x41,0x41,0x3e}, /* O */
        {0x3e,0x41,0x41,0x41,0x3e}, /* O */
        {0x01,0x01,0x7f,0x01,0x01}, /* T */
        {0x7f,0x40,0x40,0x40,0x40}, /* L */
        {0x3e,0x41,0x41,0x41,0x3e}, /* O */
        {0x7e,0x09,0x09,0x09,0x7e}, /* A */
        {0x7f,0x41,0x41,0x22,0x1c}, /* D */
        {0x7f,0x49,0x49,0x49,0x41}, /* E */
        {0x7f,0x09,0x19,0x29,0x46}, /* R */
    };
    if (ST7735S_FillScreen(0x0841U) != HAL_OK) return false;
    for (uint32_t i = 0; i < 10; ++i)
        for (uint32_t x = 0; x < 5; ++x)
            for (uint32_t y = 0; y < 7; ++y)
                if ((letters[i][x] & (1U << y)) &&
                    ST7735S_FillRect(4U+i*12U+x*2U, 24U+y*2U, 2U, 2U,
                                     ST7735S_COLOR_WHITE) != HAL_OK) return false;
    return ST7735S_FillRect(8U, 48U, 112U, 2U, ST7735S_COLOR_CYAN) == HAL_OK;
}

typedef struct {
    char code;
    uint8_t columns[5];
} BootGlyph;

/* 状态和错误提示只保留所需的5x7字形，避免引入完整字体库。 */
static const BootGlyph boot_glyphs[] = {
    {' ',{0,0,0,0,0}}, {'0',{0x3E,0x51,0x49,0x45,0x3E}},
    {'%',{0x63,0x13,0x08,0x64,0x63}},
    {'1',{0,0x42,0x7F,0x40,0}}, {'2',{0x42,0x61,0x51,0x49,0x46}},
    {'3',{0x21,0x41,0x45,0x4B,0x31}}, {'4',{0x18,0x14,0x12,0x7F,0x10}},
    {'5',{0x27,0x45,0x45,0x45,0x39}}, {'6',{0x3C,0x4A,0x49,0x49,0x30}},
    {'7',{0x01,0x71,0x09,0x05,0x03}}, {'8',{0x36,0x49,0x49,0x49,0x36}},
    {'9',{0x06,0x49,0x49,0x29,0x1E}}, {'A',{0x7E,0x11,0x11,0x11,0x7E}},
    {'B',{0x7F,0x49,0x49,0x49,0x36}}, {'C',{0x3E,0x41,0x41,0x41,0x22}},
    {'D',{0x7F,0x41,0x41,0x22,0x1C}}, {'E',{0x7F,0x49,0x49,0x49,0x41}},
    {'F',{0x7F,0x09,0x09,0x09,0x01}}, {'G',{0x3E,0x41,0x49,0x49,0x7A}},
    {'H',{0x7F,0x08,0x08,0x08,0x7F}}, {'I',{0,0x41,0x7F,0x41,0}},
    {'K',{0x7F,0x08,0x14,0x22,0x41}},
    {'L',{0x7F,0x40,0x40,0x40,0x40}}, {'M',{0x7F,0x02,0x0C,0x02,0x7F}},
    {'N',{0x7F,0x04,0x08,0x10,0x7F}}, {'O',{0x3E,0x41,0x41,0x41,0x3E}},
    {'P',{0x7F,0x09,0x09,0x09,0x06}},
    {'R',{0x7F,0x09,0x19,0x29,0x46}}, {'S',{0x46,0x49,0x49,0x49,0x31}},
    {'T',{0x01,0x01,0x7F,0x01,0x01}}, {'U',{0x3F,0x40,0x40,0x40,0x3F}},
    {'V',{0x1F,0x20,0x40,0x20,0x1F}}, {'W',{0x3F,0x40,0x38,0x40,0x3F}},
    {'Y',{0x07,0x08,0x70,0x08,0x07}},
    {0,{0,0,0,0,0}}
};

/**
  * @brief 将恢复错误码映射为屏幕上的简短英文原因。
  * @param code OtaInstall_Recover返回的OTA错误码。
  * @retval 对应原因文本，未知错误返回OTHER。
  */
static const char *boot_error_reason(uint32_t code)
{
    switch (code) {
    case OTA_BAD_FRAME: return "FRAME";
    case OTA_BAD_STATE: return "STATE";
    case OTA_BAD_OFFSET: return "OFFSET";
    case OTA_FLASH_ERROR: return "FLASH";
    case OTA_CRC_ERROR: return "CRC";
    case OTA_BAD_IMAGE: return "IMAGE";
    case OTA_BUSY: return "BUSY";
    case OTA_RECOVERY_REQUIRED: return "RECOVERY";
    default: return "OTHER";
    }
}

/**
  * @brief 在屏幕底部绘制恢复错误码和原因，不覆盖OTA进度格。
  * @param code OtaInstall_Recover返回的OTA错误码。
  * @retval 无，屏幕写入失败时保留原有红色错误格。
  */
static void display_error(uint32_t code)
{
    char text[24];
    uint16_t x=8U;
    (void)snprintf(text,sizeof(text),"ERR %02lu %s",(unsigned long)code,boot_error_reason(code));
    if (ST7735S_FillRect(8U,120U,112U,12U,0x7800U)!=HAL_OK) return;
    for (const char *p=text; *p && x<=115U; ++p, x+=6U) {
        const BootGlyph *glyph=boot_glyphs;
        while (glyph->code && glyph->code!=*p) ++glyph;
        for (uint16_t col=0; col<5U; ++col)
            for (uint16_t row=0; row<7U; ++row)
                if (glyph->columns[col] & (1U<<row))
                    (void)ST7735S_DrawPixel(x+col,122U+row,ST7735S_COLOR_WHITE);
    }
}

/**
  * @brief 清除进度条下方的旧状态，并绘制新的英文阶段文字。
  * @param text 要显示的零结尾英文字符串。
  * @param color 文字的RGB565颜色。
  * @retval 无。
  */
void BootPage_DrawStatus(const char *text, uint16_t color)
{
    uint16_t x=8U;
    if (ST7735S_FillRect(8U,94U,112U,12U,0x0841U)!=HAL_OK) return;
    for (const char *p=text; *p && x<=115U; ++p, x+=6U) {
        const BootGlyph *glyph=boot_glyphs;
        while (glyph->code && glyph->code!=*p) ++glyph;
        for (uint16_t col=0; col<5U; ++col)
            for (uint16_t row=0; row<7U; ++row)
                if (glyph->columns[col] & (1U<<row))
                    (void)ST7735S_DrawPixel(x+col,96U+row,color);
    }
}

/**
  * @brief 发送以换行结束的状态文本，text为零结尾字符串。
  * @retval 无，发送失败会在下一次心跳重试。
  */
static void send_line(const char *text)
{
    (void)HAL_UART_Transmit(&boot_uart3, (const uint8_t *)text, (uint16_t)strlen(text), 100U);
}

void BootPage_Run(void)
{
    bool requested = BootRequest_Take();
    bool uart_ok = uart_init();
    bool screen_ok = display_init() && display_page();
    if (screen_ok) BootPage_DrawStatus("WAITING",ST7735S_COLOR_CYAN);
    (void)BootOta_Init();
    /* 必须先恢复未完成安装，再读取应用向量，避免访问断电留下的Flash字。 */
    uint32_t recovery=OtaInstall_Recover();
    bool app_valid = recovery==OTA_OK && BootJump_IsApplicationValid(APP_FLASH_BASE,APP_FLASH_END);
    if (recovery==OTA_OK && !requested && app_valid && OtaInstall_BeforeBoot())
        BootJump_ToApplication(APP_FLASH_BASE);
    if (recovery!=OTA_OK) {
        OtaFlash_Progress(0,0,5);
        if (screen_ok) display_error(recovery);
    }
    uint32_t last_status = HAL_GetTick() - 1000U;
    char line[24];
    uint32_t used = 0;
    bool overflow = false;
    for (;;) {
        /* 保留原USART1字节数测试和启动命令，页面模式不启用RTOS。 */
        BootOta_Poll();
        if (!BootOta_Busy()) (void)BootUart_Process();
        if (uart_ok && (uint32_t)(HAL_GetTick() - last_status) >= 1000U) {
            send_line(screen_ok ? "BOOT_READY\r\n" : "BOOT_DISPLAY_ERROR\r\n");
            last_status = HAL_GetTick();
        }
        uint8_t byte;
        if (rx_overflow) {
            uint32_t mask=__get_PRIMASK();
            __disable_irq();
            rx_tail=rx_head;
            rx_overflow=false;
            __set_PRIMASK(mask);
            BootOta_ResetReceiver();
            used=0; overflow=false;
        }
        if (uart_ok && receive_byte(&byte)) {
            if (BootOta_Byte(&boot_uart3, byte)) { used=0; overflow=false; continue; }
            if (byte == '\n') {
                if (used && line[used-1] == '\r') --used;
                line[used] = '\0';
                /* 只有完整应用才能启动，故障恢复必须重新安装或重新下载。 */
                if (!overflow && strcmp(line, "App")==0) {
                    if (!BootOta_Busy() && app_valid) {
                        send_line("APP_STARTING\r\n");
                        NVIC_SystemReset();
                    } else send_line("BOOT_REJECTED\r\n");
                }
                used = 0;
                overflow = false;
            } else if (used < sizeof(line)-1U && byte != 0) line[used++] = (char)byte;
            else overflow = true;
        }
    }
}
