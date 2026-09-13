#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_MAGIC 0x4e4f5346u
#define SETTINGS_VERSION_V63 63u
#define SETTINGS_VERSION 64u
#define SETTINGS_FLASH_BUFFER_BYTES 1024

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t seq;
    uint16_t payload_len;
    uint16_t reserved;
} settings_header_t;

typedef enum {
    TAG_FEED_SPS = 1,
    TAG_REV_SPS = 2,
    TAG_AUTO_SPS = 3,
    TAG_SYNC_MAX_SPS = 4,
    TAG_GLOBAL_MAX_SPS = 5,
    TAG_SYNC_MIN_SPS = 6,
    TAG_SYNC_AUTO_STOP_MS = 7,
    TAG_LOAD_MAX_MM = 8,
    TAG_TC_TS_RETRIES = 9,
    TAG_TC_TS_RETRY_RETRACT_MM = 10,
    TAG_TC_TS_PARK_MM = 11,
    TAG_UNLOAD_MAX_MM = 12,
    TAG_UNLOAD_TENSION_BLOCK_MS = 13,
    TAG_RELOAD_JOIN_DELAY_MS = 14,
    TAG_AUTOLOAD_MAX_MM = 15,
    TAG_AUTO_MODE = 16,
    TAG_DIST_IN_OUT = 17,
    TAG_DIST_OUT_Y = 18,
    TAG_DIST_Y_BUF = 19,
    TAG_BUF_BODY_LEN = 20,
    TAG_BUF_MAX_TRAVEL_MM = 21,
    TAG_BUF_SWITCH_SPAN_MM = 22,
    TAG_BASELINE_SPS = 23,
    TAG_AUTOLOAD_RETRACT_MM = 24,
    TAG_SERVO_OPEN_US = 25,
    TAG_SERVO_CLOSE_US = 26,
    TAG_SERVO_BLOCK_US = 27,
    TAG_SERVO_SETTLE_MS = 28,
    TAG_CUT_FEED_SPS = 29,
    TAG_CUT_FEED_MM = 30,
    TAG_CUT_LENGTH_MM = 31,
    TAG_CUT_AMOUNT = 32,
    TAG_RUNOUT_COOLDOWN_MS = 33,
    TAG_BUF_SENSOR_TYPE = 34,
    TAG_BUF_PSF_MAX_COMP = 35,
    TAG_BUF_PSF_MAX_TENS = 36,
    TAG_BUF_PSF_NEUTRAL = 37,
    TAG_BUF_PSF_GOAL = 38,
    TAG_SYNC_KP_SPS = 39,
    TAG_SYNC_RESERVE_PCT = 40,
    TAG_JOIN_SPS = 41,
    TAG_PRESS_SPS = 42,
    TAG_COMPRESSION_SPS = 43,
    TAG_FOLLOW_TIMEOUT_MS = 44,
    TAG_AUTO_PRELOAD = 45,
    TAG_ENABLE_CUTTER = 46,
    TAG_UNLOAD_CUT = 47,
    TAG_RELOAD_MODE = 48,
    TAG_TMC_ROTATION_DISTANCE = 49,
    TAG_TMC_GEAR_RATIO = 50,
    TAG_TMC_FULL_STEPS = 51,
    TAG_TMC_MICROSTEPS = 52,
    TAG_TMC_TBL = 53,
    TAG_TMC_TOFF = 54,
    TAG_TMC_HSTRT = 55,
    TAG_TMC_HEND = 56,
    TAG_TMC_INTERPOLATE = 57,
    TAG_TMC_STEALTHCHOP_SPS = 58,
    TAG_TMC_RUN_CURRENT_MA = 59,
    TAG_TMC_HOLD_CURRENT_MA = 60,
    TAG_RELAY_CATCHUP_FRAC = 61,
    TAG_RELAY_NEUTRAL_FRAC = 62,
    TAG_SYNC_COMPRESSION_BIAS_FRAC = 63,
    TAG_FLASH_ERASE_COUNT = 64,
    TAG_SYNC_PSF_RELIEF_MULT = 65,
    TAG_SYNC_TENSION_STOP_MM = 66,
} settings_tag_t;

extern int g_active_sector;
extern uint32_t g_seq;

void settings_defaults(void);
void settings_save(void);
void settings_load(void);
void sync_tmc_settings(int lane);
