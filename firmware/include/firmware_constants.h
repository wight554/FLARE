#pragma once

/// @file firmware_constants.h
/// @brief Shared dimensionless/unit constants used across multiple translation
///        units. Single source of truth — do not redefine these per .c file.
///        Included transitively via controller_shared.h.

#define MS_PER_SECOND_F 1000.0f ///< milliseconds per second (float)
#define HALF_F 0.5f             ///< one half (float)
#define FULL_SPAN_MULT_F 2.0f   ///< half-span -> full-span multiplier (float)
