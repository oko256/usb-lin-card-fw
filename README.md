# USB LIN Card firmware

Firmware for the [USB LIN Card](https://github.com/oko256/usb-lin-card-hw).
The card is an STM32F042 based USB to LIN bus adapter. On the PC it shows up as a USB CDC serial
port that speaks an slcan-like text protocol. With it the card can:

- act as a LIN **commander** (master): run a header schedule and publish command frames,
- act as a LIN **responder** (slave): publish responses and subscribe to frames,
- **monitor** all traffic on the bus.

LIN protocol handling is done by the [minlin](https://github.com/oko256/minlin) library.
This also works as a real-life usage example for the library.
Additionally, the project uses some utilities from [mcutils](https://github.com/oko256/mcutils)
library.

This README has separate sections for [user guide](#user-guide) to get you started as the user of
the firmware, and for [developer notes](#developer-notes) to help you develop the firmware itself
further.

Some of the files in this repository is under ST's licenses, and those files keep their respective
licenses regardless of the root LICENSE file.

# User guide

The card enumerates as a USB CDC ACM device (basically a virtual serial port). It does not need any
special drivers (at least on Linux). On Linux it shows up as `/dev/ttyACMx`.
The serial port settings (baud rate, parity, flow control) don't matter, because the data goes over
USB. The LIN bus baud rate is set with the `S` command (see list of commands below).

Indicator LED blinks once every 2 s when idle, and quickly for 1 s after any received LIN byte.

## Flashing

Download a pre-built binary from releases and flash it on the card with STM32CubeProgrammer or the
CLI. You need to have the MCU in USB DFU bootloader first (either empty MCU, or with `boot` command
if already flashed with this firmware before, see commands below).

```bash
STM32_Programmer_CLI -c port=usb1 -w usb-lin-card-fw.elf -g
```

## Protocol

The text-based protocol is similar to **slcan**, and partially compatible with it. This means that
you can use some of the standard slcan utilities to work with this adapter firmware too.

- **Commands:** each command is one line of ASCII text terminated with CR (`\r`, 0x0D).
  - All non-alphanumeric characters are ignored. So `\r\n` line endings work, and spaces can be used
    for readability, e.g. `R0 10 00032 4`.
  - A raw line (including the terminator and ignored characters) can be at most 32 bytes.
  - An empty line is ignored without a reply.
- **Replies:** every command gets exactly one reply.
  - A successful command replies with its possible payload followed by CR.
  - A failed or unknown command replies with BEL (`\a`, 0x07).
  - The exception is `boot`, which doesn't reply.
- **Numbers** are hexadecimal; both upper and lower case are accepted.
  - `ii`: LIN ID, `00` to `3F`.
  - `l`: payload length, `1` to `8`.
  - `ttttt`: frame slot time in milliseconds, 5 hex digits, non-zero.
  - `dd`: one payload byte; `l` indicates how long the payload is in bytes.

## Modes

The device can be in one of 3 modes:

- **Closed:** LIN traffic is ignored. Baud rate can be changed. Powers up in this mode.
- **Open:** Commander runs the configured schedule. Responder publishes and subscribes.
- **Monitoring:** All frames on the bus are reported to user. Configured tables (schedule, publish
  subscribe) are not used, but single frames can be sent.

The configured tables and settings are kept when the mode changes. Opening restarts the schedule
from its first entry. Everything is reset to defaults on power-up.

## Commands

### Channel and device

| Command | Reply | Description |
|---|---|---|
| `O` | CR or BEL | Open the channel: commander and responder run with the configured tables. Replies BEL if not closed before invoking the command. |
| `L` | CR or BEL | Open in monitoring mode: every frame on the bus is reported. Replies BEL if not closed before invoking the command. |
| `C` | CR or BEL | Close the channel. Also cancels a pending single frame. Replies BEL if already closed. |
| `Sx` | CR or BEL | Set LIN baud rate: `S1` 2400, `S2` 9600, `S3` 10400, any other `x` 19200 (default). Replies BEL if the channel isn't closed. |
| `x1` | CR | Use enhanced checksum (LIN 2.x, default). |
| `x2` | CR | Use classic checksum (LIN 1.x). |
| `x3` | CR | Diagnostic frames (IDs `3C` and `3D`) always use the classic checksum (default). |
| `x4` | CR | Diagnostic frames are treated like normal frames. |
| `V` | `Vhhff` CR | Hardware version `hh` and firmware version `ff`. |
| `N` | `N` + 24 hex digits, CR | Serial number (MCU unique ID). |
| `F` | `Fxx` CR | Read and clear the latched error flags (see [Error flags](#error-flags)). |
| `boot` | none | Reboot into the STM32 USB DFU bootloader for flashing. |
| `d1` | `dxxxxxxxx` CR | Debug builds only: stack usage high-water mark in bytes. |

The `x` settings take effect immediately and apply to everything that is sent, received and monitored.

### Commander schedule and responder table

The commander runs a schedule of headers, each followed by its frame slot. The schedule repeats from the
beginning after its last entry. The responder has a table of IDs to publish (send the response) or
subscribe to (receive the response). Each table holds at most **32** entries.

Changes take effect immediately. When the channel is open, the schedule then restarts from its first entry.

| Command | Description |
|---|---|
| `R0iitttttl` | Add a header for ID `ii` to the schedule with slot time `ttttt` ms. Also subscribes to its `l`-byte response, which is reported when received. |
| `r0iil` | Same as `R0`, but the slot time is calculated from the baud rate and payload length. |
| `T0iitttttldd...` | Add a command frame to the schedule: header for `ii` with slot time `ttttt` ms, and the card itself publishes the `l`-byte response `dd...`. |
| `r1iil` | Subscribe to responses of ID `ii` with `l` bytes; received frames are reported. No header is scheduled. |
| `t1iildd...` | Publish the response `dd...` whenever a header for ID `ii` is received. No header is scheduled. |
| `w` | Wipe both the schedule and the responder table. |

All of them reply CR, or BEL if the command is malformed or a needed table is full. `R0`, `r0` and
`T0` need room in both tables.

The automatic slot time of `r0` and `t0` is the LIN maximum frame time with a 2.0 margin instead of 1.4:
`ceil((88 + 20 × l) bit times)`. E.g. 6 ms for 1 byte and 13 ms for 8 bytes at 19200 baud.

### Transmit single frame

Command `t0iildd...` sends one full frame (header and response `dd...`) for ID `ii`. This works in
both open and monitoring modes. In open mode, the current schedule is stopped, the single frame is
transmitted, and then the schedule restarts from its first entry.

Replies with CR if successful, and BEL if the channel is closed, or while the previous single frame
is still being sent. In that case, retry after the frame's slot time.

### Received frames

| Line | Meaning |
|---|---|
| `t0iildd...` | A frame with ID `ii` and `l` payload bytes `dd...` (checksum was valid). |
| `t0ii0` | Monitoring mode only: a header with ID `ii` that got no response. |

- **Open mode:** subscribed frames (`R0`, `r0`, `r1`) are reported. Frames the card publishes itself
  are not.
- **Monitoring mode:** every frame on the bus is reported, including the ones the card sent with `t0`.
- **Frames with errors** (bad checksum, wrong length, no response) are not reported. They latch error
  flag `0x80` instead.

### Error flags

Errors latch flags until they are read with `F`. Reading clears them. Several flags can be set at once.

| Flag (hex) | Name | Meaning |
|---|---|---|
| `01` | RX overrun | LIN receiver overrun: received bytes were lost. |
| `02` | - | Reserved. |
| `04` | Noise | A received byte had noise; the byte was dropped. Typical with bus collisions or a baud rate mismatch. |
| `08` | Data overrun | A received frame overran the previous one before it could be sent to USB. |
| `10` | - | Reserved. |
| `20` | USB TX overrun | Output to USB didn't fit in the buffer; output was lost. |
| `40` | LIN TX failed | Sending a header or response failed (e.g. timed out). |
| `80` | LIN frame error | Malformed frame (sync, parity, checksum), a missing or too short response, an unexpected BREAK, or a rejected configuration. |

## Examples

`>` denotes commands sent by user to the adapter. `<` denotes replies received by user from the
adapter. These characters themselves do not appear in the inputs/outputs.

Commander requesting a 4-byte response from ID `10` every 50 ms, which another node answers:

```
> R0 10 00032 4      < CR
> O                  < CR
                     < t0104DEADBEEF   (every 50 ms)
```

Responder answering ID `10` with `DEADBEEF` whenever it sees the header being sent, and receiving
the command frame with ID `20`:

```
> t1 10 4 DEADBEEF   < CR
> r1 20 2            < CR
> O                  < CR
                     < t0202CAFE       (whenever the commander sends it)
```

Monitoring the bus at 9600 baud and sending one frame:

```
> S2                 < CR
> L                  < CR
> t0 05 2 CAFE       < CR
                     < t0052CAFE       (our own frame, as seen on the bus)
```

Received frames use the standard slcan 11-bit frame format, with the 3-digit CAN ID `0ii`. So
slcan-style tools can parse them, and `t0` is the same format those tools send frames in.

```bash
slcand -o -c -s6 ttyACM0 can0   # create can0 interface that is actually our LIN at 19200 baud in "open" mode
ip link set up can0             # set the interface up
cangen can0 -I 010 -L 4 -D i    # start sending incrementing 4-byte payloads to LIN ID 0x10

# ...or if you want to monitor the LIN bus with candump or wireshark or similar tool:

slcand -l -c -s6 ttyACM0 can0   # create interface in "monitoring" mode instead (-l)
ip link set up can0             # set the interface up
candump can0                    # dump all received frames, you could also use wireshark
```

## Known limitations

- Single LIN channel, at the preset baud rates (2400, 9600, 10400, 19200) only. There's no automatic
  baud rate detection.
- No persistent configuration. Defaults are loaded at power-on.
- Entries can't be edited or removed individually; only the whole tables can be wiped (`w`).
- The payload length of each subscribed or published ID is fixed in the table.
- **LIN features not implemented:**
  - Sleep and wake-up: no go-to-sleep command handling, and no wake-up pulse detection or generation.
  - Event-triggered frame collision resolution.

# Developer notes

## Clone, flash, build

```bash
git clone --recurse-submodules https://github.com/oko256/usb-lin-card-fw.git
cd usb-lin-card-fw
cmake --preset Debug && cmake --build --preset Debug    # or Release
# Send "boot<CR>" to the card first, then:
STM32_Programmer_CLI -c port=usb1 -w build/Debug/usb-lin-card-fw.elf -g
```

## Notes about the architecture

- **Toolchain:** `arm-none-eabi-gcc` 14, CMake >= 3.22, Ninja; STM32CubeMX project `usb-lin-card-fw.ioc`
- **Versioning:** `FIRMWARE_VERSION` and `HARDWARE_VERSION` in `CMakeLists.txt`
- Edit generated files only inside `USER CODE` blocks (`main.c`, `usbd_cdc_if.c/.h`); everything else
  is overwritten on regeneration.
- Clock and peripheral settings belong in the `.ioc`. **CRS must stay enabled:** without it HSI48 runs
  ~0.6% fast, outside the ±0.5% LIN commander tolerance.
- **Everything runs in the main loop** except the USART receive interrupt, which feeds bytes to the
  minlin responder and/or the monitor. Shared state is guarded with `board_irq_save()`/`restore()`
  (PRIMASK save/restore, nesting-safe).
- **Transmits are blocking**, and the card receives its own bytes as the transceiver echo. minlin's
  responder is driven by the echo, so the card's own headers trigger its own responses, and the
  monitor sees the card's own frames.
- **BREAK:** received as `0x00` with a framing error (dropped); LBDF is still set on the next byte. A
  BREAK-flagged `0x55` is SYNC; any other BREAK-flagged byte means SYNC is still to come.
- **USB RX flow control:** when the 128-byte input ring is full, the USB packet is held and reception
  isn't re-armed (host gets NAKs); `CDC_Service_Rx()` retries from the main loop.
- **Mode changes** (`O`/`C`/`L`) fully reset the minlin responder; `minlin_responder_set_frames()`
  alone keeps a half-received header.
- **Single frame (`t0`)** temporarily replaces the minlin tables with one-entry tables. It is done when
  the commander has stopped and the slot time has passed (not on the echo). Then the configured
  tables are restored (open mode) or cleared (monitoring mode), and table changes made meanwhile are
  applied.
- **Monitor** doesn't know frame lengths: a frame ends on the 9th byte after the PID, the next BREAK,
  or 50 bit times of silence. The last byte is the checksum.

## Unit tests

```sh
cd test
cmake --preset test && cmake --build --preset test && ctest --preset test
cmake --preset coverage && cmake --build --preset coverage   # build-coverage/coverage/index.html
```

- Host build of `src/` (except `board_stm32.c` and `stm32_minlin_hw.c`) with the real minlin, Unity,
  ASan/UBSan and `-Werror`. Unity is fetched on first configure.
- `stubs/minlin_hw_stub.c` records bus traffic and can echo it back after a transmit, or during it
  with the tick advancing (`g_bus_live_echo`) like on real HW.
- Coverage needs gcov, fastcov (`pip install fastcov`) and lcov. It reports `src/` only.
- When fixing a bug, first add a host test that reproduces it if at all possible.

## System tests

Run against two cards on the same LIN bus (with bus power and termination). Needs `pytest` and
`pyserial`, and both cards running the build under test.

```sh
cd test_sys
pytest                    # ~70 s
pytest -m "not slow"      # skip timing and soak tests, ~25 s
pytest --dev1=/dev/ttyACM0 --dev2=/dev/ttyACM1 --soak-seconds=300
```

- Ports default to `/tmp/tty-lin-master` and `/tmp/tty-lin-slave` which can be used as convenient
  symlinks to actual `ttyACMx` devices.
- Both cards are reset before each test and must report no error flags after it, unless the test is
  marked `expect_flags`.
- `test_timing.py` checks the schedule period to ±0.5%, which fails if CRS is off.
- Host timestamps have a few ms of jitter, so timing is checked as averages over many frames.
- The stack usage test (`d1`, Debug only) runs last; usage is ~550 of 1024 bytes.
