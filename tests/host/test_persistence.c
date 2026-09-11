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
#define SETTINGS_MAGIC 0x4e4f5346u
#define SETTINGS_VERSION 63u

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

int main(void) {
    printf("Running persistence host unit tests...\n");
    test_fresh_board_zero();
    test_fresh_board_ff();
    test_save_ping_pong_alternation();
    test_brownout_corrupted_crc_recovery();
    test_brownout_during_erase_recovery();
    test_brownout_sector_b_corrupted_recovers_sector_a();
    test_sequence_wraparound();
    printf("All persistence host unit tests PASSED.\n");
    return 0;
}
