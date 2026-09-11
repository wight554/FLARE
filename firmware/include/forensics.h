#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FORENSICS_MAGIC 0x43525348u /* "CRSH" */
#define FORENSICS_VERSION 1u
#define FORENSICS_RING_CAPACITY 32u

typedef enum {
    FORENSICS_ENTRY_BREADCRUMB = 0,
    FORENSICS_ENTRY_TASK_CHANGE = 1,
    FORENSICS_ENTRY_TC_CHANGE = 2,
    FORENSICS_ENTRY_SYNC_CHANGE = 3,
    FORENSICS_ENTRY_SENSOR_EDGE = 4,
    FORENSICS_ENTRY_CMD_RECV = 5,
    FORENSICS_ENTRY_EVENT_EMIT = 6,
} forensics_entry_type_t;

typedef enum {
    CRASH_REASON_NONE = 0,
    CRASH_REASON_WATCHDOG = 1,
    CRASH_REASON_PANIC = 2,
    CRASH_REASON_HARDFAULT = 3,
} crash_reason_t;

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint8_t type;
    uint8_t lane;
    uint8_t tc_state;
    uint8_t sync_state;
    uint8_t sensors; // bit 0: in1, 1: out1, 2: in2, 3: out2, 4: y_split, 5: th, 6: tens, 7: comp
    int16_t buf_pos_raw;   // g_buf_pos * 100 (type-P norm or type-D virtual mm)
    int16_t step_rate_sps; // active lane rate
    uint32_t payload;      // command hash, task enum, or event data
} forensics_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint8_t crash_reason;
    uint8_t head;
    uint32_t count;
    uint32_t last_boot_ms;
    forensics_entry_t entries[FORENSICS_RING_CAPACITY];
    uint32_t crc32;
} crash_data_t;

/// @brief Initialize forensics system on boot.
/// @param watchdog_reset True if boot was caused by hardware watchdog.
void forensics_init(bool watchdog_reset);

/// @brief Record a discrete state transition or event.
void forensics_log_entry(forensics_entry_type_t type, uint8_t lane, uint32_t payload);

/// @brief Per-loop tick: logs lane task / TC / sync / sensor transitions as they
/// appear, and a breadcrumb snapshot when 100ms pass without one.
void forensics_tick(uint32_t now_ms);

/// @brief Cheap FNV-1a hash of a command verb for CMD_RECV payloads.
uint32_t forensics_hash_str(const char *s);

/// @brief True if valid post-mortem crash record exists.
bool forensics_has_crash(void);

/// @brief Reason for recorded crash.
crash_reason_t forensics_crash_reason(void);

const char *forensics_crash_reason_str(crash_reason_t reason);

/// @brief Uptime timestamp of crash in ms.
uint32_t forensics_crash_time(void);

/// @brief Number of entries in recorded crash buffer (up to FORENSICS_RING_CAPACITY).
uint32_t forensics_entry_count(void);

/// @brief Retrieve an entry by chronological index (0 = oldest, count-1 = newest).
bool forensics_get_entry(uint32_t index, forensics_entry_t *out);

/// @brief Clear crash dump signature.
void forensics_clear(void);

/// @brief Convert sensor states into 8-bit bitmask.
uint8_t forensics_pack_sensors(void);

/// @brief Raw buffer accessor for host tests and diagnostics.
crash_data_t *forensics_get_raw_buffer(void);
