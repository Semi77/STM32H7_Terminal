#include "lvgl_port.h"

#include "app_ui.h"
#include "cmsis_os2.h"
#include "modbus_sensor.h"
#include "usart3_test.h"
#include "ili9341.h"
#include "cpu_stats.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define LVGL_DRAW_BUFFER_LINES 40U
#define LVGL_TASK_MAX_DELAY_MS 10U
/* 资源占用每秒重算一次，避免在图形循环里反复遍历任务列表。 */
#define LVGL_RESOURCE_INTERVAL_MS 1000U
#define LVGL_RESOURCE_MAX_TASKS 16U

static uint8_t s_lvgl_draw_buffer[ILI9341_WIDTH * LVGL_DRAW_BUFFER_LINES * 2U]
    __attribute__((aligned(32)));

/**
  * @brief 统计所有任务的CPU占用比例和堆使用比例并刷新右下角显示。
  * @retval 无，必须由图形任务调用。
  */
static void LVGL_UpdateResource(void)
{
  static TaskStatus_t s_tasks[LVGL_RESOURCE_MAX_TASKS];
  static uint32_t s_next_tick_ms;
  uint32_t now = HAL_GetTick();

  if ((int32_t)(now - s_next_tick_ms) < 0)
  {
    return;
  }
  s_next_tick_ms = now + LVGL_RESOURCE_INTERVAL_MS;

  /* 该函数内部已用vTaskSuspendAll保护任务列表，可直接从任务上下文调用。 */
  configRUN_TIME_COUNTER_TYPE elapsed = 0;
  UBaseType_t count = uxTaskGetSystemState(s_tasks, LVGL_RESOURCE_MAX_TASKS, &elapsed);

  /* 对累计快照取差分，显示最近一秒负载；无效快照保留上次结果。 */
  static CpuStatsTask resource_tasks[LVGL_RESOURCE_MAX_TASKS];
  for (UBaseType_t i = 0U; i < count; ++i)
  {
    strncpy(resource_tasks[i].name, s_tasks[i].pcTaskName, CPU_STATS_NAME_MAX - 1U);
    resource_tasks[i].name[CPU_STATS_NAME_MAX - 1U] = '\0';
    resource_tasks[i].run_time = s_tasks[i].ulRunTimeCounter;
  }

  static uint32_t cpu_percent;
  static CpuStatsSample sample;
  (void)CpuStats_Update(&sample, resource_tasks, (uint32_t)count, elapsed, &cpu_percent);

  /* configTOTAL_HEAP_SIZE在本工程被重定义为64KiB，动态换算避免硬编码。 */
  size_t total_heap = configTOTAL_HEAP_SIZE;
  size_t free_heap = xPortGetFreeHeapSize();
  uint32_t heap_percent = 0U;
  if (total_heap > 0U && free_heap <= total_heap)
  {
    heap_percent = (uint32_t)(((total_heap - free_heap) * 100U) / total_heap);
  }

  app_ui_update_resource(cpu_percent, heap_percent);
}

/**
  * @brief 把Modbus状态码压成标题栏可容纳的短标签。
  * @param status 最近一次采集状态。
  * @retval 常量字符串，不需要释放。
  */
static const char *LVGL_ModbusStatusText(ModbusStatus status)
{
  switch (status)
  {
  case MODBUS_OK:             return "OK";
  case MODBUS_TIMEOUT:        return "TMO";
  case MODBUS_UART_ERROR:     return "UART";
  case MODBUS_FRAME_ERROR:    return "FRM";
  case MODBUS_CRC_ERROR:      return "CRC";
  case MODBUS_EXCEPTION:      return "EXC";
  case MODBUS_BUSY:           return "BUSY";
  case MODBUS_ARGUMENT_ERROR: return "ARG";
  default:                    return "ERR";
  }
}

/**
  * @brief 温湿度取RS485真实采样，光照与上传状态仍取网关模拟快照。
  * @retval 无，所有LVGL操作保持在图形任务。
  */
static void LVGL_UpdateSensorCards(void)
{
  /* 只在内容变化时调用，避免每10毫秒进一次临界区并重设标签文本。 */
  static int16_t last_temperature;
  static uint16_t last_humidity;
  static bool last_valid;
  static bool last_has_data;
  static uint32_t last_sequence;
  static bool last_online;
  static bool displayed;
  static bool last_has_snapshot;
  static uint32_t last_success_count;
  static uint32_t last_error_count;

  bool online = Usart3Test_IsOnline();
  if (online != last_online) {
    app_ui_update_network(online);
    last_online = online;
  }

  /* 站号1是唯一的温湿度来源，失败时保留上次数值并置为橙色告警。 */
  ModbusSensorSample modbus;
  bool has_snapshot = ModbusSensor_GetSnapshot(0U, &modbus);
  if (!has_snapshot) {
    modbus.temperature_x10 = last_temperature;
    modbus.humidity_x10 = last_humidity;
    modbus.valid = false;
  }
  bool has_data = last_has_data || modbus.valid;
  if (!last_has_data || modbus.temperature_x10 != last_temperature ||
      modbus.humidity_x10 != last_humidity || modbus.valid != last_valid) {
    app_ui_update_temperature_humidity(modbus.temperature_x10,
                                       modbus.humidity_x10, has_data,
                                       modbus.valid);
  }
  /* 原始读数与状态计数直接上屏，不依赖调试器即可确认采集结果。 */
  if (has_snapshot != last_has_snapshot ||
      modbus.success_count != last_success_count ||
      modbus.error_count != last_error_count) {
    if (has_snapshot) {
      app_ui_update_modbus_readout(LVGL_ModbusStatusText(modbus.status),
                                   modbus.success_count, modbus.error_count);
    } else {
      app_ui_update_modbus_readout(NULL, 0U, 0U);
    }
    last_has_snapshot = has_snapshot;
    last_success_count = modbus.success_count;
    last_error_count = modbus.error_count;
  }
  last_temperature = modbus.temperature_x10;
  last_humidity = modbus.humidity_x10;
  last_valid = modbus.valid;
  last_has_data = has_data;

  /* 光照没有RS485来源，继续使用网关任务的模拟采样。 */
  Usart3TestSnapshot_t sample;
  if (Usart3Test_GetSnapshot(&sample) &&
      (!displayed || sample.sequence != last_sequence))
  {
    app_ui_update_light_status(sample.light_lux, sample.tx_status == HAL_OK);
    app_ui_update_upload_sequence(sample.sequence);
    last_sequence = sample.sequence;
    displayed = true;
  }
}

/**
  * @brief 将LVGL生成的RGB565区域同步发送到ILI9341。
  * @param display 当前执行刷新的LVGL显示对象。
  * @param area 待刷新的屏幕坐标区域。
  * @param pixel_map 待发送的RGB565像素缓冲区。
  * @retval 无。
  */
static void LVGL_DisplayFlush(lv_display_t *display,
                              const lv_area_t *area,
                              uint8_t *pixel_map)
{
  uint32_t pixel_count;
  uint16_t width;
  uint16_t height;

  width = (uint16_t)lv_area_get_width(area);
  height = (uint16_t)lv_area_get_height(area);
  pixel_count = (uint32_t)width * height;

  lv_draw_sw_rgb565_swap(pixel_map, pixel_count);
  if (ILI9341_WriteRGB565((uint16_t)area->x1,
                          (uint16_t)area->y1,
                          width,
                          height,
                          pixel_map) != HAL_OK)
  {
    Error_Handler();
  }
  lv_display_flush_ready(display);
}

/**
  * @brief 初始化LVGL与ILI9341并持续运行图形任务。
  * @param hspi 屏幕使用的SPI句柄。
  * @retval 无。
  */
void LVGL_Port_Task(SPI_HandleTypeDef *hspi)
{
  lv_display_t *display;
  uint32_t delay_ms;

  if (ILI9341_Init(hspi) != HAL_OK)
  {
    Error_Handler();
  }

  lv_init();
  lv_tick_set_cb(HAL_GetTick);

  display = lv_display_create(ILI9341_WIDTH, ILI9341_HEIGHT);
  if (display == NULL)
  {
    Error_Handler();
  }

  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, LVGL_DisplayFlush);
  lv_display_set_buffers(display,
                         s_lvgl_draw_buffer,
                         NULL,
                         sizeof(s_lvgl_draw_buffer),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  app_ui_create();
  /* 上电先显示占位符，等RS485站号1首个有效采样到达再填入真实值。 */
  app_ui_update_temperature_humidity(0, 0U, false, false);
  g_lvgl_started = true;

  for (;;)
  {
    LVGL_UpdateSensorCards();
    LVGL_UpdateResource();
    /* 复用TIM6维护的HAL毫秒时基更新运行时长，不在定时器中断中操作LVGL。 */
    app_ui_update_uptime(HAL_GetTick());
    delay_ms = lv_timer_handler();
    if ((delay_ms == LV_NO_TIMER_READY) ||
        (delay_ms > LVGL_TASK_MAX_DELAY_MS))
    {
      delay_ms = LVGL_TASK_MAX_DELAY_MS;
    }
    if (delay_ms == 0U)
    {
      delay_ms = 1U;
    }
    g_lvgl_heartbeat = HAL_GetTick();
    osDelay(delay_ms);
  }
}
