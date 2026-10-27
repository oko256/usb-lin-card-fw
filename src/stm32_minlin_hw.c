/**
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/usb-lin-card-fw>
 */

#include "minlin/minlin_hw.h"
#include "stm32f0xx_hal.h"
#include "stm32f0xx_hal_uart.h"

// Note: minlin_hw_check_and_clear_break() is implemented in app.c, because the BREAK flag is read
// once per received byte in the receive callback (board_stm32.c) and shared with the LIN monitor.

extern UART_HandleTypeDef huart1;

// PRIMASK state saved on critical section entry; minlin never nests critical sections.
static uint32_t s_primask;

/// Returns a transmit timeout for given number of bytes at current baud rate: transmission time of
/// the bytes plus a possible preceding BREAK (~2 bytes), with 1.5x margin and rounded up, plus 2 ms
/// for the tick granularity.
static uint32_t s_transmit_timeout_ms(uint8_t data_size)
{
    const uint32_t baud_rate = huart1.Init.BaudRate;
    return 2 + (((uint32_t)data_size + 2) * 15000 + baud_rate - 1) / baud_rate;
}

bool minlin_hw_transmit_break(void) { return (HAL_LIN_SendBreak(&huart1) == HAL_OK); }

// Blocking transmit; the HAL timeout relies on SysTick. If minlin_responder_task() is ever called
// from an interrupt instead of the main loop, that interrupt must have lower priority than both
// SysTick and USART1 (USART1 must still receive the echoed bytes while transmitting).
bool minlin_hw_transmit_data(const uint8_t* data, uint8_t data_size)
{
    return (HAL_UART_Transmit(&huart1, data, data_size, s_transmit_timeout_ms(data_size)) == HAL_OK);
}

void minlin_hw_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_primask = primask;
}

void minlin_hw_exit_critical(void) { __set_PRIMASK(s_primask); }
