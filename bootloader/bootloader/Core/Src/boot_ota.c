#include "boot_ota.h"
#include "ota_store.h"
#include "ota_install.h"
#include "memory_layout.h"
#include "w25q64_qspi.h"
#include "st7735s.h"
#include "boot_trial.h"
#include "boot_page.h"
#include <stdio.h>
#include <string.h>

static QSPI_HandleTypeDef qspi;
static bool flash_ok;
/**
  * @brief 擦除逻辑非活动Bank的扇区1至3，保留两侧Boot和活动App。
  * @retval true表示384KiB候选区已擦除并重新上锁。
  */
bool OtaApp_Erase(void)
{
    if (HAL_FLASH_Unlock()!=HAL_OK) return false;
    FLASH_EraseInitTypeDef erase={0};
    uint32_t error=0;
    erase.TypeErase=FLASH_TYPEERASE_SECTORS; erase.Banks=FLASH_BANK_2;
    /* RM0433表20：CR1/CR2也随SWAP交换，HAL_BANK_2对应逻辑高地址。 */
    erase.Sector=FLASH_SECTOR_1; erase.NbSectors=3;
    erase.VoltageRange=FLASH_VOLTAGE_RANGE_3;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);
    bool ok=HAL_FLASHEx_Erase(&erase,&error)==HAL_OK;
    (void)HAL_FLASH_Lock();
    return ok;
}

/**
  * @brief 将data的size字节写到应用区offset处，末尾不足32字节时补FF。
  * @retval true表示每个256位Flash字均编程成功并重新上锁。
  */
bool OtaApp_Write(uint32_t offset, const uint8_t *data, uint32_t size)
{
    if ((offset&31U) || !size ||
        offset>=APP_FLASH_SIZE || size>APP_FLASH_SIZE-offset || HAL_FLASH_Unlock()!=HAL_OK) return false;
    uint32_t word[8] __attribute__((aligned(32)));
    bool ok=true;
    while (size && ok) {
        uint32_t n=size>32U?32U:size;
        memset(word,0xFF,sizeof(word)); memcpy(word,data,n);
        ok=HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,APP_INACTIVE_BASE+offset,(uint32_t)word)==HAL_OK;
        offset+=32; data+=n; size-=n;
    }
    (void)HAL_FLASH_Lock();
    return ok;
}

/**
  * @brief 从物理bank的应用区offset读取size字节到data，并失效对应数据缓存。
  * @retval true表示读取范围有效，供安装后的完整CRC校验使用。
  */
bool OtaApp_ReadBank(uint32_t bank, uint32_t offset, uint8_t *data, uint32_t size)
{
    if (bank>1 || offset>=APP_FLASH_SIZE || size>APP_FLASH_SIZE-offset) return false;
    uint32_t base=bank==OtaApp_CurrentBank()?APP_FLASH_BASE:APP_INACTIVE_BASE;
    if (SCB->CCR & SCB_CCR_DC_Msk) {
        uint32_t start=(base+offset)&~31U;
        SCB_InvalidateDCache_by_Addr((uint32_t *)start,(int32_t)((offset+size+31U)&~31U)-(int32_t)(offset&~31U));
    }
    memcpy(data,(const void *)(base+offset),size);
    return true;
}

/** @brief 返回当前映射到低地址的物理Bank，0表示Bank1、1表示Bank2。 @retval 物理Bank编号。 */
uint32_t OtaApp_CurrentBank(void) { return (FLASH->OPTCR & FLASH_OPTCR_SWAP_BANK)?1U:0U; }

/** @brief 确认高地址Boot与正在运行的Boot整扇区一致，避免切到无引导Bank。 @retval 是否一致。 */
bool OtaApp_BootCopyValid(void)
{
    return !memcmp((const void *)BOOT_FLASH_BASE,(const void *)BOOT_INACTIVE_BASE,BOOT_FLASH_SIZE);
}

/** @brief 设置物理bank为下一启动Bank，仅修改SWAP选项，提交成功后系统复位。 @retval 失败返回false。 */
bool OtaApp_SelectBank(uint32_t bank)
{
    if (bank>1) return false;
    if (bank==OtaApp_CurrentBank()) return true;
    /* 本工程480MHz/VOS0按Rev.V验证；早期硅片的即时交换时序不同，拒绝冒险切换。 */
    if (HAL_GetREVID()!=REV_ID_V) return false;
    if (HAL_FLASH_Unlock()!=HAL_OK) return false;
    bool ok=HAL_FLASH_OB_Unlock()==HAL_OK;
    FLASH_OBProgramInitTypeDef option={0};
    option.OptionType=OPTIONBYTE_USER;
    option.USERType=OB_USER_SWAP_BANK;
    option.USERConfig=bank?OB_SWAP_BANK_ENABLE:OB_SWAP_BANK_DISABLE;
    if (ok) ok=HAL_FLASHEx_OBProgram(&option)==HAL_OK;
    if (ok) ok=HAL_FLASH_OB_Launch()==HAL_OK;
    (void)HAL_FLASH_OB_Lock();
    (void)HAL_FLASH_Lock();
    if (ok) { __DSB(); NVIC_SystemReset(); }
    return false;
}

/** @brief 返回本次上电复位原因，交给试启动日志保存。 @retval RCC复位标志。 */
uint32_t OtaBoot_ResetReason(void) { return RCC->RSR; }
/** @brief 把试启动token交给应用健康确认信箱。 @retval 无。 */
void OtaBoot_ArmTrial(uint32_t token) { BootTrial_Arm(token); }
/** @brief 检查token是否被应用确认且本次为软件复位。 @retval 是否确认。 */
bool OtaBoot_Confirmed(uint32_t token)
{
    return (RCC->RSR & RCC_RSR_SFTRSTF) && BootTrial_Confirmed(token);
}
/** @brief 清除跨复位试启动信箱。 @retval 无。 */
void OtaBoot_ClearTrial(void) { BootTrial_Clear(); }
/** @brief 跳转前启动约32秒IWDG，覆盖应用进入main之前的启动故障。 @retval 是否启动成功。 */
bool OtaBoot_StartWatchdog(void)
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
    /* 应用自检后会改为原有短超时；Boot恢复模式不主动启动看门狗。 */
    IWDG1->KR=0xCCCCU;
    IWDG1->KR=0x5555U;
    IWDG1->PR=6U;
    IWDG1->RLR=4095U;
    IWDG1->WINR=4095U;
    uint32_t start=HAL_GetTick();
    while (IWDG1->SR) if ((uint32_t)(HAL_GetTick()-start)>1000U) return false;
    IWDG1->KR=0xAAAAU;
    return true;
}

bool BootOta_Init(void)
{
    RCC_PeriphCLKInitTypeDef clock={0};
    clock.PeriphClockSelection=RCC_PERIPHCLK_QSPI;
    clock.QspiClockSelection=RCC_QSPICLKSOURCE_D1HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&clock)!=HAL_OK) return false;
    __HAL_RCC_QSPI_CLK_ENABLE();
    __HAL_RCC_QSPI_FORCE_RESET(); __HAL_RCC_QSPI_RELEASE_RESET();
    __HAL_RCC_GPIOE_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef gpio={0};
    gpio.Mode=GPIO_MODE_AF_PP; gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate=GPIO_AF9_QUADSPI; gpio.Pin=GPIO_PIN_2;
    HAL_GPIO_Init(GPIOE,&gpio); HAL_GPIO_Init(GPIOB,&gpio);
    gpio.Pin=GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13; HAL_GPIO_Init(GPIOD,&gpio);
    gpio.Pin=GPIO_PIN_6; gpio.Alternate=GPIO_AF10_QUADSPI; HAL_GPIO_Init(GPIOB,&gpio);
    qspi.Instance=QUADSPI;
    qspi.Init.ClockPrescaler=11; qspi.Init.FifoThreshold=4;
    qspi.Init.SampleShifting=QSPI_SAMPLE_SHIFTING_NONE; qspi.Init.FlashSize=22;
    qspi.Init.ChipSelectHighTime=QSPI_CS_HIGH_TIME_2_CYCLE;
    qspi.Init.ClockMode=QSPI_CLOCK_MODE_0; qspi.Init.FlashID=QSPI_FLASH_ID_1;
    qspi.Init.DualFlash=QSPI_DUALFLASH_DISABLE;
    flash_ok=HAL_QSPI_Init(&qspi)==HAL_OK && W25Q64_Init(&qspi)==HAL_OK;
    return flash_ok;
}

/**
  * @brief 读取address起的size字节到data，供下载状态机和读回校验使用。
  * @retval true表示读操作成功。
  */
bool OtaFlash_Read(uint32_t address, uint8_t *data, uint32_t size)
{
    return flash_ok && W25Q64_ReadData(address,data,size)==HAL_OK;
}
/**
  * @brief 将data的size字节按256字节页边界写入address。
  * @retval true表示所有页均完成编程。
  */
bool OtaFlash_Write(uint32_t address, const uint8_t *data, uint32_t size)
{
    while (size) {
        uint32_t n=256U-(address&255U); if (n>size) n=size;
        if (!flash_ok || W25Q64_PageProgram(address,data,(uint16_t)n)!=HAL_OK) return false;
        address+=n; data+=n; size-=n;
    }
    return true;
}
/**
  * @brief 擦除address所在的4KiB外部Flash扇区。
  * @retval true表示擦除完成。
  */
bool OtaFlash_Erase(uint32_t address)
{
    return flash_ok && W25Q64_SectorErase(address)==HAL_OK;
}
/**
  * @brief 显示done/total进度及英文阶段，state为0准备、1接收、2下载校验、3下载完成、4取消、5失败、6擦除、7安装、8安装校验、9安装完成。
  * @retval 无，显示失败不改变Flash操作结果。
  */
void OtaFlash_Progress(uint32_t done, uint32_t total, uint32_t state)
{
    static uint32_t previous=0xFFFFFFFFU;
    static uint32_t previous_label=0xFFFFFFFFU;
    uint32_t width=total?(done*112U/total):0;
    uint32_t key=(state<<16)|width;
    if (previous==key) return;
    previous=key;
    uint16_t color=(state==3 || state==9)?0x07E0U:(state==4 || state==5)?0xF800U:
                   (state==2 || state==8)?0xFFE0U:state==6?0xF81FU:0x07FFU;
    (void)ST7735S_FillRect(8U,76U,112U,10U,0x2104U);
    if (width) (void)ST7735S_FillRect(8U,76U,(uint16_t)width,10U,color);
    static const char *const stages[] = {
        "WAITING", "RECEIVING", "CHECK FILE", "FILE READY", "CANCELED",
        "FAILED", "ERASING", "INSTALLING", "CHECK APP", "DONE"
    };
    uint32_t percent=total?done*100U/total:0U;
    uint32_t label_key=(state<<8)|percent;
    if (previous_label==label_key) return;
    previous_label=label_key;
    char label[24];
    const char *stage=state<10U?stages[state]:"FAILED";
    if (total && (state==1U || state==2U || state==6U || state==7U || state==8U))
        (void)snprintf(label,sizeof(label),"%s %lu%%",stage,(unsigned long)percent);
    else
        (void)snprintf(label,sizeof(label),"%s",stage);
    BootPage_DrawStatus(label,color);
}

