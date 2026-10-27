"""Bus timing. Host timestamps have USB polling and scheduling jitter (a few ms), so timing is
measured as averages over many frames instead of single frame intervals."""

import time

import pytest

from usblincard import T0, auto_slot_ms, r0, t1

pytestmark = pytest.mark.slow


# LIN commander clock tolerance is +-0.5 % (needs CRS syncing HSI48 to USB SOF).
@pytest.mark.parametrize("slot_ms", [20, 50])
def test_schedule_period(dev1, dev2, slot_ms):
    dev2.cmd_ok("L")
    dev1.cmd_ok(T0(0x01, slot_ms, b"\x01"))
    dev1.cmd_ok("O")
    time.sleep(0.2)
    frames = dev2.collect_frames(3.0)
    periods_ms = [(b.t - a.t) * 1000 for a, b in zip(frames, frames[1:])]
    mean_ms = (frames[-1].t - frames[0].t) * 1000 / (len(frames) - 1)
    print(f"slot {slot_ms} ms: mean {mean_ms:.3f} ms, min {min(periods_ms):.1f} ms, max {max(periods_ms):.1f} ms")
    # No missing frames (a gap would be at least two periods).
    assert max(periods_ms) < slot_ms * 1.8
    assert abs(mean_ms - slot_ms) < slot_ms * 0.005


def test_back_to_back_auto_slots(dev1, dev2):
    ids = list(range(0x10, 0x18))
    for i in ids:
        dev2.cmd_ok(t1(i, bytes([i] * 8)))
    dev2.cmd_ok("O")
    for i in ids:
        dev1.cmd_ok(r0(i, 8))
    dev1.cmd_ok("O")
    time.sleep(0.2)
    duration = 5.0
    frames = dev1.collect_frames(duration)

    assert all(f.data == bytes([f.id] * 8) for f in frames)
    seen = [f.id for f in frames]
    start = ids.index(seen[0])
    assert seen == [ids[(start + n) % len(ids)] for n in range(len(seen))]
    expected = duration * 1000 / auto_slot_ms(8, 19200)
    print(f"{len(frames)} frames, expected {expected:.0f}")
    assert len(frames) >= expected * 0.98
