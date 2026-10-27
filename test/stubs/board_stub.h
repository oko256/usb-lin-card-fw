/**
 * @file
 * @brief Host test implementation of board.h with inspectable state.
 */
#pragma once

#include "board.h"
#include <stdbool.h>
#include <stdint.h>

/// Current tick returned by board_tick_ms().
extern uint32_t g_board_tick_ms;
/// Current LIN baud rate.
extern uint32_t g_board_baud_rate;
/// Number of board_lin_start_receive() and board_set_lin_baud_rate() calls.
extern int      g_board_receive_starts;
extern int      g_board_baud_rate_sets;
/// LED state.
extern bool     g_board_led;
/// Set when board_reboot_to_bootloader() was called.
extern bool     g_board_rebooted_to_bootloader;

/// Interrupt save/restore nesting depth, and whether unbalanced use was detected.
extern int  g_board_irq_depth;
extern bool g_board_irq_error;

/// Captured USB output as a NUL-terminated string.
#define USB_OUT_MAX 2048
extern char     g_usb_out[USB_OUT_MAX + 1];
extern uint32_t g_usb_out_len;
/// If set, board_usb_write() fails.
extern bool     g_usb_write_fail;

/// Resets all board stub state.
void board_stub_reset(void);
/// Clears captured USB output.
void usb_out_clear(void);
