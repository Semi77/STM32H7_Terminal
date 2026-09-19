#ifndef W25Q64_QSPI_H
#define W25Q64_QSPI_H

#include "stm32h7xx_hal.h"

#define W25Q64_FLASH_SIZE_BYTES  0x00800000U
#define W25Q64_PAGE_SIZE_BYTES   256U
#define W25Q64_SECTOR_SIZE_BYTES 4096U

/**
  * @brief 初始化W25Q64并校验JEDEC ID及开启四线模式。
  * @param hqspi 指向连接W25Q64的QSPI句柄。
  * @retval HAL状态，HAL_OK表示芯片识别和四线模式配置成功。
  */
HAL_StatusTypeDef W25Q64_Init(QSPI_HandleTypeDef *hqspi);

/**
  * @brief 读取W25Q64的制造商和器件ID。
  * @param manufacturer_id 用于返回8位制造商ID的指针。
  * @param device_id 用于返回16位存储类型和容量ID的指针。
  * @retval HAL状态，HAL_OK表示读取成功。
  */
HAL_StatusTypeDef W25Q64_ReadID(uint8_t *manufacturer_id, uint16_t *device_id);

/**
  * @brief 按JEDEC顺序读取制造商、存储器类型和容量ID。
  * @param manufacturer_id 用于保存制造商ID的1字节数组。
  * @param memory_type_id 用于保存存储器类型ID的1字节数组。
  * @param capacity_id 用于保存容量ID的1字节数组。
  * @retval HAL状态，HAL_OK表示三个ID均读取成功。
  */
HAL_StatusTypeDef W25Q64_ReadJedecID(uint8_t manufacturer_id[1],
                                    uint8_t memory_type_id[1],
                                    uint8_t capacity_id[1]);

/**
  * @brief 使用四线数据模式在单个页内编程最多256字节。
  * @param address 页内写入起始地址，范围为0x000000至0x7FFFFF。
  * @param data 指向待写入数据的缓冲区。
  * @param count 待写入字节数，范围为0至256且不能跨页。
  * @retval HAL状态，HAL_OK表示编程完成。
  */
HAL_StatusTypeDef W25Q64_PageProgram(uint32_t address,
                                    const uint8_t *data,
                                    uint16_t count);

/**
  * @brief 擦除指定地址所在的4KB扇区。
  * @param address 扇区内任意地址，范围为0x000000至0x7FFFFF。
  * @retval HAL状态，HAL_OK表示擦除完成。
  */
HAL_StatusTypeDef W25Q64_SectorErase(uint32_t address);

/**
  * @brief 使用四线输出模式从W25Q64连续读取数据。
  * @param address 读取起始地址，范围为0x000000至0x7FFFFF。
  * @param data 指向接收数据的缓冲区。
  * @param count 待读取字节数且地址范围不能超过芯片容量。
  * @retval HAL状态，HAL_OK表示读取成功。
  */
HAL_StatusTypeDef W25Q64_ReadData(uint32_t address,
                                 uint8_t *data,
                                 uint32_t count);

/**
  * @brief 自动轮询状态寄存器并等待W25Q64结束内部操作。
  * @param timeout 等待超时时间，单位为毫秒。
  * @retval HAL状态，HAL_OK表示BUSY位已经清零。
  */
HAL_StatusTypeDef W25Q64_WaitBusy(uint32_t timeout);

#endif
