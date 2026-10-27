/**
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Olli Kokko <https://github.com/oko256/usb-lin-card-fw>
 */

#include "app.h"
#include "board.h"
#include "lin_monitor.h"
#include "minlin/minlin.h"
#include "minlin/minlin_hw.h"
#include "slcan.h"

/// How long does LED indicate a receive?
#define RECEIVE_INDICATOR_PERIOD_MS 1000
/// LED blink on-time and periods without and with recent receive activity.
#define LED_ON_MS                   25
#define LED_PERIOD_IDLE_MS          2000
#define LED_PERIOD_ACTIVE_MS        150

static minlin_commander s_commander;
static minlin_responder s_responder;
static lin_monitor      s_monitor;

/// BREAK flag of the byte currently being processed by the receive interrupt; consumed by minlin
/// via minlin_hw_check_and_clear_break().
static bool s_rx_break = false;

/// Receive activity for the LED indication.
static volatile uint32_t s_last_rx_ms         = 0;
static volatile bool     s_rx_seen            = false;
static uint32_t          s_led_cycle_start_ms = 0;

bool minlin_hw_check_and_clear_break(void)
{
    const bool had_break = s_rx_break;
    s_rx_break           = false;
    return had_break;
}

void minlin_responder_subscribe_callback(const minlin_frame* frame) { slcan_send_frame(frame); }

bool minlin_responder_publish_callback(minlin_frame* frame)
{
    (void)frame; // UNUSED
    // Payload we want to publish is already stored in the frame struct.
    return true;
}

/// Latches error flags for the minlin results that indicate a problem.
static void s_latch_minlin_result(minlin_res res)
{
    switch (res) {
    case MINLIN_RES_OK:
        break;
    case MINLIN_RES_TRANSMIT_FAILED:
        slcan_latch_error_flag(SLCAN_ERROR_LIN_TX_FAILED);
        break;
    case MINLIN_RES_RECEIVE_OVERRUN:
        slcan_latch_error_flag(SLCAN_ERROR_DATA_OVERRUN);
        break;
    default:
        // Malformed frames, missing responses, unexpected BREAKs, and rejected configuration.
        slcan_latch_error_flag(SLCAN_ERROR_LIN_FRAME);
        break;
    }
}

/// Applies given schedule and responder frames to minlin. This also restarts the schedule from its
/// first entry and resets the responder state.
static void s_apply_tables(
    const minlin_schedule_entry* schedule, uint8_t schedule_count, bool loop, minlin_frame* frames,
    uint8_t frames_count)
{
    s_latch_minlin_result(minlin_commander_set_schedule(&s_commander, schedule, schedule_count, loop));
    s_latch_minlin_result(minlin_responder_set_frames(&s_responder, frames, frames_count));
}

/// Applies the schedule and responder frames configured by the client.
static void s_apply_configured_tables(void)
{
    uint8_t                      schedule_count = 0;
    uint8_t                      frames_count   = 0;
    const minlin_schedule_entry* schedule       = slcan_schedule(&schedule_count);
    minlin_frame*                frames         = slcan_responder_frames(&frames_count);
    s_apply_tables(schedule, schedule_count, true, frames, frames_count);
}

/// Resets the responder to wait for the next BREAK, keeping its configuration. Setting new frames
/// keeps a header that is being received, but after a mode change it must not be continued.
static void s_reset_responder(void)
{
    const uint32_t      irq    = board_irq_save();
    const minlin_config config = s_responder.config;
    minlin_responder_init(&s_responder);
    s_responder.config = config;
    s_rx_break         = false;
    board_irq_restore(irq);
}

/// Runs commander and responder, and finishes the single frame transmit when it's done.
static void s_run_commander_and_responder(uint32_t now_ms)
{
    s_latch_minlin_result(minlin_commander_task(&s_commander, now_ms));
    s_latch_minlin_result(minlin_responder_task(&s_responder));

    if (slcan_single_frame_pending() && s_commander.stopped && s_commander.frame_slot_time_left_ms == 0) {
        // The single frame schedule has finished and its frame slot (including the response) has
        // passed. Return to the configured tables when open, or to just monitoring.
        slcan_single_frame_done();
        if (slcan_get_mode() == SLCAN_MODE_OPEN) {
            s_apply_configured_tables();
        } else {
            s_apply_tables(NULL, 0, false, NULL, 0);
        }
    }
}

/// Completes timed out monitored frames and sends them to the client.
static void s_run_monitor(void)
{
    const uint32_t timeout_ms = slcan_max_lin_byte_time_ms();
    minlin_frame   frame;

    const uint32_t irq = board_irq_save();
    // Read the tick only now: bytes may have been received with a later tick during a blocking
    // transmit after this main loop iteration started.
    lin_monitor_poll(&s_monitor, &s_responder.config, board_tick_ms(), timeout_ms);
    const bool have_frame  = lin_monitor_take_frame(&s_monitor, &frame);
    const bool overrun     = lin_monitor_take_overrun(&s_monitor);
    const bool frame_error = lin_monitor_take_frame_error(&s_monitor);
    board_irq_restore(irq);

    if (have_frame) {
        slcan_send_frame(&frame);
    }
    if (overrun) {
        slcan_latch_error_flag(SLCAN_ERROR_DATA_OVERRUN);
    }
    if (frame_error) {
        slcan_latch_error_flag(SLCAN_ERROR_LIN_FRAME);
    }
}

static void s_handle_cmd(slcan_cmd cmd)
{
    switch (cmd) {
    case SLCAN_CMD_GOTO_BOOTLOADER:
        board_reboot_to_bootloader();
        break;
    case SLCAN_CMD_OPEN:
    case SLCAN_CMD_CLOSE:
        // Start from a clean state: schedule from the beginning and no stale responder state.
        s_reset_responder();
        s_apply_configured_tables();
        break;
    case SLCAN_CMD_START_MONITORING: {
        const uint32_t irq = board_irq_save();
        lin_monitor_reset(&s_monitor);
        board_irq_restore(irq);
        s_reset_responder();
        s_apply_tables(NULL, 0, false, NULL, 0);
    } break;
    case SLCAN_CMD_SET_ENHANCED_CHECKSUM:
        s_responder.config.classic_checksum = false;
        break;
    case SLCAN_CMD_SET_CLASSIC_CHECKSUM:
        s_responder.config.classic_checksum = true;
        break;
    case SLCAN_CMD_SET_DIAG_FRAMES_SPECIAL:
        s_responder.config.diagnostic_frames_as_normal_frames = false;
        break;
    case SLCAN_CMD_SET_DIAG_FRAMES_NORMAL:
        s_responder.config.diagnostic_frames_as_normal_frames = true;
        break;
    case SLCAN_CMD_RELOAD_COMMANDER_AND_RESPONDER:
        // While a single frame is pending, the configured tables are applied when it's done.
        if (!slcan_single_frame_pending()) {
            s_apply_configured_tables();
        }
        break;
    case SLCAN_CMD_SINGLE_FRAME:
        s_apply_tables(slcan_single_frame_schedule(), 1, false, slcan_single_frame(), 1);
        break;
    default:
        break;
    }
}

static void s_update_led(uint32_t now_ms)
{
    const bool     active = s_rx_seen && (now_ms - s_last_rx_ms) < RECEIVE_INDICATOR_PERIOD_MS;
    const uint32_t period = active ? LED_PERIOD_ACTIVE_MS : LED_PERIOD_IDLE_MS;
    if (now_ms - s_led_cycle_start_ms >= period) {
        s_led_cycle_start_ms = now_ms;
    }
    board_set_led((now_ms - s_led_cycle_start_ms) < LED_ON_MS);
}

void app_init(void)
{
    minlin_commander_init(&s_commander);
    minlin_responder_init(&s_responder);
    lin_monitor_reset(&s_monitor);
    s_rx_break           = false;
    s_rx_seen            = false;
    s_led_cycle_start_ms = board_tick_ms();
    s_apply_configured_tables();
}

void app_poll(void)
{
    const uint32_t   now_ms = board_tick_ms();
    const slcan_mode mode   = slcan_get_mode();

    if (mode == SLCAN_MODE_OPEN || (mode == SLCAN_MODE_MONITOR && slcan_single_frame_pending())) {
        s_run_commander_and_responder(now_ms);
    }
    if (mode == SLCAN_MODE_MONITOR) {
        s_run_monitor();
    }

    s_handle_cmd(slcan_process_input());

    s_update_led(board_tick_ms());
}

void app_lin_rx_byte(uint8_t received_byte, bool is_break)
{
    const uint32_t now_ms = board_tick_ms();
    s_last_rx_ms          = now_ms;
    s_rx_seen             = true;

    const slcan_mode mode = slcan_get_mode();
    if (mode == SLCAN_MODE_OPEN || (mode == SLCAN_MODE_MONITOR && slcan_single_frame_pending())) {
        s_rx_break = is_break;
        s_latch_minlin_result(minlin_responder_receive_byte(&s_responder, received_byte));
    }
    if (mode == SLCAN_MODE_MONITOR) {
        lin_monitor_receive_byte(&s_monitor, &s_responder.config, received_byte, is_break, now_ms);
    }
}
