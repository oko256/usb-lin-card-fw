#!/bin/bash

find "Core" "USB_DEVICE" "src" "test" \
    -type d -path "test/build*" -prune -o \
    -type f \( -name "*.c" -o -name "*.h" \) \
    -exec clang-format-19 -i {} +
