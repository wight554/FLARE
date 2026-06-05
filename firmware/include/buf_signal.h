#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "controller_shared.h"

/// @brief Source that produced the current buffer signal.
typedef enum {
    BUF_SRC_VIRTUAL_ENDSTOP = 0,
    BUF_SRC_ANALOG = 1,
} buf_source_kind_t;

/// @brief Canonical buffer signal consumed by sync control.
typedef struct {
    float pos_norm;         ///< Normalized [-1..+1]: -1 = full compression, +1 = full tension.
    float pos_mm;           ///< Physical mm: endstop virtual position or analog norm * half-travel.
    float confidence;       ///< Signal reliability, 0.0..1.0; low means treat as stale.
    uint32_t age_ms;        ///< Milliseconds since this signal was last meaningfully updated.
    buf_state_t zone;       ///< Quantized buffer zone.
    buf_source_kind_t kind; ///< Source that produced this signal.
    bool fault;             ///< Source-reported hard fault.
} buf_signal_t;

/// @brief Vtable for a buffer signal source.
///
/// Populated by the adapter's init function; tick/read are called each control
/// cycle by buf_sensor_tick(). The full adapter split into separate .c files is
/// deferred until type-P analog Sync-Feedback Sensor hardware is available.
typedef struct buf_source_s {
    void (*tick)(struct buf_source_s *src, uint32_t now_ms);
    void (*read)(struct buf_source_s *src, buf_signal_t *out);
    const char *name;
} buf_source_t;

/// @brief Canonical signal produced by the active source for the current tick.
extern buf_signal_t g_buf_signal;
