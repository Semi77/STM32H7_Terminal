#ifndef OTA_INSTALL_H
#define OTA_INSTALL_H
#include "ota_store.h"

/* bank为物理Bank编号0或1，硬件层根据当前映射选择地址。 */
uint32_t OtaApp_CurrentBank(void);
bool OtaApp_Erase(void);
bool OtaApp_Write(uint32_t offset, const uint8_t *data, uint32_t size);
bool OtaApp_ReadBank(uint32_t bank, uint32_t offset, uint8_t *data, uint32_t size);
bool OtaApp_BootCopyValid(void);
bool OtaApp_SelectBank(uint32_t bank);
uint32_t OtaBoot_ResetReason(void);
void OtaBoot_ArmTrial(uint32_t token);
bool OtaBoot_Confirmed(uint32_t token);
void OtaBoot_ClearTrial(void);
bool OtaBoot_StartWatchdog(void);
/** @brief 跳转前持久化试启动次数并启动看门狗，所有启动入口必须调用。 @retval true允许跳转。 */
bool OtaInstall_BeforeBoot(void);

/**
  * @brief 恢复A/B状态并校验当前应用，必要时交换到目标Bank复位。
  * @retval OTA_OK表示允许按原引导请求启动，否则必须留在Bootloader。
  */
uint32_t OtaInstall_Recover(void);
/**
  * @brief 将size字节且CRC为crc的下载镜像安装到非活动Bank并提交试运行。
  * @retval OTA_OK表示内部Flash已完整读回校验，可回复后复位启动。
  */
uint32_t OtaInstall_Run(uint32_t size, uint32_t crc);
/**
  * @brief 查询是否禁止启动当前应用。
  * @retval true表示必须停留在Bootloader，直到完整镜像安装成功。
  */
bool OtaInstall_Blocked(void);
/** @brief 检查A/B状态已加载且允许开始新下载。 @retval true表示允许。 */
bool OtaInstall_PrepareDownload(void);
#endif
