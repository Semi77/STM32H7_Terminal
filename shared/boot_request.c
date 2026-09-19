#include "boot_request.h"
#define BOOT_REQUEST_MAGIC 0x424F4F54U

/**
  * @brief 开启备份寄存器总线访问，不复位备份域或配置RTC计时。
  * @retval 无。
  */
static void enable_backup(void)
{
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_CLK_ENABLE();
}

bool BootRequest_Set(void)
{
    enable_backup();
    RTC->BKP0R = BOOT_REQUEST_MAGIC;
    RTC->BKP1R = ~BOOT_REQUEST_MAGIC;
    __DSB();
    return RTC->BKP0R == BOOT_REQUEST_MAGIC && RTC->BKP1R == ~BOOT_REQUEST_MAGIC;
}

bool BootRequest_Take(void)
{
    enable_backup();
    bool requested = RTC->BKP0R == BOOT_REQUEST_MAGIC && RTC->BKP1R == ~BOOT_REQUEST_MAGIC;
    RTC->BKP0R = 0U;
    RTC->BKP1R = 0U;
    __DSB();
    return requested;
}
