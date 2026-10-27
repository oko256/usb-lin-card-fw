"""Bus errors and mode changes: the devices must recover and keep working correctly."""

import time

import pytest

from usblincard import FLAG_LIN_FRAME, FLAG_LIN_TX_FAILED, FLAG_NOISE, FLAG_RX_OVERRUN, R0, T0, r1, t0, t1


def assert_exchange_works(dev1, dev2):
    dev2.cmd_ok("L")
    dev1.cmd_ok("O")
    dev1.send_frame(0x05, b"\xca\xfe")
    f = dev2.wait_frame()
    assert (f.id, f.data) == (0x05, b"\xca\xfe")
    dev1.cmd_ok("C")
    dev2.cmd_ok("C")


@pytest.mark.expect_flags
def test_recovers_from_bus_contention(dev1, dev2):
    # Two commanders with different periods: their frames are bound to collide.
    dev1.cmd_ok(T0(0x01, 10, bytes(range(8))))
    dev2.cmd_ok(T0(0x02, 7, bytes(range(8, 16))))
    dev1.cmd_ok("O")
    dev2.cmd_ok("O")
    time.sleep(1.0)
    for d in (dev1, dev2):
        d.cmd_ok("V")  # Still alive.
        d.cmd_ok("C")
    time.sleep(0.05)
    flags = [d.flags() for d in (dev1, dev2)]
    print(f"flags after contention: {[hex(f) for f in flags]}")
    errors = FLAG_LIN_FRAME | FLAG_LIN_TX_FAILED | FLAG_NOISE | FLAG_RX_OVERRUN
    assert any(f & errors for f in flags)

    for d in (dev1, dev2):
        d.cmd_ok("w")
        d.clear_frames()
    assert_exchange_works(dev1, dev2)
    assert [d.flags() for d in (dev1, dev2)] == [0, 0]


@pytest.mark.expect_flags
def test_mode_churn_under_traffic(dev1, dev2):
    dev1.cmd_ok(R0(0x10, 15, 2))  # dev2 responds
    dev1.cmd_ok(T0(0x20, 15, b"\x20\x21"))  # dev2 subscribes
    dev1.cmd_ok("O")
    dev2.cmd_ok(t1(0x10, b"\x10\x11"))
    dev2.cmd_ok(r1(0x20, 2))
    for _ in range(50):
        for cmd in ("O", "C", "L", "C"):
            dev2.cmd_ok(cmd)
            time.sleep(0.003)

    # Closed: nothing is reported.
    time.sleep(0.1)
    assert dev2.collect_frames(0.3) == []

    dev2.cmd_ok("O")
    time.sleep(0.1)
    for d in (dev1, dev2):
        d.flags()  # Clear the errors from the churn.
        d.clear_frames()
    time.sleep(0.6)
    assert {f.text for f in dev1.frames()} == {"t01021011"}
    assert {f.text for f in dev2.frames()} == {"t02022021"}
    assert len(dev1.frames()) >= 15 and len(dev2.frames()) >= 15
    assert [d.flags() for d in (dev1, dev2)] == [0, 0]


@pytest.mark.expect_flags
def test_response_length_mismatch(dev1, dev2):
    dev2.cmd_ok(t1(0x10, b"\x01\x02"))
    dev2.cmd_ok("O")
    dev1.cmd_ok(R0(0x10, 20, 4))
    dev1.cmd_ok("O")
    time.sleep(0.3)
    dev1.cmd_ok("C")
    assert dev1.frames() == []
    assert dev1.flags() & FLAG_LIN_FRAME
    assert dev2.flags() == 0


@pytest.mark.slow
def test_soak(dev1, dev2, pytestconfig):
    schedule = [
        (0x01, 15, bytes(range(8))),
        (0x02, 10, b"\x02"),
        (0x3C, 15, bytes(range(0x80, 0x88))),
        (0x2A, 12, b"\xde\xad\xbe\xef"),
    ]
    expected = {lin_id: data for lin_id, _, data in schedule}
    dev2.cmd_ok("L")
    for lin_id, slot_ms, data in schedule:
        dev1.cmd_ok(T0(lin_id, slot_ms, data))
    dev1.cmd_ok("O")
    time.sleep(0.2)
    duration = pytestconfig.getoption("--soak-seconds")
    frames = dev2.collect_frames(duration)

    ids = [lin_id for lin_id, _, _ in schedule]
    seen = [f.id for f in frames]
    start = ids.index(seen[0])
    assert seen == [ids[(start + n) % len(ids)] for n in range(len(seen))]
    assert all(f.data == expected[f.id] for f in frames)
    cycle_ms = sum(slot_ms for _, slot_ms, _ in schedule)
    expected_count = duration * 1000 / cycle_ms * len(schedule)
    print(f"{len(frames)} frames, expected {expected_count:.0f}")
    assert len(frames) >= expected_count * 0.98
