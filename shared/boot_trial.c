#include "boot_trial.h"
#define TRIAL_MAGIC 0x54524941U
#define CONFIRM_MAGIC 0x434F4E46U
#define HEALTHY_MS 20000U

/** @brief 开启备份寄存器访问，不复位RTC域。 @retval 无。 */
static void access_backup(void)
{
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_CLK_ENABLE();
}
void BootTrial_Clear(void)
{
    access_backup();
    RTC->BKP2R=0; RTC->BKP3R=0; RTC->BKP4R=0; RTC->BKP5R=0;
    __DSB();
}
void BootTrial_Arm(uint32_t token)
{
    BootTrial_Clear();
    RTC->BKP3R=token; RTC->BKP4R=~token;
    RTC->BKP2R=TRIAL_MAGIC;
    __DSB();
}
bool BootTrial_Confirmed(uint32_t token)
{
    access_backup();
    return RTC->BKP2R==TRIAL_MAGIC && RTC->BKP3R==token &&
           RTC->BKP4R==~token && RTC->BKP5R==CONFIRM_MAGIC;
}
void BootTrial_Poll(bool healthy)
{
    static bool tracking;
    static uint32_t since;
    access_backup();
    if (RTC->BKP2R!=TRIAL_MAGIC || RTC->BKP4R!=~RTC->BKP3R) return;
    if (!healthy) { tracking=false; return; }
    if (!tracking) { since=HAL_GetTick(); tracking=true; }
    if ((uint32_t)(HAL_GetTick()-since)<HEALTHY_MS) return;
    /* 下一次Boot提交双副本确认，应用不与缓存任务争用QSPI。 */
    RTC->BKP5R=CONFIRM_MAGIC;
    __DSB();
    NVIC_SystemReset();
}
