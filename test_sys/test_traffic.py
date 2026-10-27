"""Functional LIN traffic between the two devices at the default 19200 baud."""

import random
import time

import pytest

from usblincard import FLAG_LIN_FRAME, R0, T0, r1, t0, t1


def test_traffic_between_devices(dev1, dev2):
    dev2.cmd_ok("L")
    dev1.cmd_ok("O")
    dev1.cmd_ok("t0052CAFE")
    f = dev2.wait_frame()
    assert f.text == "t0052CAFE"


def test_all_ids(dev1, dev2):
    # Checks the PID parity of every ID on both ends.
    dev2.cmd_ok("L")
    dev1.cmd_ok("O")
    for lin_id in range(0x40):
        data = bytes([lin_id, 0xA5])
        dev2.clear_frames()
        dev1.send_frame(lin_id, data)
        f = dev2.wait_frame()
        assert (f.id, f.data) == (lin_id, data)
    time.sleep(0.05)
    assert len(dev2.frames()) == 1


@pytest.mark.parametrize("length", range(1, 9))
def test_payload_lengths(dev1, dev2, length):
    data = random.Random(length).randbytes(length)
    dev2.cmd_ok("L")
    dev1.cmd_ok("O")
    for lin_id in (0x01, 0x2A):
        dev2.clear_frames()
        dev1.send_frame(lin_id, data)
        f = dev2.wait_frame()
        assert (f.id, f.data) == (lin_id, data)


def test_single_frame_from_monitoring_mode(dev1, dev2):
    dev1.cmd_ok("L")
    dev2.cmd_ok("L")
    dev1.cmd_ok(t0(0x12, b"\x01\x02\x03"))
    # Sender's own monitor sees the frame on the bus as well.
    for d in (dev1, dev2):
        f = d.wait_frame()
        assert (f.id, f.data) == (0x12, b"\x01\x02\x03")
    time.sleep(0.1)
    assert len(dev1.frames()) == 1 and len(dev2.frames()) == 1


def test_commander_requests_response_from_responder(dev1, dev2):
    # Start responders before commanders so that no header goes unanswered.
    dev2.cmd_ok(t1(0x10, bytes.fromhex("DEADBEEF")))
    dev2.cmd_ok("O")
    dev1.cmd_ok(R0(0x10, 50, 4))
    dev1.cmd_ok("O")
    frames = dev1.collect_frames(1.0)
    assert len(frames) >= 18
    assert all(f.text == "t0104DEADBEEF" for f in frames)
    assert dev2.frames() == []  # Published frames aren't reported.


def test_command_frame_to_subscriber(dev1, dev2):
    dev2.cmd_ok(r1(0x20, 2))
    dev2.cmd_ok("O")
    dev1.cmd_ok(T0(0x20, 50, bytes.fromhex("CAFE")))
    dev1.cmd_ok("O")
    frames = dev2.collect_frames(1.0)
    assert len(frames) >= 18
    assert all(f.text == "t0202CAFE" for f in frames)
    assert dev1.frames() == []


def assert_cyclic(ids: list[int], schedule: list[int], min_cycles: int):
    """Asserts that ids follow the schedule order cyclically, starting from any position."""
    start = schedule.index(ids[0])
    expected = [schedule[(start + i) % len(schedule)] for i in range(len(ids))]
    assert ids == expected
    assert len(ids) >= min_cycles * len(schedule)


@pytest.mark.expect_flags
def test_mixed_schedule_is_monitored_in_order(dev1, dev2):
    dev2.cmd_ok("L")
    dev1.cmd_ok(T0(0x01, 20, b"\x11\x22"))
    dev1.cmd_ok(R0(0x02, 20, 2))  # nobody responds: header only
    dev1.cmd_ok(T0(0x3C, 20, bytes(range(8))))
    dev1.cmd_ok(T0(0x05, 20, b"\x55"))
    dev1.cmd_ok("O")
    frames = dev2.collect_frames(1.0)
    dev1.cmd_ok("C")

    assert_cyclic([f.id for f in frames], [0x01, 0x02, 0x3C, 0x05], min_cycles=10)
    expected = {0x01: b"\x11\x22", 0x02: b"", 0x3C: bytes(range(8)), 0x05: b"\x55"}
    assert all(f.data == expected[f.id] for f in frames)
    # Commander's own subscription got no response.
    assert dev1.flags() & FLAG_LIN_FRAME
    assert dev2.flags() == 0


def test_full_schedule(dev1, dev2):
    dev2.cmd_ok("L")
    for i in range(32):
        dev1.cmd_ok(T0(i, 10, bytes([i])))
    dev1.cmd_ok("O")
    frames = dev2.wait_frames(32 * 3, timeout=2.0)
    ids = [f.id for f in frames]
    # The first frames may be from the middle of the schedule; check at least two full cycles.
    assert_cyclic(ids, list(range(32)), min_cycles=2)
    assert all(f.data == bytes([f.id]) for f in frames)


def test_single_frame_during_schedule(dev1, dev2):
    schedule = [0x01, 0x02, 0x03]
    dev2.cmd_ok("L")
    for i in schedule:
        dev1.cmd_ok(T0(i, 30, bytes([i])))
    dev1.cmd_ok("O")
    time.sleep(0.3)
    dev2.clear_frames()
    dev1.cmd_ok(t0(0x33, b"\xab\xcd"))
    time.sleep(0.5)
    frames = dev2.frames()
    singles = [i for i, f in enumerate(frames) if f.id == 0x33]
    assert len(singles) == 1
    assert frames[singles[0]].data == b"\xab\xcd"
    # Schedule resumes from its first entry after the single frame.
    after = [f.id for f in frames[singles[0] + 1 :]]
    assert_cyclic(after, schedule, min_cycles=3)
    assert after[0] == schedule[0]
    before = [f.id for f in frames[: singles[0]]]
    assert_cyclic(before, schedule, min_cycles=0)


def test_closed_device_ignores_bus(dev1, dev2):
    dev2.cmd_ok(t1(0x10, b"\x01\x02\x03\x04"))  # configured but not opened
    dev1.cmd_ok(R0(0x10, 50, 4))
    dev1.cmd_ok("O")
    time.sleep(0.5)
    assert dev1.frames() == []
    assert dev1.flags() & FLAG_LIN_FRAME  # no response
    assert dev2.frames() == []

    dev2.cmd_ok("O")
    time.sleep(0.2)
    dev1.flags()  # Clear the errors from before dev2 was opened.
    frames = dev1.collect_frames(0.5)
    assert len(frames) >= 8
    assert all(f.text == "t010401020304" for f in frames)


def test_close_stops_traffic(dev1, dev2):
    dev2.cmd_ok("L")
    dev1.cmd_ok(T0(0x07, 20, b"\x07"))
    dev1.cmd_ok("O")
    assert len(dev2.collect_frames(0.3)) >= 10
    dev1.cmd_ok("C")
    time.sleep(0.1)
    assert dev2.collect_frames(0.5) == []
