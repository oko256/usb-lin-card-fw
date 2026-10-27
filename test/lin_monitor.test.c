#include "lin_monitor.h"
#include <unity.h>

#define TIMEOUT_MS 3

static lin_monitor   s_mon;
static minlin_config s_config;
static uint32_t      s_now;

// Not used by these tests, but minlin references them.
bool minlin_hw_check_and_clear_break(void) { return false; }
void minlin_responder_subscribe_callback(const minlin_frame* frame) { (void)frame; }
bool minlin_responder_publish_callback(minlin_frame* frame)
{
    (void)frame;
    return false;
}

void setUp(void)
{
    lin_monitor_reset(&s_mon);
    s_config = (minlin_config){0};
    s_now    = 1000;
}

void tearDown(void) { }

static void s_rx(uint8_t byte, bool is_break)
{
    s_now++;
    lin_monitor_receive_byte(&s_mon, &s_config, byte, is_break, s_now);
}

/// Receives BREAK + SYNC + PID, and the given data bytes followed by their checksum (if any data).
static void s_rx_frame(uint8_t id, const uint8_t* data, uint8_t len)
{
    s_rx(MINLIN_SYNC_BYTE, true);
    s_rx(minlin_id_to_pid(id, NULL), false);
    for (uint8_t i = 0; i < len; ++i) {
        s_rx(data[i], false);
    }
    if (len > 0) {
        s_rx(minlin_checksum(&s_config, id, data, len, NULL), false);
    }
}

static void s_poll_after(uint32_t ms) { lin_monitor_poll(&s_mon, &s_config, s_now + ms, TIMEOUT_MS); }

static void s_assert_frame(uint8_t id, const uint8_t* data, uint8_t len)
{
    minlin_frame f;
    TEST_ASSERT_TRUE(lin_monitor_take_frame(&s_mon, &f));
    TEST_ASSERT_EQUAL_HEX8(id, f.id);
    TEST_ASSERT_EQUAL(len, f.payload_size);
    if (len > 0) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(data, f.payload, len);
    }
    TEST_ASSERT_FALSE(lin_monitor_take_frame(&s_mon, &f));
}

static const uint8_t s_data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

void test_header_only_after_timeout(void)
{
    minlin_frame f;
    s_rx_frame(0x12, NULL, 0);
    s_poll_after(TIMEOUT_MS - 1);
    TEST_ASSERT_FALSE(lin_monitor_take_frame(&s_mon, &f));
    s_poll_after(TIMEOUT_MS);
    s_assert_frame(0x12, NULL, 0);
}

void test_byte_received_after_poll_time_is_not_timed_out(void)
{
    // Regression: a byte received after the poll time was read looked like it was received long ago.
    minlin_frame f;
    s_rx(MINLIN_SYNC_BYTE, true);
    s_rx(minlin_id_to_pid(0x12, NULL), false);
    s_rx(s_data[0], false);
    lin_monitor_poll(&s_mon, &s_config, s_now - 5, TIMEOUT_MS);
    TEST_ASSERT_FALSE(lin_monitor_take_frame(&s_mon, &f));
    TEST_ASSERT_FALSE(lin_monitor_take_frame_error(&s_mon));
}

void test_all_lengths_ended_by_timeout(void)
{
    // Includes regression: 7 data bytes + checksum used to be treated as a full 8 byte frame.
    for (uint8_t len = 1; len < 8; ++len) {
        s_rx_frame(0x21, s_data, len);
        s_poll_after(TIMEOUT_MS);
        s_assert_frame(0x21, s_data, len);
        TEST_ASSERT_FALSE(lin_monitor_take_frame_error(&s_mon));
    }
}

void test_full_frame_completes_without_timeout(void)
{
    s_rx_frame(0x30, s_data, 8);
    s_assert_frame(0x30, s_data, 8);
}

void test_frame_ended_by_next_break(void)
{
    s_rx_frame(0x05, s_data, 7);
    // Next header ends the previous frame.
    s_rx_frame(0x06, NULL, 0);
    s_assert_frame(0x05, s_data, 7);
    s_poll_after(TIMEOUT_MS);
    s_assert_frame(0x06, NULL, 0);
}

void test_bad_checksum_is_dropped(void)
{
    minlin_frame f;
    s_rx(MINLIN_SYNC_BYTE, true);
    s_rx(minlin_id_to_pid(0x10, NULL), false);
    s_rx(0x01, false);
    s_rx(0x02, false);
    s_rx(0x00, false); // Wrong checksum.
    s_poll_after(TIMEOUT_MS);
    TEST_ASSERT_FALSE(lin_monitor_take_frame(&s_mon, &f));
    TEST_ASSERT_TRUE(lin_monitor_take_frame_error(&s_mon));
    TEST_ASSERT_FALSE(lin_monitor_take_frame_error(&s_mon));
}

void test_checksum_config(void)
{
    // Classic checksum frames are rejected with enhanced checksum config (and vice versa).
    s_config.classic_checksum = true;
    s_rx_frame(0x10, s_data, 4);
    s_config.classic_checksum = false;
    s_poll_after(TIMEOUT_MS);
    TEST_ASSERT_TRUE(lin_monitor_take_frame_error(&s_mon));

    s_config.classic_checksum = true;
    s_rx_frame(0x10, s_data, 4);
    s_poll_after(TIMEOUT_MS);
    s_assert_frame(0x10, s_data, 4);

    // Diagnostic frames use classic checksum even with enhanced checksum config.
    s_config.classic_checksum = false;
    s_rx_frame(MINLIN_DIAG_REQUEST_ID, s_data, 8);
    s_assert_frame(MINLIN_DIAG_REQUEST_ID, s_data, 8);
}

void test_break_reported_separately(void)
{
    s_rx(0x00, true);
    s_rx(MINLIN_SYNC_BYTE, false);
    s_rx(minlin_id_to_pid(0x22, NULL), false);
    s_rx(0xAB, false);
    s_rx(minlin_checksum(&s_config, 0x22, (const uint8_t[]){0xAB}, 1, NULL), false);
    s_poll_after(TIMEOUT_MS);
    s_assert_frame(0x22, (const uint8_t[]){0xAB}, 1);
}

void test_malformed_header(void)
{
    minlin_frame f;
    // Bad parity.
    s_rx(MINLIN_SYNC_BYTE, true);
    s_rx(0x12, false); // PID of 0x12 is 0x92.
    s_poll_after(TIMEOUT_MS);
    TEST_ASSERT_FALSE(lin_monitor_take_frame(&s_mon, &f));
    TEST_ASSERT_TRUE(lin_monitor_take_frame_error(&s_mon));
    // Bad sync after separately reported BREAK.
    s_rx(0x00, true);
    s_rx(0x54, false);
    TEST_ASSERT_TRUE(lin_monitor_take_frame_error(&s_mon));
    TEST_ASSERT_EQUAL(LIN_MONITOR_IDLE, s_mon.state);
    // Random bytes while idle are ignored.
    s_rx(0x55, false);
    TEST_ASSERT_EQUAL(LIN_MONITOR_IDLE, s_mon.state);
}

void test_overrun_keeps_first_frame(void)
{
    s_rx_frame(0x01, s_data, 8);
    s_rx_frame(0x02, s_data, 8);
    TEST_ASSERT_TRUE(lin_monitor_take_overrun(&s_mon));
    TEST_ASSERT_FALSE(lin_monitor_take_overrun(&s_mon));
    s_assert_frame(0x01, s_data, 8);
}

void test_reset(void)
{
    minlin_frame f;
    s_rx_frame(0x01, s_data, 8);
    s_rx(MINLIN_SYNC_BYTE, true);
    lin_monitor_reset(&s_mon);
    TEST_ASSERT_FALSE(lin_monitor_take_frame(&s_mon, &f));
    TEST_ASSERT_EQUAL(LIN_MONITOR_IDLE, s_mon.state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_only_after_timeout);
    RUN_TEST(test_byte_received_after_poll_time_is_not_timed_out);
    RUN_TEST(test_all_lengths_ended_by_timeout);
    RUN_TEST(test_full_frame_completes_without_timeout);
    RUN_TEST(test_frame_ended_by_next_break);
    RUN_TEST(test_bad_checksum_is_dropped);
    RUN_TEST(test_checksum_config);
    RUN_TEST(test_break_reported_separately);
    RUN_TEST(test_malformed_header);
    RUN_TEST(test_overrun_keeps_first_frame);
    RUN_TEST(test_reset);
    return UNITY_END();
}
