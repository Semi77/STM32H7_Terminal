#ifndef BOOT_PAGE_H
#define BOOT_PAGE_H
/**
  * @brief 无请求时启动有效Bank2应用，否则进入裸机引导页面并处理串口命令。
  * @retval 无，函数不返回。
  */
void BootPage_Run(void);
#endif
