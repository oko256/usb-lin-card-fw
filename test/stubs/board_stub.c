#include "board_stub.h"
#include <string.h>

uint32_t g_board_tick_ms                = 0;
uint32_t g_board_baud_rate              = 19200;
int      g_board_receive_starts         = 0;
int      g_board_baud_rate_sets         = 0;
bool     g_board_led                    = false;
bool     g_board_rebooted_to_bootloader = false;
int      g_board_irq_depth              = 0;
bool     g_board_irq_error              = false;
char     g_usb_out[USB_OUT_MAX + 1];
uint32_t g_usb_out_len    = 0;
bool     g_usb_write_fail = false;

void board_stub_reset(void)
{
    g_board_tick_ms                = 0;
    g_board_baud_rate              = 19200;
    g_board_receive_starts         = 0;
    g_board_baud_rate_sets         = 0;
    g_board_led                    = false;
    g_board_rebooted_to_bootloader = false;
    g_board_irq_depth              = 0;
    g_board_irq_error              = false;
    g_usb_write_fail               = false;
    usb_out_clear();
}

void usb_out_clear(void)
{
    memset(g_usb_out, 0, sizeof(g_usb_out));
    g_usb_out_len = 0;
}

uint32_t board_tick_ms(void) { return g_board_tick_ms; }

uint32_t board_irq_save(void)
{
    g_board_irq_depth++;
    return (uint32_t)(g_board_irq_depth - 1);
}

void board_irq_restore(uint32_t state)
{
    if ((uint32_t)(g_board_irq_depth - 1) != state) {
        g_board_irq_error = true;
    }
    g_board_irq_depth--;
}

void board_lin_start_receive(void) { g_board_receive_starts++; }

void board_set_lin_baud_rate(uint32_t baud_rate)
{
    g_board_baud_rate = baud_rate;
    g_board_baud_rate_sets++;
    board_lin_start_receive();
}

uint32_t board_lin_baud_rate(void) { return g_board_baud_rate; }

void board_read_unique_id(uint8_t out[BOARD_UNIQUE_ID_LEN])
{
    for (uint8_t i = 0; i < BOARD_UNIQUE_ID_LEN; ++i) {
        out[i] = (uint8_t)(0xA0 + i);
    }
}

void board_bootloader_switcher(void) { }

void board_reboot_to_bootloader(void) { g_board_rebooted_to_bootloader = true; }

void board_set_led(bool on) { g_board_led = on; }

bool board_usb_write(const uint8_t* data, uint32_t len)
{
    if (g_usb_write_fail || g_usb_out_len + len > USB_OUT_MAX) {
        return false;
    }
    memcpy(&g_usb_out[g_usb_out_len], data, len);
    g_usb_out_len += len;
    return true;
}
