"""LIN parameters (baud rate, checksum types) that must match between the nodes."""

import time

import pytest

from usblincard import BAUD_PRESETS, FLAG_LIN_FRAME, auto_slot_ms, r0, t0, t1


@pytest.mark.parametrize("preset", BAUD_PRESETS.keys())
def test_baud_rates(dev1, dev2, preset):
    baud = BAUD_PRESETS[preset]
    for d in (dev1, dev2):
        d.cmd_ok(preset)

    # Single frames in both directions with the shortest and longest payloads.
    for sender, receiver in ((dev1, dev2), (dev2, dev1)):
        receiver.cmd_ok("L")
        sender.cmd_ok("O")
        for data in (b"\x5a", bytes(range(0xF8, 0x100))):
            receiver.clear_frames()
            sender.send_frame(0x15, data)
            f = receiver.wait_frame()
            assert (f.id, f.data) == (0x15, data)
        sender.cmd_ok("C")
        receiver.cmd_ok("C")

    # Request and response back to back with the automatic (worst-case) slot time: a too short slot
    # or transmit timeout would show up as missing frames or error flags.
    data = bytes(range(1, 9))
    dev2.cmd_ok(t1(0x21, data))
    dev2.cmd_ok("O")
    dev1.cmd_ok(r0(0x21, len(data)))
    dev1.cmd_ok("O")
    time.sleep(0.1)
    duration = 1.5
    frames = dev1.collect_frames(duration)
    expected = duration * 1000 / auto_slot_ms(len(data), baud)
    assert len(frames) >= expected * 0.9
    assert all((f.id, f.data) == (0x21, data) for f in frames)


@pytest.mark.expect_flags
def test_baud_rate_mismatch_gives_no_frames(dev1, dev2):
    dev2.cmd_ok("S2")  # 9600 vs. 19200
    dev2.cmd_ok("L")
    dev1.cmd_ok("O")
    for i in range(10):
        dev1.send_frame(0x10 + i, bytes([i, 0x00, 0xFF]))
    time.sleep(0.1)
    assert dev2.frames() == []
    print(f"dev2 flags 0x{dev2.flags():02X}")
    assert dev1.flags() == 0


def uses_classic(checksum_cmd: str, diag_cmd: str, lin_id: int) -> bool:
    return checksum_cmd == "x2" or (lin_id in (0x3C, 0x3D) and diag_cmd == "x3")


@pytest.mark.expect_flags
@pytest.mark.parametrize("lin_id", [0x10, 0x3C])
@pytest.mark.parametrize("diag", ["x3", "x4"])
@pytest.mark.parametrize("rx_checksum", ["x1", "x2"])
@pytest.mark.parametrize("tx_checksum", ["x1", "x2"])
def test_checksum_types(dev1, dev2, tx_checksum, rx_checksum, diag, lin_id):
    dev1.cmd_ok(tx_checksum)
    dev2.cmd_ok(rx_checksum)
    for d in (dev1, dev2):
        d.cmd_ok(diag)
    dev2.cmd_ok("L")
    dev1.cmd_ok("O")
    data = bytes([0x01, 0x80, 0xFF, 0x7F])
    dev1.send_frame(lin_id, data)
    time.sleep(0.1)

    if uses_classic(tx_checksum, diag, lin_id) == uses_classic(rx_checksum, diag, lin_id):
        assert [(f.id, f.data) for f in dev2.frames()] == [(lin_id, data)]
        assert dev2.flags() == 0
    else:
        assert dev2.frames() == []
        assert dev2.flags() == FLAG_LIN_FRAME
    assert dev1.flags() == 0
