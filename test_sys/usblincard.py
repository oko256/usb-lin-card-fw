"""Host-side driver for the USB LIN Card slcan-like serial protocol."""

import math
import queue
import threading
import time
from dataclasses import dataclass

import serial

BAUD_RATE = 115200  # Usually doesn't matter because these are USB serial ports.

CR = 0x0D
BEL = 0x07

# Error flags reported by the "F" command.
FLAG_RX_OVERRUN = 0x01
FLAG_NOISE = 0x04
FLAG_DATA_OVERRUN = 0x08
FLAG_USB_TX_OVERRUN = 0x20
FLAG_LIN_TX_FAILED = 0x40
FLAG_LIN_FRAME = 0x80

# "Sx" presets.
BAUD_PRESETS = {"S1": 2400, "S2": 9600, "S3": 10400, "S4": 19200}


@dataclass
class Response:
    payload: str
    ok: bool


@dataclass
class Frame:
    """LIN frame reported by the device as "t0iildd..."; payload is empty for a header only."""

    id: int
    data: bytes
    t: float
    text: str


def parse_frame(text: str, t: float) -> Frame:
    assert len(text) >= 5 and text.startswith("t0"), f"Malformed frame line {text!r}"
    length = int(text[4], 16)
    data = bytes.fromhex(text[5:])
    assert len(data) == length, f"Frame line {text!r} payload doesn't match its length"
    return Frame(int(text[2:4], 16), data, t, text)


# Command formatting helpers.
def t0(lin_id: int, data: bytes) -> str:
    return f"t0{lin_id:02X}{len(data)}{data.hex().upper()}"


def t1(lin_id: int, data: bytes) -> str:
    return f"t1{lin_id:02X}{len(data)}{data.hex().upper()}"


def r0(lin_id: int, length: int) -> str:
    return f"r0{lin_id:02X}{length}"


def r1(lin_id: int, length: int) -> str:
    return f"r1{lin_id:02X}{length}"


def R0(lin_id: int, slot_ms: int, length: int) -> str:
    return f"R0{lin_id:02X}{slot_ms:05X}{length}"


def T0(lin_id: int, slot_ms: int, data: bytes) -> str:
    return f"T0{lin_id:02X}{slot_ms:05X}{len(data)}{data.hex().upper()}"


def auto_slot_ms(length: int, baud: int) -> int:
    """Frame slot time the firmware calculates for "r0" and "t0" (worst-case frame time)."""
    return math.ceil((88 + 20 * length) * 1000 / baud)


class UsbLinCard:
    """One USB LIN Card. A reader thread splits the output into command replies and frame lines.

    Every command gets exactly one reply terminated with CR (ok) or BEL (error), except "boot" and
    empty lines. Replies never start with 't', while received LIN frames are reported
    asynchronously as lines starting with 't', so the two can be told apart.
    """

    def __init__(self, port: str, name: str):
        self.name = name
        self.seri = serial.Serial(port, BAUD_RATE, timeout=0.02)
        self._replies: queue.Queue[Response] = queue.Queue()
        self._frames: list[Frame] = []
        self._frames_cond = threading.Condition()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._reader, name=f"reader-{name}", daemon=True)
        self._thread.start()

    def _reader(self):
        buf = bytearray()
        while not self._stop.is_set():
            try:
                chunk = self.seri.read(self.seri.in_waiting or 1)
            except (serial.SerialException, OSError, TypeError):
                if self._stop.is_set():
                    return
                raise
            now = time.monotonic()
            for b in chunk:
                if b not in (CR, BEL):
                    buf.append(b)
                    continue
                text = bytes(buf).decode(errors="replace")
                buf.clear()
                if text.startswith("t"):
                    with self._frames_cond:
                        self._frames.append(parse_frame(text, now))
                        self._frames_cond.notify_all()
                else:
                    self._replies.put(Response(text, ok=(b == CR)))

    def close(self):
        self._stop.set()
        self._thread.join(timeout=1)
        self.seri.close()

    def __repr__(self):
        return f"UsbLinCard({self.name})"

    # Commands.
    def send_raw(self, data: bytes):
        self.seri.write(data)
        self.seri.flush()

    def send(self, cmd: str):
        self.send_raw((cmd + "\r").encode())

    def read_response(self, timeout: float = 2.0) -> Response:
        try:
            return self._replies.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError(f"{self.name}: no reply within {timeout} s") from None

    def cmd(self, cmd: str, timeout: float = 2.0) -> Response:
        self.send(cmd)
        return self.read_response(timeout)

    def cmd_ok(self, cmd: str, timeout: float = 2.0) -> Response:
        r = self.cmd(cmd, timeout)
        assert r.ok, f"{self.name}: command {cmd!r} failed"
        return r

    def send_frame(self, lin_id: int, data: bytes, timeout: float = 1.0):
        """Sends a single frame with "t0". The device rejects it while its previous single frame is
        still being transmitted, so retries until accepted."""
        deadline = time.monotonic() + timeout
        while not self.cmd(t0(lin_id, data)).ok:
            assert time.monotonic() < deadline, f"{self.name}: single frame not accepted within {timeout} s"
            time.sleep(0.002)

    def flags(self) -> int:
        r = self.cmd_ok("F")
        assert len(r.payload) == 3 and r.payload[0] == "F", f"{self.name}: bad F reply {r.payload!r}"
        return int(r.payload[1:], 16)

    def clear_replies(self):
        while True:
            try:
                self._replies.get_nowait()
            except queue.Empty:
                return

    def pending_replies(self) -> list[Response]:
        out = []
        while True:
            try:
                out.append(self._replies.get_nowait())
            except queue.Empty:
                return out

    # Received frames.
    def frames(self) -> list[Frame]:
        with self._frames_cond:
            return list(self._frames)

    def clear_frames(self):
        with self._frames_cond:
            self._frames.clear()

    def wait_frames(self, n: int, timeout: float = 2.0) -> list[Frame]:
        """Waits until at least n frames have been received (since last clear) and returns them all."""
        deadline = time.monotonic() + timeout
        with self._frames_cond:
            while len(self._frames) < n:
                left = deadline - time.monotonic()
                if left <= 0:
                    raise TimeoutError(
                        f"{self.name}: got {len(self._frames)}/{n} frames within {timeout} s: "
                        f"{[f.text for f in self._frames]}"
                    )
                self._frames_cond.wait(left)
            return list(self._frames)

    def wait_frame(self, timeout: float = 2.0) -> Frame:
        """Waits for the first frame since last clear and returns it."""
        return self.wait_frames(1, timeout)[0]

    def collect_frames(self, duration: float) -> list[Frame]:
        """Clears received frames, waits for duration and returns the frames received meanwhile."""
        self.clear_frames()
        time.sleep(duration)
        return self.frames()
