#include "app.h"
#include "board_stub.h"
#include "bus_stub.h"
#include "slcan.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

#define OK  "\r"
#define ERR "\a"

/// How many of the transmitted bus symbols have been echoed back to the receiver.
static uint32_t s_echoed = 0;

void setUp(void)
{
    board_stub_reset();
    bus_reset();
    s_echoed = 0;
    slcan_init();
    app_init();
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL(0, g_board_irq_depth);
    TEST_ASSERT_FALSE(g_board_irq_error);
    TEST_ASSERT_EQUAL(0, g_bus_critical_depth);
    TEST_ASSERT_FALSE(g_bus_critical_error);
}

/// Sends a command line and runs one application iteration to process it.
static void s_cmd(const char* line, const char* expected_reply)
{
    usb_out_clear();
    TEST_ASSERT_TRUE(slcan_append_input((const uint8_t*)line, (uint32_t)strlen(line)));
    TEST_ASSERT_TRUE(slcan_append_input((const uint8_t*)"\r", 1));
    app_poll();
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected_reply, g_usb_out, line);
}

/// Advances time and runs one application iteration.
static void s_poll_at(uint32_t tick_ms)
{
    g_board_tick_ms = tick_ms;
    app_poll();
}

/// Feeds the transmitted bus symbols back to the receiver like the LIN transceiver echo does:
/// the BREAK flag is set when the following (SYNC) byte is received.
static void s_echo(void)
{
    bool is_break = false;
    for (; s_echoed < g_bus_len; ++s_echoed) {
        if (g_bus[s_echoed] == BUS_BREAK) {
            is_break = true;
        } else {
            app_lin_rx_byte((uint8_t)g_bus[s_echoed], is_break);
            is_break = false;
        }
    }
}

/// Discards transmitted bus symbols (without echoing them).
static void s_bus_clear(void)
{
    g_bus_len = 0;
    s_echoed  = 0;
}

/// Receives a header sent by another node.
static void s_rx_header(uint8_t id)
{
    app_lin_rx_byte(MINLIN_SYNC_BYTE, true);
    app_lin_rx_byte(minlin_id_to_pid(id, NULL), false);
}

/// Receives a response sent by another node, with checksum calculated with given config.
static void s_rx_response(uint8_t id, const uint8_t* data, uint8_t len, bool classic)
{
    const minlin_config config = {.classic_checksum = classic};
    for (uint8_t i = 0; i < len; ++i) {
        app_lin_rx_byte(data[i], false);
    }
    app_lin_rx_byte(minlin_checksum(&config, id, data, len, NULL), false);
}

/// Asserts that the bus contains exactly a header for given ID, followed by given response bytes.
static void s_assert_bus_frame(uint8_t id, const uint8_t* response, uint8_t response_len)
{
    TEST_ASSERT_EQUAL(3 + response_len, g_bus_len);
    TEST_ASSERT_EQUAL_HEX16(BUS_BREAK, g_bus[0]);
    TEST_ASSERT_EQUAL_HEX16(MINLIN_SYNC_BYTE, g_bus[1]);
    TEST_ASSERT_EQUAL_HEX16(minlin_id_to_pid(id, NULL), g_bus[2]);
    for (uint8_t i = 0; i < response_len; ++i) {
        TEST_ASSERT_EQUAL_HEX16(response[i], g_bus[3 + i]);
    }
}

static uint8_t s_read_flags(void)
{
    usb_out_clear();
    TEST_ASSERT_TRUE(slcan_append_input((const uint8_t*)"F\r", 2));
    app_poll();
    TEST_ASSERT_EQUAL_MESSAGE(4, strlen(g_usb_out), g_usb_out);
    unsigned flags = 0;
    TEST_ASSERT_EQUAL(1, sscanf(g_usb_out, "F%02X\r", &flags));
    return (uint8_t)flags;
}

void test_schedule_and_subscription(void)
{
    s_cmd("R0120000A2", OK);
    s_cmd("O", OK);

    s_poll_at(0);
    s_assert_bus_frame(0x12, NULL, 0);
    s_echo();
    // Another node responds; the response is reported to the client.
    const uint8_t data[2] = {0x10, 0x20};
    s_rx_response(0x12, data, 2, false);
    usb_out_clear();
    s_poll_at(1);
    TEST_ASSERT_EQUAL_STRING("t01221020" OK, g_usb_out);

    // Next header is sent when the 10 ms frame slot has passed.
    s_bus_clear();
    s_poll_at(9);
    TEST_ASSERT_EQUAL(0, g_bus_len);
    s_poll_at(10);
    s_assert_bus_frame(0x12, NULL, 0);
    TEST_ASSERT_EQUAL(0, s_read_flags());
}

void test_command_frame(void)
{
    s_cmd("T0120000A2AABB", OK);
    s_cmd("O", OK);
    s_poll_at(0);
    s_echo();
    s_poll_at(0);
    const uint8_t       payload[2]  = {0xAA, 0xBB};
    const minlin_config config      = {0};
    const uint8_t       response[3] = {0xAA, 0xBB, minlin_checksum(&config, 0x12, payload, 2, NULL)};
    s_assert_bus_frame(0x12, response, 3);
    // Own response is not reported to the client.
    s_echo();
    usb_out_clear();
    s_poll_at(1);
    TEST_ASSERT_EQUAL_STRING("", g_usb_out);
    TEST_ASSERT_EQUAL(0, s_read_flags());
}

void test_single_frame_in_open_mode(void)
{
    s_cmd("R0010000A1", OK);
    s_cmd("O", OK);
    s_poll_at(0);
    s_assert_bus_frame(0x01, NULL, 0);
    s_bus_clear();

    // Single frame is sent when the current frame slot has passed.
    s_cmd("t0122AABB", OK);
    TEST_ASSERT_TRUE(slcan_single_frame_pending());
    s_poll_at(9);
    TEST_ASSERT_EQUAL(0, g_bus_len);
    s_poll_at(10);
    s_echo();
    s_poll_at(10);
    const uint8_t       payload[2]  = {0xAA, 0xBB};
    const minlin_config config      = {0};
    const uint8_t       response[3] = {0xAA, 0xBB, minlin_checksum(&config, 0x12, payload, 2, NULL)};
    s_assert_bus_frame(0x12, response, 3);
    s_echo();
    s_bus_clear();

    // Schedule resumes after the single frame's slot (7 ms for 2 bytes at 19200).
    s_poll_at(16);
    TEST_ASSERT_TRUE(slcan_single_frame_pending());
    TEST_ASSERT_EQUAL(0, g_bus_len);
    s_poll_at(17);
    TEST_ASSERT_FALSE(slcan_single_frame_pending());
    s_poll_at(18);
    s_assert_bus_frame(0x01, NULL, 0);
}

void test_single_frame_resumes_schedule_without_echo(void)
{
    // Regression: schedule resume used to wait for the responder to become idle, which never
    // happened if the echo of the header was lost.
    s_cmd("R0010000A1", OK);
    s_cmd("O", OK);
    // The iteration processing the command sends the first scheduled header before it.
    s_cmd("t0122AABB", OK);
    s_assert_bus_frame(0x01, NULL, 0);
    s_bus_clear();
    s_poll_at(10);
    s_assert_bus_frame(0x12, NULL, 0);
    app_lin_rx_byte(MINLIN_SYNC_BYTE, true); // Echo of the PID is lost.
    s_bus_clear();
    s_poll_at(16);
    TEST_ASSERT_TRUE(slcan_single_frame_pending());
    s_poll_at(17);
    TEST_ASSERT_FALSE(slcan_single_frame_pending());
    s_poll_at(18);
    s_assert_bus_frame(0x01, NULL, 0);
}

void test_single_frame_in_monitor_mode(void)
{
    s_cmd("L", OK);
    s_cmd("t0122AABB", OK);
    s_poll_at(100);
    s_echo();
    s_poll_at(100);
    TEST_ASSERT_EQUAL(6, g_bus_len);
    s_echo();
    // The monitor reports the frame we sent, after the byte timeout.
    usb_out_clear();
    s_poll_at(103);
    TEST_ASSERT_EQUAL_STRING("t0122AABB" OK, g_usb_out);

    // When done, nothing more is transmitted, but monitoring continues.
    s_poll_at(107);
    TEST_ASSERT_FALSE(slcan_single_frame_pending());
    s_bus_clear();
    s_poll_at(500);
    TEST_ASSERT_EQUAL(0, g_bus_len);
    s_rx_header(0x20);
    usb_out_clear();
    s_poll_at(510);
    TEST_ASSERT_EQUAL_STRING("t0200" OK, g_usb_out);
    TEST_ASSERT_EQUAL(0, s_read_flags());
}

/// Byte whose echo is still being received after the transmit returned (-1 if none).
static int16_t s_late_echo_byte = -1;

/// Echoes a transmit like on real HW: the tick advances while the bytes are transmitted and
/// received, and the echo of the last byte completes only after the blocking transmit has returned.
static void s_live_echo(const uint8_t* data, uint8_t data_size, bool after_break)
{
    for (uint8_t i = 0; i + 1 < data_size; ++i) {
        g_board_tick_ms++;
        app_lin_rx_byte(data[i], after_break && i == 0);
    }
    s_late_echo_byte = data[data_size - 1];
}

/// Completes the reception of the last echoed byte.
static void s_finish_late_echo(void)
{
    if (s_late_echo_byte >= 0) {
        g_board_tick_ms++;
        app_lin_rx_byte((uint8_t)s_late_echo_byte, false);
        s_late_echo_byte = -1;
    }
}

void test_single_frame_in_monitor_mode_with_real_echo_timing(void)
{
    // Regression: the monitor timeout compared the receive time of the bytes to the tick read at
    // the start of the main loop iteration. Bytes received during the blocking transmit looked like
    // they were from the future, so the frame was completed before its checksum was received.
    g_bus_live_echo  = s_live_echo;
    s_late_echo_byte = -1;
    s_cmd("L", OK);
    s_cmd("t0123010203", OK);
    usb_out_clear();
    g_board_tick_ms = 100;
    for (int i = 0; i < 20; ++i) {
        app_poll();
        s_finish_late_echo();
        g_board_tick_ms++;
    }
    TEST_ASSERT_EQUAL(7, g_bus_len);
    TEST_ASSERT_EQUAL_STRING("t0123010203" OK, g_usb_out);
    TEST_ASSERT_FALSE(slcan_single_frame_pending());
    TEST_ASSERT_EQUAL(0, s_read_flags());
}

void test_closed_channel_ignores_bus(void)
{
    // Regression: bytes received while closed were still processed by the responder, and a stale
    // response could be transmitted when the channel was opened again.
    s_cmd("t1122AABB", OK);
    s_rx_header(0x12);
    s_poll_at(1);
    s_cmd("O", OK);
    s_poll_at(2);
    s_poll_at(3);
    TEST_ASSERT_EQUAL(0, g_bus_len);

    // Responds when open.
    s_rx_header(0x12);
    s_poll_at(4);
    TEST_ASSERT_EQUAL(3, g_bus_len);
}

void test_no_stale_header_state_after_reopen(void)
{
    // Regression: closing the channel in the middle of a header left the responder waiting for the
    // PID, so the first BREAK after opening again was reported as an unexpected BREAK.
    s_cmd("O", OK);
    app_lin_rx_byte(MINLIN_SYNC_BYTE, true);
    s_cmd("C", OK);
    s_cmd("O", OK);
    s_rx_header(0x12);
    s_poll_at(1);
    TEST_ASSERT_EQUAL(0, s_read_flags());

    // Same when switching to monitoring mode and back.
    app_lin_rx_byte(MINLIN_SYNC_BYTE, true);
    s_cmd("C", OK);
    s_cmd("L", OK);
    s_cmd("C", OK);
    s_cmd("O", OK);
    s_rx_header(0x12);
    s_poll_at(2);
    TEST_ASSERT_EQUAL(0, s_read_flags());
}

void test_monitor_mode_reports_frames(void)
{
    const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    s_cmd("L", OK);
    g_board_tick_ms = 50;
    s_rx_header(0x05);
    s_rx_response(0x05, data, 7, false);
    usb_out_clear();
    s_poll_at(52);
    TEST_ASSERT_EQUAL_STRING("", g_usb_out);
    s_poll_at(53);
    TEST_ASSERT_EQUAL_STRING("t005701020304050607" OK, g_usb_out);

    // Full frame is reported without waiting for the timeout.
    s_rx_header(0x06);
    s_rx_response(0x06, data, 8, false);
    usb_out_clear();
    s_poll_at(53);
    TEST_ASSERT_EQUAL_STRING("t00680102030405060708" OK, g_usb_out);

    // Closing stops monitoring.
    s_cmd("C", OK);
    s_rx_header(0x07);
    usb_out_clear();
    s_poll_at(100);
    TEST_ASSERT_EQUAL_STRING("", g_usb_out);
}

void test_checksum_config(void)
{
    const uint8_t data[2] = {0x10, 0x20};
    s_cmd("r1122", OK);
    s_cmd("x2", OK);
    s_cmd("O", OK);
    s_rx_header(0x12);
    s_rx_response(0x12, data, 2, true);
    usb_out_clear();
    s_poll_at(1);
    TEST_ASSERT_EQUAL_STRING("t01221020" OK, g_usb_out);

    // Enhanced checksum response is now a checksum error.
    s_rx_header(0x12);
    s_rx_response(0x12, data, 2, false);
    usb_out_clear();
    s_poll_at(2);
    TEST_ASSERT_EQUAL_STRING("", g_usb_out);
    TEST_ASSERT_EQUAL(SLCAN_ERROR_LIN_FRAME, s_read_flags());
}

void test_error_flags(void)
{
    // Transmit failure.
    s_cmd("R0120000A1", OK);
    s_cmd("O", OK);
    g_bus_tx_fail = true;
    s_poll_at(0);
    g_bus_tx_fail = false;
    TEST_ASSERT_EQUAL(SLCAN_ERROR_LIN_TX_FAILED, s_read_flags());

    // Subscribed frame received but not yet reported when the next frame starts: data overrun.
    const uint8_t data[1] = {0x42};
    s_rx_header(0x12);
    s_rx_response(0x12, data, 1, false);
    s_rx_header(0x30);
    TEST_ASSERT_EQUAL(SLCAN_ERROR_DATA_OVERRUN, s_read_flags());

    // Missing response to a subscribed frame.
    s_rx_header(0x12);
    s_rx_header(0x30);
    TEST_ASSERT_EQUAL(SLCAN_ERROR_LIN_FRAME, s_read_flags());

    // Monitor overrun: second frame completes before the first one has been reported.
    s_cmd("C", OK);
    s_cmd("L", OK);
    const uint8_t full[8] = {0};
    s_rx_header(0x01);
    s_rx_response(0x01, full, 8, false);
    s_rx_header(0x02);
    s_rx_response(0x02, full, 8, false);
    usb_out_clear();
    s_poll_at(1);
    TEST_ASSERT_EQUAL_STRING("t00180000000000000000" OK, g_usb_out);
    TEST_ASSERT_EQUAL(SLCAN_ERROR_DATA_OVERRUN, s_read_flags());
}

void test_reload_is_deferred_during_single_frame(void)
{
    s_cmd("O", OK);
    s_cmd("t0122AABB", OK);
    s_cmd("r0011", OK);
    s_poll_at(0);
    s_assert_bus_frame(0x12, NULL, 0);
    s_bus_clear();
    // The new schedule entry is applied after the single frame.
    s_poll_at(7);
    s_poll_at(8);
    s_assert_bus_frame(0x01, NULL, 0);
}

void test_bootloader(void)
{
    s_cmd("boot", "");
    TEST_ASSERT_TRUE(g_board_rebooted_to_bootloader);
}

void test_led(void)
{
    s_poll_at(0);
    TEST_ASSERT_TRUE(g_board_led);
    s_poll_at(30);
    TEST_ASSERT_FALSE(g_board_led);
    s_poll_at(200);
    TEST_ASSERT_FALSE(g_board_led);
    s_poll_at(2000);
    TEST_ASSERT_TRUE(g_board_led);
    // Blinks faster for a while after receive activity.
    app_lin_rx_byte(0x00, false);
    s_poll_at(2100);
    TEST_ASSERT_FALSE(g_board_led);
    s_poll_at(2150);
    TEST_ASSERT_TRUE(g_board_led);
    s_poll_at(3100);
    TEST_ASSERT_FALSE(g_board_led);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_schedule_and_subscription);
    RUN_TEST(test_command_frame);
    RUN_TEST(test_single_frame_in_open_mode);
    RUN_TEST(test_single_frame_resumes_schedule_without_echo);
    RUN_TEST(test_single_frame_in_monitor_mode);
    RUN_TEST(test_single_frame_in_monitor_mode_with_real_echo_timing);
    RUN_TEST(test_closed_channel_ignores_bus);
    RUN_TEST(test_no_stale_header_state_after_reopen);
    RUN_TEST(test_monitor_mode_reports_frames);
    RUN_TEST(test_checksum_config);
    RUN_TEST(test_error_flags);
    RUN_TEST(test_reload_is_deferred_during_single_frame);
    RUN_TEST(test_bootloader);
    RUN_TEST(test_led);
    return UNITY_END();
}
