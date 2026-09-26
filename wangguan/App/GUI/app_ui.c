#include "app_ui.h"

#include "lvgl.h"
#include <string.h>

typedef struct {
    lv_obj_t *temperature_value;
    lv_obj_t *humidity_value;
    lv_obj_t *light_value;
    lv_obj_t *uptime_value;
    lv_obj_t *network_value;
    lv_obj_t *network_online_icon;
    lv_obj_t *network_offline_icon;
    lv_obj_t *ethernet_value;
    lv_obj_t *system_value;
    lv_obj_t *resource_value;
    lv_obj_t *modbus_value;
    uint32_t uptime_last_tick_ms;
    uint32_t uptime_remainder_ms;
    uint32_t uptime_total_seconds;
} app_ui_state_t;

static app_ui_state_t s_ui;

/**
  * @brief 只刷新上传帧序号标签，温湿度改由RS485采样独立驱动。
  * @param sequence 当前发送帧序号。
  * @retval 无，必须由图形任务调用。
  */
void app_ui_update_upload_sequence(uint32_t sequence)
{
    char text[64];
    lv_snprintf(text, sizeof(text), "SEQ %lu\nUPLOAD --", (unsigned long)sequence);
    lv_label_set_text(s_ui.system_value, text);
}

/**
  * @brief 只刷新光照卡片和串口发送结果，不触碰温湿度卡片。
  * @param light_lux 模拟光照，单位勒克斯。
  * @param tx_ok 当前记录的串口发送是否成功。
  * @retval 无。
  */
void app_ui_update_light_status(uint32_t light_lux, bool tx_ok)
{
    char text[64];
    lv_snprintf(text, sizeof(text), "%lu lx", (unsigned long)light_lux);
    lv_label_set_text(s_ui.light_value, text);
    lv_label_set_text(s_ui.ethernet_value,
                      tx_ok ? "SIM DATA\nTX OK" : "SIM DATA\nTX ERROR");
    lv_obj_set_style_text_color(s_ui.ethernet_value,
                                lv_color_hex(tx_ok ? 0x45D19A : 0xFF7A59), 0);
}

/**
  * @brief 在标题栏显示RS485采集状态与成败计数，数值本身由温湿度卡片显示。
  * @param status_text 状态短标签；NULL表示尚无快照。
  * @param success_count 累计成功次数。
  * @param error_count 累计失败次数。
  * @param tx_started 本次请求是否开始发送；tx_complete表示发送完成；rx_bytes为接收字节数。
  * @retval 无，必须从LVGL所在线程调用。
  */
void app_ui_update_modbus_readout(const char *status_text,
                                  uint32_t success_count, uint32_t error_count,
                                  bool tx_started, bool tx_complete, uint16_t rx_bytes)
{
    char text[48];

    if(status_text == NULL)
    {
        lv_label_set_text(s_ui.modbus_value, "RS485 --");
        lv_obj_set_style_text_color(s_ui.modbus_value, lv_color_hex(0x8FA4C7), 0);
        return;
    }
    /* 同时给出状态和成功/失败计数，便于区分"没接通"与"偶发校验错"。 */
    lv_snprintf(text, sizeof(text), "RS485 %s %lu/%lu T%u C%u R%u", status_text,
                (unsigned long)success_count, (unsigned long)error_count,
                tx_started ? 1U : 0U, tx_complete ? 1U : 0U,
                (unsigned int)rx_bytes);
    lv_label_set_text(s_ui.modbus_value, text);
    lv_obj_set_style_text_color(s_ui.modbus_value,
                                strcmp(status_text, "OK") == 0
                                    ? lv_color_hex(0x45D19A)
                                    : lv_color_hex(0xFF7A59), 0);
}

/**
  * @brief 在图形线程同步模拟数值和本地发送结果，联网状态由独立接口更新。
  * @param sequence 帧序号。
  * @param temperature_c 温度，单位摄氏度。
  * @param humidity_percent 湿度，单位百分比。
  * @param light_lux 光照，单位勒克斯。
  * @param has_data 是否已有模拟发送记录。
  * @param tx_ok 当前记录的串口发送是否成功。
  * @retval 无。
  */
void app_ui_update_uart_test(uint32_t sequence, uint32_t temperature_c,
                             uint32_t humidity_percent, uint32_t light_lux,
                             bool has_data, bool tx_ok)
{
    app_ui_update_temperature_humidity((int16_t)(temperature_c * 10U),
                                        (uint16_t)(humidity_percent * 10U),
                                        has_data, true);
    if(has_data) {
        app_ui_update_light_status(light_lux, tx_ok);
    }
    app_ui_update_upload_sequence(sequence);
}

/**
  * @brief 将累计运行秒数换算为时分秒并刷新UP标签，小时数不按24小时归零。
  * @param total_seconds 自上电以来累计的完整秒数。
  * @retval 无，必须在界面创建后从图形任务调用。
  */
static void set_uptime_seconds(uint32_t total_seconds)
{
    char text[24];
    uint32_t hours = total_seconds / 3600U;
    uint32_t minutes = (total_seconds / 60U) % 60U;
    uint32_t seconds = total_seconds % 60U;

    lv_snprintf(text,
                sizeof(text),
                "UP %02lu:%02lu:%02lu",
                (unsigned long)hours,
                (unsigned long)minutes,
                (unsigned long)seconds);
    lv_label_set_text(s_ui.uptime_value, text);
}

/**
  * @brief 累积实际经过的毫秒数以更新运行时长，任务短暂延迟不会漏算整秒。
  * @param tick_ms 上电后从零开始的32位毫秒计数，相邻调用间隔必须小于一次计数回绕周期。
  * @retval 无，仅在完整秒数变化时更新屏幕标签。
  */
void app_ui_update_uptime(uint32_t tick_ms)
{
    uint32_t elapsed_ms = (uint32_t)(tick_ms - s_ui.uptime_last_tick_ms);
    uint32_t elapsed_seconds = elapsed_ms / 1000U;

    s_ui.uptime_last_tick_ms = tick_ms;
    s_ui.uptime_remainder_ms += elapsed_ms % 1000U;
    elapsed_seconds += s_ui.uptime_remainder_ms / 1000U;
    s_ui.uptime_remainder_ms %= 1000U;
    if(elapsed_seconds == 0U) {
        return;
    }
    s_ui.uptime_total_seconds += elapsed_seconds;
    set_uptime_seconds(s_ui.uptime_total_seconds);
}

/**
  * @brief 创建一个传感器数据卡片，参数 parent 为父对象、title 为标题、value 为初始值、accent 为强调色。
  * @param parent 卡片所属的父对象。
  * @param title 卡片顶部显示的名称。
  * @param value 卡片中显示的初始测量值。
  * @param accent 卡片顶部装饰条的颜色。
  * @retval 返回用于后续更新测量值的标签对象。
  */
static lv_obj_t *create_sensor_card(lv_obj_t *parent,
                                    const char *title,
                                    const char *value,
                                    lv_color_t accent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_t *bar;
    lv_obj_t *title_label;
    lv_obj_t *value_label;

    lv_obj_set_size(card, 92, 82);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x18243A), 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    bar = lv_obj_create(card);
    lv_obj_set_size(bar, 32, 3);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, accent, 0);

    title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 12);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x8FA4C7), 0);

    value_label = lv_label_create(card);
    lv_label_set_text(value_label, value);
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xF4F7FC), 0);

    return value_label;
}

/**
  * @brief 创建可在 PC 与 STM32 共用的环境网关主界面。
  * @retval 无。
  */
void app_ui_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *header;
    lv_obj_t *title;
    lv_obj_t *status;
    lv_obj_t *network_panel;
    lv_obj_t *online_icon;
    lv_obj_t *offline_icon;
    lv_obj_t *cards;
    lv_obj_t *footer;
    lv_obj_t *footer_title;
    lv_obj_t *footer_left_value;
    lv_obj_t *footer_divider;
    lv_obj_t *footer_right_value;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_pad_all(screen, 8, 0);

    header = lv_obj_create(screen);
    lv_obj_set_size(header, 304, 38);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(header);
    lv_label_set_text(title, "ENV GATEWAY");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF4F7FC), 0);

    network_panel = lv_obj_create(header);
    lv_obj_set_size(network_panel, 90, 28);
    lv_obj_align(network_panel, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(network_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(network_panel, 0, 0);
    lv_obj_set_style_pad_all(network_panel, 0, 0);
    lv_obj_clear_flag(network_panel, LV_OBJ_FLAG_SCROLLABLE);

    online_icon = lv_label_create(network_panel);
    lv_label_set_text(online_icon, LV_SYMBOL_WIFI);
    lv_obj_align(online_icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(online_icon, lv_color_hex(0x45D19A), 0);
    lv_obj_add_flag(online_icon, LV_OBJ_FLAG_HIDDEN);
    s_ui.network_online_icon = online_icon;

    offline_icon = lv_label_create(network_panel);
    lv_label_set_text(offline_icon, LV_SYMBOL_CLOSE);
    lv_obj_align(offline_icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(offline_icon, lv_color_hex(0xFF7A59), 0);
    s_ui.network_offline_icon = offline_icon;

    status = lv_label_create(network_panel);
    lv_label_set_text(status, "OFFLINE");
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0xFF7A59), 0);
    s_ui.network_value = status;

    cards = lv_obj_create(screen);
    lv_obj_set_size(cards, 304, 86);
    lv_obj_align(cards, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cards, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cards, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cards, 0, 0);
    lv_obj_set_style_pad_all(cards, 0, 0);

    s_ui.temperature_value = create_sensor_card(cards, "TEMP", "--.- C", lv_color_hex(0xFF7A59));
    s_ui.humidity_value = create_sensor_card(cards, "HUMID", "--.- %", lv_color_hex(0x55A7FF));
    s_ui.light_value = create_sensor_card(cards, "LIGHT", "---- lx", lv_color_hex(0xFFD166));

    footer = lv_obj_create(screen);
    lv_obj_set_size(footer, 304, 91);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(footer, 10, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, lv_color_hex(0x263755), 0);
    lv_obj_set_style_bg_color(footer, lv_color_hex(0x111C30), 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    /* 在底部面板创建后放置RS485状态，避免使用未初始化的父对象。 */
    s_ui.modbus_value = lv_label_create(footer);
    lv_label_set_text(s_ui.modbus_value, "RS485 --");
    lv_obj_align(s_ui.modbus_value, LV_ALIGN_TOP_LEFT, 0, 18);
    lv_obj_set_style_text_color(s_ui.modbus_value, lv_color_hex(0x8FA4C7), 0);

    footer_title = lv_label_create(footer);
    lv_label_set_text(footer_title, "SYSTEM STATUS");
    lv_obj_align(footer_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_color(footer_title, lv_color_hex(0x8FA4C7), 0);

    s_ui.uptime_value = lv_label_create(footer);
    lv_label_set_text(s_ui.uptime_value, "UP 00:00:00");
    lv_obj_align(s_ui.uptime_value, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_text_color(s_ui.uptime_value, lv_color_hex(0x45D19A), 0);

    footer_left_value = lv_label_create(footer);
    lv_label_set_text(footer_left_value, "STM32H743\nW5500 DOWN");
    lv_obj_align(footer_left_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_color(footer_left_value, lv_color_hex(0xD8E2F2), 0);
    s_ui.ethernet_value = footer_left_value;

    footer_divider = lv_obj_create(footer);
    lv_obj_set_size(footer_divider, 2, 34);
    lv_obj_align(footer_divider, LV_ALIGN_BOTTOM_LEFT, 99, 0);
    lv_obj_set_style_radius(footer_divider, 1, 0);
    lv_obj_set_style_border_width(footer_divider, 0, 0);
    lv_obj_set_style_bg_color(footer_divider, lv_color_hex(0x526784), 0);
    lv_obj_clear_flag(footer_divider, LV_OBJ_FLAG_SCROLLABLE);

    footer_right_value = lv_label_create(footer);
    lv_label_set_text(footer_right_value, "MQTT DOWN\nFLASH ERR");
    lv_obj_align(footer_right_value, LV_ALIGN_BOTTOM_LEFT, 111, 0);
    lv_obj_set_style_text_color(footer_right_value, lv_color_hex(0xD8E2F2), 0);
    s_ui.system_value = footer_right_value;
    /* 资源占用独立贴右下角，不随其他标签的偏移量漂移。 */
    s_ui.resource_value = lv_label_create(footer);
    lv_label_set_text(s_ui.resource_value, "CPU --%\nMEM --%");
    lv_obj_set_style_text_color(s_ui.resource_value, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_ui.resource_value, LV_TEXT_ALIGN_RIGHT, 0);
    /* 忽略footer的flex布局，避免与左侧MQTT/FLASH标签互相挤动。 */
    lv_obj_add_flag(s_ui.resource_value, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(s_ui.resource_value, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    /* 毫秒计数从上电零点累计，因此首次更新会包含屏幕初始化所用时间。 */
    s_ui.uptime_last_tick_ms = 0U;
    s_ui.uptime_remainder_ms = 0U;
    s_ui.uptime_total_seconds = 0U;
}

/**
  * @brief 更新温湿度卡片，失败时保留有效数值并使用橙色提示数据未更新。
  * @param temperature_tenths_c 温度值，单位为0.1摄氏度。
  * @param humidity_tenths_percent 湿度值，单位为0.1百分比。
  * @param has_data 是否已有有效测量，无有效测量时显示占位符。
  * @param read_ok 最近一次读取是否成功，成功时恢复正常文字颜色。
  * @retval 无，必须从LVGL所在线程调用。
  */
void app_ui_update_temperature_humidity(int16_t temperature_tenths_c,
                                        uint16_t humidity_tenths_percent,
                                        bool has_data,
                                        bool read_ok)
{
    char text[20];
    int32_t temperature = temperature_tenths_c;
    lv_color_t color = lv_color_hex(read_ok ? 0xF4F7FC : 0xFF7A59);

    lv_obj_set_style_text_color(s_ui.temperature_value, color, 0);
    lv_obj_set_style_text_color(s_ui.humidity_value, color, 0);
    if(!has_data) {
        lv_label_set_text(s_ui.temperature_value, "--.- C");
        lv_label_set_text(s_ui.humidity_value, "--.- %");
        return;
    }
    lv_snprintf(text,
                sizeof(text),
                "%s%ld.%lu C",
                temperature < 0 ? "-" : "",
                (long)(temperature < 0 ? -temperature : temperature) / 10L,
                (unsigned long)(temperature < 0 ? -temperature : temperature) % 10UL);
    lv_label_set_text(s_ui.temperature_value, text);

    lv_snprintf(text,
                sizeof(text),
                "%lu.%lu %%",
                (unsigned long)humidity_tenths_percent / 10UL,
                (unsigned long)humidity_tenths_percent % 10UL);
    lv_label_set_text(s_ui.humidity_value, text);
}

/**
  * @brief 使用一份数据快照刷新界面，参数 data 包含环境数据、运行时间和设备状态。
  * @param data 待显示的数据快照，必须在创建界面后从 LVGL 所在线程调用。
  * @retval 无。
  */
void app_ui_update_data(const app_ui_data_t *data)
{
    char text[20];

    app_ui_update_temperature_humidity(data->temperature_tenths_c,
                                        data->humidity_tenths_percent,
                                        true,
                                        true);

    lv_snprintf(text, sizeof(text), "%lu lx", (unsigned long)data->light_lux);
    lv_label_set_text(s_ui.light_value, text);

    set_uptime_seconds(data->uptime_seconds);

    lv_label_set_text(s_ui.network_value, data->network_online ? "ONLINE" : "OFFLINE");
    lv_obj_set_style_text_color(s_ui.network_value,
                                data->network_online ? lv_color_hex(0x45D19A) : lv_color_hex(0xFF7A59),
                                0);

    if(data->network_online) {
        lv_obj_clear_flag(s_ui.network_online_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.network_offline_icon, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(s_ui.network_online_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_ui.network_offline_icon, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(s_ui.ethernet_value,
                      data->ethernet_link ? "STM32H743\nW5500 LINK" : "STM32H743\nW5500 DOWN");
    lv_label_set_text(s_ui.system_value,
                      data->mqtt_connected
                          ? (data->flash_ready ? "MQTT READY\nFLASH OK" : "MQTT READY\nFLASH ERR")
                          : (data->flash_ready ? "MQTT DOWN\nFLASH OK" : "MQTT DOWN\nFLASH ERR"));
}

/**
  * @brief 更新右下角CPU与堆占用率，数值由统计模块每秒重算一次。
  * @param cpu_percent 所有任务占用时间占总运行时间的百分比，0~100。
  * @param heap_percent 已分配堆占configTOTAL_HEAP_SIZE的百分比，0~100。
  * @retval 无，必须从LVGL所在线程调用。
  */
void app_ui_update_resource(uint32_t cpu_percent, uint32_t heap_percent)
{
    char text[24];

    lv_snprintf(text, sizeof(text), "CPU %lu%%\nMEM %lu%%",
                (unsigned long)cpu_percent, (unsigned long)heap_percent);
    lv_label_set_text(s_ui.resource_value, text);
}


/**
  * @brief 用绿色ONLINE或橙色OFFLINE显示当前联网状态。
  * @param online 是否在线。
  * @retval 无，必须在图形任务中调用。
  */
void app_ui_update_network(bool online)
{
    lv_label_set_text(s_ui.network_value, online ? "ONLINE" : "OFFLINE");
    lv_obj_set_style_text_color(s_ui.network_value,
                                lv_color_hex(online ? 0x45D19A : 0xFF7A59), 0);
    if (online) {
        lv_obj_clear_flag(s_ui.network_online_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.network_offline_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.network_online_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_ui.network_offline_icon, LV_OBJ_FLAG_HIDDEN);
    }
}
