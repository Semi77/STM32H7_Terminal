#include "stm32h7xx_hal.h"
HAL_StatusTypeDef W25Q64_ReadData(uint32_t, uint8_t *, uint32_t);
HAL_StatusTypeDef W25Q64_PageProgram(uint32_t, const uint8_t *, uint16_t);
HAL_StatusTypeDef W25Q64_SectorErase(uint32_t);
