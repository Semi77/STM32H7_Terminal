#include "app_ui.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/**
  * @brief 递归查找指定文本的真实LVGL标签以验证屏幕数据绑定。
  * @param object 本次搜索的父对象。
  * @param text 需要查找的标签文本。
  * @retval 匹配对象，未找到时返回NULL。
  */
static lv_obj_t *find_label(lv_obj_t *object, const char *text)
{
  uint32_t index;
  if (lv_obj_check_type(object, &lv_label_class) &&
      strcmp(lv_label_get_text(object), text) == 0) return object;
  for (index = 0; index < lv_obj_get_child_count(object); index++)
  {
    lv_obj_t *found = find_label(lv_obj_get_child(object, index), text);
    if (found != NULL) return found;
  }
  return NULL;
}

/** @brief 验证初始占位符、有效数据、失败变色及恢复，同时确认光照占位符不被覆盖。 @retval 零表示通过。 */
int main(void)
{
  lv_obj_t *screen;
  lv_obj_t *temperature;
  lv_display_t *display;
  lv_init();
  display = lv_display_create(320, 240);
  assert(display != NULL);
  app_ui_create();
  screen = lv_screen_active();
  assert(find_label(screen, "--.- C") != NULL);
  assert(find_label(screen, "--.- %") != NULL);

  app_ui_update_temperature_humidity(0, 0U, false, false);
  assert(find_label(screen, "--.- C") != NULL);
  app_ui_update_temperature_humidity(240, 550U, true, true);
  temperature = find_label(screen, "24.0 C");
  assert(temperature != NULL && find_label(screen, "55.0 %") != NULL);
  assert(lv_color_to_u32(lv_obj_get_style_text_color(temperature, 0)) ==
         lv_color_to_u32(lv_color_hex(0xF4F7FC)));
  app_ui_update_temperature_humidity(240, 550U, true, false);
  assert(find_label(screen, "24.0 C") == temperature);
  assert(lv_color_to_u32(lv_obj_get_style_text_color(temperature, 0)) ==
         lv_color_to_u32(lv_color_hex(0xFF7A59)));
  app_ui_update_temperature_humidity(251, 612U, true, true);
  assert(find_label(screen, "25.1 C") == temperature);
  assert(find_label(screen, "61.2 %") != NULL);
  assert(lv_color_to_u32(lv_obj_get_style_text_color(temperature, 0)) ==
         lv_color_to_u32(lv_color_hex(0xF4F7FC)));
  assert(find_label(screen, "---- lx") != NULL);

  /* 验证秒、分、小时进位及任务延迟后按实际经过时间补算。 */
  app_ui_update_uptime(999U);
  assert(find_label(screen, "UP 00:00:00") != NULL);
  app_ui_update_uptime(1000U);
  assert(find_label(screen, "UP 00:00:01") != NULL);
  app_ui_update_uptime(59999U);
  assert(find_label(screen, "UP 00:00:59") != NULL);
  app_ui_update_uptime(60000U);
  assert(find_label(screen, "UP 00:01:00") != NULL);
  app_ui_update_uptime(3599999U);
  assert(find_label(screen, "UP 00:59:59") != NULL);
  app_ui_update_uptime(3600000U);
  assert(find_label(screen, "UP 01:00:00") != NULL);
  app_ui_update_uptime(3603500U);
  assert(find_label(screen, "UP 01:00:03") != NULL);
  app_ui_update_uptime(3603999U);
  assert(find_label(screen, "UP 01:00:03") != NULL);
  app_ui_update_uptime(3604000U);
  assert(find_label(screen, "UP 01:00:04") != NULL);
  app_ui_update_uptime(86400000U);
  assert(find_label(screen, "UP 24:00:00") != NULL);

  /* HAL毫秒计数回绕时运行时长应继续累计，不能突然返回零。 */
  app_ui_update_uptime(UINT32_MAX);
  assert(find_label(screen, "UP 1193:02:47") != NULL);
  app_ui_update_uptime(704U);
  assert(find_label(screen, "UP 1193:02:48") != NULL);
  app_ui_update_uptime(1704U);
  assert(find_label(screen, "UP 1193:02:49") != NULL);
  assert(find_label(screen, "25.1 C") == temperature);
  assert(find_label(screen, "61.2 %") != NULL);
  assert(find_label(screen, "---- lx") != NULL);
  puts("PASS: temperature/humidity, stale recovery, uptime carries, delayed updates, fractional milliseconds, 24 hours, tick rollover, independent cards.");
  lv_display_delete(display);
  lv_deinit();
  return 0;
}
