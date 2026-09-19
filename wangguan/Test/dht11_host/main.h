#ifndef DHT11_HOST_MAIN_H
#define DHT11_HOST_MAIN_H

#include <stddef.h>
#include <stdint.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t CTRL, CYCCNT, LAR; } DWT_Type;
typedef struct { uint32_t DEMCR; } CoreDebug_Type;
extern CoreDebug_Type mock_core_debug;
extern uint32_t SystemCoreClock;
DWT_Type *Mock_DWT(void);
uint32_t Mock_GetPrimask(void);
void Mock_SetPrimask(uint32_t value);

#define DWT Mock_DWT()
#define CoreDebug (&mock_core_debug)
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define DHT_GPIO_Port ((void *)0)
#define DHT_Pin 32U
#define __get_PRIMASK() Mock_GetPrimask()
#define __disable_irq() Mock_SetPrimask(1U)
#define __set_PRIMASK(value) Mock_SetPrimask(value)
#define __DSB() ((void)0)
#define __ISB() ((void)0)
#define __NOP() ((void)0)

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
void HAL_GPIO_WritePin(void *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin);

#endif
