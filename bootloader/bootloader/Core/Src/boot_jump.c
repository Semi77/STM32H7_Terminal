#include "boot_jump.h"

#define BOOT_APP_DTCM_START_ADDRESS   0x20000000U
#define BOOT_APP_DTCM_END_ADDRESS     0x20020000U
#define BOOT_APP_AXI_START_ADDRESS    0x24000000U
#define BOOT_APP_AXI_END_ADDRESS      0x24080000U
#define BOOT_STACK_ALIGNMENT_MASK     0x00000007U
#define BOOT_THUMB_ADDRESS_MASK       0x00000001U

/**
  * @brief 使用指定的初始栈顶地址和复位中断地址进入应用程序。
  * @param stack_address 应用程序向量表中保存的初始栈顶地址。
  * @param reset_address 应用程序向量表中保存的复位中断地址。
  * @retval 无。
  */
__attribute__((naked, noreturn))
static void BootJump_Start(uint32_t stack_address, uint32_t reset_address)
{
  __asm volatile ("MSR MSP, r0\n"
                  "CPSIE i\n"
                  "BX r1\n");
}

/**
  * @brief 校验应用程序初始栈顶地址是否位于H7的DTCM或AXI SRAM内。
  * @param stack_address 待校验的初始栈顶地址。
  * @retval true表示地址有效，false表示无效。
  */
static bool BootJump_IsStackAddressValid(uint32_t stack_address)
{
  bool in_dtcm;
  bool in_axi_sram;

  in_dtcm = ((stack_address > BOOT_APP_DTCM_START_ADDRESS) &&
             (stack_address <= BOOT_APP_DTCM_END_ADDRESS));
  in_axi_sram = ((stack_address >= BOOT_APP_AXI_START_ADDRESS) &&
                 (stack_address <= BOOT_APP_AXI_END_ADDRESS));

  return ((in_dtcm || in_axi_sram) &&
          ((stack_address & BOOT_STACK_ALIGNMENT_MASK) == 0U));
}

/**
  * @brief 校验应用程序复位入口是否为AppA分区内的Thumb指令地址。
  * @param reset_address 待校验的复位中断地址。
  * @param app_start 应用程序分区的起始地址。
  * @param app_end 应用程序分区结束后的第一个地址。
  * @retval true表示地址有效，false表示无效。
  */
static bool BootJump_IsResetAddressValid(uint32_t reset_address,
                                         uint32_t app_start,
                                         uint32_t app_end)
{
  uint32_t code_address = reset_address & ~BOOT_THUMB_ADDRESS_MASK;

  return (((reset_address & BOOT_THUMB_ADDRESS_MASK) != 0U) &&
          (code_address >= app_start) &&
          (code_address < app_end));
}

/**
  * @brief 校验应用程序向量表中的初始栈顶地址和复位中断地址。
  * @param app_start 应用程序分区的起始地址。
  * @param app_end 应用程序分区结束后的第一个地址。
  * @retval true表示应用程序入口地址有效，false表示无效。
  */
bool BootJump_IsApplicationValid(uint32_t app_start, uint32_t app_end)
{
  uint32_t stack_address;
  uint32_t reset_address;

  if ((app_start >= app_end) || ((app_start & 0x3U) != 0U))
  {
    return false;
  }

  stack_address = *(volatile const uint32_t *)app_start;
  reset_address = *(volatile const uint32_t *)(app_start + sizeof(uint32_t));

  return (BootJump_IsStackAddressValid(stack_address) &&
          BootJump_IsResetAddressValid(reset_address, app_start, app_end));
}

/**
  * @brief 注销Bootloader运行环境并跳转到应用程序复位入口。
  * @param app_start 应用程序向量表的起始地址。
  * @retval 无。
  */
void BootJump_ToApplication(uint32_t app_start)
{
  uint32_t stack_address;
  uint32_t reset_address;
  uint32_t register_index;

  stack_address = *(volatile const uint32_t *)app_start;
  reset_address = *(volatile const uint32_t *)(app_start + sizeof(uint32_t));

	// HAL库取消初始化
  (void)HAL_RCC_DeInit();
  (void)HAL_DeInit();

	// 失能中断请求
  __disable_irq();

  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;

  for (register_index = 0U;
       register_index < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]));
       register_index++)
  {
    NVIC->ICER[register_index] = 0xFFFFFFFFU;
    NVIC->ICPR[register_index] = 0xFFFFFFFFU;
  }

  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

    /**
    * @brief 仅在缓存已经启用时执行缓存清理和关闭操作。
    */
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_CleanDCache();
    SCB_DisableDCache();
  }

  if ((SCB->CCR & SCB_CCR_IC_Msk) != 0U)
  {
    SCB_DisableICache();
  }

  HAL_MPU_Disable();	

  __set_CONTROL(0U);
  __set_PSP(0U);
  __set_BASEPRI(0U);
  __set_FAULTMASK(0U);
  SCB->VTOR = app_start;
  __DSB();
  __ISB();

  BootJump_Start(stack_address, reset_address);
}
