/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
  * @brief 在任务栈溢出时关闭中断并停机以便调试定位。
  * @param task_handle 发生栈溢出的任务句柄。
  * @param task_name 发生栈溢出的任务名称。
  * @retval 无。
  */
void vApplicationStackOverflowHook(TaskHandle_t task_handle, char *task_name)
{
  (void)task_handle;
  (void)task_name;
  taskDISABLE_INTERRUPTS();
  for (;;)
  {
  }
}

/**
  * @brief 在FreeRTOS动态内存分配失败时关闭中断并停机。
  * @retval 无。
  */
void vApplicationMallocFailedHook(void)
{
  taskDISABLE_INTERRUPTS();
  for (;;)
  {
  }
}

/**
  * @brief 为FreeRTOS空闲任务提供静态控制块和任务栈。
  * @param task_tcb 输出空闲任务控制块地址。
  * @param task_stack 输出空闲任务栈地址。
  * @param stack_size 输出任务栈的字数。
  * @retval 无。
  */
void vApplicationGetIdleTaskMemory(StaticTask_t **task_tcb,
                                   StackType_t **task_stack,
                                   uint32_t *stack_size)
{
  static StaticTask_t idle_tcb;
  static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
  *task_tcb = &idle_tcb;
  *task_stack = idle_stack;
  *stack_size = configMINIMAL_STACK_SIZE;
}

/**
  * @brief 为FreeRTOS定时器任务提供静态控制块和任务栈。
  * @param task_tcb 输出定时器任务控制块地址。
  * @param task_stack 输出定时器任务栈地址。
  * @param stack_size 输出任务栈的字数。
  * @retval 无。
  */
void vApplicationGetTimerTaskMemory(StaticTask_t **task_tcb,
                                    StackType_t **task_stack,
                                    uint32_t *stack_size)
{
  static StaticTask_t timer_tcb;
  static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH];
  *task_tcb = &timer_tcb;
  *task_stack = timer_stack;
  *stack_size = configTIMER_TASK_STACK_DEPTH;
}

/* USER CODE END Application */

