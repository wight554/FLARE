#pragma once
/* Host shim for hardware/pio.h. Required transitively by tmc2209.h via
 * controller_shared.h. Opaque — nothing in the linked sync/motion/toolchange
 * path dereferences a PIO value, it is only stored by pointer. */

typedef struct pio_hw *PIO;
