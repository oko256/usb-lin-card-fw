#pragma once

// Host unit testing replacement for CMSIS compiler intrinsics.

/// Data memory barrier; on the host a compiler barrier is enough for single-threaded tests.
#define __DMB() __asm__ volatile("" ::: "memory")
