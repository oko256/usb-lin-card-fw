/**
 * @file
 * @brief USB LIN Card application logic: connects slcan commands, minlin, and the LIN monitor.
 *
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/usb-lin-card-fw>
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/// Initializes the application; call once after the peripherals have been initialized.
void app_init(void);

/// Runs one iteration of the application; call continuously from the main loop.
void app_poll(void);

/// Processes one received LIN byte; call from the LIN receive interrupt for each valid byte.
/// @p is_break tells if BREAK was detected before or with this byte.
void app_lin_rx_byte(uint8_t received_byte, bool is_break);
