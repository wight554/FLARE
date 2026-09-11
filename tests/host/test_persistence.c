/// @file test_persistence.c
/// @brief Host unit tests for flash ping-pong atomic persistence and brownout recovery.

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "controller_shared.h"
#include "motion.h"
#include "settings_store.h"
#include "sim_fakes.h"
#include "sync.h"

#define SECTOR_A_OFFSET (PICO_FLASH_SIZE_BYTES - (2 * FLASH_SECTOR_SIZE))
#define SECTOR_B_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

static uint32_t test_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static void test_fresh_board_zero(void) {
    printf("test_fresh_board_zero... ");
    memset(g_sim_flash, 0, sizeof(g_sim_flash));

    settings_load();

    // Must fall back to defaults without writing back
    assert(g_active_sector == 1);
    assert(g_seq == 0);
    assert(g_flash_erase_count == 0);

    for (size_t i = 0; i < 2 * FLASH_SECTOR_SIZE; i++) {
        assert(g_sim_flash[SECTOR_A_OFFSET + i] == 0);
    }
    printf("OK\n");
}

static void test_fresh_board_ff(void) {
    printf("test_fresh_board_ff... ");
    memset(g_sim_flash, 0xFF, sizeof(g_sim_flash));

    settings_load();

    assert(g_active_sector == 1);
    assert(g_seq == 0);
    assert(g_flash_erase_count == 0);

    for (size_t i = 0; i < 2 * FLASH_SECTOR_SIZE; i++) {
        assert(g_sim_flash[SECTOR_A_OFFSET + i] == 0xFF);
    }
    printf("OK\n");
}

static void test_save_ping_pong_alternation(void) {
    printf("test_save_ping_pong_alternation... ");
    memset(g_sim_flash, 0, sizeof(g_sim_flash));
    settings_load();

    // 1st save: active was 1 -> targets 0 (Sector A)
    g_feed_sps = 1234;
    settings_save();
    assert(g_active_sector == 0);
    assert(g_seq == 1);
    assert(g_flash_erase_count == 1);

    // 2nd save: active was 0 -> targets 1 (Sector B)
    g_feed_sps = 2345;
    settings_save();
    assert(g_active_sector == 1);
    assert(g_seq == 2);
    assert(g_flash_erase_count == 2);

    // 3rd save: active was 1 -> targets 0 (Sector A)
    g_feed_sps = 3456;
    settings_save();
    assert(g_active_sector == 0);
    assert(g_seq == 3);
    assert(g_flash_erase_count == 3);

    // Reboot / reload from flash -> must pick Sector A (seq 3 > seq 2)
    g_feed_sps = 0;
    settings_load();
    assert(g_active_sector == 0);
    assert(g_seq == 3);
    assert(g_feed_sps == 3456);
    assert(g_flash_erase_count == 3);

    printf("OK\n");
}

static void test_brownout_corrupted_crc_recovery(void) {
    printf("test_brownout_corrupted_crc_recovery... ");
    memset(g_sim_flash, 0, sizeof(g_sim_flash));
    settings_load();

    // Save initial state to Sector A (seq 1)
    g_feed_sps = 1111;
    settings_save();
    assert(g_active_sector == 0);
    assert(g_seq == 1);

    // Save new state to Sector B (seq 2)
    g_feed_sps = 2222;
    settings_save();
    assert(g_active_sector == 1);
    assert(g_seq == 2);

    // Simulate brownout during a subsequent save to Sector A:
    // Sector A is corrupted with truncated/bad data
    g_sim_flash[SECTOR_A_OFFSET + 10] ^= 0xFF; // corrupt payload/CRC

    // Reboot / reload: Sector A is invalid, Sector B is valid
    g_feed_sps = 0;
    settings_load();

    // Must successfully recover Sector B!
    assert(g_active_sector == 1);
    assert(g_seq == 2);
    assert(g_feed_sps == 2222);

    printf("OK\n");
}

static void test_brownout_during_erase_recovery(void) {
    printf("test_brownout_during_erase_recovery... ");
    memset(g_sim_flash, 0, sizeof(g_sim_flash));
    settings_load();

    // Save to Sector A (seq 1)
    g_feed_sps = 5555;
    settings_save();
    assert(g_active_sector == 0);

    // Save to Sector B (seq 2)
    g_feed_sps = 6666;
    settings_save();
    assert(g_active_sector == 1);

    // Simulate brownout while erasing Sector A for the 3rd save:
    // Sector A is wiped to all 0xFF
    memset(g_sim_flash + SECTOR_A_OFFSET, 0xFF, FLASH_SECTOR_SIZE);

    // Reload: Sector A is blank 0xFF, Sector B is valid
    g_feed_sps = 0;
    settings_load();
    assert(g_active_sector == 1);
    assert(g_seq == 2);
    assert(g_feed_sps == 6666);

    printf("OK\n");
}

static void test_brownout_sector_b_corrupted_recovers_sector_a(void) {
    printf("test_brownout_sector_b_corrupted_recovers_sector_a... ");
    memset(g_sim_flash, 0, sizeof(g_sim_flash));
    settings_load();

    // Save to Sector A (seq 1)
    g_feed_sps = 7777;
    settings_save();
    assert(g_active_sector == 0);

    // Power dropped mid-write to Sector B -> Sector B contains garbage
    memset(g_sim_flash + SECTOR_B_OFFSET, 0xAA, FLASH_SECTOR_SIZE);

    // Reload: Sector B is corrupted, Sector A is valid
    g_feed_sps = 0;
    settings_load();
    assert(g_active_sector == 0);
    assert(g_seq == 1);
    assert(g_feed_sps == 7777);

    // Next save must safely target Sector B and repair it
    g_feed_sps = 8888;
    settings_save();
    assert(g_active_sector == 1);
    assert(g_seq == 2);

    g_feed_sps = 0;
    settings_load();
    assert(g_active_sector == 1);
    assert(g_seq == 2);
    assert(g_feed_sps == 8888);

    printf("OK\n");
}

static void test_sequence_wraparound(void) {
    printf("test_sequence_wraparound... ");
    memset(g_sim_flash, 0, sizeof(g_sim_flash));
    settings_load();

    // Manually test the signed sequence arbitration boundary
    // Case 1: normal difference
    uint32_t seq_a = 100;
    uint32_t seq_b = 99;
    assert((int32_t)(seq_a - seq_b) > 0);
    assert((int32_t)(seq_b - seq_a) < 0);

    // Case 2: 32-bit modular wraparound (seq_b near 0xFFFFFFFF, seq_a wrapped to 0)
    seq_a = 0;
    seq_b = 0xFFFFFFFFu;
    // 0 - 0xFFFFFFFFu = 1 in modulo 2^32 arithmetic
    assert((int32_t)(seq_a - seq_b) > 0);
    assert((int32_t)(seq_b - seq_a) < 0);

    printf("OK\n");
}

static void test_buffer_1024_capacity(void) {
    printf("test_buffer_1024_capacity... ");
    assert(SETTINGS_FLASH_BUFFER_BYTES == 1024);
    printf("OK\n");
}

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t seq;
    int feed_sps, rev_sps, auto_sps;
    int sync_max_sps, global_max_sps, sync_min_sps;
    int sync_auto_stop_ms;
    int load_max_mm;
    int tc_ts_retries;
    float tc_ts_retry_retract_mm;
    float tc_ts_park_mm;
    int unload_max_mm;
    int unload_tension_block_ms;
    int reload_join_delay_ms;
    int autoload_max_mm;
    int auto_mode;
    int dist_in_out, dist_out_y, dist_y_buf, buf_body_len, buf_max_travel_mm;
    float buf_switch_span_mm;
    int baseline_sps;
    int autoload_retract_mm;

    int servo_open_us, servo_close_us, servo_block_us;
    int servo_settle_ms;
    int cut_feed_sps;
    int cut_feed_mm, cut_length_mm, cut_amount;

    int runout_cooldown_ms;

    int buf_sensor_type;
    float buf_psf_max_comp, buf_psf_max_tens, buf_psf_neutral, buf_psf_goal;
    int sync_kp_sps;
    int sync_reserve_pct;

    int join_sps;
    int press_sps;
    int compression_sps;
    int follow_timeout_ms[NUM_LANES];

    bool auto_preload;
    bool enable_cutter;
    bool unload_cut;
    bool reload_mode;

    float tmc_rotation_distance[NUM_LANES];
    float tmc_gear_ratio[NUM_LANES];
    int tmc_full_steps[NUM_LANES];
    int tmc_microsteps[NUM_LANES];
    int tmc_tbl[NUM_LANES], tmc_toff[NUM_LANES], tmc_hstrt[NUM_LANES], tmc_hend[NUM_LANES];
    bool tmc_interpolate[NUM_LANES];
    int tmc_stealthchop_sps[NUM_LANES];
    int tmc_run_current_ma[NUM_LANES], tmc_hold_current_ma[NUM_LANES];

    float relay_catchup_frac;
    float relay_neutral_frac;

    float sync_compression_bias_frac;

    uint32_t flash_erase_count;
    uint32_t crc32;
} legacy_v63_fixture_t;

static void test_legacy_v63_load_and_lazy_migration(void) {
    printf("test_legacy_v63_load_and_lazy_migration... ");
    memset(g_sim_flash, 0xFF, sizeof(g_sim_flash));

    legacy_v63_fixture_t v63;
    memset(&v63, 0, sizeof(v63));
    v63.magic = SETTINGS_MAGIC;
    v63.version = SETTINGS_VERSION_V63; // 63
    v63.seq = 50;
    v63.feed_sps = 4321;
    v63.rev_sps = 1234;
    v63.global_max_sps = 3000;
    v63.buf_max_travel_mm = 50;
    v63.buf_switch_span_mm = 10.0f;
    for (int i = 0; i < NUM_LANES; i++) {
        v63.tmc_rotation_distance[i] = 22.678f;
        v63.tmc_gear_ratio[i] = 1.0f;
        v63.tmc_full_steps[i] = 200;
        v63.tmc_microsteps[i] = 16;
    }
    v63.crc32 = test_crc32((const uint8_t *)&v63, offsetof(legacy_v63_fixture_t, crc32));

    // Put legacy v63 in Sector A
    memcpy(g_sim_flash + SECTOR_A_OFFSET, &v63, sizeof(v63));

    // Boot: load settings
    settings_load();

    // Verify v63 data read accurately
    assert(g_active_sector == 0);
    assert(g_seq == 50);
    assert(g_feed_sps == 4321);

    // Verify Sector B was NOT touched yet (lazy migration)
    for (size_t i = 0; i < FLASH_SECTOR_SIZE; i++) {
        assert(g_sim_flash[SECTOR_B_OFFSET + i] == 0xFF);
    }

    // Now operator saves: must write new v64 TLV to Sector B
    settings_save();
    assert(g_active_sector == 1);
    assert(g_seq == 51);

    const settings_header_t *b_hdr = (const settings_header_t *)(g_sim_flash + SECTOR_B_OFFSET);
    assert(b_hdr->magic == SETTINGS_MAGIC);
    assert(b_hdr->version == SETTINGS_VERSION); // 64
    assert(b_hdr->seq == 51);

    // Wipe RAM and reload from flash
    g_feed_sps = 0;
    settings_load();
    assert(g_active_sector == 1);
    assert(g_seq == 51);
    assert(g_feed_sps == 4321);

    printf("OK\n");
}

static void test_unknown_tag_skipping_and_pruning(void) {
    printf("test_unknown_tag_skipping_and_pruning... ");
    memset(g_sim_flash, 0, sizeof(g_sim_flash));
    settings_load();

    g_feed_sps = 7788;
    settings_save(); // writes to Sector A (seq 1, v64 TLV)
    assert(g_active_sector == 0);

    // Inject unknown tag 0xDD of length 4 at end of TLV stream in Sector A
    settings_header_t *hdr = (settings_header_t *)(g_sim_flash + SECTOR_A_OFFSET);
    uint8_t *payload = g_sim_flash + SECTOR_A_OFFSET + sizeof(settings_header_t);
    uint16_t old_len = hdr->payload_len;

    payload[old_len] = 0xDD;     // unknown tag
    payload[old_len + 1] = 4;    // length
    uint32_t unk_val = 0xCAFEBABE;
    memcpy(payload + old_len + 2, &unk_val, 4);

    hdr->payload_len = old_len + 6;
    size_t total_payload = sizeof(settings_header_t) + hdr->payload_len;
    uint32_t crc = test_crc32(g_sim_flash + SECTOR_A_OFFSET, total_payload);
    memcpy(g_sim_flash + SECTOR_A_OFFSET + total_payload, &crc, sizeof(crc));

    // Reload from flash: unknown tag must be cleanly skipped
    g_feed_sps = 0;
    settings_load();
    assert(g_active_sector == 0);
    assert(g_feed_sps == 7788);

    // Save back: must write to Sector B, pruning unknown tag 0xDD
    settings_save();
    assert(g_active_sector == 1);

    const settings_header_t *b_hdr = (const settings_header_t *)(g_sim_flash + SECTOR_B_OFFSET);
    const uint8_t *b_payload = g_sim_flash + SECTOR_B_OFFSET + sizeof(settings_header_t);
    size_t off = 0;
    bool found_unknown = false;
    while (off + 2 <= b_hdr->payload_len) {
        uint8_t t = b_payload[off];
        uint8_t l = b_payload[off + 1];
        if (t == 0xDD) {
            found_unknown = true;
        }
        off += 2 + l;
    }
    assert(!found_unknown);

    printf("OK\n");
}

static void test_truncated_tlv_bounds_defense(void) {
    printf("test_truncated_tlv_bounds_defense... ");
    memset(g_sim_flash, 0, sizeof(g_sim_flash));
    settings_load();

    g_feed_sps = 5555;
    settings_save(); // Sector A
    assert(g_active_sector == 0);

    // Corrupt the first tag's length to 250 in Sector A, exceeding total payload length
    uint8_t *payload = g_sim_flash + SECTOR_A_OFFSET + sizeof(settings_header_t);
    payload[1] = 250;
    settings_header_t *hdr = (settings_header_t *)(g_sim_flash + SECTOR_A_OFFSET);
    size_t total_payload = sizeof(settings_header_t) + hdr->payload_len;
    uint32_t crc = test_crc32(g_sim_flash + SECTOR_A_OFFSET, total_payload);
    memcpy(g_sim_flash + SECTOR_A_OFFSET + total_payload, &crc, sizeof(crc));

    // Loading must safely detect boundary overflow and stop cleanly without hang or segfault
    g_feed_sps = 0;
    settings_load();

    printf("OK\n");
}

int main(void) {
    printf("Running persistence host unit tests...\n");
    test_buffer_1024_capacity();
    test_fresh_board_zero();
    test_fresh_board_ff();
    test_save_ping_pong_alternation();
    test_brownout_corrupted_crc_recovery();
    test_brownout_during_erase_recovery();
    test_brownout_sector_b_corrupted_recovers_sector_a();
    test_sequence_wraparound();
    test_legacy_v63_load_and_lazy_migration();
    test_unknown_tag_skipping_and_pruning();
    test_truncated_tlv_bounds_defense();
    printf("All persistence host unit tests PASSED.\n");
    return 0;
}
