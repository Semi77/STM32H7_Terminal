#ifndef STM32H7XX_HAL_H
#define STM32H7XX_HAL_H
#include <stdint.h>
typedef struct { int unused; } UART_HandleTypeDef;
uint32_t HAL_GetTick(void);
int HAL_UART_Transmit(UART_HandleTypeDef *uart,const uint8_t *data,uint16_t size,uint32_t timeout);
void HAL_Delay(uint32_t delay);
void NVIC_SystemReset(void);
#endif
