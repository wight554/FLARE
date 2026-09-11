/// @file forensics.c
/// @brief Blackbox crash logging in uninitialized SRAM, surviving watchdog resets.

#include "forensics.h"
#include "controller_shared.h"
#include "sync.h"
#include "toolchange.h"
#include <string.h>

#if defined(PICO_BOARD)
#include "pico/platform.h"
static crash_data_t __uninitialized_ram(g_crash_ram);
#else
static crash_data_t g_crash_ram;
#endif

#define FORENSICS_BREADCRUMB_MS 100
#define FORENSICS_EDGE_UNSET 0xFFu
#define CRC32_INIT 0xFFFFFFFFu
#define CRC32_POLY_REFLECTED 0xEDB88320u
#define BITS_PER_BYTE 8
#define FNV1A_OFFSET 2166136261u
#define FNV1A_PRIME 16777619u
#define SENSOR_BIT_IN1 0
#define SENSOR_BIT_OUT1 1
#define SENSOR_BIT_IN2 2
#define SENSOR_BIT_OUT2 3
#define SENSOR_BIT_Y 4
#define SENSOR_BIT_TH 5
#define SENSOR_BIT_TENS 6
#define SENSOR_BIT_COMP 7

static bool g_has_crash_record = false;
static uint32_t g_last_breadcrumb_ms = 0;

/* Edge detector state for event-driven logging (spec §2.3). */
static uint8_t g_prev_tc_state = FORENSICS_EDGE_UNSET;
static uint8_t g_prev_sync_state = FORENSICS_EDGE_UNSET;
static uint8_t g_prev_lane_task[NUM_LANES] = {FORENSICS_EDGE_UNSET, FORENSICS_EDGE_UNSET};
static uint8_t g_prev_sensors = FORENSICS_EDGE_UNSET;

static uint32_t forensics_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = CRC32_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < BITS_PER_BYTE; j++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (CRC32_POLY_REFLECTED & mask);
        }
    }
    return ~crc;
}

static uint32_t compute_buffer_crc(const crash_data_t *d) {
    return forensics_crc32((const uint8_t *)d, offsetof(crash_data_t, crc32));
}

static void forensics_reset_buffer(void) {
    memset(&g_crash_ram, 0, sizeof(g_crash_ram));
    g_crash_ram.magic = FORENSICS_MAGIC;
    g_crash_ram.version = FORENSICS_VERSION;
    g_crash_ram.crash_reason = CRASH_REASON_NONE;
    g_crash_ram.head = 0;
    g_crash_ram.count = 0;
    g_crash_ram.last_boot_ms = 0;
    g_crash_ram.crc32 = compute_buffer_crc(&g_crash_ram);
    g_has_crash_record = false;
}

uint8_t forensics_pack_sensors(void) {
    uint8_t m = 0;
    if (lane_in_present(&g_lane_l1))
        m |= (1u << SENSOR_BIT_IN1);
    if (lane_out_present(&g_lane_l1))
        m |= (1u << SENSOR_BIT_OUT1);
    if (lane_in_present(&g_lane_l2))
        m |= (1u << SENSOR_BIT_IN2);
    if (lane_out_present(&g_lane_l2))
        m |= (1u << SENSOR_BIT_OUT2);
    if (on_al(&g_y_split))
        m |= (1u << SENSOR_BIT_Y);
    if (g_toolhead_has_filament)
        m |= (1u << SENSOR_BIT_TH);
    if (on_al(&g_buf_tension_din))
        m |= (1u << SENSOR_BIT_TENS);
    if (on_al(&g_buf_compression_din))
        m |= (1u << SENSOR_BIT_COMP);
    return m;
}

void forensics_init(bool watchdog_reset) {
    uint32_t expected_crc = compute_buffer_crc(&g_crash_ram);
    bool valid = (g_crash_ram.magic == FORENSICS_MAGIC &&
                  g_crash_ram.version == FORENSICS_VERSION && g_crash_ram.crc32 == expected_crc);

    if (valid) {
        if (watchdog_reset) {
            g_crash_ram.crash_reason = CRASH_REASON_WATCHDOG;
            g_crash_ram.crc32 = compute_buffer_crc(&g_crash_ram);
            g_has_crash_record = true;
        } else if (g_crash_ram.crash_reason != CRASH_REASON_NONE) {
            g_has_crash_record = true;
        } else {
            forensics_reset_buffer();
        }
    } else {
        forensics_reset_buffer();
    }
}

void forensics_log_entry(forensics_entry_type_t type, uint8_t lane, uint32_t payload) {
    if (g_crash_ram.magic != FORENSICS_MAGIC) {
        forensics_reset_buffer();
    }

    forensics_entry_t e;
    e.timestamp_ms = g_now_ms;
    e.type = (uint8_t)type;
    e.lane = lane;
    e.tc_state = (uint8_t)g_tc_ctx.state;
    e.sync_state = (uint8_t)g_sync_state;
    e.sensors = forensics_pack_sensors();
    e.buf_pos_raw = (int16_t)clamp_i((int)(g_buf_pos * 100.0f), INT16_MIN, INT16_MAX);
    e.step_rate_sps = (int16_t)clamp_i(g_sync_current_sps, INT16_MIN, INT16_MAX);
    e.payload = payload;

    g_crash_ram.entries[g_crash_ram.head] = e;
    g_crash_ram.head = (g_crash_ram.head + 1u) % FORENSICS_RING_CAPACITY;
    g_crash_ram.count++;
    g_crash_ram.last_boot_ms = g_now_ms;
    g_crash_ram.crc32 = compute_buffer_crc(&g_crash_ram);
}

static void forensics_snapshot_edges(void) {
    g_prev_tc_state = (uint8_t)g_tc_ctx.state;
    g_prev_sync_state = (uint8_t)g_sync_state;
    for (int i = 0; i < NUM_LANES; i++) {
        g_prev_lane_task[i] = (uint8_t)lane_ptr(i + 1)->task;
    }
    g_prev_sensors = forensics_pack_sensors();
}

void forensics_tick(uint32_t now_ms) {
    if (g_last_breadcrumb_ms == 0) {
        /* First tick after boot: establish the edge baseline, log nothing. */
        g_last_breadcrumb_ms = now_ms;
        forensics_snapshot_edges();
        return;
    }

    bool logged = false;
    uint8_t tc = (uint8_t)g_tc_ctx.state;
    if (tc != g_prev_tc_state) {
        g_prev_tc_state = tc;
        forensics_log_entry(FORENSICS_ENTRY_TC_CHANGE, (uint8_t)g_active_lane, tc);
        logged = true;
    }
    uint8_t st = (uint8_t)g_sync_state;
    if (st != g_prev_sync_state) {
        g_prev_sync_state = st;
        forensics_log_entry(FORENSICS_ENTRY_SYNC_CHANGE, (uint8_t)g_active_lane, st);
        logged = true;
    }
    for (int i = 0; i < NUM_LANES; i++) {
        lane_t *lane = lane_ptr(i + 1);
        uint8_t task = (uint8_t)lane->task;
        if (task != g_prev_lane_task[i]) {
            g_prev_lane_task[i] = task;
            forensics_log_entry(FORENSICS_ENTRY_TASK_CHANGE, (uint8_t)(i + 1), task);
            logged = true;
        }
    }
    uint8_t sensors = forensics_pack_sensors();
    if (sensors != g_prev_sensors) {
        uint8_t changed = (uint8_t)(sensors ^ g_prev_sensors);
        g_prev_sensors = sensors;
        forensics_log_entry(FORENSICS_ENTRY_SENSOR_EDGE, (uint8_t)g_active_lane, changed);
        logged = true;
    }

    if (logged) {
        g_last_breadcrumb_ms = now_ms;
    } else if ((int32_t)(now_ms - g_last_breadcrumb_ms) >= FORENSICS_BREADCRUMB_MS) {
        g_last_breadcrumb_ms = now_ms;
        forensics_log_entry(FORENSICS_ENTRY_BREADCRUMB, (uint8_t)g_active_lane, 0);
    }
}

uint32_t forensics_hash_str(const char *s) {
    uint32_t h = FNV1A_OFFSET;
    while (s && *s) {
        h ^= (uint8_t)*s++;
        h *= FNV1A_PRIME;
    }
    return h;
}

bool forensics_has_crash(void) {
    return g_has_crash_record;
}

crash_reason_t forensics_crash_reason(void) {
    return (crash_reason_t)g_crash_ram.crash_reason;
}

const char *forensics_crash_reason_str(crash_reason_t reason) {
    switch (reason) {
    case CRASH_REASON_WATCHDOG:
        return "WATCHDOG";
    case CRASH_REASON_PANIC:
        return "PANIC";
    case CRASH_REASON_HARDFAULT:
        return "HARDFAULT";
    default:
        return "NONE";
    }
}

uint32_t forensics_crash_time(void) {
    return g_crash_ram.last_boot_ms;
}

uint32_t forensics_entry_count(void) {
    if (g_crash_ram.count > FORENSICS_RING_CAPACITY) {
        return FORENSICS_RING_CAPACITY;
    }
    return g_crash_ram.count;
}

bool forensics_get_entry(uint32_t index, forensics_entry_t *out) {
    uint32_t total = forensics_entry_count();
    if (index >= total || !out) {
        return false;
    }

    uint32_t slot;
    if (g_crash_ram.count <= FORENSICS_RING_CAPACITY) {
        slot = index;
    } else {
        slot = (g_crash_ram.head + index) % FORENSICS_RING_CAPACITY;
    }

    *out = g_crash_ram.entries[slot];
    return true;
}

void forensics_clear(void) {
    forensics_reset_buffer();
}

crash_data_t *forensics_get_raw_buffer(void) {
    return &g_crash_ram;
}
