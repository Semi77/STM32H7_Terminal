#ifndef MEMORY_LAYOUT_H
#define MEMORY_LAYOUT_H

/* 内部Flash使用交换后的逻辑地址，两份引导各独占首个128KiB扇区。 */
#define BOOT_FLASH_BASE       0x08000000U
#define BOOT_FLASH_SIZE       0x00020000U
#define APP_FLASH_BASE        0x08020000U
#define APP_FLASH_SIZE        0x00060000U
#define APP_FLASH_END         (APP_FLASH_BASE + APP_FLASH_SIZE)
#define BOOT_INACTIVE_BASE    0x08100000U
#define APP_INACTIVE_BASE     0x08120000U

/* 外部Flash地址均为偏移：槽1接收固件，参数区保存下载完成记录，槽0预留。 */
#define EXT_FW_SLOT0_BASE     0x00000000U
#define EXT_FW_SLOT1_BASE     0x00080000U
#define EXT_FW_SLOT_SIZE      0x00080000U
#define EXT_BOOT_PARA0_BASE   0x00100000U
#define EXT_BOOT_PARA1_BASE   0x00101000U
#define EXT_BOOT_PARA_SIZE    0x00001000U
/* 独立恢复标记：重新下载擦除参数时仍阻止启动半写入的内部应用。 */
#define EXT_RECOVERY_BASE     0x00102000U
/* A/B状态独立于下载参数，下载和取消均不得擦除这两个扇区。 */
#define EXT_AB_STATE0_BASE    0x00103000U
#define EXT_AB_STATE1_BASE    0x00104000U
#define EXT_CACHE_BASE        0x00400000U
#define EXT_FLASH_END         0x00800000U
#endif
