/**
 * @file
 * @brief Host test implementation of the minlin HW hooks that records the transmitted LIN traffic.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/// Symbol recorded for a transmitted BREAK (other symbols are the transmitted bytes).
#define BUS_BREAK 0x100

/// Transmitted symbols.
#define BUS_MAX 512
extern uint16_t g_bus[BUS_MAX];
extern uint32_t g_bus_len;
/// If set, transmit hooks fail without recording anything.
extern bool     g_bus_tx_fail;
/// If set, called with the data of every transmit during the transmit, so that a test can echo it
/// like the LIN transceiver does. @p after_break is set if a BREAK was transmitted before the data.
extern void (*g_bus_live_echo)(const uint8_t* data, uint8_t data_size, bool after_break);

/// Critical section nesting depth, and whether nesting or unbalanced use was detected.
extern int  g_bus_critical_depth;
extern bool g_bus_critical_error;

/// Resets all bus stub state.
void bus_reset(void);
