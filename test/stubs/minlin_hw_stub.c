#include "bus_stub.h"
#include "minlin/minlin_hw.h"
#include <stddef.h>

uint16_t g_bus[BUS_MAX];
uint32_t g_bus_len                                                                = 0;
bool     g_bus_tx_fail                                                            = false;
void (*g_bus_live_echo)(const uint8_t* data, uint8_t data_size, bool after_break) = NULL;
int  g_bus_critical_depth                                                         = 0;
bool g_bus_critical_error                                                         = false;

/// A BREAK was transmitted and the data following it hasn't been transmitted yet.
static bool s_break_pending = false;

void bus_reset(void)
{
    g_bus_len            = 0;
    g_bus_tx_fail        = false;
    g_bus_live_echo      = NULL;
    s_break_pending      = false;
    g_bus_critical_depth = 0;
    g_bus_critical_error = false;
}

static void s_record(uint16_t symbol)
{
    if (g_bus_len < BUS_MAX) {
        g_bus[g_bus_len] = symbol;
        g_bus_len++;
    }
}

bool minlin_hw_transmit_break(void)
{
    if (g_bus_tx_fail) {
        return false;
    }
    s_record(BUS_BREAK);
    s_break_pending = true;
    return true;
}

bool minlin_hw_transmit_data(const uint8_t* data, uint8_t data_size)
{
    if (g_bus_tx_fail || g_bus_critical_depth != 0) {
        return false;
    }
    for (uint8_t i = 0; i < data_size; ++i) {
        s_record(data[i]);
    }
    if (g_bus_live_echo != NULL) {
        g_bus_live_echo(data, data_size, s_break_pending);
    }
    s_break_pending = false;
    return true;
}

void minlin_hw_enter_critical(void)
{
    if (g_bus_critical_depth != 0) {
        g_bus_critical_error = true;
    }
    g_bus_critical_depth++;
}

void minlin_hw_exit_critical(void)
{
    if (g_bus_critical_depth != 1) {
        g_bus_critical_error = true;
    }
    g_bus_critical_depth--;
}
