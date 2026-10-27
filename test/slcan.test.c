#include "slcan.h"
#include "board_stub.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

#define OK  "\r"
#define ERR "\a"

void setUp(void)
{
    board_stub_reset();
    slcan_init();
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL(0, g_board_irq_depth);
    TEST_ASSERT_FALSE(g_board_irq_error);
}

/// Sends one command line (CR appended) and processes it. Clears previous USB output first.
static slcan_cmd s_cmd(const char* line)
{
    usb_out_clear();
    TEST_ASSERT_TRUE(slcan_append_input((const uint8_t*)line, (uint32_t)strlen(line)));
    TEST_ASSERT_TRUE(slcan_append_input((const uint8_t*)"\r", 1));
    return slcan_process_input();
}

/// Asserts that command line gets given reply and returns given command.
static void s_expect(const char* line, const char* reply, slcan_cmd cmd)
{
    const slcan_cmd res = s_cmd(line);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(reply, g_usb_out, line);
    TEST_ASSERT_EQUAL_MESSAGE(cmd, res, line);
}

static uint8_t s_schedule_count(void)
{
    uint8_t count = 0;
    slcan_schedule(&count);
    return count;
}

static uint8_t s_frames_count(void)
{
    uint8_t count = 0;
    slcan_responder_frames(&count);
    return count;
}

void test_open_close_monitor(void)
{
    TEST_ASSERT_EQUAL(SLCAN_MODE_CLOSED, slcan_get_mode());
    s_expect("C", ERR, SLCAN_CMD_NONE);
    s_expect("O", OK, SLCAN_CMD_OPEN);
    TEST_ASSERT_EQUAL(SLCAN_MODE_OPEN, slcan_get_mode());
    s_expect("O", ERR, SLCAN_CMD_NONE);
    s_expect("L", ERR, SLCAN_CMD_NONE);
    s_expect("C", OK, SLCAN_CMD_CLOSE);
    TEST_ASSERT_EQUAL(SLCAN_MODE_CLOSED, slcan_get_mode());
    s_expect("L", OK, SLCAN_CMD_START_MONITORING);
    TEST_ASSERT_EQUAL(SLCAN_MODE_MONITOR, slcan_get_mode());
    s_expect("O", ERR, SLCAN_CMD_NONE);
    s_expect("C", OK, SLCAN_CMD_CLOSE);
    TEST_ASSERT_EQUAL(SLCAN_MODE_CLOSED, slcan_get_mode());
}

void test_version_and_serial_number(void)
{
    s_expect("V", "V0301" OK, SLCAN_CMD_NONE);
    s_expect("N", "NA0A1A2A3A4A5A6A7A8A9AAAB" OK, SLCAN_CMD_NONE);
}

void test_status_flags_read_and_clear(void)
{
    s_expect("F", "F00" OK, SLCAN_CMD_NONE);
    slcan_latch_error_flag(SLCAN_ERROR_LIN_FRAME);
    slcan_latch_error_flag(SLCAN_ERROR_RX_OVERRUN);
    s_expect("F", "F81" OK, SLCAN_CMD_NONE);
    s_expect("F", "F00" OK, SLCAN_CMD_NONE);
}

void test_set_baud_rate(void)
{
    s_expect("S1", OK, SLCAN_CMD_NONE);
    TEST_ASSERT_EQUAL(2400, g_board_baud_rate);
    s_expect("S2", OK, SLCAN_CMD_NONE);
    TEST_ASSERT_EQUAL(9600, g_board_baud_rate);
    s_expect("S3", OK, SLCAN_CMD_NONE);
    TEST_ASSERT_EQUAL(10400, g_board_baud_rate);
    // Any other preset selects the most common LIN baud rate.
    s_expect("S1", OK, SLCAN_CMD_NONE);
    s_expect("S9", OK, SLCAN_CMD_NONE);
    TEST_ASSERT_EQUAL(19200, g_board_baud_rate);
    s_expect("S", ERR, SLCAN_CMD_NONE);
    s_expect("S12", ERR, SLCAN_CMD_NONE);
    // Only allowed when closed.
    s_expect("O", OK, SLCAN_CMD_OPEN);
    s_expect("S1", ERR, SLCAN_CMD_NONE);
    TEST_ASSERT_EQUAL(19200, g_board_baud_rate);
    TEST_ASSERT_EQUAL(5, g_board_baud_rate_sets);
}

void test_config_lin_params(void)
{
    s_expect("x1", OK, SLCAN_CMD_SET_ENHANCED_CHECKSUM);
    s_expect("x2", OK, SLCAN_CMD_SET_CLASSIC_CHECKSUM);
    s_expect("x3", OK, SLCAN_CMD_SET_DIAG_FRAMES_SPECIAL);
    s_expect("x4", OK, SLCAN_CMD_SET_DIAG_FRAMES_NORMAL);
    s_expect("x5", ERR, SLCAN_CMD_NONE);
    s_expect("x", ERR, SLCAN_CMD_NONE);
    s_expect("x11", ERR, SLCAN_CMD_NONE);
}

void test_add_schedule(void)
{
    uint8_t count = 0;
    s_expect("R0120000A8", OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
    s_expect("R03FFFFFF1", OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
    const minlin_schedule_entry* sch    = slcan_schedule(&count);
    const minlin_frame*          frames = slcan_responder_frames(&count);
    TEST_ASSERT_EQUAL(2, s_schedule_count());
    TEST_ASSERT_EQUAL(2, s_frames_count());
    TEST_ASSERT_EQUAL_HEX8(0x12, sch[0].id);
    TEST_ASSERT_EQUAL(10, sch[0].frame_slot_ms);
    TEST_ASSERT_EQUAL_HEX8(0x3F, sch[1].id);
    TEST_ASSERT_EQUAL(0xFFFFF, sch[1].frame_slot_ms);
    TEST_ASSERT_EQUAL_HEX8(0x12, frames[0].id);
    TEST_ASSERT_EQUAL(MINLIN_SUBSCRIBE, frames[0].type);
    TEST_ASSERT_EQUAL(8, frames[0].payload_size);
    TEST_ASSERT_EQUAL(1, frames[1].payload_size);

    // Invalid ID, zero frame time, invalid payload sizes, wrong lengths, non-hex.
    s_expect("R0400000A8", ERR, SLCAN_CMD_NONE);
    s_expect("R012000008", ERR, SLCAN_CMD_NONE);
    s_expect("R0120000A0", ERR, SLCAN_CMD_NONE);
    s_expect("R0120000A9", ERR, SLCAN_CMD_NONE);
    s_expect("R0120000A", ERR, SLCAN_CMD_NONE);
    s_expect("R0120000A81", ERR, SLCAN_CMD_NONE);
    s_expect("R0120000G8", ERR, SLCAN_CMD_NONE);
    s_expect("R1120000A8", ERR, SLCAN_CMD_NONE);
    TEST_ASSERT_EQUAL(2, s_schedule_count());
    TEST_ASSERT_EQUAL(2, s_frames_count());
}

void test_add_schedule_auto_timing(void)
{
    uint8_t count = 0;
    s_expect("r0121", OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
    s_expect("r0138", OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
    const minlin_schedule_entry* sch = slcan_schedule(&count);
    // 2 * (44 + 10 * N) bit times rounded up: 108 bits = 5.6 ms, 248 bits = 12.9 ms at 19200.
    TEST_ASSERT_EQUAL(6, sch[0].frame_slot_ms);
    TEST_ASSERT_EQUAL(13, sch[1].frame_slot_ms);
    TEST_ASSERT_EQUAL(2, s_frames_count());

    g_board_baud_rate = 2400;
    TEST_ASSERT_EQUAL(104, slcan_worst_case_frame_time_ms(8));
    TEST_ASSERT_EQUAL(21, slcan_max_lin_byte_time_ms());
    g_board_baud_rate = 19200;
    TEST_ASSERT_EQUAL(3, slcan_max_lin_byte_time_ms());

    s_expect("r0120", ERR, SLCAN_CMD_NONE);
    s_expect("r0409", ERR, SLCAN_CMD_NONE);
    s_expect("r012", ERR, SLCAN_CMD_NONE);
}

void test_add_subscription_and_response(void)
{
    uint8_t count = 0;
    s_expect("r1214", OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
    s_expect("t1222AABB", OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
    const minlin_frame* frames = slcan_responder_frames(&count);
    TEST_ASSERT_EQUAL(0, s_schedule_count());
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL_HEX8(0x21, frames[0].id);
    TEST_ASSERT_EQUAL(MINLIN_SUBSCRIBE, frames[0].type);
    TEST_ASSERT_EQUAL(4, frames[0].payload_size);
    TEST_ASSERT_EQUAL_HEX8(0x22, frames[1].id);
    TEST_ASSERT_EQUAL(MINLIN_PUBLISH, frames[1].type);
    TEST_ASSERT_EQUAL(2, frames[1].payload_size);
    TEST_ASSERT_EQUAL_HEX8(0xAA, frames[1].payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, frames[1].payload[1]);

    s_expect("t1222AA", ERR, SLCAN_CMD_NONE);
    s_expect("t1222AABBCC", ERR, SLCAN_CMD_NONE);
    s_expect("t1220", ERR, SLCAN_CMD_NONE);
    s_expect("t1222AAXX", ERR, SLCAN_CMD_NONE);
    s_expect("r1210", ERR, SLCAN_CMD_NONE);
    s_expect("r2214", ERR, SLCAN_CMD_NONE);
    TEST_ASSERT_EQUAL(2, s_frames_count());
}

void test_add_scheduled_transmit(void)
{
    uint8_t count = 0;
    s_expect("T0120001421234", OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
    const minlin_schedule_entry* sch    = slcan_schedule(&count);
    const minlin_frame*          frames = slcan_responder_frames(&count);
    TEST_ASSERT_EQUAL_HEX8(0x12, sch[0].id);
    TEST_ASSERT_EQUAL(0x14, sch[0].frame_slot_ms);
    TEST_ASSERT_EQUAL(MINLIN_PUBLISH, frames[0].type);
    TEST_ASSERT_EQUAL(2, frames[0].payload_size);
    TEST_ASSERT_EQUAL_HEX8(0x12, frames[0].payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, frames[0].payload[1]);

    s_expect("T012000142123", ERR, SLCAN_CMD_NONE);
    s_expect("T0120000021234", ERR, SLCAN_CMD_NONE);
    s_expect("T012000140", ERR, SLCAN_CMD_NONE);
    TEST_ASSERT_EQUAL(1, s_schedule_count());
    TEST_ASSERT_EQUAL(1, s_frames_count());
}

void test_tables_full(void)
{
    // Regression: when only the responder table was full, adding a schedule entry with a
    // subscription wrote past the end of the responder table.
    char line[16];
    for (int i = 0; i < SLCAN_TABLE_MAX_SIZE; ++i) {
        snprintf(line, sizeof(line), "r1%02X1", i);
        s_expect(line, OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
    }
    s_expect("r1011", ERR, SLCAN_CMD_NONE);
    s_expect("t10111", ERR, SLCAN_CMD_NONE);
    s_expect("R0010000A1", ERR, SLCAN_CMD_NONE);
    s_expect("r0011", ERR, SLCAN_CMD_NONE);
    s_expect("T0010000A111", ERR, SLCAN_CMD_NONE);
    TEST_ASSERT_EQUAL(0, s_schedule_count());
    TEST_ASSERT_EQUAL(SLCAN_TABLE_MAX_SIZE, s_frames_count());

    // Wipe empties both tables.
    s_expect("w", OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
    TEST_ASSERT_EQUAL(0, s_frames_count());
    for (int i = 0; i < SLCAN_TABLE_MAX_SIZE; ++i) {
        snprintf(line, sizeof(line), "r0%02X1", i);
        s_expect(line, OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
    }
    s_expect("r0011", ERR, SLCAN_CMD_NONE);
    TEST_ASSERT_EQUAL(SLCAN_TABLE_MAX_SIZE, s_schedule_count());
    TEST_ASSERT_EQUAL(SLCAN_TABLE_MAX_SIZE, s_frames_count());
}

void test_single_frame(void)
{
    // Not allowed when closed.
    s_expect("t0122AABB", ERR, SLCAN_CMD_NONE);
    TEST_ASSERT_FALSE(slcan_single_frame_pending());

    s_expect("O", OK, SLCAN_CMD_OPEN);
    s_expect("t0122AABB", OK, SLCAN_CMD_SINGLE_FRAME);
    TEST_ASSERT_TRUE(slcan_single_frame_pending());
    TEST_ASSERT_EQUAL_HEX8(0x12, slcan_single_frame_schedule()->id);
    TEST_ASSERT_EQUAL(slcan_worst_case_frame_time_ms(2), slcan_single_frame_schedule()->frame_slot_ms);
    TEST_ASSERT_EQUAL_HEX8(0x12, slcan_single_frame()->id);
    TEST_ASSERT_EQUAL(MINLIN_PUBLISH, slcan_single_frame()->type);
    TEST_ASSERT_EQUAL(2, slcan_single_frame()->payload_size);
    TEST_ASSERT_EQUAL_HEX8(0xBB, slcan_single_frame()->payload[1]);

    // Busy until the previous one is done.
    s_expect("t0131CC", ERR, SLCAN_CMD_NONE);
    slcan_single_frame_done();
    s_expect("t0131CC", OK, SLCAN_CMD_SINGLE_FRAME);
    s_expect("t0131", ERR, SLCAN_CMD_NONE);

    // Closing cancels the pending single frame.
    s_expect("C", OK, SLCAN_CMD_CLOSE);
    TEST_ASSERT_FALSE(slcan_single_frame_pending());

    // Allowed in monitoring mode too.
    s_expect("L", OK, SLCAN_CMD_START_MONITORING);
    s_expect("t0131CC", OK, SLCAN_CMD_SINGLE_FRAME);
}

void test_bootloader(void)
{
    s_expect("boot", "", SLCAN_CMD_GOTO_BOOTLOADER);
    s_expect("boo", ERR, SLCAN_CMD_NONE);
    s_expect("boots", ERR, SLCAN_CMD_NONE);
    s_expect("bxxt", ERR, SLCAN_CMD_NONE);
}

void test_unknown_and_empty_lines(void)
{
    s_expect("Z", ERR, SLCAN_CMD_NONE);
    s_expect("R", ERR, SLCAN_CMD_NONE);
    s_expect("t2", ERR, SLCAN_CMD_NONE);
    // Empty lines are ignored without reply, and don't repeat the previous command.
    s_expect("w", OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
    s_expect("", "", SLCAN_CMD_NONE);
    s_expect(" .-", "", SLCAN_CMD_NONE);
    // Non-alphanumeric characters are ignored.
    s_expect("R0 12 0000A 8", OK, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
}

void test_overlong_line_is_discarded_completely(void)
{
    // Regression: only the first 32 bytes were discarded, and the rest was run as a command.
    s_expect("wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww", ERR, SLCAN_CMD_NONE);
    TEST_ASSERT_EQUAL(0, slcan_process_input());
    s_expect("V", "V0301" OK, SLCAN_CMD_NONE);
}

void test_full_input_without_line_end_is_discarded(void)
{
    // Regression: a full input buffer without CR blocked all further input.
    uint8_t garbage[128];
    memset(garbage, 'A', sizeof(garbage));
    TEST_ASSERT_TRUE(slcan_append_input(garbage, sizeof(garbage)));
    TEST_ASSERT_FALSE(slcan_append_input(garbage, 1));
    usb_out_clear();
    TEST_ASSERT_EQUAL(SLCAN_CMD_NONE, slcan_process_input());
    TEST_ASSERT_EQUAL_STRING(ERR, g_usb_out);
    s_expect("V", "V0301" OK, SLCAN_CMD_NONE);
}

void test_multiple_lines_in_one_input(void)
{
    const char* input = "O\rV\r";
    TEST_ASSERT_TRUE(slcan_append_input((const uint8_t*)input, (uint32_t)strlen(input)));
    TEST_ASSERT_EQUAL(SLCAN_CMD_OPEN, slcan_process_input());
    TEST_ASSERT_EQUAL(SLCAN_CMD_NONE, slcan_process_input());
    TEST_ASSERT_EQUAL_STRING(OK "V0301" OK, g_usb_out);
    // Partial line waits for CR.
    TEST_ASSERT_TRUE(slcan_append_input((const uint8_t*)"C", 1));
    TEST_ASSERT_EQUAL(SLCAN_CMD_NONE, slcan_process_input());
    TEST_ASSERT_TRUE(slcan_append_input((const uint8_t*)"\r", 1));
    TEST_ASSERT_EQUAL(SLCAN_CMD_CLOSE, slcan_process_input());
}

void test_send_frame_and_tx_overrun(void)
{
    const minlin_frame f = {.id = 0x3D, .type = MINLIN_SUBSCRIBE, .payload = {0x0A, 0xFF}, .payload_size = 2};
    slcan_send_frame(&f);
    TEST_ASSERT_EQUAL_STRING("t03D20AFF" OK, g_usb_out);
    const minlin_frame header = {.id = 0x01, .payload_size = 0};
    usb_out_clear();
    slcan_send_frame(&header);
    TEST_ASSERT_EQUAL_STRING("t0010" OK, g_usb_out);

    g_usb_write_fail = true;
    slcan_send_frame(&f);
    g_usb_write_fail = false;
    s_expect("F", "F20" OK, SLCAN_CMD_NONE);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_close_monitor);
    RUN_TEST(test_version_and_serial_number);
    RUN_TEST(test_status_flags_read_and_clear);
    RUN_TEST(test_set_baud_rate);
    RUN_TEST(test_config_lin_params);
    RUN_TEST(test_add_schedule);
    RUN_TEST(test_add_schedule_auto_timing);
    RUN_TEST(test_add_subscription_and_response);
    RUN_TEST(test_add_scheduled_transmit);
    RUN_TEST(test_tables_full);
    RUN_TEST(test_single_frame);
    RUN_TEST(test_bootloader);
    RUN_TEST(test_unknown_and_empty_lines);
    RUN_TEST(test_overlong_line_is_discarded_completely);
    RUN_TEST(test_full_input_without_line_end_is_discarded);
    RUN_TEST(test_multiple_lines_in_one_input);
    RUN_TEST(test_send_frame_and_tx_overrun);
    return UNITY_END();
}
