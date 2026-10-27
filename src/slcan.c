/**
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/usb-lin-card-fw>
 */

#include "slcan.h"
#include "board.h"
#include "fast_ring_buffer.h"
#ifdef DEBUG
#include "stack_monitor.h"
#endif

#define ASCII_CR  13
#define ASCII_BEL 7

/// Ring buffer for storing inputs from USB.
#define INPUT_RING_SIZE 128 // Must be a power of 2
static uint8_t          s_input_ring_storage[INPUT_RING_SIZE];
static fast_ring_buffer s_input_ring;

/// Maximum length of one input command line via slcan. No valid commands should be longer than this.
#define INPUT_MAX_LEN 32
/// Holds one command line.
static uint8_t s_input[INPUT_MAX_LEN];
static uint8_t s_input_pos = 0;

/// The next transmit buffer that is being built from the app.
static uint8_t  s_next_tx_buffer[SLCAN_TX_LINE_MAX];
static uint16_t s_next_tx_buffer_pos = 0;

/// Current channel mode; read from the LIN receive interrupt.
static volatile slcan_mode s_mode                 = SLCAN_MODE_CLOSED;
/// True while the single frame is waiting to be transmitted; read from the LIN receive interrupt.
static volatile bool       s_single_frame_pending = false;

/// Error flags that have been latched on and not cleared yet by reading the status.
static volatile uint8_t s_latched_errors = 0;

/// Commander schedule and responder frames configured by the client.
static minlin_schedule_entry s_schedule[SLCAN_TABLE_MAX_SIZE];
static uint8_t               s_schedule_count = 0;
static minlin_frame          s_responder_frames[SLCAN_TABLE_MAX_SIZE];
static uint8_t               s_responder_frames_count = 0;

/// Single entry schedule and response frame for publishing a single full frame on command.
static minlin_schedule_entry s_single_frame_schedule;
static minlin_frame          s_single_frame;

/// Returns true if ch is alphanumeric.
static bool s_is_alnum(uint8_t ch)
{
    return ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'));
}

/// Parses len hexadecimals starting from position pos of the input line into a maximum of 32-bit
/// unsigned integer out_value. Returns false if parsing failed, otherwise true.
static bool s_parse_hex(uint8_t pos, uint8_t len, uint32_t* out_value)
{
    *out_value = 0;
    if (len < 1 || len > 8 || pos + len > s_input_pos) {
        return false;
    }
    for (uint8_t i = pos; i < pos + len; ++i) {
        const uint8_t ch = s_input[i];
        *out_value <<= 4;
        if (ch >= '0' && ch <= '9') {
            *out_value += ch - '0';
        } else if (ch >= 'A' && ch <= 'F') {
            *out_value += ch - 'A' + 10U;
        } else if (ch >= 'a' && ch <= 'f') {
            *out_value += ch - 'a' + 10U;
        } else {
            return false;
        }
    }
    return true;
}

/// Parses a 2 hex digit LIN ID at pos.
static bool s_parse_id(uint8_t pos, uint8_t* out_id)
{
    uint32_t tmp = 0;
    if (!s_parse_hex(pos, 2, &tmp) || tmp > MINLIN_MAX_LIN_ID) {
        return false;
    }
    *out_id = (uint8_t)tmp;
    return true;
}

/// Parses a 1 hex digit payload size (1-8) at pos.
static bool s_parse_payload_size(uint8_t pos, uint8_t* out_size)
{
    uint32_t tmp = 0;
    if (!s_parse_hex(pos, 1, &tmp) || tmp < 1 || tmp > MINLIN_MAX_PAYLOAD_BYTES) {
        return false;
    }
    *out_size = (uint8_t)tmp;
    return true;
}

/// Parses a 5 hex digit frame time (milliseconds, non-zero) at pos.
static bool s_parse_frame_time(uint8_t pos, uint32_t* out_time_ms)
{
    return s_parse_hex(pos, 5, out_time_ms) && *out_time_ms > 0;
}

/// Parses size number of 2 hex digit payload bytes at pos; the input line must end after them.
static bool s_parse_payload(uint8_t pos, uint8_t size, uint8_t* out_payload)
{
    if (s_input_pos != pos + size * 2) {
        return false;
    }
    uint32_t tmp = 0;
    for (uint8_t i = 0; i < size; ++i) {
        if (!s_parse_hex((uint8_t)(pos + i * 2), 2, &tmp)) {
            return false;
        }
        out_payload[i] = (uint8_t)tmp;
    }
    return true;
}

/// Sends OK or error reply.
static void s_reply(bool ok)
{
    if (ok) {
        slcan_tx_append_ok_term();
    } else {
        slcan_tx_append_error_term();
    }
    slcan_tx_send();
}

/// Replies and returns the command to be processed by the app if ok, otherwise SLCAN_CMD_NONE.
static slcan_cmd s_reply_cmd(bool ok, slcan_cmd cmd)
{
    s_reply(ok);
    return ok ? cmd : SLCAN_CMD_NONE;
}

static void s_add_schedule_entry(uint8_t id, uint32_t frame_time_ms)
{
    s_schedule[s_schedule_count].id            = id;
    s_schedule[s_schedule_count].frame_slot_ms = frame_time_ms;
    s_schedule_count++;
}

/// Adds responder frame; payload may be NULL for subscriptions.
static void s_add_responder_frame(uint8_t id, minlin_frame_type type, uint8_t payload_size, const uint8_t* payload)
{
    minlin_frame* frame = &s_responder_frames[s_responder_frames_count];
    frame->id           = id;
    frame->type         = type;
    frame->payload_size = payload_size;
    for (uint8_t i = 0; i < payload_size; ++i) {
        frame->payload[i] = (payload != NULL) ? payload[i] : 0;
    }
    s_responder_frames_count++;
}

static bool s_schedule_full(void) { return s_schedule_count >= SLCAN_TABLE_MAX_SIZE; }

static bool s_responder_frames_full(void) { return s_responder_frames_count >= SLCAN_TABLE_MAX_SIZE; }

void slcan_init(void)
{
    fast_ring_buffer_init(&s_input_ring, s_input_ring_storage, INPUT_RING_SIZE);
    s_input_pos              = 0;
    s_next_tx_buffer_pos     = 0;
    s_mode                   = SLCAN_MODE_CLOSED;
    s_single_frame_pending   = false;
    s_latched_errors         = 0;
    s_schedule_count         = 0;
    s_responder_frames_count = 0;
}

bool slcan_append_input(const uint8_t* src, uint32_t len)
{
    // USB reception is flow controlled: caller holds the data and retries if it doesn't fit.
    return fast_ring_buffer_push(&s_input_ring, src, len);
}

/**
 * "O": Open channel; means that we enable the commander and responder tasks.
 */
static slcan_cmd s_cmd_open_channel(void)
{
    if (s_mode != SLCAN_MODE_CLOSED) {
        return s_reply_cmd(false, SLCAN_CMD_NONE);
    }
    s_mode = SLCAN_MODE_OPEN;
    return s_reply_cmd(true, SLCAN_CMD_OPEN);
}

/**
 * "C": Close channel; means that we disable the commander and responder tasks.
 * Also stops monitoring mode if device was opened with "L" command, and cancels a pending single
 * frame transmit.
 */
static slcan_cmd s_cmd_close_channel(void)
{
    if (s_mode == SLCAN_MODE_CLOSED) {
        return s_reply_cmd(false, SLCAN_CMD_NONE);
    }
    s_mode                 = SLCAN_MODE_CLOSED;
    s_single_frame_pending = false;
    return s_reply_cmd(true, SLCAN_CMD_CLOSE);
}

/**
 * "L": Open in monitoring mode; receives all frames on the LIN bus and can transmit single
 * frames when commanded to do so ("t0").
 */
static slcan_cmd s_cmd_monitor(void)
{
    if (s_mode != SLCAN_MODE_CLOSED) {
        return s_reply_cmd(false, SLCAN_CMD_NONE);
    }
    s_mode = SLCAN_MODE_MONITOR;
    return s_reply_cmd(true, SLCAN_CMD_START_MONITORING);
}

/**
 * "V": Get hardware and software versions.
 */
static void s_cmd_version(void)
{
    slcan_tx_append_byte('V');
    slcan_tx_append_byte_as_hex(HARDWARE_VERSION);
    slcan_tx_append_byte_as_hex(FIRMWARE_VERSION);
    s_reply(true);
}

/**
 * "N": Get serial number.
 */
static void s_cmd_serial_number(void)
{
    uint8_t id[BOARD_UNIQUE_ID_LEN];
    board_read_unique_id(id);
    slcan_tx_append_byte('N');
    for (uint8_t i = 0; i < BOARD_UNIQUE_ID_LEN; ++i) {
        slcan_tx_append_byte_as_hex(id[i]);
    }
    s_reply(true);
}

/**
 * "F": Get latched error flags and clear them.
 */
static void s_cmd_status_flags(void)
{
    const uint32_t irq   = board_irq_save();
    const uint8_t  flags = s_latched_errors;
    s_latched_errors     = 0;
    board_irq_restore(irq);

    slcan_tx_append_byte('F');
    slcan_tx_append_byte_as_hex(flags);
    s_reply(true);
}

/**
 * "Sx": Set LIN baud rate to preset x. Only allowed when channel is closed.
 *      x=1: 2400
 *      x=2: 9600
 *      x=3: 10400
 *      any other x: 19200 (the most common LIN baud rate)
 */
static void s_cmd_set_baud_rate(void)
{
    if (s_mode != SLCAN_MODE_CLOSED || s_input_pos != 2) {
        s_reply(false);
        return;
    }
    uint32_t new_baud_rate = 19200;
    switch (s_input[1]) {
    case '1':
        new_baud_rate = 2400;
        break;
    case '2':
        new_baud_rate = 9600;
        break;
    case '3':
        new_baud_rate = 10400;
        break;
    default:
        break;
    }
    board_set_lin_baud_rate(new_baud_rate);
    s_reply(true);
}

/**
 * "xm": Configure LIN network parameters:
 * Checksum calculation mode:
 *      m=1: enhanced checksum (default)
 *      m=2: classic checksum
 * Diagnostic frame handling (IDs 0x3C and 0x3D):
 *      m=3: diagnostic frames always use classic checksum (default)
 *      m=4: diagnostic frames are considered just normal frames
 */
static slcan_cmd s_cmd_config_lin_params(void)
{
    slcan_cmd res = SLCAN_CMD_NONE;
    if (s_input_pos == 2) {
        switch (s_input[1]) {
        case '1':
            res = SLCAN_CMD_SET_ENHANCED_CHECKSUM;
            break;
        case '2':
            res = SLCAN_CMD_SET_CLASSIC_CHECKSUM;
            break;
        case '3':
            res = SLCAN_CMD_SET_DIAG_FRAMES_SPECIAL;
            break;
        case '4':
            res = SLCAN_CMD_SET_DIAG_FRAMES_NORMAL;
            break;
        default:
            break;
        }
    }
    return s_reply_cmd(res != SLCAN_CMD_NONE, res);
}

/**
 * "w": Wipes header sending schedule and responder publish and subscribe table.
 */
static slcan_cmd s_cmd_wipe_schedule_and_responder(void)
{
    s_schedule_count         = 0;
    s_responder_frames_count = 0;
    return s_reply_cmd(true, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
}

/**
 * "R0iitttttl": Add frame header to commander schedule with ID ii (in hex),
 * frame time ttttt (in hex, milliseconds, non-zero), and payload length l (1-8).
 * Also adds subscription to responses so that a possible response is transmitted to the user.
 */
static slcan_cmd s_cmd_add_schedule(void)
{
    uint8_t    id            = 0;
    uint32_t   frame_time_ms = 0;
    uint8_t    payload_size  = 0;
    const bool ok            = s_input_pos == 10 && s_parse_id(2, &id) && s_parse_frame_time(4, &frame_time_ms)
        && s_parse_payload_size(9, &payload_size) && !s_schedule_full() && !s_responder_frames_full();
    if (ok) {
        s_add_schedule_entry(id, frame_time_ms);
        s_add_responder_frame(id, MINLIN_SUBSCRIBE, payload_size, NULL);
    }
    return s_reply_cmd(ok, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
}

/**
 * "r0iil": Add frame header to commander schedule with ID ii (in hex) and payload length l (1-8).
 * Frame time is estimated automatically from current baud rate and payload length.
 * Also adds subscription to responses so that a possible response is transmitted to the user.
 */
static slcan_cmd s_cmd_add_schedule_auto_timing(void)
{
    uint8_t    id           = 0;
    uint8_t    payload_size = 0;
    const bool ok           = s_input_pos == 5 && s_parse_id(2, &id) && s_parse_payload_size(4, &payload_size)
        && !s_schedule_full() && !s_responder_frames_full();
    if (ok) {
        s_add_schedule_entry(id, slcan_worst_case_frame_time_ms(payload_size));
        s_add_responder_frame(id, MINLIN_SUBSCRIBE, payload_size, NULL);
    }
    return s_reply_cmd(ok, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
}

/**
 * "r1iil": Add subscription to responses with ID ii (in hex) and payload length l (1-8).
 * Transmits the frames with this ID to the user.
 */
static slcan_cmd s_cmd_add_subscription(void)
{
    uint8_t    id           = 0;
    uint8_t    payload_size = 0;
    const bool ok           = s_input_pos == 5 && s_parse_id(2, &id) && s_parse_payload_size(4, &payload_size)
        && !s_responder_frames_full();
    if (ok) {
        s_add_responder_frame(id, MINLIN_SUBSCRIBE, payload_size, NULL);
    }
    return s_reply_cmd(ok, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
}

/**
 * "T0iitttttldd...": Add schedule to transmit ID ii (in hex) header from commander with
 * frame time ttttt (in hex, milliseconds, non-zero), and also add a self-respond to make it a
 * command frame with payload length l (1-8) and payload data dd... (in hex, length number of bytes).
 */
static slcan_cmd s_cmd_add_scheduled_transmit(void)
{
    uint8_t    id            = 0;
    uint32_t   frame_time_ms = 0;
    uint8_t    payload_size  = 0;
    uint8_t    payload[MINLIN_MAX_PAYLOAD_BYTES];
    const bool ok = s_parse_id(2, &id) && s_parse_frame_time(4, &frame_time_ms)
        && s_parse_payload_size(9, &payload_size) && s_parse_payload(10, payload_size, payload) && !s_schedule_full()
        && !s_responder_frames_full();
    if (ok) {
        s_add_schedule_entry(id, frame_time_ms);
        s_add_responder_frame(id, MINLIN_PUBLISH, payload_size, payload);
    }
    return s_reply_cmd(ok, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
}

/**
 * "t1iildd...": Add a persistent response publish whenever a frame with ID ii (in hex) is received.
 * Use payload length l (1-8) and payload data dd... (in hex, length number of bytes).
 */
static slcan_cmd s_cmd_add_response(void)
{
    uint8_t    id           = 0;
    uint8_t    payload_size = 0;
    uint8_t    payload[MINLIN_MAX_PAYLOAD_BYTES];
    const bool ok = s_parse_id(2, &id) && s_parse_payload_size(4, &payload_size)
        && s_parse_payload(5, payload_size, payload) && !s_responder_frames_full();
    if (ok) {
        s_add_responder_frame(id, MINLIN_PUBLISH, payload_size, payload);
    }
    return s_reply_cmd(ok, SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER);
}

/**
 * "t0iildd...": Send single frame with header and response for ID ii (in hex),
 * payload length l (1-8), and payload data dd... (in hex, length number of bytes).
 * This is implemented by temporarily switching to a single frame schedule. Only allowed when the
 * channel is open ("O" or "L"), and when the previous single frame has already been transmitted.
 */
static slcan_cmd s_cmd_single_frame(void)
{
    uint8_t    id           = 0;
    uint8_t    payload_size = 0;
    const bool ok           = s_mode != SLCAN_MODE_CLOSED && !s_single_frame_pending && s_parse_id(2, &id)
        && s_parse_payload_size(4, &payload_size) && s_parse_payload(5, payload_size, s_single_frame.payload);
    if (ok) {
        // Calculate frame slot period from worst-case byte time estimate.
        s_single_frame_schedule.id            = id;
        s_single_frame_schedule.frame_slot_ms = slcan_worst_case_frame_time_ms(payload_size);
        s_single_frame.id                     = id;
        s_single_frame.type                   = MINLIN_PUBLISH;
        s_single_frame.payload_size           = payload_size;
        s_single_frame_pending                = true;
    }
    return s_reply_cmd(ok, SLCAN_CMD_SINGLE_FRAME);
}

/**
 * "boot": Command device to go to USB bootloader for flashing updates.
 */
static slcan_cmd s_cmd_goto_bootloader(void)
{
    if (s_input_pos == 4 && s_input[1] == 'o' && s_input[2] == 'o' && s_input[3] == 't') {
        // No reply; the device reboots.
        return SLCAN_CMD_GOTO_BOOTLOADER;
    }
    s_reply(false);
    return SLCAN_CMD_NONE;
}

#ifdef DEBUG
/**
 * "d": Debug commands. Only included when compiled as Debug build type.
 */
static void s_cmd_debug(void)
{
    if (s_input_pos == 2 && s_input[1] == '1') {
        // "d1": Output stack usage high-water mark in hex bytes.
        slcan_tx_append_byte('d');
        uint32_t stk = stack_monitor_used();
        slcan_tx_append_byte_as_hex((uint8_t)((stk >> 24) & 0xFF));
        slcan_tx_append_byte_as_hex((uint8_t)((stk >> 16) & 0xFF));
        slcan_tx_append_byte_as_hex((uint8_t)((stk >> 8) & 0xFF));
        slcan_tx_append_byte_as_hex((uint8_t)((stk) & 0xFF));
        s_reply(true);
    } else {
        s_reply(false);
    }
}
#endif

slcan_cmd slcan_process_input(void)
{
    uint32_t cmd_bytes = fast_ring_buffer_find_byte_count_to(&s_input_ring, ASCII_CR);
    if (cmd_bytes == 0) {
        if (fast_ring_buffer_used(&s_input_ring) >= INPUT_RING_SIZE) {
            // Ring is full but contains no complete line, so it never will. Discard.
            fast_ring_buffer_skip(&s_input_ring, INPUT_RING_SIZE);
            s_reply(false);
        }
        // No command available. Do nothing.
        return SLCAN_CMD_NONE;
    }
    if (cmd_bytes > INPUT_MAX_LEN) {
        // Command is too long to be valid. Discard the whole line.
        fast_ring_buffer_skip(&s_input_ring, cmd_bytes);
        s_reply(false);
        return SLCAN_CMD_NONE;
    }

    const uint8_t line_len = (uint8_t)fast_ring_buffer_pop(&s_input_ring, s_input, cmd_bytes);

    // Remove non-alphanumeric characters (including the CR) from input command line.
    s_input_pos = 0;
    for (uint8_t i = 0; i < line_len; ++i) {
        if (s_is_alnum(s_input[i])) {
            s_input[s_input_pos] = s_input[i];
            s_input_pos++;
        }
    }
    if (s_input_pos == 0) {
        // Empty line; tools commonly send these to flush the line, so ignore without reply.
        return SLCAN_CMD_NONE;
    }

    slcan_cmd res = SLCAN_CMD_NONE;

    // Handle commands. Second character selects the sub-command for some of them.
    const uint8_t sub = (s_input_pos >= 2) ? s_input[1] : 0;
    switch (s_input[0]) {
    case 'O':
        res = s_cmd_open_channel();
        break;
    case 'C':
        res = s_cmd_close_channel();
        break;
    case 'L':
        res = s_cmd_monitor();
        break;
    case 'V':
        s_cmd_version();
        break;
    case 'N':
        s_cmd_serial_number();
        break;
    case 'F':
        s_cmd_status_flags();
        break;
    case 'S':
        s_cmd_set_baud_rate();
        break;
    case 'x':
        res = s_cmd_config_lin_params();
        break;
    case 'w':
        res = s_cmd_wipe_schedule_and_responder();
        break;
    case 'R':
        if (sub == '0') {
            res = s_cmd_add_schedule();
        } else {
            s_reply(false);
        }
        break;
    case 'r':
        if (sub == '0') {
            res = s_cmd_add_schedule_auto_timing();
        } else if (sub == '1') {
            res = s_cmd_add_subscription();
        } else {
            s_reply(false);
        }
        break;
    case 'T':
        if (sub == '0') {
            res = s_cmd_add_scheduled_transmit();
        } else {
            s_reply(false);
        }
        break;
    case 't':
        if (sub == '0') {
            res = s_cmd_single_frame();
        } else if (sub == '1') {
            res = s_cmd_add_response();
        } else {
            s_reply(false);
        }
        break;
    case 'b':
        res = s_cmd_goto_bootloader();
        break;
#ifdef DEBUG
    case 'd':
        s_cmd_debug();
        break;
#endif
    default:
        // Unknown command.
        s_reply(false);
        break;
    }

    return res;
}

void slcan_latch_error_flag(slcan_error e)
{
    const uint32_t irq = board_irq_save();
    s_latched_errors |= (uint8_t)e;
    board_irq_restore(irq);
}

void slcan_tx_append_byte(uint8_t byte)
{
    if (s_next_tx_buffer_pos >= SLCAN_TX_LINE_MAX) {
        return;
    }
    s_next_tx_buffer[s_next_tx_buffer_pos] = byte;
    s_next_tx_buffer_pos++;
}

void slcan_tx_append_byte_as_hex(uint8_t byte)
{
    slcan_tx_append_byte((uint8_t)NIBBLE_TO_HEX(HIGH_NIBBLE(byte)));
    slcan_tx_append_byte((uint8_t)NIBBLE_TO_HEX(LOW_NIBBLE(byte)));
}

void slcan_tx_append_ok_term(void) { slcan_tx_append_byte(ASCII_CR); }

void slcan_tx_append_error_term(void) { slcan_tx_append_byte(ASCII_BEL); }

void slcan_tx_send(void)
{
    if (!board_usb_write(s_next_tx_buffer, s_next_tx_buffer_pos)) {
        slcan_latch_error_flag(SLCAN_ERROR_USB_TX_OVERRUN);
    }
    s_next_tx_buffer_pos = 0;
}

void slcan_send_frame(const minlin_frame* frame)
{
    // "t0iildd...": formatted as a standard slcan frame with 3 digit ID 0ii.
    slcan_tx_append_byte('t');
    slcan_tx_append_byte('0');
    slcan_tx_append_byte_as_hex(frame->id);
    slcan_tx_append_byte((uint8_t)NIBBLE_TO_HEX(LOW_NIBBLE(frame->payload_size)));
    for (uint8_t i = 0; i < frame->payload_size; ++i) {
        slcan_tx_append_byte_as_hex(frame->payload[i]);
    }
    s_reply(true);
}

slcan_mode slcan_get_mode(void) { return s_mode; }

bool slcan_single_frame_pending(void) { return s_single_frame_pending; }

void slcan_single_frame_done(void) { s_single_frame_pending = false; }

const minlin_schedule_entry* slcan_schedule(uint8_t* out_count)
{
    *out_count = s_schedule_count;
    return s_schedule;
}

minlin_frame* slcan_responder_frames(uint8_t* out_count)
{
    *out_count = s_responder_frames_count;
    return s_responder_frames;
}

const minlin_schedule_entry* slcan_single_frame_schedule(void) { return &s_single_frame_schedule; }

minlin_frame* slcan_single_frame(void) { return &s_single_frame; }

uint32_t slcan_max_lin_byte_time_ms(void)
{
    // 50 bit times should contain all margins for a LIN byte to be transmitted.
    // Calculated in milliseconds and rounded up for current baud rate.
    const uint32_t baud_rate = board_lin_baud_rate();
    return (((1000 * 50) + baud_rate - 1) / baud_rate);
}

uint32_t slcan_worst_case_frame_time_ms(uint8_t payload_size)
{
    /**
     * Max frame time estimated based on the equations in LIN spec, but with extra margin added
     * to take into account possible worst-case jitter.
     * See LIN spec sections 2.3.2 and 2.4.2:
     * T_bit       = duration of single bit in the used bus speed (~0.05208 ms at 19.2 kbit/s)
     * T_frame_max = 1.4*(34 * T_bit) + 1.4*(10 * (N_data_bytes + 1) * T_bit)
     * Multiplier of 2.0 is used instead of 1.4, giving 2.0*(44 + 10*N) = 88 + 20*N bit times,
     * which is converted to milliseconds and rounded up for current baud rate.
     */
    const uint32_t baud_rate = board_lin_baud_rate();
    return (((88 + 20 * (uint32_t)payload_size) * 1000 + baud_rate - 1) / baud_rate);
}
