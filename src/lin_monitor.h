/**
 * @file
 * @brief Passive LIN bus monitor that receives all frames of unknown lengths from the bus.
 *
 * Bytes are fed in from the receive interrupt with @c lin_monitor_receive_byte(). Because frame
 * lengths are unknown, a frame ends after the maximum of 8 data bytes + checksum, on the next
 * BREAK, or when no more bytes have been received for a timeout (checked by @c lin_monitor_poll()).
 * The last received byte is then the checksum. A header without a response is reported as a frame
 * with zero payload size.
 *
 * Functions other than @c lin_monitor_receive_byte() must be called with the receive interrupt
 * disabled.
 *
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/usb-lin-card-fw>
 */
#pragma once

#include "minlin/minlin.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { LIN_MONITOR_IDLE, LIN_MONITOR_WAIT_SYNC, LIN_MONITOR_WAIT_PID, LIN_MONITOR_RECEIVE } lin_monitor_state;

typedef struct {
    lin_monitor_state state;
    /// ID of the frame being received.
    uint8_t           id;
    /// Data bytes and checksum of the frame being received.
    uint8_t           rx_buf[MINLIN_MAX_PAYLOAD_BYTES + 1];
    /// Number of bytes received into @c rx_buf.
    uint8_t           rx_count;
    /// Tick when the last byte was received.
    uint32_t          last_byte_ms;

    /// Completed frame waiting to be taken.
    minlin_frame ready;
    bool         ready_valid;
    /// A completed frame was dropped because the previous one wasn't taken yet.
    bool         overrun;
    /// A malformed frame was received (bad sync, parity, or checksum).
    bool         frame_error;
} lin_monitor;

/// Resets the monitor to wait for the next BREAK and discards any frame waiting to be taken.
void lin_monitor_reset(lin_monitor* self);

/// Processes one received byte; @p is_break tells if BREAK was detected before or with this byte.
void lin_monitor_receive_byte(
    lin_monitor* self, const minlin_config* config, uint8_t received_byte, bool is_break, uint32_t now_ms);

/// Completes the frame being received if no bytes have been received for @p timeout_ms.
void lin_monitor_poll(lin_monitor* self, const minlin_config* config, uint32_t now_ms, uint32_t timeout_ms);

/// Takes the completed frame into @p out_frame. Returns false if there was no frame to take.
bool lin_monitor_take_frame(lin_monitor* self, minlin_frame* out_frame);

/// Returns and clears the overrun flag.
bool lin_monitor_take_overrun(lin_monitor* self);

/// Returns and clears the frame error flag.
bool lin_monitor_take_frame_error(lin_monitor* self);
