"""Command interface over real USB. Parser details are covered by host unit tests; these check the
USB CDC path (fragmentation, flow control, recovery) and basic replies on the real devices."""

import re
import time

import pytest

from usblincard import R0, t0

# Reserved stack size (_Min_Stack_Size in the linker script).
STACK_SIZE = 0x400


def test_version(devs):
    for d in devs:
        r = d.cmd_ok("V")
        assert re.fullmatch(r"V[0-9A-F]{4}", r.payload)


def test_serial_numbers_are_unique(dev1, dev2):
    serials = []
    for d in (dev1, dev2):
        r = d.cmd_ok("N")
        assert re.fullmatch(r"N[0-9A-F]{24}", r.payload)
        serials.append(r.payload)
    assert serials[0] != serials[1]


def test_mode_rules(dev1):
    assert not dev1.cmd("C").ok  # already closed
    assert not dev1.cmd(t0(0x05, b"\x01")).ok  # single frame needs open channel
    dev1.cmd_ok("O")
    assert not dev1.cmd("O").ok
    assert not dev1.cmd("L").ok
    assert not dev1.cmd("S2").ok  # baud rate only when closed
    dev1.cmd_ok("C")
    dev1.cmd_ok("L")
    assert not dev1.cmd("O").ok
    dev1.cmd_ok("C")


def test_unknown_and_malformed_commands(dev1):
    assert not dev1.cmd("Z").ok
    assert not dev1.cmd("boox").ok
    assert not dev1.cmd("R0").ok
    # Device still works.
    dev1.cmd_ok("V")


def test_fragmented_command(dev1):
    # Each byte in its own USB packet.
    for b in b"V\r":
        dev1.send_raw(bytes([b]))
        time.sleep(0.01)
    r = dev1.read_response()
    assert r.ok and r.payload.startswith("V")
    assert dev1.pending_replies() == []


def test_config_burst_is_flow_controlled(dev1):
    # 33 schedule lines in one write is much more than the device's input ring; nothing may be lost.
    lines = [R0(i, 10, 1) for i in range(33)]
    dev1.send_raw("".join(line + "\r" for line in lines).encode())
    replies = [dev1.read_response() for _ in lines]
    assert all(r.ok for r in replies[:32])
    assert not replies[32].ok  # tables full
    time.sleep(0.1)
    assert dev1.pending_replies() == []


def test_recovers_from_garbage_without_line_end(dev1):
    dev1.send_raw(b"A" * 300 + b"\r")
    dev1.send("V")
    errors = 0
    while True:
        r = dev1.read_response()
        if r.ok:
            break
        errors += 1
    assert errors >= 1
    assert r.payload.startswith("V")
    assert dev1.pending_replies() == []


@pytest.mark.last
def test_stack_usage(devs):
    for d in devs:
        r = d.cmd("d1")
        if not r.ok:
            pytest.skip("Not a Debug build")
        used = int(r.payload[1:], 16)
        print(f"{d.name}: stack used {used} / {STACK_SIZE} bytes")
        assert used < STACK_SIZE * 3 // 4
