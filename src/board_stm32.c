/**
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/usb-lin-card-fw>
 */

#include "app.h"
#include "board.h"
#include "main.h"
#include "slcan.h"
#include "usbd_cdc_if.h"
#include "usbd_desc.h"

/// Brightness (PWM compare value) of the LED when it's on.
#define LED_ON_BRIGHTNESS 10

extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef  htim3;

/// Receive buffer for one LIN byte.
static uint8_t s_lin_rx_byte = 0;

uint32_t board_tick_ms(void) { return HAL_GetTick(); }

uint32_t board_irq_save(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

void board_irq_restore(uint32_t state) { __set_PRIMASK(state); }

void board_lin_start_receive(void) { HAL_UART_Receive_IT(&huart1, &s_lin_rx_byte, 1); }

void board_set_lin_baud_rate(uint32_t baud_rate)
{
    // Abort any possible on-going receive operation.
    HAL_UART_AbortReceive_IT(&huart1);

    // Reconfigure with new baud rate.
    huart1.Init.BaudRate = baud_rate;
    if (HAL_LIN_Init(&huart1, UART_LINBREAKDETECTLENGTH_11B) != HAL_OK) {
        Error_Handler();
    }

    // Resume receiving LIN traffic.
    board_lin_start_receive();
}

uint32_t board_lin_baud_rate(void) { return huart1.Init.BaudRate; }

void board_read_unique_id(uint8_t out[BOARD_UNIQUE_ID_LEN])
{
    const uint32_t words[3] = {
        *(volatile uint32_t*)DEVICE_ID3,
        *(volatile uint32_t*)DEVICE_ID2,
        *(volatile uint32_t*)DEVICE_ID1,
    };
    const uint8_t* bytes = (const uint8_t*)words;
    for (uint8_t i = 0; i < BOARD_UNIQUE_ID_LEN; ++i) {
        out[i] = bytes[i];
    }
}

void board_bootloader_switcher(void)
{
    FLASH_OBProgramInitTypeDef obparam;
    HAL_FLASHEx_OBGetConfig(&obparam);

    if (((obparam.USERConfig & OB_BOOT0_SET) == 0) || ((obparam.USERConfig & OB_BOOT_SEL_SET) == OB_BOOT_SEL_SET)) {
        obparam.OptionType = OPTIONBYTE_USER;
        obparam.USERConfig |= OB_BOOT0_SET;
        obparam.USERConfig &= (uint8_t)(~OB_BOOT_SEL_SET);

        HAL_FLASH_Unlock();
        HAL_FLASH_OB_Unlock();
        HAL_FLASHEx_OBErase();
        HAL_FLASHEx_OBProgram(&obparam);
        HAL_FLASH_OB_Lock();
        HAL_FLASH_OB_Launch();
    }
}

void board_reboot_to_bootloader(void)
{
    FLASH_OBProgramInitTypeDef obparam;
    HAL_FLASHEx_OBGetConfig(&obparam);

    obparam.OptionType = OPTIONBYTE_USER;
    // Reset nBOOT0 (boot from system memory) and BOOT_SEL (use nBOOT0 bit instead of the BOOT0 pin),
    // keeping the other user option bits as they are.
    obparam.USERConfig &= (uint8_t)(~(OB_BOOT0_SET | OB_BOOT_SEL_SET));

    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();
    HAL_FLASHEx_OBErase();
    HAL_FLASHEx_OBProgram(&obparam);
    HAL_FLASH_OB_Lock();
    HAL_FLASH_Lock();
    HAL_FLASH_OB_Launch();
}

void board_set_led(bool on) { __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, on ? LED_ON_BRIGHTNESS : 0); }

bool board_usb_write(const uint8_t* data, uint32_t len) { return CDC_Transmit_NonBlocking(data, len); }

void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart)
{
    if (huart->Instance != USART1) {
        return;
    }
    if ((huart->ErrorCode & HAL_UART_ERROR_ORE) != 0U) {
        // Previous byte(s) were lost; this byte is still valid.
        slcan_latch_error_flag(SLCAN_ERROR_RX_OVERRUN);
    }
    if ((huart->ErrorCode & HAL_UART_ERROR_NE) != 0U) {
        // Reject bytes with noise errors, and latch noise error flag for diagnostics purposes.
        slcan_latch_error_flag(SLCAN_ERROR_NOISE);
    } else if ((huart->ErrorCode & HAL_UART_ERROR_FE) == 0U) {
        // Reject bytes with framing errors: BREAK always causes a framing error with received
        // byte 0x00, which is a false byte that we don't want to process. The BREAK flag is left
        // set for the following SYNC byte.
        const bool is_break = (__HAL_UART_GET_FLAG(huart, UART_FLAG_LBDF) != RESET);
        if (is_break) {
            __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_LBDF);
        }
        app_lin_rx_byte(s_lin_rx_byte, is_break);
    }
    board_lin_start_receive();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
    if (huart->Instance != USART1) {
        return;
    }
    // Blocking errors (e.g. overrun without received data) abort the reception: restart it.
    if (huart->RxState == HAL_UART_STATE_READY) {
        slcan_latch_error_flag(SLCAN_ERROR_RX_OVERRUN);
        board_lin_start_receive();
    }
}
