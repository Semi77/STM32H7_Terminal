#ifndef STM32H7XX_HAL_H
#define STM32H7XX_HAL_H
#include <stdint.h>
typedef struct { uint32_t Prescaler,CounterMode,Period,ClockDivision,AutoReloadPreload; } TIM_Base_InitTypeDef;
typedef struct { void *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
typedef struct { uint32_t APB1CLKDivider; } RCC_ClkInitTypeDef;
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define RCC_HCLK_DIV1 0
#define TIM2 ((void *)2)
#define TIM_COUNTERMODE_UP 0
#define TIM_CLOCKDIVISION_DIV1 0
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0
extern uint32_t mock_counter, mock_mask;
#define __HAL_RCC_TIM2_CLK_ENABLE() ((void)0)
#define __HAL_TIM_GET_COUNTER(handle) (mock_counter)
#define __get_PRIMASK() (mock_mask)
#define __disable_irq() (mock_mask=1)
#define __set_PRIMASK(value) (mock_mask=(value))
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *config,uint32_t *latency);
uint32_t HAL_RCC_GetPCLK1Freq(void);
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *handle);
#endif
