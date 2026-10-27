/**
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/usb-lin-card-fw>
 */

#include "lin_monitor.h"

static void s_finish_frame(lin_monitor* self, const minlin_config* config)
{
    self->state = LIN_MONITOR_IDLE;

    uint8_t data_len = 0;
    if (self->rx_count >= 2) {
        // Response received: the last byte is the checksum.
        data_len               = (uint8_t)(self->rx_count - 1);
        minlin_res    res      = MINLIN_RES_OK;
        const uint8_t checksum = minlin_checksum(config, self->id, self->rx_buf, data_len, &res);
        if (res != MINLIN_RES_OK || checksum != self->rx_buf[data_len]) {
            self->frame_error = true;
            return;
        }
    }
    // Otherwise just a header (a single byte can't be a valid response either).

    if (self->ready_valid) {
        // Previous frame hasn't been taken yet.
        self->overrun = true;
        return;
    }
    self->ready.id           = self->id;
    self->ready.type         = MINLIN_SUBSCRIBE;
    self->ready.payload_size = data_len;
    for (uint8_t i = 0; i < data_len; ++i) {
        self->ready.payload[i] = self->rx_buf[i];
    }
    self->ready_valid = true;
}

void lin_monitor_reset(lin_monitor* self)
{
    self->state       = LIN_MONITOR_IDLE;
    self->rx_count    = 0;
    self->ready_valid = false;
    self->overrun     = false;
    self->frame_error = false;
}

void lin_monitor_receive_byte(
    lin_monitor* self, const minlin_config* config, uint8_t received_byte, bool is_break, uint32_t now_ms)
{
    self->last_byte_ms = now_ms;

    if (is_break) {
        if (self->state == LIN_MONITOR_RECEIVE) {
            // BREAK during receive means that the previous frame has ended.
            s_finish_frame(self, config);
        }
        // Same as minlin: if the BREAK flag was set with the SYNC byte, it has already been received,
        // otherwise the BREAK was reported separately (e.g. with break character 0x00).
        self->state = (received_byte == MINLIN_SYNC_BYTE) ? LIN_MONITOR_WAIT_PID : LIN_MONITOR_WAIT_SYNC;
        return;
    }

    switch (self->state) {
    case LIN_MONITOR_IDLE:
        // Do nothing if we receive random bytes while idle.
        break;
    case LIN_MONITOR_WAIT_SYNC:
        if (received_byte == MINLIN_SYNC_BYTE) {
            self->state = LIN_MONITOR_WAIT_PID;
        } else {
            self->frame_error = true;
            self->state       = LIN_MONITOR_IDLE;
        }
        break;
    case LIN_MONITOR_WAIT_PID: {
        const uint8_t received_id = (received_byte & MINLIN_MAX_LIN_ID);
        if (minlin_id_to_pid(received_id, NULL) == received_byte) {
            self->id       = received_id;
            self->rx_count = 0;
            self->state    = LIN_MONITOR_RECEIVE;
        } else {
            // Parity bits are not correct.
            self->frame_error = true;
            self->state       = LIN_MONITOR_IDLE;
        }
    } break;
    case LIN_MONITOR_RECEIVE:
        self->rx_buf[self->rx_count] = received_byte;
        self->rx_count++;
        if (self->rx_count == sizeof(self->rx_buf)) {
            // Maximum length frame received; no need to wait for timeout.
            s_finish_frame(self, config);
        }
        break;
    }
}

void lin_monitor_poll(lin_monitor* self, const minlin_config* config, uint32_t now_ms, uint32_t timeout_ms)
{
    // Signed difference: a byte received after now_ms was read must not look like a timeout.
    if (self->state == LIN_MONITOR_RECEIVE && (int32_t)(now_ms - self->last_byte_ms) >= (int32_t)timeout_ms) {
        s_finish_frame(self, config);
    }
}

bool lin_monitor_take_frame(lin_monitor* self, minlin_frame* out_frame)
{
    if (!self->ready_valid) {
        return false;
    }
    *out_frame        = self->ready;
    self->ready_valid = false;
    return true;
}

bool lin_monitor_take_overrun(lin_monitor* self)
{
    const bool overrun = self->overrun;
    self->overrun      = false;
    return overrun;
}

bool lin_monitor_take_frame_error(lin_monitor* self)
{
    const bool frame_error = self->frame_error;
    self->frame_error      = false;
    return frame_error;
}
