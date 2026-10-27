/**
 * @file
 * @brief Board support functions; everything that touches the MCU peripherals directly.
 *
 * Implemented for the real hardware in board_stm32.c, and by a stub for host unit tests, so that
 * the application logic (app.c, slcan.c, lin_monitor.c) can be tested on the host.
 *
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/usb-lin-card-fw>
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/// Length of the MCU unique ID in bytes.
#define BOARD_UNIQUE_ID_LEN 12

/// Returns the millisecond tick counter.
uint32_t board_tick_ms(void);

/// Disables interrupts and returns the previous interrupt state for @c board_irq_restore().
uint32_t board_irq_save(void);
/// Restores the interrupt state returned by @c board_irq_save().
void     board_irq_restore(uint32_t state);

/// Starts (or restarts) receiving LIN bytes. Each received byte is passed to @c app_lin_rx_byte().
void     board_lin_start_receive(void);
/// Reconfigures the LIN interface with another baud rate and restarts receiving.
void     board_set_lin_baud_rate(uint32_t baud_rate);
/// Returns the current LIN baud rate.
uint32_t board_lin_baud_rate(void);

/// Reads the MCU unique ID to be used as the serial number.
void board_read_unique_id(uint8_t out[BOARD_UNIQUE_ID_LEN]);

/// Makes sure option bytes are set to boot to the application; call first thing in main().
void board_bootloader_switcher(void);
/// Reboots into the system memory USB bootloader. Does not return on real HW.
void board_reboot_to_bootloader(void);

/// Turns the status LED on or off.
void board_set_led(bool on);

/// Queues data to be sent over USB. Returns false if the data did not fit.
bool board_usb_write(const uint8_t* data, uint32_t len);
