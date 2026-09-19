#ifndef MODBUS_HOST_HAL_H
#define MODBUS_HOST_HAL_H
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t BaudRate, WordLength, Parity, StopBits; } UART_InitTypeDef;
typedef struct { void *Instance; UART_InitTypeDef Init; } UART_HandleTypeDef;
typedef struct { uint32_t CYCCNT, CTRL, LAR; } DWT_Type;
typedef struct { uint32_t DEMCR; } CoreDebug_Type;
extern DWT_Type mock_dwt;
extern CoreDebug_Type mock_debug;
extern uint32_t SystemCoreClock;
#define DWT (&mock_dwt)
#define CoreDebug (&mock_debug)
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define USART2 ((void *)2)
#define UART_WORDLENGTH_8B 8U
#define UART_PARITY_NONE 0U
#define UART_STOPBITS_1 1U
#define GPIO_PIN_RESET 0U
#define GPIO_PIN_SET 1U
#define UART_CLEAR_OREF 1U
#define UART_CLEAR_NEF 2U
#define UART_CLEAR_FEF 4U
#define UART_CLEAR_PEF 8U
#define UART_RXDATA_FLUSH_REQUEST 1U
#define __HAL_UART_CLEAR_FLAG(u, f) ((void)(u), (void)(f))
#define __HAL_UART_SEND_REQ(u, f) ((void)(u), (void)(f))
#define __get_IPSR() 0U
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(void *port, uint16_t pin, uint32_t state);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t count);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, const uint8_t *data, uint16_t count);
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *uart);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart);
#endif
