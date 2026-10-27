#!/usr/bin/env bash
# Flashes the given firmware (default: Debug build) to the test devices one at a time: each device
# is commanded to its USB bootloader with "boot" and then programmed over USB DFU.
#
# Usage: flash.sh [elf] [serial ports...]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROGRAMMER="${STM32_PROGRAMMER_CLI:-$HOME/STM32CubeProg/bin/STM32_Programmer_CLI}"
ELF="${1:-$SCRIPT_DIR/../build/Debug/usb-lin-card-fw.elf}"
shift || true
PORTS=("$@")
if [ ${#PORTS[@]} -eq 0 ]; then
    PORTS=("${LIN_DEV1:-/tmp/tty-lin-master}" "${LIN_DEV2:-/tmp/tty-lin-slave}")
fi

wait_for() {
    # wait_for <exists|gone> <path>
    for _ in $(seq 100); do
        if { [ "$1" = exists ] && [ -e "$2" ]; } || { [ "$1" = gone ] && [ ! -e "$2" ]; }; then
            return 0
        fi
        sleep 0.1
    done
    echo "Timeout waiting for $2 to be $1" >&2
    return 1
}

for port in "${PORTS[@]}"; do
    echo "=== Flashing $port"
    printf 'boot\r' > "$port"
    wait_for gone "$port"
    sleep 1 # DFU enumeration
    log="$(mktemp)"
    if ! "$PROGRAMMER" -c port=usb1 -w "$ELF" -g > "$log" 2>&1; then
        cat "$log"
        rm -f "$log"
        echo "Programming $port failed" >&2
        exit 1
    fi
    rm -f "$log"
    wait_for exists "$port"
done
echo "=== All devices flashed"
