"""System tests for two USB LIN Cards connected to the same LIN bus.

dev1 and dev2 are the serial ports of the two devices. Before each test both devices are closed,
their tables wiped, and settings and error flags reset. After each test, both devices must have no
latched error flags unless the test is marked with expect_flags (then the test checks them itself).
"""

import os
import time

import pytest

from usblincard import UsbLinCard

# Default paths to the USB serial ports of the two test USB LIN Card devices.
DEV_1_PATH = "/tmp/tty-lin-master"
DEV_2_PATH = "/tmp/tty-lin-slave"

# Time for the bus and the device output to settle after closing.
SETTLE_S = 0.05


def pytest_addoption(parser):
    parser.addoption("--dev1", default=os.environ.get("LIN_DEV1", DEV_1_PATH), help="Serial port of device 1")
    parser.addoption("--dev2", default=os.environ.get("LIN_DEV2", DEV_2_PATH), help="Serial port of device 2")
    parser.addoption("--soak-seconds", type=float, default=30.0, help="Duration of the soak test")


def pytest_collection_modifyitems(config, items):
    # Tests marked "last" check things accumulated over the whole session (e.g. stack usage).
    items.sort(key=lambda item: item.get_closest_marker("last") is not None)


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    setattr(item, f"rep_{rep.when}", rep)


@pytest.fixture(scope="session")
def dev1(pytestconfig):
    d = UsbLinCard(pytestconfig.getoption("--dev1"), "dev1")
    yield d
    d.close()


@pytest.fixture(scope="session")
def dev2(pytestconfig):
    d = UsbLinCard(pytestconfig.getoption("--dev2"), "dev2")
    yield d
    d.close()


@pytest.fixture
def devs(dev1, dev2):
    return (dev1, dev2)


def reset_device(dev: UsbLinCard):
    """Closes the device and resets its tables and LIN settings to defaults."""
    dev.cmd("C")  # Fails if already closed; doesn't matter.
    dev.cmd_ok("w")
    dev.cmd_ok("S4")  # 19200 baud
    dev.cmd_ok("x1")  # enhanced checksum
    dev.cmd_ok("x3")  # diagnostic frames with classic checksum


@pytest.fixture(autouse=True)
def clean_state(request, dev1, dev2):
    # Drop leftovers (e.g. unread replies) from the previous test before sending commands.
    time.sleep(SETTLE_S)
    for d in (dev1, dev2):
        d.clear_replies()
    for d in (dev1, dev2):
        reset_device(d)
    time.sleep(SETTLE_S)
    for d in (dev1, dev2):
        d.flags()
        d.clear_frames()
        d.clear_replies()

    yield

    rep = getattr(request.node, "rep_call", None)
    if rep is None or rep.failed or request.node.get_closest_marker("expect_flags"):
        return
    for d in (dev1, dev2):
        d.clear_replies()
        flags = d.flags()
        assert flags == 0, f"{d.name}: unexpected error flags 0x{flags:02X}"
