/**
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/usb-lin-card-fw>
 */

#pragma once

#include "minlin/minlin.h"
#include <stdbool.h>
#include <stdint.h>

/// Converts nibble (half byte) to hexadecimal for slcan output.
#define NIBBLE_TO_HEX(n) ((n) > 9 ? (n) - 10 + 'A' : (n) + '0')
/// Extracts the higher nibble from a full byte.
#define HIGH_NIBBLE(b)   ((b) >> 4)
/// Extracts the lower nibble from a full byte.
#define LOW_NIBBLE(b)    ((b) & 0x0F)

/// Number of commander schedule entries and responder frames that can be configured.
#define SLCAN_TABLE_MAX_SIZE 32
/// Maximum length of one output line.
#define SLCAN_TX_LINE_MAX    64

typedef enum {
    /// LIN receiver overrun: received byte(s) were lost.
    SLCAN_ERROR_RX_OVERRUN     = 0x01,
    /// Reserved (not used).
    SLCAN_ERROR_TX_OVERRUN     = 0x02,
    /// LIN bus has noise interference distorting transmitted bits.
    SLCAN_ERROR_NOISE          = 0x04,
    /// New frame overran the frame waiting to be transmitted to client before transmission happened.
    SLCAN_ERROR_DATA_OVERRUN   = 0x08,
    /// Reserved (not used: USB reception is flow controlled, so its buffer can't overrun).
    SLCAN_ERROR_USB_RX_OVERRUN = 0x10,
    /// Transmit buffer towards USB serial line was overrun.
    SLCAN_ERROR_USB_TX_OVERRUN = 0x20,
    /// Transmitting a LIN header or response failed.
    SLCAN_ERROR_LIN_TX_FAILED  = 0x40,
    /// Malformed LIN frame (sync, parity, checksum), missing response, or unexpected BREAK.
    SLCAN_ERROR_LIN_FRAME      = 0x80
} slcan_error;

typedef enum {
    /// Channel closed: LIN traffic is ignored.
    SLCAN_MODE_CLOSED,
    /// Channel open ("O"): commander and responder are running.
    SLCAN_MODE_OPEN,
    /// Monitoring mode ("L"): all frames on the bus are reported.
    SLCAN_MODE_MONITOR
} slcan_mode;

/// Commands that the application needs to act on.
typedef enum {
    SLCAN_CMD_NONE = 0,
    SLCAN_CMD_GOTO_BOOTLOADER,
    SLCAN_CMD_OPEN,
    SLCAN_CMD_CLOSE,
    SLCAN_CMD_START_MONITORING,
    SLCAN_CMD_SET_ENHANCED_CHECKSUM,
    SLCAN_CMD_SET_CLASSIC_CHECKSUM,
    SLCAN_CMD_SET_DIAG_FRAMES_SPECIAL,
    SLCAN_CMD_SET_DIAG_FRAMES_NORMAL,
    SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER,
    SLCAN_CMD_SINGLE_FRAME
} slcan_cmd;

/// Initializes (or resets) slcan state; call before USB is started.
void slcan_init(void);

/// Append (copy) given input data to slcan's own input buffer. Copies all or nothing; returns false
/// if it didn't fit, in which case the caller should retry later.
bool      slcan_append_input(const uint8_t* src, uint32_t len);
/// Process possible pending line of input.
slcan_cmd slcan_process_input(void);

/// Set latching error flag that can be read over slcan. Can be called from interrupts.
void slcan_latch_error_flag(slcan_error e);

/// Append byte to transmit buffer.
void slcan_tx_append_byte(uint8_t byte);
/// Append a byte as a hexadecimal to transmit buffer.
void slcan_tx_append_byte_as_hex(uint8_t byte);
/// Append OK terminator to transmit buffer.
void slcan_tx_append_ok_term(void);
/// Append error terminator to transmit buffer.
void slcan_tx_append_error_term(void);
/// Send the transmit buffer and clear the buffer (i.e. zero the cursor position).
void slcan_tx_send(void);
/// Sends a received LIN frame to the client.
void slcan_send_frame(const minlin_frame* frame);

/// Returns the current channel mode.
slcan_mode slcan_get_mode(void);

/// Returns true while a single frame ("t0") is waiting to be transmitted.
bool slcan_single_frame_pending(void);
/// Application calls this when the pending single frame has been transmitted.
void slcan_single_frame_done(void);

/// Returns the configured commander schedule and sets @p out_count to its number of entries.
const minlin_schedule_entry* slcan_schedule(uint8_t* out_count);
/// Returns the configured responder frames and sets @p out_count to their number.
minlin_frame*                slcan_responder_frames(uint8_t* out_count);
/// Returns the schedule entry of the single frame.
const minlin_schedule_entry* slcan_single_frame_schedule(void);
/// Returns the responder frame of the single frame.
minlin_frame*                slcan_single_frame(void);

/// Returns the maximum time in milliseconds a single byte can take with current baud rate.
uint32_t slcan_max_lin_byte_time_ms(void);
/// Estimates the worst-case frame time in milliseconds for a frame with given payload size at current baud rate.
uint32_t slcan_worst_case_frame_time_ms(uint8_t payload_size);
