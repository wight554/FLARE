#pragma once
/* Host shim for hardware/clocks.h. Not on the measured link surface for the
 * currently-compiled sources, but required by design.md's shim set (kept for
 * any future clock-frequency query, e.g. if PIO sources are added). */

typedef enum { clk_sys = 0 } clock_index_t;

unsigned int clock_get_hz(clock_index_t clk);
