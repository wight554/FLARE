#pragma once

/// @file firmware_constants.h
/// @brief Shared dimensionless/unit constants used across multiple translation
///        units. Single source of truth — do not redefine these per .c file.
///        Included transitively via controller_shared.h.

#define MS_PER_SECOND_F 1000.0f ///< milliseconds per second (float)
#define HALF_F 0.5f             ///< one half (float)
#define FULL_SPAN_MULT_F 2.0f   ///< half-span -> full-span multiplier (float)

#define TMC_ROTATION_MIN_MM 0.1f
#define TMC_ROTATION_MAX_MM 1000.0f
#define TMC_GEAR_RATIO_MIN 0.001f
#define TMC_GEAR_RATIO_MAX 1000.0f

/* Toolchange toolhead-sensor retry/park clamp bounds — shared by the SET:
   handler (protocol.c) and settings_apply_clamps() (settings_store.c). */
#define TC_TS_RETRIES_MIN 0
#define TC_TS_RETRIES_MAX 10
#define TC_TS_RETRY_RETRACT_MIN_MM 5.0f
#define TC_TS_RETRY_RETRACT_MAX_MM 500.0f
#define TC_TS_PARK_MIN_MM 0.0f
#define TC_TS_PARK_MAX_MM 100.0f
