/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   外部W25Q64整片擦除工具，通过ESP32-C3桥接上报进度到上位机。
  *
  * 用途：清除W25Q64上的全部内容（OTA下载槽、两份参数记录、恢复保护标记、
  *       离线缓存等），把网关恢复到"从未做过OTA"的干净状态。
  *
  * 上报方式：走USART3（PB10/PB11，460800）发给ESP32-C3。ESP32解析
  *           "SELFTEST:"协议并存入自检报告，上位机轮询status时在
  *           selftest字段返回，由format_selftest显示为中文日志。
  *           因此本工具不需要修改ESP32固件。
  *
  * 用法：烧录运行一次，在上位机打开"自动刷新"观察进度，看到
  *       FLASH IS BLANK后断电，再重新烧录Bootloader和主程序。
  *
  * 注意：本程序链接在0x08000000，会覆盖Bootloader，完成后必须重新烧录。
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "w25q64_qspi.h"
#include "ili9341.h"
#include "selftest_wire.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* W25Q64的QSPI引脚，与Bootloader及主程序保持一致。 */
#define CLEAN_QSPI_CLK_PIN     GPIO_PIN_2
#define CLEAN_QSPI_CLK_PORT    GPIOB
#define CLEAN_QSPI_NCS_PIN     GPIO_PIN_6
#define CLEAN_QSPI_NCS_PORT    GPIOB
#define CLEAN_QSPI_IO0_PIN     GPIO_PIN_11
#define CLEAN_QSPI_IO1_PIN     GPIO_PIN_12
#define CLEAN_QSPI_IO2_PIN     GPIO_PIN_13
#define CLEAN_QSPI_IO_PORT     GPIOD
#define CLEAN_QSPI_IO3_PIN     GPIO_PIN_2
#define CLEAN_QSPI_IO3_PORT    GPIOE

/* ESP32-C3桥接串口：USART3，PB10=TX、PB11=RX，与主程序一致。 */
#define CLEAN_LINK_TX_PIN      GPIO_PIN_10
#define CLEAN_LINK_RX_PIN      GPIO_PIN_11
#define CLEAN_LINK_PORT        GPIOB
#define CLEAN_LINK_BAUD        460800U

/* 上报记录条数：1条开始 + 10条进度 + 3条结束。 */
#define CLEAN_REPORT_TOTAL     14U
/* 借用已有"通过"语义的code，使上位机同时显示字节数与耗时。 */
#define CLEAN_CODE_OK          0U

/* 屏幕上的进度条与文字位置。 */
#define CLEAN_BAR_X            20U
#define CLEAN_BAR_Y            150U
#define CLEAN_BAR_W            280U
#define CLEAN_BAR_H            26U
#define CLEAN_STATUS_Y         96U
#define CLEAN_BANNER_Y         190U

/* 屏幕配色。 */
#define CLEAN_BG_COLOR         0x0000U
#define CLEAN_TITLE_COLOR      0xFFFFU
#define CLEAN_STATUS_COLOR     0xFFE0U
#define CLEAN_TRACK_COLOR      0x39E7U
#define CLEAN_FILL_COLOR       0x07FFU
#define CLEAN_OK_COLOR         0x07E0U
#define CLEAN_FAIL_COLOR       0xF800U
#define CLEAN_IDLE_COLOR       0x7BEFU

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

QSPI_HandleTypeDef hqspi;

UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
static QSPI_HandleTypeDef clean_qspi;
static UART_HandleTypeDef clean_link;
static SPI_HandleTypeDef clean_spi;
static uint32_t clean_run_id;
static uint32_t clean_report_index;
static char clean_line[SELFTEST_LINE_SIZE];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_QSPI_Init(void);
static void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */
static void Clean_InitPeripheralClocks(void);
static void MX_SPI1_Init(void);
static void Clean_Report(const char *message, uint32_t done, uint32_t total,
                         uint32_t elapsed_ms);
static HAL_StatusTypeDef Clean_EraseEntireFlash(uint32_t *out_elapsed_ms);
static HAL_StatusTypeDef Clean_VerifyBlank(void);
static void Screen_Text(uint16_t x, uint16_t y, const char *text,
                        uint16_t color, uint16_t background, uint8_t scale);
static void Screen_Status(const char *text, uint16_t color);
static void Screen_Banner(const char *text, uint16_t color);
static void Screen_Progress(uint32_t percent);
static void Screen_Init(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* 5x7点阵字库，每字符5列、每列7位（bit0在上）。只需本工具用到的字符。 */
typedef struct {
  char code;
  uint8_t column[5];
} Clean_Glyph;

static const Clean_Glyph clean_glyphs[] = {
  {' ', {0x00,0x00,0x00,0x00,0x00}},
  {'%', {0x62,0x64,0x08,0x13,0x23}},
  {'-', {0x08,0x08,0x08,0x08,0x08}},
  {'.', {0x00,0x60,0x60,0x00,0x00}},
  {'0', {0x3E,0x51,0x49,0x45,0x3E}},
  {'1', {0x00,0x42,0x7F,0x40,0x00}},
  {'2', {0x42,0x61,0x51,0x49,0x46}},
  {'3', {0x21,0x41,0x45,0x4B,0x31}},
  {'4', {0x18,0x14,0x12,0x7F,0x10}},
  {'5', {0x27,0x45,0x45,0x45,0x39}},
  {'6', {0x3C,0x4A,0x49,0x49,0x30}},
  {'7', {0x01,0x71,0x09,0x05,0x03}},
  {'8', {0x36,0x49,0x49,0x49,0x36}},
  {'9', {0x06,0x49,0x49,0x29,0x1E}},
  {'A', {0x7E,0x11,0x11,0x11,0x7E}},
  {'B', {0x7F,0x49,0x49,0x49,0x36}},
  {'C', {0x3E,0x41,0x41,0x41,0x22}},
  {'D', {0x7F,0x41,0x41,0x22,0x1C}},
  {'E', {0x7F,0x49,0x49,0x49,0x41}},
  {'F', {0x7F,0x09,0x09,0x09,0x01}},
  {'G', {0x3E,0x41,0x49,0x49,0x7A}},
  {'I', {0x00,0x41,0x7F,0x41,0x00}},
  {'K', {0x7F,0x08,0x14,0x22,0x41}},
  {'L', {0x7F,0x40,0x40,0x40,0x40}},
  {'M', {0x7F,0x02,0x0C,0x02,0x7F}},
  {'N', {0x7F,0x04,0x08,0x10,0x7F}},
  {'O', {0x3E,0x41,0x41,0x41,0x3E}},
  {'P', {0x7F,0x09,0x09,0x09,0x06}},
  {'R', {0x7F,0x09,0x19,0x29,0x46}},
  {'S', {0x46,0x49,0x49,0x49,0x31}},
  {'T', {0x01,0x01,0x7F,0x01,0x01}},
  {'U', {0x3F,0x40,0x40,0x40,0x3F}},
  {'V', {0x1F,0x20,0x40,0x20,0x1F}},
  {'W', {0x3F,0x40,0x38,0x40,0x3F}},
  {'X', {0x63,0x14,0x08,0x14,0x63}},
  {'Y', {0x07,0x08,0x70,0x08,0x07}},
  {0, {0,0,0,0,0}}
};

/**
  * @brief 用一个点阵字符在屏幕上绘制文本，超出字库的字符按空格处理。
  * @param x 起始横坐标；y 起始纵坐标。
  * @param text 以零结尾的ASCII字符串。
  * @param color 文字颜色；background 背景色。
  * @param scale 放大倍数，1表示5x7像素，2表示10x14像素。
  * @retval 无。
  */
static void Screen_Text(uint16_t x, uint16_t y, const char *text,
                        uint16_t color, uint16_t background, uint8_t scale)
{
  uint16_t cursor = x;
  if (!text || scale == 0U) return;
  for (const char *p = text; *p; ++p)
  {
    char upper = (*p >= 'a' && *p <= 'z') ? (char)(*p - 'a' + 'A') : *p;
    const Clean_Glyph *glyph = NULL;
    for (uint32_t i = 0U; clean_glyphs[i].code; ++i)
    {
      if (clean_glyphs[i].code == upper) { glyph = &clean_glyphs[i]; break; }
    }
    for (uint16_t col = 0U; col < 5U; ++col)
    {
      uint8_t bits = glyph ? glyph->column[col] : 0U;
      for (uint16_t row = 0U; row < 7U; ++row)
      {
        uint16_t pixel_color = (bits & (1U << row)) ? color : background;
        (void)ILI9341_FillRect((uint16_t)(cursor + col * scale),
                               (uint16_t)(y + row * scale),
                               scale, scale, pixel_color);
      }
    }
    cursor = (uint16_t)(cursor + 6U * scale);
  }
}

/**
  * @brief 刷新中部状态文本，先擦除整行再绘制。
  * @param text 以零结尾的显示文本。
  * @param color 文字颜色。
  * @retval 无。
  */
static void Screen_Status(const char *text, uint16_t color)
{
  (void)ILI9341_FillRect(CLEAN_BAR_X, CLEAN_STATUS_Y, CLEAN_BAR_W, 16U,
                         CLEAN_BG_COLOR);
  Screen_Text(CLEAN_BAR_X, CLEAN_STATUS_Y, text, color, CLEAN_BG_COLOR, 2U);
}

/**
  * @brief 刷新底部结果横幅。
  * @param text 以零结尾的显示文本。
  * @param color 背景颜色。
  * @retval 无。
  */
static void Screen_Banner(const char *text, uint16_t color)
{
  (void)ILI9341_FillRect(CLEAN_BAR_X, CLEAN_BANNER_Y, CLEAN_BAR_W, 30U, color);
  Screen_Text(CLEAN_BAR_X + 8U, CLEAN_BANNER_Y + 8U, text,
              ILI9341_COLOR_BLACK, color, 2U);
}

/**
  * @brief 按百分比重画进度条，只在百分比变化时由调用方触发。
  * @param percent 0至100。
  * @retval 无。
  */
static void Screen_Progress(uint32_t percent)
{
  uint32_t filled;
  if (percent > 100U) percent = 100U;
  filled = (CLEAN_BAR_W * percent) / 100U;
  (void)ILI9341_FillRect(CLEAN_BAR_X, CLEAN_BAR_Y, CLEAN_BAR_W, CLEAN_BAR_H,
                         CLEAN_TRACK_COLOR);
  if (filled) (void)ILI9341_FillRect(CLEAN_BAR_X, CLEAN_BAR_Y,
                                     (uint16_t)filled, CLEAN_BAR_H,
                                     CLEAN_FILL_COLOR);
}

/**
  * @brief 画出静止界面元素：标题、进度槽和初始横幅。
  * @retval 无。
  */
static void Screen_Init(void)
{
  (void)ILI9341_FillScreen(CLEAN_BG_COLOR);
  Screen_Text(20U, 20U, "W25Q64 ERASE", CLEAN_TITLE_COLOR, CLEAN_BG_COLOR, 3U);
  Screen_Text(20U, 56U, "CLEAN ALL 8MiB", CLEAN_IDLE_COLOR, CLEAN_BG_COLOR, 2U);
  Screen_Progress(0U);
  Screen_Status("STARTING", CLEAN_STATUS_COLOR);
  Screen_Banner("WORKING", CLEAN_TRACK_COLOR);
}

/**
  * @brief 按自检协议上报一条记录，ESP32解析后存入自检报告。
  * @param message 显示在上位机日志中的提示文本。
  * @param done 已完成字节数，用于显示进度。
  * @param total 总字节数。
  * @param elapsed_ms 本条对应的耗时，单位毫秒。
  * @retval 无，发送失败不重试。
  */
static void Clean_Report(const char *message, uint32_t done, uint32_t total,
                         uint32_t elapsed_ms)
{
  SelfTestRecord record;
  uint32_t crc = 0U;
  size_t length;

  if (clean_report_index >= CLEAN_REPORT_TOTAL) return;
  memset(&record, 0, sizeof(record));
  record.run = clean_run_id;
  record.index = clean_report_index;
  record.total = CLEAN_REPORT_TOTAL;
  record.code = CLEAN_CODE_OK;
  record.size = total;
  record.read = done;
  record.elapsed = elapsed_ms;
  /* 名称字段承载提示文本，协议要求非空且以零结尾。 */
  (void)snprintf(record.name, sizeof(record.name), "%s",
                 (message && message[0]) ? message : "-");

  length = SelfTest_Encode(&record, clean_line, &crc);
  if (length > 0U)
    (void)HAL_UART_Transmit(&clean_link, (const uint8_t *)clean_line,
                            (uint16_t)length, 500U);
  ++clean_report_index;
}

/**
  * @brief 逐扇区擦除整片W25Q64，每10%上报一次进度。
  * @param out_elapsed_ms 返回本次擦除总耗时。
  * @retval HAL_OK表示所有扇区擦除成功。
  */
static HAL_StatusTypeDef Clean_EraseEntireFlash(uint32_t *out_elapsed_ms)
{
  const uint32_t total = W25Q64_FLASH_SIZE_BYTES;
  uint32_t next_report = 10U;
  uint32_t last_shown = 0xFFFFFFFFU;
  uint32_t started = HAL_GetTick();
  char text[32];

  for (uint32_t address = 0U; address < total; address += W25Q64_SECTOR_SIZE_BYTES)
  {
    if (W25Q64_SectorErase(address) != HAL_OK)
    {
      if (out_elapsed_ms) *out_elapsed_ms = HAL_GetTick() - started;
      return HAL_ERROR;
    }
    uint32_t percent = ((address + W25Q64_SECTOR_SIZE_BYTES) * 100U) / total;
    /* 屏幕每个百分点都刷新，视觉上连续；串口只每10%上报一次。 */
    if (percent != last_shown)
    {
      last_shown = percent;
      Screen_Progress(percent);
      (void)snprintf(text, sizeof(text), "ERASE %lu%%", (unsigned long)percent);
      Screen_Status(text, CLEAN_STATUS_COLOR);
    }
    if (percent >= next_report)
    {
      (void)snprintf(text, sizeof(text), "ERASE %lu%%", (unsigned long)percent);
      Clean_Report(text, address + W25Q64_SECTOR_SIZE_BYTES, total,
                   HAL_GetTick() - started);
      next_report += 10U;
    }
  }
  if (out_elapsed_ms) *out_elapsed_ms = HAL_GetTick() - started;
  return HAL_OK;
}

/**
  * @brief 抽样读回整个地址空间，确认全部为0xFF。
  * @retval HAL_OK表示抽样点均为空；HAL_ERROR表示仍有非空数据。
  */
static HAL_StatusTypeDef Clean_VerifyBlank(void)
{
  const uint32_t total = W25Q64_FLASH_SIZE_BYTES;
  const uint32_t samples = 64U;
  uint8_t scratch[32];

  for (uint32_t i = 0U; i < samples; ++i)
  {
    uint32_t address = (total / samples) * i;
    if (W25Q64_ReadData(address, scratch, sizeof(scratch)) != HAL_OK) return HAL_ERROR;
    for (uint32_t j = 0U; j < sizeof(scratch); ++j)
    {
      if (scratch[j] != 0xFFU) return HAL_ERROR;
    }
  }
  return HAL_OK;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  /* 必须先选定USART3/QSPI内核时钟，再初始化外设：
     HAL_UART_Init按当前时钟算波特率分频，顺序颠倒会算错导致串口无输出。 */
  Clean_InitPeripheralClocks();
  MX_GPIO_Init();
  MX_USART3_UART_Init();
  MX_SPI1_Init();
  MX_QSPI_Init();
  /* USER CODE BEGIN 2 */
  clean_link = huart3;
  clean_report_index = 0U;
  /* 用UID与tick拼运行号，上位机据此区分是否为新一次运行。 */
  clean_run_id = HAL_GetUIDw0() ^ HAL_GetTick();

  /* 屏幕是主要反馈渠道，串口上报作为可选备份。 */
  if (ILI9341_Init(&clean_spi) == HAL_OK)
  {
    Screen_Init();
  }

  Clean_Report("W25Q64 FULL ERASE START", 0U, W25Q64_FLASH_SIZE_BYTES, 0U);

  uint8_t manufacturer = 0U;
  uint16_t device = 0U;
  if (W25Q64_Init(&clean_qspi) != HAL_OK ||
      W25Q64_ReadID(&manufacturer, &device) != HAL_OK)
  {
    /* 认不到芯片必须停手，避免把别的器件当成Flash来擦。 */
    Screen_Status("W25Q64 NOT FOUND", CLEAN_FAIL_COLOR);
    Screen_Banner("CHECK WIRING", CLEAN_FAIL_COLOR);
    Clean_Report("W25Q64 INIT FAILED - CHECK WIRING", 0U,
                 W25Q64_FLASH_SIZE_BYTES, 0U);
    Error_Handler();
  }
  {
    char id_text[48];
    (void)snprintf(id_text, sizeof(id_text), "ID %02X %04X  8MiB READY",
                   (unsigned)manufacturer, (unsigned)device);
    Screen_Status(id_text, CLEAN_IDLE_COLOR);
    (void)snprintf(id_text, sizeof(id_text), "ID 0x%02X/0x%04X, erasing 8MiB",
                   (unsigned)manufacturer, (unsigned)device);
    Clean_Report(id_text, 0U, W25Q64_FLASH_SIZE_BYTES, 0U);
  }

  uint32_t erase_ms = 0U;
  Screen_Banner("ERASING", CLEAN_STATUS_COLOR);
  Clean_Report("ERASE 0%", 0U, W25Q64_FLASH_SIZE_BYTES, 0U);
  if (Clean_EraseEntireFlash(&erase_ms) != HAL_OK)
  {
    Screen_Status("ERASE FAILED", CLEAN_FAIL_COLOR);
    Screen_Banner("ERASE FAILED", CLEAN_FAIL_COLOR);
    Clean_Report("ERASE FAILED", 0U, W25Q64_FLASH_SIZE_BYTES, erase_ms);
    Error_Handler();
  }
  Screen_Progress(100U);
  Screen_Status("ERASED, VERIFYING", CLEAN_STATUS_COLOR);
  Clean_Report("ALL SECTORS ERASED", W25Q64_FLASH_SIZE_BYTES,
               W25Q64_FLASH_SIZE_BYTES, erase_ms);

  if (Clean_VerifyBlank() != HAL_OK)
  {
    Screen_Status("VERIFY FAILED", CLEAN_FAIL_COLOR);
    Screen_Banner("VERIFY FAILED", CLEAN_FAIL_COLOR);
    Clean_Report("VERIFY FAILED", W25Q64_FLASH_SIZE_BYTES,
                 W25Q64_FLASH_SIZE_BYTES, erase_ms);
    Error_Handler();
  }
  {
    char done_text[48];
    (void)snprintf(done_text, sizeof(done_text), "BLANK OK  %lu ms",
                   (unsigned long)erase_ms);
    Screen_Status(done_text, CLEAN_OK_COLOR);
  }
  Screen_Banner("DONE - POWER OFF", CLEAN_OK_COLOR);
  Clean_Report("VERIFY OK - FLASH IS BLANK", W25Q64_FLASH_SIZE_BYTES,
               W25Q64_FLASH_SIZE_BYTES, erase_ms);
  Clean_Report("POWER OFF, THEN FLASH BOOTLOADER", W25Q64_FLASH_SIZE_BYTES,
               W25Q64_FLASH_SIZE_BYTES, erase_ms);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 保持运行，便于用调试器复查寄存器或重新上电重做。 */
    HAL_Delay(1000U);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART3 Initialization Function
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{
  clean_link.Instance = USART3;
  clean_link.Init.BaudRate = CLEAN_LINK_BAUD;
  clean_link.Init.WordLength = UART_WORDLENGTH_8B;
  clean_link.Init.StopBits = UART_STOPBITS_1;
  clean_link.Init.Parity = UART_PARITY_NONE;
  clean_link.Init.Mode = UART_MODE_TX_RX;
  clean_link.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  clean_link.Init.OverSampling = UART_OVERSAMPLING_16;
  clean_link.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  clean_link.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  clean_link.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&clean_link) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief 选定显示屏SPI1、USART3与QSPI的内核时钟源，必须在三者初始化之前调用。
  * @retval None
  */
static void Clean_InitPeripheralClocks(void)
{
  RCC_PeriphCLKInitTypeDef clocks = {0};

  /* CKPER选HSI并作为SPI1的内核时钟：SPI123复位默认源是PLL1Q，
     而本工程只开HSI不开PLL，不显式选择会让SPI1内核停摆、屏幕无反应。 */
  clocks.PeriphClockSelection = RCC_PERIPHCLK_CKPER | RCC_PERIPHCLK_SPI1 |
                                RCC_PERIPHCLK_USART3 | RCC_PERIPHCLK_QSPI;
  clocks.CkperClockSelection = RCC_CLKPSOURCE_HSI;
  clocks.Spi123ClockSelection = RCC_SPI123CLKSOURCE_CLKP;
  clocks.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
  clocks.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
  if (HAL_RCCEx_PeriphCLKConfig(&clocks) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief QUADSPI Initialization Function，按Bootloader已验证的参数配置。
  * @retval None
  */
static void MX_QSPI_Init(void)
{
  __HAL_RCC_QSPI_CLK_ENABLE();
  __HAL_RCC_QSPI_FORCE_RESET();
  __HAL_RCC_QSPI_RELEASE_RESET();

  clean_qspi.Instance = QUADSPI;
  clean_qspi.Init.ClockPrescaler = 11;
  clean_qspi.Init.FifoThreshold = 4;
  clean_qspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_NONE;
  clean_qspi.Init.FlashSize = 22;
  clean_qspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
  clean_qspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
  clean_qspi.Init.FlashID = QSPI_FLASH_ID_1;
  clean_qspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
  if (HAL_QSPI_Init(&clean_qspi) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function，仅发送、用于驱动ILI9341。
  * @retval None
  */
static void MX_SPI1_Init(void)
{
  clean_spi.Instance = SPI1;
  clean_spi.Init.Mode = SPI_MODE_MASTER;
  clean_spi.Init.Direction = SPI_DIRECTION_2LINES_TXONLY;
  clean_spi.Init.DataSize = SPI_DATASIZE_8BIT;
  clean_spi.Init.CLKPolarity = SPI_POLARITY_HIGH;
  clean_spi.Init.CLKPhase = SPI_PHASE_2EDGE;
  clean_spi.Init.NSS = SPI_NSS_SOFT;
  clean_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  clean_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
  clean_spi.Init.TIMode = SPI_TIMODE_DISABLE;
  clean_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  clean_spi.Init.CRCPolynomial = 0x0;
  clean_spi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  clean_spi.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  clean_spi.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  clean_spi.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  clean_spi.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  clean_spi.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  clean_spi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  clean_spi.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  clean_spi.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
  clean_spi.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&clean_spi) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  /* 显示屏控制脚先置为无效电平，避免初始化瞬间误动作。 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);    /* CS */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);  /* BLK */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4|GPIO_PIN_5, GPIO_PIN_RESET); /* RES/DC */

  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* SPI1: PA5=SCK, PA7=MOSI */
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
  GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* QUADSPI: PB2=CLK, PB6=NCS, PD11..13=IO0..2, PE2=IO3 */
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;

  GPIO_InitStruct.Pin = CLEAN_QSPI_CLK_PIN;
  HAL_GPIO_Init(CLEAN_QSPI_CLK_PORT, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = CLEAN_QSPI_IO0_PIN|CLEAN_QSPI_IO1_PIN|CLEAN_QSPI_IO2_PIN;
  HAL_GPIO_Init(CLEAN_QSPI_IO_PORT, &GPIO_InitStruct);

  /* NCS与IO3使用AF10，与IO0..2的AF9不同。 */
  GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;
  GPIO_InitStruct.Pin = CLEAN_QSPI_NCS_PIN;
  HAL_GPIO_Init(CLEAN_QSPI_NCS_PORT, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = CLEAN_QSPI_IO3_PIN;
  HAL_GPIO_Init(CLEAN_QSPI_IO3_PORT, &GPIO_InitStruct);

  /* USART3: PB10=TX, PB11=RX */
  GPIO_InitStruct.Pin = CLEAN_LINK_TX_PIN|CLEAN_LINK_RX_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
  HAL_GPIO_Init(CLEAN_LINK_PORT, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* 停机但仍然让看门狗不参与：本工具没有使能IWDG。 */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
